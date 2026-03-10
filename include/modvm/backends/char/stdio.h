/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_BACKENDS_CHAR_STDIO_H
#define MODVM_BACKENDS_CHAR_STDIO_H

struct vm_chardev;

struct vm_chardev *vm_chardev_stdio_create(void);

void vm_chardev_stdio_destroy(struct vm_chardev *dev);

#endif /* MODVM_BACKENDS_CHAR_STDIO_H */