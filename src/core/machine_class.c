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

static const struct vm_machine_class *machine_classes[MAX_MACHINE_CLASSES];
static int nr_machine_classes = 0;

/**
 * vm_machine_class_register - register a new machine topological blueprint.
 * @cls: the machine class to register.
 */
void vm_machine_class_register(const struct vm_machine_class *cls)
{
	if (nr_machine_classes < MAX_MACHINE_CLASSES)
		machine_classes[nr_machine_classes++] = cls;
}

/**
 * vm_machine_class_find - retrieve a machine blueprint by name.
 * @name: the string identifier of the machine type.
 *
 * return: pointer to the machine class, or NULL if not found.
 */
const struct vm_machine_class *vm_machine_class_find(const char *name)
{
	int i;

	for (i = 0; i < nr_machine_classes; i++) {
		if (strcmp(machine_classes[i]->name, name) == 0)
			return machine_classes[i];
	}

	return NULL;
}