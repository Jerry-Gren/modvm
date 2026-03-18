/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>
#include <modvm/core/net.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>

#define MAX_NET_DRIVERS 16
static const struct modvm_net_driver *net_drivers[MAX_NET_DRIVERS];
static int nr_net_drivers = 0;

void modvm_net_driver_register(const struct modvm_net_driver *drv)
{
	if (WARN_ON(!drv || !drv->name || !drv->create))
		return;
	if (WARN_ON(nr_net_drivers >= MAX_NET_DRIVERS))
		return;
	net_drivers[nr_net_drivers++] = drv;
}

struct modvm_net *modvm_net_create(const char *name, const char *opts)
{
	int i;
	if (WARN_ON(!name))
		return NULL;
	for (i = 0; i < nr_net_drivers; i++) {
		if (strcmp(net_drivers[i]->name, name) == 0)
			return net_drivers[i]->create(opts);
	}
	pr_err("net driver '%s' not found\n", name);
	return NULL;
}

void modvm_net_release(struct modvm_net *net)
{
	if (!net)
		return;
	if (net->ops && net->ops->release)
		net->ops->release(net);
}