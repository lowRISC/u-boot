// SPDX-License-Identifier: Apache-2.0
/*
 * lowRISC Opentitan GPIO driver
 *
 * Copyright (C) 2026 lowRISC Contributors.
 * Author: Alice Ziuziakowska <a.ziuziakowska@lowrisc.org>
 */

#include <dm.h>
#include <asm/arch/gpio.h>
#include <asm/io.h>
#include <errno.h>
#include <asm/gpio.h>
#include <linux/bitops.h>

#define OT_NR_GPIOS 32

struct lowrisc_ot_gpio {
	u32 unused[4]; /* interrupt registers */
	u32 data_in;
	u32 direct_out;
	u32 masked_out_lower;
	u32 masked_out_upper;
	u32 direct_oe;
	u32 masked_oe_lower;
	u32 masked_oe_upper;
};

struct lowrisc_ot_gpio_plat {
	struct lowrisc_ot_gpio *regs;
};

static int lowrisc_ot_gpio_probe(struct udevice *dev)
{
	struct lowrisc_ot_gpio_plat *plat = dev_get_plat(dev);
	struct gpio_dev_priv *priv = dev_get_uclass_priv(dev);
	char name[18], *str;

	sprintf(name, "gpio@%4lx_", (uintptr_t)plat->regs);
	str = strdup(name);
	if (!str)
		return -ENOMEM;
	priv->bank_name = str;

	priv->gpio_count = dev_read_u32_default(dev, "ngpios", OT_NR_GPIOS);

	return 0;
}

static int lowrisc_ot_gpio_get_value(struct udevice *dev, u32 offset)
{
	struct lowrisc_ot_gpio_plat *plat = dev_get_plat(dev);
	struct lowrisc_ot_gpio *regs = plat->regs;
	struct gpio_dev_priv *priv = dev_get_uclass_priv(dev);
	int val;
	u32 dir;

	if (offset > priv->gpio_count)
		return -EINVAL;

	dir = readl(&regs->direct_oe) & BIT(offset);
	if (dir)
		val = readl(&regs->direct_out) & BIT(offset);
	else
		val = readl(&regs->data_in) & BIT(offset);

	return val ? HIGH : LOW;
}

static int lowrisc_ot_gpio_set_value(struct udevice *dev, u32 offset, int value)
{
	struct lowrisc_ot_gpio_plat *plat = dev_get_plat(dev);
	struct lowrisc_ot_gpio *regs = plat->regs;
	struct gpio_dev_priv *priv = dev_get_uclass_priv(dev);
	u32 val;

	if (offset > priv->gpio_count)
		return -EINVAL;

	val = readl(&regs->direct_out);
	if (value)
		writel(val | BIT(offset), &regs->direct_out);
	else
		writel(val & ~BIT(offset), &regs->direct_out);

	return 0;
}

static int lowrisc_ot_gpio_get_function(struct udevice *dev, unsigned int offset)
{
	struct lowrisc_ot_gpio_plat *plat = dev_get_plat(dev);
	struct lowrisc_ot_gpio *regs = plat->regs;
	struct gpio_dev_priv *priv = dev_get_uclass_priv(dev);
	u32 dir;
	int val;

	if (offset > priv->gpio_count)
		return -EINVAL;

	dir = readl(&regs->direct_oe) & BIT(offset);
	if (dir)
		val = GPIOF_OUTPUT;
	else
		val = GPIOF_INPUT;

	return val;
}

static int lowrisc_ot_gpio_set_flags(struct udevice *dev, unsigned int offset, ulong flags)
{
	struct lowrisc_ot_gpio_plat *plat = dev_get_plat(dev);
	struct lowrisc_ot_gpio *regs = plat->regs;
	struct gpio_dev_priv *priv = dev_get_uclass_priv(dev);
	u32 val;
	u32 oe;

	if (offset > priv->gpio_count)
		return -EINVAL;

	if (flags & ~GPIOD_MASK_DIR)
		return -EINVAL;

	oe = readl(&regs->direct_oe);
	if (flags & GPIOD_IS_OUT) {
		val = readl(&regs->direct_out);
		if (flags & GPIOD_IS_OUT_ACTIVE)
			writel(val | BIT(offset), &regs->direct_out);
		else
			writel(val & ~BIT(offset), &regs->direct_out);
		writel(oe | BIT(offset), &regs->direct_oe);
	} else if (flags & GPIOD_IS_IN) {
		writel(oe & ~BIT(offset), &regs->direct_oe);
	}

	return 0;
}

static const struct udevice_id lowrisc_ot_gpio_match[] = {
	{ .compatible = "lowrisc,opentitan-gpio-v1" },
	{ }
};

static const struct dm_gpio_ops lowrisc_ot_gpio_ops = {
	.get_value	= lowrisc_ot_gpio_get_value,
	.set_value	= lowrisc_ot_gpio_set_value,
	.get_function	= lowrisc_ot_gpio_get_function,
	.set_flags	= lowrisc_ot_gpio_set_flags,
};

static int lowrisc_ot_gpio_of_to_plat(struct udevice *dev)
{
	struct lowrisc_ot_gpio_plat *plat = dev_get_plat(dev);

	plat->regs = dev_read_addr_ptr(dev);
	if (!plat->regs)
		return -ENXIO;

	return 0;
}

U_BOOT_DRIVER(lowrisc_ot_gpio) = {
	.name		= "gpio_lowrisc_opentitan",
	.id		= UCLASS_GPIO,
	.of_match	= lowrisc_ot_gpio_match,
	.of_to_plat	= of_match_ptr(lowrisc_ot_gpio_of_to_plat),
	.plat_auto	= sizeof(struct lowrisc_ot_gpio_plat),
	.ops		= &lowrisc_ot_gpio_ops,
	.probe		= lowrisc_ot_gpio_probe,
};
