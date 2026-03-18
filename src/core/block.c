/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <modvm/core/block.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>

#define MAX_BLOCK_DRIVERS 16
static const struct modvm_block_driver *block_drivers[MAX_BLOCK_DRIVERS];
static int nr_block_drivers = 0;

void modvm_block_driver_register(const struct modvm_block_driver *drv)
{
	if (WARN_ON(!drv || !drv->name || !drv->create))
		return;
	if (WARN_ON(nr_block_drivers >= MAX_BLOCK_DRIVERS))
		return;
	block_drivers[nr_block_drivers++] = drv;
}

struct modvm_block *modvm_block_create(const char *name, const char *opts)
{
	int i;
	if (WARN_ON(!name))
		return NULL;
	for (i = 0; i < nr_block_drivers; i++) {
		if (strcmp(block_drivers[i]->name, name) == 0)
			return block_drivers[i]->create(opts);
	}
	pr_err("block driver '%s' not found\n", name);
	return NULL;
}

void modvm_block_release(struct modvm_block *blk)
{
	if (!blk)
		return;
	if (blk->ops && blk->ops->release)
		blk->ops->release(blk);
}