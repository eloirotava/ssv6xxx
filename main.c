// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared SDIO entry for the SSV6xxx parts.
 *
 * The SSV6051 (sold as SSV6030 too) and the SSV6256 present the same
 * SDIO vendor and device identity, so a single driver has to serve
 * both.  Probe asks each chip in turn whether the card is its own; the
 * first one that recognises itself keeps it, and the others are never
 * touched again for that card.
 */
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include "ssv6xxx.h"

#ifndef SDIO_VENDOR_ID_SSV
#define SDIO_VENDOR_ID_SSV		0x3030
#define SDIO_DEVICE_ID_SSV_6XXX		0x3030
#endif

/*
 * The SSV6256 says what it is in plain text, so it is asked first; the
 * SSV6051 has no such answer and takes what is left.
 */
static const struct ssv6xxx_chip_ops * const ssv6xxx_chips[] = {
	&ssv6256_chip_ops,
	&ssv6051_chip_ops,
};

static struct sdio_func *ssv6xxx_reboot_func;

static int ssv6xxx_reboot_notify(struct notifier_block *nb, unsigned long event,
				 void *unused)
{
	struct sdio_func *func = ssv6xxx_reboot_func;
	struct ssv6xxx_common *common = func ? sdio_get_drvdata(func) : NULL;

	if (common && common->ops->shutdown)
		common->ops->shutdown(func);
	return NOTIFY_DONE;
}

static struct notifier_block ssv6xxx_reboot_nb = {
	.notifier_call = ssv6xxx_reboot_notify,
};

static int ssv6xxx_probe(struct sdio_func *func,
			 const struct sdio_device_id *id)
{
	int i, ret = -ENODEV;

	if (func->num != 1)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(ssv6xxx_chips); i++) {
		ret = ssv6xxx_chips[i]->probe(func);
		if (!ret) {
			dev_info(&func->dev, "%s\n", ssv6xxx_chips[i]->name);
			ssv6xxx_reboot_func = func;
			return 0;
		}
		if (ret != -ENODEV)
			return ret;
	}
	return ret;
}

static void ssv6xxx_remove(struct sdio_func *func)
{
	struct ssv6xxx_common *common = sdio_get_drvdata(func);

	if (ssv6xxx_reboot_func == func)
		ssv6xxx_reboot_func = NULL;
	if (common)
		common->ops->remove(func);
}

static int ssv6xxx_suspend(struct device *dev)
{
	struct ssv6xxx_common *common = dev_get_drvdata(dev);

	if (!common || !common->ops->suspend)
		return 0;
	return common->ops->suspend(dev);
}

static int ssv6xxx_resume(struct device *dev)
{
	struct ssv6xxx_common *common = dev_get_drvdata(dev);

	if (!common || !common->ops->resume)
		return 0;
	return common->ops->resume(dev);
}

static DEFINE_SIMPLE_DEV_PM_OPS(ssv6xxx_pm, ssv6xxx_suspend, ssv6xxx_resume);

static const struct sdio_device_id ssv6xxx_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_SSV, SDIO_DEVICE_ID_SSV_6XXX) },
	{ }
};
MODULE_DEVICE_TABLE(sdio, ssv6xxx_sdio_ids);

static struct sdio_driver ssv6xxx_sdio_driver = {
	.name = "ssv6xxx",
	.id_table = ssv6xxx_sdio_ids,
	.probe = ssv6xxx_probe,
	.remove = ssv6xxx_remove,
	.drv = {
		.pm = pm_sleep_ptr(&ssv6xxx_pm),
	},
};

static int __init ssv6xxx_init(void)
{
	int ret;

	register_reboot_notifier(&ssv6xxx_reboot_nb);
	ret = sdio_register_driver(&ssv6xxx_sdio_driver);
	if (ret)
		unregister_reboot_notifier(&ssv6xxx_reboot_nb);
	return ret;
}
module_init(ssv6xxx_init);

static void __exit ssv6xxx_exit(void)
{
	unregister_reboot_notifier(&ssv6xxx_reboot_nb);
	sdio_unregister_driver(&ssv6xxx_sdio_driver);
}
module_exit(ssv6xxx_exit);

MODULE_DESCRIPTION("iComm/South Silicon Valley SSV6xxx SDIO 802.11 driver");
MODULE_LICENSE("GPL");
