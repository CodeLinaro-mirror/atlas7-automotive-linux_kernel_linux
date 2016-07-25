/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <linux/dma-mapping.h>
#include "../../dsp.h"
#include "../../iacc.h"
#include "../../i2s.h"
#include "../../usp-pcm.h"

/* Implements "start, stop, config" of IACC, I2S, USP. Original functions
 * in obj->ops are replaced directly. It eliminates one level of indirection,
 * at the cost of some redundant code.
 */

/* IACC implementation */
static int hw_iacc_config(struct kasobj_hw *hw)
{
	/* No configuration needed for IACC digital part */
	if (kcm_force_iacc_cap)
		iacc_setup(hw->db->is_sink, hw->channels,
			hw->param, hw->rate, 0);
	return 0;
}

static int hw_iacc_start(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt++ == 0) {
		iacc_start(hw->db->is_sink, hw->channels);
		kcm_debug("HW '%s' started\n", obj->name);
	}
	return 0;
}

static int hw_iacc_stop(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt && --obj->start_cnt == 0) {
		iacc_stop(hw->db->is_sink);
		kcm_debug("HW '%s' stopped\n", obj->name);
	}
	return 0;
}

/* I2S implementation */
static int hw_i2s_config(struct kasobj_hw *hw)
{
	struct i2s_params param;

	param.channels = hw->channels;
	param.rate = hw->rate;

	/*
	 * If i2s is master mode, that means the device is salve mode.
	 * The endpointer clock master configuration is set the external
	 * device clock mode. So if The i2s host is master mode, the endpoint
	 * clock mode must be set slave mode.
	 */
	param.slave = !(hw->db->is_slave);
	param.playback = hw->db->is_sink;

	return sirf_i2s_params_adv(&param);
}

static int hw_i2s_start(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt++ == 0) {
		sirf_i2s_start(hw->db->is_sink);
		kcm_debug("HW '%s' started\n", obj->name);
	}
	return 0;
}

static int hw_i2s_stop(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt && --obj->start_cnt == 0) {
		sirf_i2s_stop(hw->db->is_sink);
		kcm_debug("HW '%s' stopped\n", obj->name);
	}
	return 0;
}

/* USP implementation */
static int hw_usp_config(struct kasobj_hw *hw)
{
	sirf_usp_pcm_params(hw->param, hw->db->is_sink, hw->channels, hw->rate);
	return 0;
}

static int hw_usp_start(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt++ == 0) {
		sirf_usp_pcm_start(hw->param, hw->db->is_sink);
		kcm_debug("HW '%s' started\n", obj->name);
	}
	return 0;
}

static int hw_usp_stop(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);

	BUG_ON(!obj->life_cnt);
	if (obj->start_cnt && --obj->start_cnt == 0) {
		sirf_usp_pcm_stop(hw->param, hw->db->is_sink);
		kcm_debug("HW '%s' stopped\n", obj->name);
	}
	return 0;
}

static struct hw_name_ops {
	const char *name;
	int (*config)(struct kasobj_hw *hw);
	int (*start)(struct kasobj *obj);
	int (*stop)(struct kasobj *obj);
	int ep_type;
	int ep_phy_dev;
	int port;	/* Only for USP: 0~3 */
} _hw_name_ops[] = {
	{ "iacc_linein", hw_iacc_config, hw_iacc_start, hw_iacc_stop,
		ENDPOINT_TYPE_IACC, ENDPOINT_PHY_DEV_IACC, 4 },
	{ "iacc", hw_iacc_config, hw_iacc_start, hw_iacc_stop,
		ENDPOINT_TYPE_IACC, ENDPOINT_PHY_DEV_IACC, 0 },
	{ "usp3", hw_usp_config, hw_usp_start, hw_usp_stop,
		ENDPOINT_TYPE_USP, ENDPOINT_PHY_DEV_A7CA, 3 },
	{ "usp2", hw_usp_config, hw_usp_start, hw_usp_stop,
		ENDPOINT_TYPE_USP, ENDPOINT_PHY_DEV_PCM2, 2 },
	{ "usp1", hw_usp_config, hw_usp_start, hw_usp_stop,
		ENDPOINT_TYPE_USP, ENDPOINT_PHY_DEV_PCM1, 1 },
	{ "usp0", hw_usp_config, hw_usp_start, hw_usp_stop,
		ENDPOINT_TYPE_USP, ENDPOINT_PHY_DEV_PCM0, 0 },
	{ "i2s", hw_i2s_config, hw_i2s_start, hw_i2s_stop,
		ENDPOINT_TYPE_I2S, ENDPOINT_PHY_DEV_I2S1, 0 },
};

static struct hw_name_ops *hw_find_ops(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(_hw_name_ops); i++) {
		if (kcm_strcasestr(name, _hw_name_ops[i].name))
			return &_hw_name_ops[i];
	}

	return NULL;
}

static int hw_init(struct kasobj *obj)
{
	int i;
	struct kasobj_hw *hw = kasobj_to_hw(obj);
	const struct kasdb_hw *db = hw->db;
	struct hw_name_ops *ops = hw_find_ops(db->name.s);

	/* Replace start, stop implementation */
	if (!ops) {
		pr_err("KASHW: unknown hardware '%s'!\n", db->name.s);
		return -EINVAL;
	}

	hw->param = ops->port;
	for (i = 0; i < db->max_channels; i++)
		hw->ep_id[i] = KCM_INVALID_EP_ID;

	return 0;
}

static int hw_start(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);
	const struct kasdb_hw *db = hw->db;
	struct hw_name_ops *ops = hw_find_ops(db->name.s);

	if (!obj->life_cnt)
		return 0;

	ops->start(obj);

	return 0;
}

static int hw_stop(struct kasobj *obj)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);
	const struct kasdb_hw *db = hw->db;
	struct hw_name_ops *ops = hw_find_ops(db->name.s);

	if (!obj->life_cnt)
		return 0;

	ops->stop(obj);

	return 0;
}

static int hw_get(struct kasobj *obj, const struct kasobj_param *param)
{
	int i;
	int ret = 0;
	struct kasobj_hw *hw = kasobj_to_hw(obj);
	const struct kasdb_hw *db = hw->db;
	struct hw_name_ops *ops = hw_find_ops(db->name.s);

	if (obj->life_cnt++) {
		kcm_debug("HW '%s' refcnt++: %d\n", obj->name, obj->life_cnt);
		return 0;
	}

	/* Get rate and channel count */
	hw->channels = db->def_channels;
	hw->rate = db->def_rate;
	if (hw->channels == 0)
		hw->channels = param->channels;
	if (hw->rate == 0)
		hw->rate = param->rate;

	/* Configure audio controller */
	ret = ops->config(hw);
	if (ret) {
		kcm_debug("KASHW: config hw error!\n");
		return -EINVAL;
	}

	/* Allocate audio buffer */
	hw->buff_bytes = db->bytes_per_ch * hw->channels;
	hw->ep_handle = dma_zalloc_coherent(kcm_get_dev(),
			sizeof(struct endpoint_handle),
			&hw->ep_handle_pa, GFP_KERNEL);
	if (!hw->ep_handle) {
		pr_err("KASHW: allocate buffer failure\n");
		return -ENOMEM;
	}
	hw->ep_handle->buff_length = hw->buff_bytes / sizeof(u32);
	hw->buff = dma_zalloc_coherent(kcm_get_dev(), hw->buff_bytes,
			&hw->ep_handle->buff_addr, GFP_KERNEL);
	if (!hw->buff) {
		pr_err("KASHW: cannot allocate buffer, size = %u!\n",
				hw->buff_bytes);
		dma_free_coherent(kcm_get_dev(), sizeof(struct endpoint_handle),
				hw->ep_handle, hw->ep_handle_pa);
		obj->life_cnt--;
		return -ENOMEM;
	}

	/* Create Endpoints */
	if (db->is_sink)
		kalimba_get_sink(ops->ep_type, ops->ep_phy_dev,
				hw->channels, hw->ep_handle_pa,
				hw->ep_id, __kcm_resp);
	else
		kalimba_get_source(ops->ep_type, ops->ep_phy_dev,
				hw->channels, hw->ep_handle_pa,
				hw->ep_id, __kcm_resp);
	for (i = 0; i < hw->channels; i++) {
		kalimba_config_endpoint(hw->ep_id[i],
				ENDPOINT_CONF_AUDIO_SAMPLE_RATE,
				hw->rate, __kcm_resp);
		kalimba_config_endpoint(hw->ep_id[i],
				ENDPOINT_CONF_AUDIO_DATA_FORMAT,
				db->audio_format, __kcm_resp);
		kalimba_config_endpoint(hw->ep_id[i],
				ENDPOINT_CONF_DRAM_PACKING_FORMAT,
				db->pack_format, __kcm_resp);
		kalimba_config_endpoint(hw->ep_id[i],
				ENDPOINT_CONF_INTERLEAVING_MODE,
				1, __kcm_resp);	/* Needs configure? */
		kalimba_config_endpoint(hw->ep_id[i],
				ENDPOINT_CONF_CLOCK_MASTER,
				!db->is_slave, __kcm_resp);
	}

	kcm_debug("HW '%s' created\n", obj->name);
	return 0;
}

static int hw_put(struct kasobj *obj)
{
	int i;
	struct kasobj_hw *hw = kasobj_to_hw(obj);
	const struct kasdb_hw *db = hw->db;

	BUG_ON(!obj->life_cnt);
	if (--obj->life_cnt) {
		kcm_debug("HW '%s' refcnt--: %d\n", obj->name, obj->life_cnt);
		return 0;
	}
	BUG_ON(obj->start_cnt);

	if (hw->ep_id[0] != KCM_INVALID_EP_ID) {
		if (hw->db->is_sink)
			kalimba_close_sink(hw->channels, hw->ep_id,
					__kcm_resp);
		else
			kalimba_close_source(hw->channels, hw->ep_id,
					__kcm_resp);
	}
	for (i = 0; i < db->max_channels; i++)
		hw->ep_id[i] = KCM_INVALID_EP_ID;

	dma_free_coherent(kcm_get_dev(), hw->buff_bytes,
			hw->buff, hw->ep_handle->buff_addr);

	memset(hw->ep_handle, 0, sizeof(struct endpoint_handle));
	dma_free_coherent(kcm_get_dev(), sizeof(struct endpoint_handle),
			hw->ep_handle, hw->ep_handle_pa);
	hw->ep_handle = NULL;
	hw->ep_handle_pa = 0;

	kcm_debug("HW '%s' destroyed\n", obj->name);
	return 0;
}

static u16 hw_get_ep(struct kasobj *obj, unsigned pin, int is_sink)
{
	struct kasobj_hw *hw = kasobj_to_hw(obj);

	BUG_ON(pin >= hw->db->max_channels);
	return hw->ep_id[pin];
}

static struct kasobj_ops hw_ops = {
	.init = hw_init,
	.get = hw_get,
	.put = hw_put,
	.get_ep = hw_get_ep,
	.start = hw_start,
	.stop = hw_stop,
};
