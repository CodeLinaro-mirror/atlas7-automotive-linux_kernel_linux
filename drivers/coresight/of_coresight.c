/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/coresight.h>
#include <asm/smp_plat.h>

static int of_get_coresight_id(struct device_node *node, int *id)
{
	const __be32 *reg;
	u64 addr;

	/* derive component id from its memory map */
	reg = of_get_property(node, "reg", NULL);
	if (reg) {
		addr = of_translate_address(node, reg);
		if (addr != OF_BAD_ADDR) {
			*id = addr;
			return 0;
		}
	}

	/* no "reg", we have a non-configurable replicator */
	reg = of_get_property(node, "id", NULL);
	if (reg) {
		*id = of_read_ulong(reg, 1);
		return 0;
	}

	return -EINVAL;
}

static struct device_node *of_get_coresight_endpoint(
		const struct device_node *parent, struct device_node *prev)
{
	struct device_node *node = of_graph_get_next_endpoint(parent, prev);

	of_node_put(prev);
	return node;
}

static void of_coresight_get_ports(struct device_node *node,
				   int *nr_inports, int *nr_outports)
{
	struct device_node *ep = NULL;
	int in = 0, out = 0;

	do {
		ep = of_get_coresight_endpoint(node, ep);
		if (!ep)
			break;

		if (of_property_read_bool(ep, "slave-mode"))
			in++;
		else
			out++;

	} while (ep);

	*nr_inports = in;
	*nr_outports = out;
}

static int of_coresight_alloc_memory(struct device *dev,
			struct coresight_platform_data *pdata)
{
	/* list of output port on this component */
	pdata->outports = devm_kzalloc(dev, pdata->nr_outports *
				       sizeof(*pdata->outports),
				       GFP_KERNEL);
	if (!pdata->outports)
		return -ENOMEM;


	/* children connected to this component via @outport */
	pdata->child_ids = devm_kzalloc(dev, pdata->nr_outports *
					sizeof(*pdata->child_ids),
					GFP_KERNEL);
	if (!pdata->child_ids)
		return -ENOMEM;

	/* port number on the child this component is connected to */
	pdata->child_ports = devm_kzalloc(dev, pdata->nr_outports *
					  sizeof(*pdata->child_ports),
					  GFP_KERNEL);
	if (!pdata->child_ports)
		return -ENOMEM;

	return 0;
}

struct coresight_platform_data *of_get_coresight_platform_data(
				struct device *dev, struct device_node *node)
{
	int id, i = 0, ret = 0;
	struct device_node *cpu;
	struct coresight_platform_data *pdata;
	struct of_endpoint endpoint, rendpoint;
	struct device_node *ep = NULL;
	struct device_node *rparent = NULL;
	struct device_node *rport = NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	/* use the base address as id */
	ret = of_get_coresight_id(node, &id);
	if (ret)
		return ERR_PTR(ret);
	pdata->id = id;

	/* use device name as debugfs handle */
	pdata->name = dev_name(dev);

	/* get the number of input and output port for this component */
	of_coresight_get_ports(node, &pdata->nr_inports, &pdata->nr_outports);

	if (pdata->nr_outports) {
		ret = of_coresight_alloc_memory(dev, pdata);
		if (ret)
			return ERR_PTR(ret);

		/* iterate through each port to discover topology */
		do {
			/* get a handle on a port */
			ep = of_get_coresight_endpoint(node, ep);
			if (!ep)
				break;

			/* no need to deal with input ports, processing for as
			 * processing for output ports will deal with them.
			 */
			if (of_find_property(ep, "slave-mode", NULL))
				continue;

			/* get a handle on the local endpoint */
			ret = of_graph_parse_endpoint(ep, &endpoint);

			if (ret)
				continue;

			/* the local out port number */
			pdata->outports[i] = endpoint.id;

			/* get a handle the remote port and parent
			 * attached to it.
			 */
			rparent = of_graph_get_remote_port_parent(ep);
			rport = of_graph_get_remote_port(ep);

			if (!rparent || !rport)
				continue;

			if (of_graph_parse_endpoint(rport, &rendpoint))
				continue;

			ret = of_get_coresight_id(rparent, &id);
			if (ret)
				continue;
			pdata->child_ids[i] = id;
			pdata->child_ports[i] = rendpoint.id;

			i++;
		} while (ep);
	}

	/* affinity defaults to CPU0 */
	pdata->cpu = 0;
	cpu = of_parse_phandle(node, "cpu", 0);
	if (cpu) {
		const u32 *mpidr;
		int len, index;

		mpidr = of_get_property(cpu, "reg", &len);
		if (mpidr && len == 4) {
			index = get_logical_index(be32_to_cpup(mpidr));
			if (index != -EINVAL)
				pdata->cpu = index;
		}
	}

	return pdata;
}
EXPORT_SYMBOL_GPL(of_get_coresight_platform_data);
