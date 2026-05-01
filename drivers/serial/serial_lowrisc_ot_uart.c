// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2026 lowRISC Contributors.
 * Author: Alice Ziuziakowska <a.ziuziakowska@lowrisc.org>
 */

#include <clk.h>
#include <debug_uart.h>
#include <dm.h>
#include <errno.h>
#include <fdtdec.h>
#include <log.h>
#include <watchdog.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/compiler.h>
#include <serial.h>
#include <linux/err.h>

DECLARE_GLOBAL_DATA_PTR;

struct lowrisc_ot_uart {
	u32 unused[4]; /* interrupt registers */
	u32 ctrl;
	u32 status;
	u32 rdata;
	u32 wdata;
	u32 fifo_ctrl;
	u32 fifo_status;
};

#define UART_STATUS_TXFULL 0x1
#define UART_STATUS_RXEMPTY 0x20
#define UART_FIFO_CTRL_RXRST 0x1
#define UART_FIFO_CTRL_TXRST 0x2
#define UART_FIFO_STATUS_TXLVL_MASK 0xff
#define UART_FIFO_STATUS_TXLVL_SH 0
#define UART_FIFO_STATUS_RXLVL_MASK 0xff
#define UART_FIFO_STATUS_RXLVL_SH 16

struct lowrisc_ot_uart_plat {
	struct lowrisc_ot_uart *regs;
};

static int lowrisc_ot_uart_serial_probe(struct udevice *dev)
{
	 struct lowrisc_ot_uart_plat *plat = dev_get_plat(dev);
	 struct lowrisc_ot_uart *regs = plat->regs;

	/* No need to reinitialize the UART after relocation */
	if (gd->flags & GD_FLG_RELOC)
		return 0;

	writel(UART_FIFO_CTRL_RXRST | UART_FIFO_CTRL_TXRST, &regs->fifo_ctrl);

	/* TODO set up baud */
	return 0;
}

static int lowrisc_ot_uart_serial_of_to_plat(struct udevice *dev)
{
	struct lowrisc_ot_uart_plat *plat = dev_get_plat(dev);

	plat->regs = (struct lowrisc_ot_uart *)dev_read_addr_ptr(dev);
	if (!plat->regs)
		return -ENXIO;

	return 0;
}

static int lowrisc_ot_uart_putc(struct udevice *dev, const char ch)
{
	struct lowrisc_ot_uart_plat *plat = dev_get_plat(dev);
	struct lowrisc_ot_uart *regs = plat->regs;

	while (readl(&regs->status) & UART_STATUS_TXFULL) {}

	writel((u32)ch, &regs->wdata);

	return 0;
}

static int lowrisc_ot_uart_getc(struct udevice *dev)
{
	struct lowrisc_ot_uart_plat *plat = dev_get_plat(dev);
	struct lowrisc_ot_uart *regs = plat->regs;

	if (readl(&regs->status) & UART_STATUS_RXEMPTY)
	    return -EAGAIN;

	int c = (u8)readl(&regs->rdata);

	return c;
}

static int lowrisc_ot_uart_pending(struct udevice *dev, bool input)
{
	struct lowrisc_ot_uart_plat *plat = dev_get_plat(dev);
	struct lowrisc_ot_uart *regs = plat->regs;

	if (input)
		return (readl(&regs->fifo_status) >> UART_FIFO_STATUS_RXLVL_SH) & UART_FIFO_STATUS_RXLVL_MASK;
	else
		return (readl(&regs->fifo_status) >> UART_FIFO_STATUS_TXLVL_SH) & UART_FIFO_STATUS_TXLVL_MASK;
}

static int lowrisc_ot_uart_setbrg(struct udevice *dev, int baudrate)
{
	/* TODO: setup baud */
	return 0;
}

static const struct dm_serial_ops lowrisc_ot_uart_serial_ops = {
	.putc = lowrisc_ot_uart_putc,
	.getc = lowrisc_ot_uart_getc,
	.pending = lowrisc_ot_uart_pending,
	.setbrg = lowrisc_ot_uart_setbrg,
};

static const struct udevice_id lowrisc_ot_uart_serial_ids[] = {
	{ .compatible = "lowrisc,opentitan-uart-v2" },
	{ }
};

U_BOOT_DRIVER(serial_ot_uart) = {
	.name		= "serial_lowrisc_opentitan_uart",
	.id			= UCLASS_SERIAL,
	.of_match	= lowrisc_ot_uart_serial_ids,
	.of_to_plat = lowrisc_ot_uart_serial_of_to_plat,
	.plat_auto	= sizeof(struct lowrisc_ot_uart_plat),
	.probe		= lowrisc_ot_uart_serial_probe,
	.ops		= &lowrisc_ot_uart_serial_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
