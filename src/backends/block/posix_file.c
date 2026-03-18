/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include <modvm/core/block.h>
#include <modvm/utils/cmdline.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "posix_file: " fmt

struct modvm_block_posix_ctx {
	int fd;
	uint64_t capacity;
	bool readonly;
};

/**
 * modvm_block_posix_read - synchronously read from the host file
 * @blk: the block backend instance
 * @buf: destination buffer
 * @count: number of bytes to read
 * @offset: absolute byte offset within the file
 *
 * Return: number of bytes read, or a negative error code.
 */
static ssize_t modvm_block_posix_read(struct modvm_block *blk, void *buf,
				      size_t count, uint64_t offset)
{
	struct modvm_block_posix_ctx *ctx = blk->priv;
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
 * modvm_block_posix_write - synchronously write to the host file
 * @blk: the block backend instance
 * @buf: source buffer
 * @count: number of bytes to write
 * @offset: absolute byte offset within the file
 *
 * Return: number of bytes written, or a negative error code.
 */
static ssize_t modvm_block_posix_write(struct modvm_block *blk, const void *buf,
				       size_t count, uint64_t offset)
{
	struct modvm_block_posix_ctx *ctx = blk->priv;
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
static uint64_t modvm_block_posix_get_capacity(struct modvm_block *blk)
{
	struct modvm_block_posix_ctx *ctx = blk->priv;
	return ctx->capacity;
}

static void modvm_block_posix_release(struct modvm_block *blk)
{
	struct modvm_block_posix_ctx *ctx;

	if (WARN_ON(!blk))
		return;

	ctx = blk->priv;
	if (ctx->fd >= 0)
		close(ctx->fd);

	free(ctx);
	free(blk);
}

static const struct modvm_block_ops modvm_block_posix_ops = {
	.read = modvm_block_posix_read,
	.write = modvm_block_posix_write,
	.get_capacity = modvm_block_posix_get_capacity,
	.release = modvm_block_posix_release,
};

/**
 * modvm_block_posix_create - instantiate a file-backed block storage device
 * @path: host filesystem path to the raw disk image
 * @readonly: whether to open the image in read-only mode
 *
 * Return: initialized block backend pointer, or NULL on failure.
 */
static struct modvm_block *modvm_block_posix_create(const char *opts)
{
	struct modvm_block *blk;
	struct modvm_block_posix_ctx *ctx;
	struct stat st;
	char *path;
	char *ro_str;
	bool readonly = false;
	int flags;

	path = cmdline_extract_opt(opts, "path");
	if (!path) {
		pr_err("posix-file requires 'path=' argument\n");
		return NULL;
	}

	ro_str = cmdline_extract_opt(opts, "readonly");
	if (ro_str && (strcmp(ro_str, "on") == 0 || strcmp(ro_str, "1") == 0))
		readonly = true;
	free(ro_str);

	blk = calloc(1, sizeof(*blk));
	ctx = calloc(1, sizeof(*ctx));
	if (!blk || !ctx) {
		free(path);
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
	blk->ops = &modvm_block_posix_ops;
	blk->priv = ctx;

	pr_info("mounted block backend '%s', capacity: %llu MB%s\n", path,
		ctx->capacity / (1024 * 1024), readonly ? " (RO)" : "");

	free(path);
	return blk;

err_close:
	close(ctx->fd);
err_free:
	free(path);
	free(ctx);
	free(blk);
	return NULL;
}

static const struct modvm_block_driver posix_file_driver = {
	.name = "posix-file",
	.create = modvm_block_posix_create,
};

static void __attribute__((constructor)) modvm_block_posix_register(void)
{
	modvm_block_driver_register(&posix_file_driver);
}