/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_BACKENDS_CHARACTER_STDIO_H
#define MODVM_BACKENDS_CHARACTER_STDIO_H

struct vm_character_device;

struct vm_character_device *vm_character_device_stdio_create(void);

void vm_character_device_stdio_destroy(struct vm_character_device *device);

#endif /* MODVM_BACKENDS_CHARACTER_STDIO_H */