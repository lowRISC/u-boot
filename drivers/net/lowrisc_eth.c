// SPDX-License-Identifier: Apache-2.0
/*
 * lowRISC Ethernet Controller driver.
 *
 * Copyright (C) 2026 lowRISC Contributors.
 * Author: Alice Ziuziakowska <a.ziuziakowska@lowrisc.org>
 */

#include <dm.h>
#include <log.h>
#include <net.h>

struct lowrisc_eth {
	// TODO: register layout.
};

struct lowrisc_eth_priv {
	volatile struct lowrisc_eth *regs;
};

static int lowrisc_eth_probe(struct udevice *dev)
{
	struct lowrisc_eth_priv *priv = dev_get_priv(dev);
	volatile struct lowrisc_eth *regs = dev_remap_addr(dev);

	if (!regs)
		return -ENODEV;

	priv->regs = regs;

	return 0;
}

static const struct eth_ops lowrisc_eth_ops = {};

static const struct udevice_id lowrisc_eth_match[] = {
	{ .compatible = "lowrisc,eth-v1" },
	{ }
};

U_BOOT_DRIVER(lowrisc_eth) = {
	.name		= "lowrisc_eth",
	.id		= UCLASS_ETH,
	.of_match	= lowrisc_eth_match,
	.probe		= lowrisc_eth_probe,
	.ops		= &lowrisc_eth_ops,
	.priv_auto	= sizeof(struct lowrisc_eth_priv),
	.plat_auto	= sizeof(struct eth_pdata),
};
