/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_CORE_BLOCK_H
#define MODVM_CORE_BLOCK_H

#include <stdint.h>
#include <sys/types.h>

struct modvm_block;

/**
 * struct modvm_block_ops - operations for host block storage backends
 * @read: read data from the storage medium at a specific offset
 * @write: write data to the storage medium at a specific offset
 * @get_capacity: retrieve the total size of the storage medium in bytes
 * @release: ?
 */
struct modvm_block_ops {
	ssize_t (*read)(struct modvm_block *blk, void *buf, size_t count,
			uint64_t offset);
	ssize_t (*write)(struct modvm_block *blk, const void *buf, size_t count,
			 uint64_t offset);
	uint64_t (*get_capacity)(struct modvm_block *blk);
	void (*release)(struct modvm_block *blk);
};

/**
 * struct modvm_block - represents a host storage backend instance
 * @name: human-readable identifier for the storage backend
 * @ops: dispatch table for backend operations
 * @priv: backend-specific operational context (e.g., file descriptors)
 */
struct modvm_block {
	const char *name;
	const struct modvm_block_ops *ops;
	void *priv;
};

struct modvm_block_driver {
	const char *name;
	struct modvm_block *(*create)(const char *opts);
};

void modvm_block_driver_register(const struct modvm_block_driver *drv);
struct modvm_block *modvm_block_create(const char *name, const char *opts);
void modvm_block_release(struct modvm_block *blk);

#endif /* MODVM_CORE_BLOCK_H */