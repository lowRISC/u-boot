// SPDX-License-Identifier: Apache-2.0
/*
 * lowRISC Opentitan SPI Host driver
 *
 * Copyright (C) 2026 lowRISC Contributors.
 * Author: Alice Ziuziakowska <a.ziuziakowska@lowrisc.org>
 */

#include <dm.h>
#include <spi.h>
#include <errno.h>
#include <asm/io.h>
#include <asm-generic/unaligned.h>
#include <dm/device_compat.h>
#include <linux/bitops.h>

#define LOWRISC_OT_SPI_CONTROL_SPIEN		0x80000000u
#define LOWRISC_OT_SPI_CONTROL_OUTPUTEN		0x20000000u

#define LOWRISC_OT_SPI_CONFIGOPTS_CPOL		0x80000000u
#define LOWRISC_OT_SPI_CONFIGOPTS_CPHA		0x40000000u
#define LOWRISC_OT_SPI_CONFIGOPTS_CLKDIV	0xffffu

#define LOWRISC_OT_SPI_COMMAND_LEN_MASK		0xfffffu
#define LOWRISC_OT_SPI_COMMAND_LEN_SH		5u
#define LOWRISC_OT_SPI_COMMAND_DIRECTION_DUMMY	0u
#define LOWRISC_OT_SPI_COMMAND_DIRECTION_TXRX	0x18u
#define LOWRISC_OT_SPI_COMMAND_SPEED_STD	0u
#define LOWRISC_OT_SPI_COMMAND_CSAAT		0x1u

#define LOWRISC_OT_SPI_STATUS_READY		0x80000000u
#define LOWRISC_OT_SPI_STATUS_RXQD_MASK		0xffu
#define LOWRISC_OT_SPI_STATUS_RXQD_SH		8u

struct lowrisc_ot_spi_host {
	u32 unused0[4]; /* interrupt registers */
	u32 control;
	u32 status;
	u32 configopts;
	u32 csid;
	u32 command;
	u32 rxdata;
	u32 txdata;
	u32 unused1[3]; /* error reporting */
};

struct lowrisc_ot_spi_host_priv {
	volatile struct lowrisc_ot_spi_host *regs;
	u32 freq; /* max bus frequency */
};

struct lowrisc_ot_spi_host_plat {
	struct lowrisc_ot_spi_host_priv *priv;
};

static int lowrisc_ot_spi_host_set_speed(struct udevice *bus, uint speed)
{
	struct lowrisc_ot_spi_host_priv *priv = dev_get_priv(bus);
	volatile struct lowrisc_ot_spi_host *regs = priv->regs;
	u32 val = readl(&regs->configopts);

	if (speed > priv->freq)
		speed = priv->freq;

	val &= ~LOWRISC_OT_SPI_CONFIGOPTS_CLKDIV;
	val |= (DIV_ROUND_UP(priv->freq, speed) - 1u)
		& LOWRISC_OT_SPI_CONFIGOPTS_CLKDIV;

	writel(val, &regs->configopts);

	return 0;
}

static int lowrisc_ot_spi_host_set_mode(struct udevice *bus, uint mode)
{
	struct lowrisc_ot_spi_host_priv *priv = dev_get_priv(bus);
	volatile struct lowrisc_ot_spi_host *regs = priv->regs;
	u32 val = readl(&regs->configopts);

	u32 mask = LOWRISC_OT_SPI_CONFIGOPTS_CPHA | LOWRISC_OT_SPI_CONFIGOPTS_CPOL;

	val &= ~mask;
	if (mode & SPI_CPHA)
		val |= LOWRISC_OT_SPI_CONFIGOPTS_CPHA;
	if (mode & SPI_CPOL)
		val |= LOWRISC_OT_SPI_CONFIGOPTS_CPOL;

	writel(val, &regs->configopts);

	return 0;
}

static void lowrisc_ot_spi_host_xfer_part(volatile struct lowrisc_ot_spi_host *regs,
					  unsigned int n_bytes, const u8 *tx_ptr,
					  u8 *rx_ptr, bool hold_cs)
{
	bool ready;
	u32 tx, rx, rxqd, status, command;
	unsigned int n_words = (n_bytes >> 2); /* number of full words */
	unsigned int qd = n_words + ((n_bytes % 4) ? 1 : 0);

	command = ((n_bytes - 1) & LOWRISC_OT_SPI_COMMAND_LEN_MASK)
		<< LOWRISC_OT_SPI_COMMAND_LEN_SH;
	command |= LOWRISC_OT_SPI_COMMAND_DIRECTION_TXRX;
	command |= LOWRISC_OT_SPI_COMMAND_SPEED_STD;

	if (hold_cs)
		command |= LOWRISC_OT_SPI_COMMAND_CSAAT;

	for (unsigned int n = 0; n < n_words; n++) {
		tx = UINT32_MAX;
		if (tx_ptr) {
			tx = get_unaligned_le32(tx_ptr);
			tx_ptr += 4;
		}
		writel(tx, &regs->txdata);
	}

	if (n_bytes % 4) {
		tx = UINT32_MAX;
		if (tx_ptr) {
			tx = 0;
			u32 byte;
			switch (n_bytes % 4) {
			case 3:
				byte = *(tx_ptr + 2);
				tx |= (byte << 16);
				fallthrough;
			case 2:
				byte = *(tx_ptr + 1);
				tx |= (byte << 8);
				fallthrough;
			case 1:
				byte = *(tx_ptr + 0);
				tx |= (byte << 0);
				fallthrough;
			default:
				break;
			}
		}
		writel(tx, &regs->txdata);
	}

	do {
		status = readl(&regs->status);
		ready = !!(status & LOWRISC_OT_SPI_STATUS_READY);
	} while (!ready);

	writel(command, &regs->command);

	do {
		status = readl(&regs->status);
		rxqd = (status >> LOWRISC_OT_SPI_STATUS_RXQD_SH)
			& LOWRISC_OT_SPI_STATUS_RXQD_MASK;
	} while (rxqd != qd);

	for (unsigned int n = 0; n < n_words; n++) {
		rx = readl(&regs->rxdata);
		if (rx_ptr) {
			put_unaligned_le32(rx, rx_ptr);
			rx_ptr += 4;
		}
	}

	if (n_bytes % 4) {
		rx = readl(&regs->rxdata);
		if (rx_ptr) {
			switch (n_bytes % 4) {
			case 3:
				*(rx_ptr + 2) = (rx >> 16) & 0xff;
				fallthrough;
			case 2:
				*(rx_ptr + 1) = (rx >> 8) & 0xff;
				fallthrough;
			case 1:
				*(rx_ptr + 0) = (rx >> 0) & 0xff;
				fallthrough;
			default:
				break;
			}
		}
	}
}

static int lowrisc_ot_spi_host_xfer(struct udevice *dev, unsigned int bitlen,
				    const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev->parent;
	struct lowrisc_ot_spi_host_priv *priv = dev_get_priv(bus);
	volatile struct lowrisc_ot_spi_host *regs = priv->regs;

	unsigned int n_bytes = bitlen >> 3;
	const u8 *tx_ptr = dout;
	u8 *rx_ptr = din;

	if (!n_bytes) {
		// There is no way to de-assert chip select specifically,
		// so we do the shortest transaction possible - a single
		// dummy cycle with CSAAT disabled.
		if (flags & SPI_XFER_END)
			writel(0, &regs->command);
		return 0;
	}

	while (n_bytes) {
		unsigned int n = min(n_bytes, 256u);
		bool last = (n == n_bytes);
		// hold CS if the transaction isn't over because we've split
		// it into 256-byte chunks, or if it is the last chunk but we
		// explicitly want to hold CS to not end the transfer.
		bool hold_cs = (!last || !(flags & SPI_XFER_END));

		lowrisc_ot_spi_host_xfer_part(regs, n, tx_ptr, rx_ptr, hold_cs);
		if (tx_ptr)
			tx_ptr += n;
		if (rx_ptr)
			rx_ptr += n;
		n_bytes -= n;
	}

	return 0;
}

static int lowrisc_ot_spi_host_cs_info(struct udevice *bus, uint cs,
				       struct spi_cs_info *info)
{
	if (cs != 0)
		return -EINVAL;

	return 0;
}

static int lowrisc_ot_spi_host_probe(struct udevice *bus)
{
	struct lowrisc_ot_spi_host_priv *priv = dev_get_priv(bus);
	volatile struct lowrisc_ot_spi_host *regs = dev_remap_addr(bus);

	if (!regs)
		return -ENODEV;

	priv->regs = regs;
	priv->freq = 50000000u; /* hardcode 50 MHz for now */

	writel(LOWRISC_OT_SPI_CONTROL_SPIEN | LOWRISC_OT_SPI_CONTROL_OUTPUTEN,
		&regs->control);

	writel(0, &regs->csid);

	return 0;
};

static const struct dm_spi_ops lowrisc_ot_spi_host_ops = {
	.xfer		= lowrisc_ot_spi_host_xfer,
	.set_speed	= lowrisc_ot_spi_host_set_speed,
	.set_mode	= lowrisc_ot_spi_host_set_mode,
	.cs_info 	= lowrisc_ot_spi_host_cs_info,
};

static const struct udevice_id lowrisc_ot_spi_host_match[] = {
	{ .compatible = "lowrisc,opentitan-spi-host-v3" },
	{ }
};

U_BOOT_DRIVER(lowrisc_ot_spi_host) = {
	.name		= "spi_lowrisc_opentitan",
	.id		= UCLASS_SPI,
	.of_match	= lowrisc_ot_spi_host_match,
	.priv_auto	= sizeof(struct lowrisc_ot_spi_host_priv),
	.ops		= &lowrisc_ot_spi_host_ops,
	.probe		= lowrisc_ot_spi_host_probe,
};
