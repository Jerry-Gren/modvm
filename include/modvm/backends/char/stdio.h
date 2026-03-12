/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_BACKENDS_CHAR_STDIO_H
#define MODVM_BACKENDS_CHAR_STDIO_H

struct modvm_chardev;

struct modvm_chardev *modvm_chardev_stdio_create(void);
void modvm_chardev_stdio_destroy(struct modvm_chardev *dev);

#endif /* MODVM_BACKENDS_CHAR_STDIO_H */