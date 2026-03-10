/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/core/machine.h>
#include <modvm/utils/compiler.h>

/*
 * Maximum number of supported machine classes.
 * Using a static array avoids dynamic memory allocation during early
 * boot phases when constructor attributes are executing.
 */
#define MAX_MACHINE_CLASSES 16

static const struct vm_machine_class
	*machine_class_registry[MAX_MACHINE_CLASSES];
static int registered_machine_class_count = 0;

void vm_machine_class_register(const struct vm_machine_class *machine_class)
{
	if (registered_machine_class_count < MAX_MACHINE_CLASSES) {
		machine_class_registry[registered_machine_class_count++] =
			machine_class;
	}
}

const struct vm_machine_class *vm_machine_class_find(const char *name)
{
	int index;

	for (index = 0; index < registered_machine_class_count; index++) {
		if (strcmp(machine_class_registry[index]->name, name) == 0)
			return machine_class_registry[index];
	}

	return NULL;
}