/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include <modvm/core/block.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "posix_file: " fmt

struct block_posix_ctx {
	int fd;
	uint64_t capacity;
	bool readonly;
};

/**
 * block_posix_read - synchronously read from the host file
 * @blk: the block backend instance
 * @buf: destination buffer
 * @count: number of bytes to read
 * @offset: absolute byte offset within the file
 *
 * Return: number of bytes read, or a negative error code.
 */
static ssize_t block_posix_read(struct modvm_block *blk, void *buf,
				size_t count, uint64_t offset)
{
	struct block_posix_ctx *ctx = blk->priv;
	ssize_t ret;

	if (unlikely(count == 0))
		return 0;

	if (unlikely(offset >= ctx->capacity))
		return 0;

	ret = pread(ctx->fd, buf, count, (off_t)offset);
	if (unlikely(ret < 0))
		return -errno;

	return ret;
}

/**
 * block_posix_write - synchronously write to the host file
 * @blk: the block backend instance
 * @buf: source buffer
 * @count: number of bytes to write
 * @offset: absolute byte offset within the file
 *
 * Return: number of bytes written, or a negative error code.
 */
static ssize_t block_posix_write(struct modvm_block *blk, const void *buf,
				 size_t count, uint64_t offset)
{
	struct block_posix_ctx *ctx = blk->priv;
	ssize_t ret;

	if (unlikely(ctx->readonly))
		return -EPERM;

	if (unlikely(count == 0))
		return 0;

	if (unlikely(offset + count > ctx->capacity))
		return -ENOSPC;

	ret = pwrite(ctx->fd, buf, count, (off_t)offset);
	if (unlikely(ret < 0))
		return -errno;

	return ret;
}

/**
 * block_posix_get_capacity - retrieve the cached file size
 * @blk: the block backend instance
 *
 * Return: total capacity in bytes.
 */
static uint64_t block_posix_get_capacity(struct modvm_block *blk)
{
	struct block_posix_ctx *ctx = blk->priv;
	return ctx->capacity;
}

static const struct modvm_block_ops block_posix_ops = {
	.read = block_posix_read,
	.write = block_posix_write,
	.get_capacity = block_posix_get_capacity,
};

/**
 * modvm_block_posix_create - instantiate a file-backed block storage device
 * @path: host filesystem path to the raw disk image
 * @readonly: whether to open the image in read-only mode
 *
 * Return: initialized block backend pointer, or NULL on failure.
 */
struct modvm_block *modvm_block_posix_create(const char *path, bool readonly)
{
	struct modvm_block *blk;
	struct block_posix_ctx *ctx;
	struct stat st;
	int flags;

	if (WARN_ON(!path))
		return NULL;

	blk = calloc(1, sizeof(*blk));
	ctx = calloc(1, sizeof(*ctx));
	if (!blk || !ctx) {
		free(blk);
		free(ctx);
		return NULL;
	}

	flags = readonly ? O_RDONLY : O_RDWR;
	ctx->fd = open(path, flags | O_CLOEXEC);
	if (ctx->fd < 0) {
		pr_err("failed to open backing image '%s': %d\n", path, errno);
		goto err_free;
	}

	if (fstat(ctx->fd, &st) < 0) {
		pr_err("failed to probe image capacity '%s': %d\n", path,
		       errno);
		goto err_close;
	}

	if (!S_ISREG(st.st_mode) && !S_ISBLK(st.st_mode)) {
		pr_err("backing image '%s' must be a regular file or block device\n",
		       path);
		goto err_close;
	}

	ctx->capacity = (uint64_t)st.st_size;
	ctx->readonly = readonly;

	blk->name = "posix-file";
	blk->ops = &block_posix_ops;
	blk->priv = ctx;

	pr_info("mounted block backend '%s', capacity: %llu MB%s\n", path,
		ctx->capacity / (1024 * 1024), readonly ? " (RO)" : "");

	return blk;

err_close:
	close(ctx->fd);
err_free:
	free(ctx);
	free(blk);
	return NULL;
}

/**
 * modvm_block_posix_destroy - close file descriptors and free backend memory
 * @blk: the block backend instance to dismantle
 */
void modvm_block_posix_destroy(struct modvm_block *blk)
{
	struct block_posix_ctx *ctx;

	if (WARN_ON(!blk))
		return;

	ctx = blk->priv;
	if (ctx->fd >= 0)
		close(ctx->fd);

	free(ctx);
	free(blk);
}