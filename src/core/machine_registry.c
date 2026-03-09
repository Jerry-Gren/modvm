/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/machine.h>
#include <modvm/compiler.h>

/*
 * Maximum number of supported machine classes.
 * Using a static array avoids dynamic allocation during early boot
 * when constructor attributes are executing.
 */
#define MAX_MACHINE_CLASSES 16

static const struct vm_machine_class *machine_registry[MAX_MACHINE_CLASSES];
static int registered_machines = 0;

void vm_machine_class_register(const struct vm_machine_class *cls)
{
	if (registered_machines < MAX_MACHINE_CLASSES) {
		machine_registry[registered_machines++] = cls;
	}
}

const struct vm_machine_class *vm_machine_class_find(const char *name)
{
	int i;

	for (i = 0; i < registered_machines; i++) {
		if (strcmp(machine_registry[i]->name, name) == 0)
			return machine_registry[i];
	}

	return NULL;
}