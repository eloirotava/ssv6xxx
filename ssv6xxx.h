/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared entry points for the SSV6xxx SDIO parts.
 *
 * The SSV6051 (also sold as SSV6030) and the SSV6256 answer to the same
 * SDIO identity, so one driver has to serve both: the probe asks each
 * chip in turn to claim the card, and the one that recognises itself
 * keeps it.
 */
#ifndef SSV6XXX_H
#define SSV6XXX_H

#include <linux/mmc/sdio_func.h>

struct ssv6xxx_chip_ops;

/*
 * The first member of every chip's device structure, so that the shared
 * layer can find its way back from the card to the chip that owns it.
 */
struct ssv6xxx_common {
	const struct ssv6xxx_chip_ops *ops;
};

struct ssv6xxx_chip_ops {
	const char *name;
	int (*probe)(struct sdio_func *func);
	void (*remove)(struct sdio_func *func);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	void (*shutdown)(struct sdio_func *func);
};

extern const struct ssv6xxx_chip_ops ssv6051_chip_ops;
extern const struct ssv6xxx_chip_ops ssv6256_chip_ops;

#endif /* SSV6XXX_H */
