/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_BACKENDS_BLOCK_POSIX_H
#define MODVM_BACKENDS_BLOCK_POSIX_H

#include <stdbool.h>

struct modvm_block;

struct modvm_block *modvm_block_posix_create(const char *path, bool readonly);
void modvm_block_posix_destroy(struct modvm_block *blk);

#endif /* MODVM_BACKENDS_BLOCK_POSIX_H */