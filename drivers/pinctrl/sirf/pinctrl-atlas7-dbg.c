/*
 * pinctrl pads, groups, functions for CSR SiRFatlasVII
 *
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
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

#define __PINCTRL_ATLAS7_DEBUG__

#ifdef __PINCTRL_ATLAS7_DEBUG__

#include <linux/sysfs.h>

static	spinlock_t s_sysfs_lock;
static	char *s_sysfs_buf;
static	size_t s_sysfs_buf_size;

struct d_input_desc {
	const char *func_name;
	int dinput_reg;
	int dinput_bit;
	int dinput_val_reg;
	int dinput_val_bit;
};


#define DINPUT(f, dr, dvr, db, dvb)	\
	{				\
		.func_name = #f,	\
		.dinput_reg = dr,	\
		.dinput_bit = db,	\
		.dinput_val_reg = dvr,	\
		.dinput_val_bit = dvb,	\
	}

static struct d_input_desc pmx_d_input_desc_list[] = {
	DINPUT(au__urxd_2_mux0, 0x0A00, 0x0A80, 24, 24),
	DINPUT(au__urxd_2_mux1, 0x0A00, 0x0A80, 24, 24),
	DINPUT(au__usclk_2_mux0, 0x0A00, 0x0A80, 23, 23),
	DINPUT(au__usclk_2_mux1, 0x0A00, 0x0A80, 23, 23),
	DINPUT(au__utfs_2_mux0, 0x0A00, 0x0A80, 22, 22),
	DINPUT(au__utfs_2_mux1, 0x0A00, 0x0A80, 22, 22),
	DINPUT(au__utxd_2_mux0, 0x0A00, 0x0A80, 25, 25),
	DINPUT(au__utxd_2_mux1, 0x0A00, 0x0A80, 25, 25),
	DINPUT(c0__can_rxd_0_mux0, 0x0A08, 0x0A88, 9, 9),
	DINPUT(c0__can_rxd_0_mux1, 0x0A08, 0x0A88, 9, 9),
	DINPUT(c1__can_rxd_1_mux0, 0x0A00, 0x0A80, 4, 4),
	DINPUT(c1__can_rxd_1_mux1, 0x0A00, 0x0A80, 4, 4),
	DINPUT(c1__can_rxd_1_mux2, 0x0A00, 0x0A80, 4, 4),
	DINPUT(ca__spi_func_csb_mux0, 0x0A08, 0x0A88, 6, 6),
	DINPUT(clkc__trg_ref_clk_mux0, 0x0A08, 0x0A88, 14, 14),
	DINPUT(clkc__trg_ref_clk_mux1, 0x0A08, 0x0A88, 14, 14),
	DINPUT(gn__gnss_irq1_mux0, 0x0A08, 0x0A88, 10, 10),
	DINPUT(gn__gnss_irq2_mux0, 0x0A08, 0x0A88, 11, 11),
	DINPUT(gn__gnss_m0_porst_b_mux0, 0x0A08, 0x0A88, 12, 12),
	DINPUT(gn__trg_acq_clk_mux0, 0x0A00, 0x0A80, 6, 6),
	DINPUT(gn__trg_acq_clk_mux1, 0x0A00, 0x0A80, 6, 6),
	DINPUT(gn__trg_acq_d0_mux0, 0x0A00, 0x0A80, 7, 7),
	DINPUT(gn__trg_acq_d0_mux1, 0x0A00, 0x0A80, 7, 7),
	DINPUT(gn__trg_acq_d1_mux0, 0x0A00, 0x0A80, 8, 8),
	DINPUT(gn__trg_acq_d1_mux1, 0x0A00, 0x0A80, 8, 8),
	DINPUT(gn__trg_irq_b_mux0, 0x0A00, 0x0A80, 9, 9),
	DINPUT(gn__trg_irq_b_mux1, 0x0A00, 0x0A80, 9, 9),
	DINPUT(gn__trg_spi_di_mux0, 0x0A00, 0x0A80, 10, 10),
	DINPUT(gn__trg_spi_di_mux1, 0x0A00, 0x0A80, 10, 10),
	DINPUT(jtag__jt_dbg_nsrst_mux0, 0x0A08, 0x0A88, 2, 2),
	DINPUT(jtag__ntrst_mux0, 0x0A08, 0x0A88, 3, 3),
	DINPUT(ks__kas_spi_cs_n_mux0, 0x0A08, 0x0A88, 8, 8),
	DINPUT(pwc__lowbatt_b_mux0, 0x0A08, 0x0A88, 4, 4),
	DINPUT(pwc__on_key_b_mux0, 0x0A08, 0x0A88, 5, 5),
	DINPUT(rg__gmac_phy_intr_n_mux0, 0x0A08, 0x0A88, 13, 13),
	DINPUT(sd1__sd_dat_1_0_mux0, 0x0A00, 0x0A80, 0, 0),
	DINPUT(sd1__sd_dat_1_0_mux1, 0x0A00, 0x0A80, 0, 0),
	DINPUT(sd1__sd_dat_1_1_mux0, 0x0A00, 0x0A80, 1, 1),
	DINPUT(sd1__sd_dat_1_1_mux1, 0x0A00, 0x0A80, 1, 1),
	DINPUT(sd1__sd_dat_1_2_mux0, 0x0A00, 0x0A80, 2, 2),
	DINPUT(sd1__sd_dat_1_2_mux1, 0x0A00, 0x0A80, 2, 2),
	DINPUT(sd1__sd_dat_1_3_mux0, 0x0A00, 0x0A80, 3, 3),
	DINPUT(sd1__sd_dat_1_3_mux1, 0x0A00, 0x0A80, 3, 3),
	DINPUT(sd2__sd_cd_b_2_mux0, 0x0A08, 0x0A88, 7, 7),
	DINPUT(sd6__sd_clk_6_mux0, 0x0A00, 0x0A80, 27, 27),
	DINPUT(sd6__sd_clk_6_mux1, 0x0A00, 0x0A80, 27, 27),
	DINPUT(sd6__sd_cmd_6_mux0, 0x0A00, 0x0A80, 26, 26),
	DINPUT(sd6__sd_cmd_6_mux1, 0x0A00, 0x0A80, 26, 26),
	DINPUT(sd6__sd_dat_6_0_mux0, 0x0A00, 0x0A80, 28, 28),
	DINPUT(sd6__sd_dat_6_0_mux1, 0x0A00, 0x0A80, 28, 28),
	DINPUT(sd6__sd_dat_6_1_mux0, 0x0A00, 0x0A80, 29, 29),
	DINPUT(sd6__sd_dat_6_1_mux1, 0x0A00, 0x0A80, 29, 29),
	DINPUT(sd6__sd_dat_6_2_mux0, 0x0A00, 0x0A80, 30, 30),
	DINPUT(sd6__sd_dat_6_2_mux1, 0x0A00, 0x0A80, 30, 30),
	DINPUT(sd6__sd_dat_6_3_mux0, 0x0A00, 0x0A80, 31, 31),
	DINPUT(sd6__sd_dat_6_3_mux1, 0x0A00, 0x0A80, 31, 31),
	DINPUT(u3__cts_3_mux0, 0x0A08, 0x0A88, 0, 0),
	DINPUT(u3__cts_3_mux1, 0x0A08, 0x0A88, 0, 0),
	DINPUT(u3__cts_3_mux2, 0x0A08, 0x0A88, 0, 0),
	DINPUT(u3__rxd_3_mux0, 0x0A00, 0x0A80, 5, 5),
	DINPUT(u3__rxd_3_mux1, 0x0A00, 0x0A80, 5, 5),
	DINPUT(u4__cts_4_mux0, 0x0A08, 0x0A88, 1, 1),
	DINPUT(u4__cts_4_mux1, 0x0A08, 0x0A88, 1, 1),
	DINPUT(u4__cts_4_mux2, 0x0A08, 0x0A88, 1, 1),
};

const struct dt_params pull_dt_map[] = {
	{ "pull_up", PULL_UP, },
	{ "high_hysteresis", HIGH_HYSTERESIS, },
	{ "high_z", HIGH_Z, },
	{ "pull_down", PULL_DOWN, },
	{ "pull_disable", PULL_DISABLE, },
	{ "pull_enable", PULL_ENABLE, },
};

const struct dt_params drive_strength_dt_map[] = {
	{ "ds_4we_3", DS_4WE_3, },
	{ "ds_4we_2", DS_4WE_2, },
	{ "ds_4we_1", DS_4WE_1, },
	{ "ds_4we_0", DS_4WE_0, },
	{ "ds_16st_15", DS_16ST_15, },
	{ "ds_16st_14", DS_16ST_14, },
	{ "ds_16st_13", DS_16ST_13, },
	{ "ds_16st_12", DS_16ST_12, },
	{ "ds_16st_11", DS_16ST_11, },
	{ "ds_16st_10", DS_16ST_10, },
	{ "ds_16st_9", DS_16ST_9, },
	{ "ds_16st_8", DS_16ST_8, },
	{ "ds_16st_7", DS_16ST_7, },
	{ "ds_16st_6", DS_16ST_6, },
	{ "ds_16st_5", DS_16ST_5, },
	{ "ds_16st_4", DS_16ST_4, },
	{ "ds_16st_3", DS_16ST_3, },
	{ "ds_16st_2", DS_16ST_2, },
	{ "ds_16st_1", DS_16ST_1, },
	{ "ds_16st_0", DS_16ST_0, },
	{ "ds_m31_0", DS_M31_0, },
	{ "ds_m31_1", DS_M31_1, },
};

static int get_valid_ds_state(const char *property)
{
	u32 idx;

	for (idx = 0; idx < ARRAY_SIZE(drive_strength_dt_map); idx++) {
		if (!strcmp(property,
				drive_strength_dt_map[idx].property))
			return drive_strength_dt_map[idx].value;
	}
	return -EINVAL;
}

static int get_valid_pull_state(const char *property)
{
	u32 idx;

	for (idx = 0; idx < ARRAY_SIZE(pull_dt_map); idx++) {
		if (!strcmp(property, pull_dt_map[idx].property))
			return pull_dt_map[idx].value;
	}
	return -EINVAL;
}


static void get_disable_input_status(struct atlas7_pmx *pmx,
	struct d_input_desc *di_desc, ulong *di_status, ulong *di_val)
{
	ulong status, val;

	status = readl(pmx->regs[BANK_DS] + di_desc->dinput_reg);
	status = (status >> di_desc->dinput_bit) & 0x1;

	val = readl(pmx->regs[BANK_DS] + di_desc->dinput_val_reg);
	val = (val >> di_desc->dinput_val_bit) & 0x1;

	*di_status = status;
	*di_val = val;
}

static int get_disable_input_list(struct atlas7_pmx *pmx)
{
	struct d_input_desc *di_desc;
	ulong di_status, di_val;
	int idx, cnt = 0;

	for (idx = 0; idx < ARRAY_SIZE(pmx_d_input_desc_list); idx++) {
		di_desc = &pmx_d_input_desc_list[idx];
		get_disable_input_status(pmx, di_desc, &di_status, &di_val);
		cnt += snprintf(s_sysfs_buf + cnt, s_sysfs_buf_size - cnt,
			"%s: disable input:%lx disable input value:%lx\n",
			di_desc->func_name, di_status, di_val);
	}

	return 0;
}

static int set_disable_input_status(struct atlas7_pmx *pmx,
				char *name, int status, int val)
{
	struct d_input_desc *di_desc = NULL;
	int idx;

	status = status & DI_MASK;
	val = val & DIV_MASK;

	for (idx = 0; idx < ARRAY_SIZE(pmx_d_input_desc_list); idx++) {
		di_desc = &pmx_d_input_desc_list[idx];
		if (!strcmp(name, di_desc->func_name))
			break;
		di_desc = NULL;
	}

	if (!di_desc)
		return -EINVAL;

	writel(DI_MASK << di_desc->dinput_bit,
		pmx->regs[BANK_DS] + CLR_REG(di_desc->dinput_reg));
	writel(status << di_desc->dinput_bit,
		pmx->regs[BANK_DS] + di_desc->dinput_reg);

	writel(DIV_MASK << di_desc->dinput_val_bit,
		pmx->regs[BANK_DS] + CLR_REG(di_desc->dinput_val_reg));
	writel(val << di_desc->dinput_val_bit,
		pmx->regs[BANK_DS] + di_desc->dinput_val_reg);

	return 0;
}

static const char *get_pad_type_string(int o_type)
{
	if (PAD_T_4WE_PD == o_type)
		return "zio_pad3v_4we_PD";
	else if (PAD_T_4WE_PU == o_type)
		return "zio_pad3v_4we_PU";
	else if (PAD_T_M31_0610_PD == o_type)
		return "PRDW0610SDGZ_M311311";
	else if (PAD_T_M31_0610_PU == o_type)
		return "PRUW0610SDGZ_M311311";
	else if (PAD_T_M31_0204_PD == o_type)
		return "PRDW0204SDGZ_M311311";
	else if (PAD_T_M31_0204_PU == o_type)
		return "PRUW0204SDGZ_M311311";
	else if (PAD_T_16ST == o_type)
		return "zio_pad3v_sdclk_PD";
	else if (PAD_T_AD == o_type)
		return "PRDWUWHW08SCDG_HZ";

	return "Unknown_TYPE";
}

static const char *get_pad_ds_status(struct atlas7_pmx *pmx,
		struct atlas7_pad_config *conf,	ulong *status)
{
	ulong regv;
	int bank, idx;
	const struct dt_params *dtp;

	bank = conf->id >= 18 ? 1 : 0;
	regv = readl(pmx->regs[bank] + conf->drvstr_reg);

	if (PAD_T_4WE_PD == conf->type ||
		PAD_T_4WE_PU == conf->type) {
		regv = (regv >> conf->drvstr_bit) & 0x3;
		*status = regv;

		for (idx = 0; idx < 4; idx++) {
			dtp = &drive_strength_dt_map[idx];
			if (dtp->value == regv)
				return dtp->property;
		}
	} else if (PAD_T_16ST == conf->type) {
		regv = (regv >> conf->drvstr_bit) & 0xf;
		*status = regv;

		for (idx = 4; idx < 20; idx++) {
			dtp = &drive_strength_dt_map[idx];
			if (dtp->value == regv)
				return dtp->property;
		}
	} else if (PAD_T_M31_0204_PD == conf->type ||
		PAD_T_M31_0204_PU == conf->type ||
		PAD_T_M31_0610_PD == conf->type ||
		PAD_T_M31_0610_PU == conf->type) {
		regv = (regv >> conf->drvstr_bit) & 0x1;
		*status = regv;

		for (idx = 20; idx < 22; idx++) {
			dtp = &drive_strength_dt_map[idx];
			if (dtp->value == regv)
				return dtp->property;
		}
	} else
		*status = regv;

	return "Unknown";
}

static const char *get_pad_pull_status(struct atlas7_pmx *pmx,
		struct atlas7_pad_config *conf, ulong *status)
{
	ulong regv;
	int bank;

	bank = conf->id >= 18 ? 1 : 0;
	regv = readl(pmx->regs[bank] + conf->pupd_reg);

	if (PAD_T_M31_0204_PD == conf->type ||
		PAD_T_M31_0204_PU == conf->type ||
		PAD_T_M31_0610_PD == conf->type ||
		PAD_T_M31_0610_PU == conf->type) {
		regv = (regv >> conf->pupd_bit) & 0x1;
		*status = regv;

		if (regv)
			return "pull_enable";
		else
			return "pull_disable";
	} else {
		regv = (regv >> conf->pupd_bit) & 0x3;
		*status = regv;

		if (regv == P4WE_PULL_DOWN)
			return "pull_down";
		else if (regv == P4WE_HIGH_Z)
			return "high_z";
		else if (regv == P4WE_PULL_UP)
			return "pull_up";
		else if (regv == P4WE_HIGH_HYSTERESIS) {
			if (PAD_T_4WE_PD == conf->type ||
				PAD_T_4WE_PU == conf->type)
				return "high_hysteresis";
		}
	}

	return "Unknown";
}

static const char *get_pad_ad_status(struct atlas7_pmx *pmx,
			struct atlas7_pad_config *conf)
{
	ulong regv;
	int bank;

	bank = conf->id >= 18 ? 1 : 0;
	regv = readl(pmx->regs[bank] + conf->ad_ctrl_reg);
	regv = (regv >> conf->ad_ctrl_bit) & 0x1;

	if (regv)
		return "Digital";
	else
		return "Analogue";
}

static int get_pin_list(struct atlas7_pmx *pmx)
{
	int idx, cnt = 0;
	const struct pinctrl_pin_desc *desc;

	for (idx = 0; idx < ARRAY_SIZE(atlas7_ioc_pads); idx++) {
		desc = &atlas7_ioc_pads[idx];
		cnt += snprintf(s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt, "%3d: %s\n",
			desc->number, desc->name);
	}

	return 0;
}

static int get_pin_pull_selectors(struct atlas7_pad_config *conf,
		char *buf, int len)
{
	int cnt;

	if (conf->type == PAD_T_4WE_PD || conf->type == PAD_T_4WE_PU) {
		cnt = snprintf(buf, len, "\t%s, %s, %s, %s\n",
			pull_dt_map[0].property,
			pull_dt_map[3].property,
			pull_dt_map[2].property,
			pull_dt_map[1].property);
	} else if (conf->type == PAD_T_16ST) {
		cnt = snprintf(buf, len, "\t%s, %s, %s\n",
			pull_dt_map[0].property,
			pull_dt_map[3].property,
			pull_dt_map[2].property);
	} else if (conf->type == PAD_T_M31_0204_PD ||
		conf->type == PAD_T_M31_0204_PU ||
		conf->type == PAD_T_M31_0610_PD ||
		conf->type == PAD_T_M31_0610_PU) {
		cnt = snprintf(buf, len, "\t%s, %s\n",
			pull_dt_map[4].property,
			pull_dt_map[5].property);
	} else if (conf->type == PAD_T_AD) {
		cnt = snprintf(buf, len, "\t%s, %s, %s\n",
			pull_dt_map[0].property,
			pull_dt_map[3].property,
			pull_dt_map[2].property);
	} else
		cnt = snprintf(buf, len, "\tUnknown\n");

	return cnt;
}

static int get_pin_ds_selectors(struct atlas7_pad_config *conf,
		char *buf, int len)
{
	int cnt;

	if (conf->type == PAD_T_4WE_PD || conf->type == PAD_T_4WE_PU) {
		cnt = snprintf(buf, len, "\t%s, %s, %s, %s\n",
			drive_strength_dt_map[3].property,
			drive_strength_dt_map[2].property,
			drive_strength_dt_map[1].property,
			drive_strength_dt_map[0].property);
	} else if (conf->type == PAD_T_16ST) {
		cnt = snprintf(buf, len, "\t%s, %s, %s, %s\n"
			"\t%s, %s, %s, %s\n"
			"\t%s, %s, %s, %s\n"
			"\t%s, %s, %s, %s\n",
			drive_strength_dt_map[19].property,
			drive_strength_dt_map[18].property,
			drive_strength_dt_map[17].property,
			drive_strength_dt_map[16].property,
			drive_strength_dt_map[15].property,
			drive_strength_dt_map[14].property,
			drive_strength_dt_map[13].property,
			drive_strength_dt_map[12].property,
			drive_strength_dt_map[11].property,
			drive_strength_dt_map[10].property,
			drive_strength_dt_map[9].property,
			drive_strength_dt_map[8].property,
			drive_strength_dt_map[7].property,
			drive_strength_dt_map[6].property,
			drive_strength_dt_map[5].property,
			drive_strength_dt_map[4].property);
	} else if (conf->type == PAD_T_M31_0204_PD ||
		conf->type == PAD_T_M31_0204_PU ||
		conf->type == PAD_T_M31_0610_PD ||
		conf->type == PAD_T_M31_0610_PU) {
		cnt = snprintf(buf, len, "\t%s, %s\n",
			drive_strength_dt_map[20].property,
			drive_strength_dt_map[21].property);
	} else
		cnt = snprintf(buf, len, "\tUnknown\n");

	return cnt;
}

static int get_pin_status(struct atlas7_pmx *pmx, int pin)
{
	int type, cnt, bank;
	const struct pinctrl_pin_desc *desc;
	struct atlas7_pad_config *conf;
	const char *str_status;
	ulong status, regv;

	if (pin >= ARRAY_SIZE(atlas7_ioc_pads)) {
		sprintf(s_sysfs_buf,
			"%s [0~%d] is available!\n",
			"The PIN number is out of the range.",
			ARRAY_SIZE(atlas7_ioc_pads));
		return -EINVAL;
	}

	desc = &atlas7_ioc_pads[pin];
	conf = &pmx->pctl_data->confs[desc->number];
	type = conf->type;
	bank = (desc->number >= 18) ? 1 : 0;

	/* Get pull sel status */
	str_status = get_pad_pull_status(pmx, conf, &status);
	/* Get Current pin function status */
	regv = readl(pmx->regs[bank] + conf->mux_reg);
	regv = (regv >> conf->mux_bit) & FUNC_CLEAR_MASK;

	cnt = snprintf(s_sysfs_buf, s_sysfs_buf_size,
		"PIN:%s Logic#%d Type:%s BANK:%s\n"
		"\rMux Reg:0x%04x, StartBit:%d, CurrentFunc:0x%lx\n"
		"\rPull Reg:0x%04x, StartBit:%d Status:%s[0x%lx]\n",
		desc->name, desc->number,
		get_pad_type_string(conf->type),
		bank ? "IOC_TOP" : "IOC_RTC",
		conf->mux_reg, conf->mux_bit, regv,
		conf->pupd_reg, conf->pupd_bit,
		str_status, status);

	cnt += snprintf(s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt,
			"\rAvailabe Pull Options:\n");
	cnt += get_pin_pull_selectors(conf, s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt);

	if (conf->drvstr_reg != -1) {
		str_status = get_pad_ds_status(pmx, conf, &status);
		cnt += snprintf(s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt,
			"\r%s Reg:0x%04x, StartBit:%d, Status:%s[0x%lx]\n",
			"DriverStrength", conf->drvstr_reg, conf->drvstr_bit,
			str_status, status);

		cnt += snprintf(s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt,
			"\rAvailabe DriveStrength Options:\n");
		cnt += get_pin_ds_selectors(conf,
				s_sysfs_buf + cnt,
				s_sysfs_buf_size - cnt);
	} else
		cnt += snprintf(s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt,
			"\r%s Reg:N/A, StartBit:N/A, Status:N/A\n",
			"DriverStrength");

	if (conf->ad_ctrl_reg != -1) {
		str_status = get_pad_ad_status(pmx, conf);
		cnt += snprintf(s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt,
			"\r%s Reg:0x%04x, StartBit:%d, Status:%s\n",
			"Analog/Digital",
			conf->ad_ctrl_reg, conf->ad_ctrl_bit, str_status);
	} else
		cnt += snprintf(s_sysfs_buf + cnt,
			s_sysfs_buf_size - cnt,
			"\r%s Reg:N/A, StartBit:N/A, Status:N/A\n",
			"Analog/Digital");
	return 0;
}

static int set_pin_ad_sel(struct atlas7_pmx *pmx, int pin, int sel)
{
	int bank;
	struct atlas7_pad_config *conf;

	conf = &pmx->pctl_data->confs[pin];
	bank = (pin >= 18) ? 1 : 0;

	if (sel)
		return __atlas7_pmx_pin_digital_enable(pmx,
			conf, bank);
	else
		return __atlas7_pmx_pin_analog_enable(pmx,
			conf, bank);
}


static ssize_t help_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;

	ret = snprintf(buf, 4096, "ioctest help:\n"
		"\n\recho 0 > config\n"
		"\r	Get IOC pin list information.\n"
		"\n\recho 0 N > config\n"
		"\r	Get pin#N status.\n"
		"\n\recho 1 N FN > config\n"
		"\r	Set pin#N to FUNCTION FN.\n"
		"\n\recho 2 N PN > config\n"
		"\r	Set pin#N PULL status to PN. The PN could be\n"
		"\r	\"pull_up, pull_down, high_z or high_hysteresis\"\n"
		"\r	The available value is depended on PIN type.\n"
		"\n\recho 3 N DS > config\n"
		"\r	Set pin#N DriveStrengh status to DS. The DS could\n"
		"\r	be \"ds_m31_0, ds_m31_1, ds_4we_0 ... ds_4we_3,\n"
		"\r	or ds_16st_0 ... ds_16st_15\". The available value\n"
		"\r	is depended on PIN type.\n"
		"\n\recho 4 N A/D > config\n"
		"\r	Set pin#N A/D mode:\n"
		"\r	0 for Analogue mode, 1 for Digital mode.\n"
		"\n\recho 5 > config\n"
		"\r	Get IOC Disable Input Status.\n"
		"\n\recho 6 NAME STATUS VALUE > config\n"
		"\r	Set IOC Disable Input Status.\n"
		"\r	NAME is the name of function will set disable input\n"
		"\r	status.\n"
		"\r	STATUS is the target disable input status\n"
		"\r	VALUE is the target disable input value\n");
	return ret;
}
static DEVICE_ATTR_RO(help);

static ssize_t config_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	ssize_t cnt;

	spin_lock(&s_sysfs_lock);

	cnt = snprintf(buf, s_sysfs_buf_size,
		"\n************************\n%s\n************************\n",
		s_sysfs_buf);
	memset(s_sysfs_buf, 0, s_sysfs_buf_size);

	spin_unlock(&s_sysfs_lock);

	return cnt;
}


static ssize_t config_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t len)
{
	struct atlas7_pmx *pmx = dev_get_drvdata(dev);
	int opcode, pin, ret;
	int argc, arg0, sel;
	char *str_arg0;

	argc = sscanf(buf, "%d %d", &opcode, &pin);
	if (argc <= 0) {
		pr_info("invalied parameters!\n");
		return -EINVAL;
	}

	str_arg0 = kzalloc(len + 1, GFP_KERNEL);
	if (!str_arg0)
		return -ENOMEM;

	spin_lock(&s_sysfs_lock);

	ret = 0;

	switch (opcode) {
	case 0:
		if (argc == 1)
			ret = get_pin_list(pmx);
		else
			ret = get_pin_status(pmx, pin);
		break;

	case 1:
		argc = sscanf(buf, "%d %d %d", &opcode, &pin, &arg0);
		if (argc < 3) {
			ret = -EINVAL;
			goto unlock;
		}

		ret = __atlas7_pmx_pin_enable(pmx, pin, arg0);
		break;

	case 2:
		argc = sscanf(buf, "%d %d %s", &opcode, &pin, str_arg0);
		if (argc < 3) {
			ret = -EINVAL;
			goto unlock;
		}

		sel = get_valid_pull_state(str_arg0);
		if (sel < 0) {
			ret = -EINVAL;
			goto unlock;
		}

		ret = altas7_pinctrl_set_pull_sel(pmx->pctl, pin, sel);
		break;

	case 3:
		argc = sscanf(buf, "%d %d %s", &opcode, &pin, str_arg0);
		if (argc < 3) {
			ret = -EINVAL;
			goto unlock;
		}

		sel = get_valid_ds_state(str_arg0);
		if (sel < 0) {
			ret = -EINVAL;
			goto unlock;
		}
		ret = __altas7_pinctrl_set_drive_strength_sel(pmx->pctl,
							pin, sel);
		break;

	case 4:
		argc = sscanf(buf, "%d %d %d", &opcode, &pin, &sel);
		if (argc < 3) {
			ret = -EINVAL;
			goto unlock;
		}

		ret = set_pin_ad_sel(pmx, pin, sel);
		break;

	case 5:
		ret = get_disable_input_list(pmx);
		break;

	case 6:
		argc = sscanf(buf, "%d %s %d %d", &opcode,
					str_arg0, &sel, &arg0);
		if (argc < 4) {
			ret = -EINVAL;
			goto unlock;
		}
		ret = set_disable_input_status(pmx, str_arg0, sel, arg0);
		break;

	default:
		dev_err(dev, "\nUnknown config opcode=%d\n", opcode);
		ret = -EINVAL;
		break;
	}

unlock:
	spin_unlock(&s_sysfs_lock);
	kfree(str_arg0);

	if (ret) {
		dev_err(dev, "Operation Failed, err=%d\n", ret);
		return ret;
	}

	return len;
}
static DEVICE_ATTR_RW(config);

static int atlas7_visbus_initialize(struct atlas7_pmx *pmx);

static int atlas7_pinctrl_init_sysfs(struct atlas7_pmx *pmx)
{
	int ret;

	s_sysfs_buf = devm_kzalloc(pmx->dev, PAGE_SIZE, GFP_KERNEL);
	if (!s_sysfs_buf)
		return -ENOMEM;

	s_sysfs_buf_size = PAGE_SIZE;

	ret = device_create_file(pmx->dev, &dev_attr_help);
	if (ret) {
		dev_err(pmx->dev,
			"failed to create gethelp attr, %d\n",
			ret);
		goto failed;
	}

	ret = device_create_file(pmx->dev, &dev_attr_config);
	if (ret) {
		dev_err(pmx->dev,
			"failed to create gethelp attr, %d\n",
			ret);
		goto failed;
	}

	spin_lock_init(&s_sysfs_lock);

	atlas7_visbus_initialize(pmx);

	dev_info(pmx->dev, "atlas7_pinctrl_init_sysfs....\n");
	return 0;

failed:
	kfree(s_sysfs_buf);
	return ret;
}


/*
 * RSC is used for atlas7 debug:
 * visbus, audio_func_dbg, etc.
 */
#define RSC_BASE_PHY_ADDR	0x10E50000
#define RSC_PIN_MUX_SET		0x08
#define RSC_PIN_MUX_CLR		0x0C
#define RSC_SW_USE0		0x28
#define RSC_SW_USE1		0x2C
#define RSC_SW_USE2		0x30
#define RSC_SW_USE3		0x34
#define RSC_VISBUS_SEL		0x38 /* VISBUS Macro Selection */
#define RSC_AUDIO_FUNC_DBG	0x3C

#define VISBUS_MACRO_MASK	0xF
#define VISBUS_BLOCK_MASK	0xF

/* Enumeration of VISBUS MACROS*/
enum visbus_macros {
	NO_MACRO = 0,
	AUDMSCM,
	BTM,
	CGUM,
	CPUM,
	DDRM,
	GNSSM,
	GPUM,
	MEDIAM,
	RTCM,
	VDIFM,
	ALL_MACROS,
	OUTPINS,
	CLOCKS,
};

/* macro's status */
#define M_INACTIVE	0
#define M_ACTIVE	1
#define M_UNUSED	2

/* block's status */
#define B_INACTIVE	0
#define B_ACTIVE	1
#define B_UNUSED	2
/*
 * Block/Entry Selection Registers for VISBUS,
 * Each macro has one register.
 */
 /* VISBUS Block/Entry Selection Register for AUDMSCM */
#define AUDMSCM_TH_GENERAL_SW_0		0x10E60000
/* VISBUS Block/Entry Selection Register for BTM */
#define BTM_TH_GENERAL_SW_0		0x11020000
/* VISBUS Block/Entry Selection Register for CGUM */
#define CGUM_TH_GENERAL_SW_0		0x18640000
/* VISBUS Block/Entry Selection Register for CPUM */
#define CPUM_TH_GENERAL_SW_0		0x10202000
/* VISBUS Block/Entry Selection Register for DDRM */
#define DDRM_TH_GENERAL_SW_0		0x10818000
/* VISBUS Block/Entry Selection Register for GNSSM */
#define GNSSM_TH_GENERAL_SW_0		0x18070000
/* VISBUS Block/Entry Selection Register for GPUM */
#define GPUM_TH_GENERAL_SW_0		0x13021000
/* VISBUS Block/Entry Selection Register for MEDIAM */
#define MEDIAM_TH_GENERAL_SW_0		0x17090000
/* VISBUS Block/Entry Selection Register for RTCM */
#define RTCM_TH_GENERAL_SW_0		0x18813000
/* VISBUS Block/Entry Selection Register for VDIFM */
#define VDIFM_TH_GENERAL_SW_0		0x13310000

#define MAX_VISBUS_BLOCK_PER_MACRO	7
#define NO_BLOCK	0xf
#define ALL_ZERO	0

struct visbus_block_desc {
	const char *name;
	int sel;
	int in_bit;
};

struct visbus_macro_desc {
	const char *name;
	ulong reg;
	int sel;
	int used;
	struct visbus_block_desc block_descs[MAX_VISBUS_BLOCK_PER_MACRO];
};

struct visbus_block {
	struct visbus_block_desc *desc;
	int status;
};

struct visbus_macro {
	struct visbus_macro_desc *desc;
	void __iomem *base;
	int status;
	struct visbus_block blocks[MAX_VISBUS_BLOCK_PER_MACRO];
};

struct visbus {
	struct device dev;
	struct atlas7_pmx *pmx;
	/* Macro select register */
	void __iomem *base;
	/* Block/Entry Selection Registers */
	struct visbus_macro macros[ALL_MACROS - 1];
	bool pins_enable;
	bool clk_enable;
	spinlock_t lock;
	struct atlas7_grp_mux *grp_mux;
	int *pin_mux_funcs;
};

static struct visbus_macro_desc atlas7_visbus_macro_descs[] = {
	{ "audmscm", AUDMSCM_TH_GENERAL_SW_0, AUDMSCM, 5,
		{	/* visbus sel for AUDMSCM macro */
			{ "cvd", 1, 4 },
			{ "audio_ip", 2, 0 },
			{ "lvds", 3, 12 },
			{ "kas", 4, 16 },
			{ "audio", 5, 20 },
		},
	}, { "btm", BTM_TH_GENERAL_SW_0, BTM, 1,
		{	/* visbus sel for BTM macro */
			{ "a7ca", 1, 4 },
		},
	}, { "cgum", CGUM_TH_GENERAL_SW_0, CGUM, 2,
		{	/* visbus sel for CGUM macro */
			{ "rstc", 1, 4 },
			{ "clkc", 2, 8 },
		},
	}, { "cpum", CPUM_TH_GENERAL_SW_0, CPUM, 2,
		{	/* visbus sel for CPUM macro */
			{ "cpu1", 1, 0 },
			{ "cpu2", 2, 0 },
		},
	}, { "ddrm", DDRM_TH_GENERAL_SW_0, DDRM, 0,
		{	/* visbus sel for DDR macro */
			{},
		},
	}, { "gnssm", GNSSM_TH_GENERAL_SW_0, GNSSM, 2,
		{	/* visbus sel for GNSSM macro */
			{ "gmac", 1, 0 },
			{ "can1", 2, 0 },
		},
	}, { "gpum", GPUM_TH_GENERAL_SW_0, GPUM, 1,
		{	/* visbus sel for GPUM macro */
			{ "viterbi", 1, 4 },
		},
	}, { "mediam", MEDIAM_TH_GENERAL_SW_0, MEDIAM, 2,
		{	/* visbus sel for MEDIAM macro */
			{ "nand", 1, 4 },
			{ "sdio01", 2, 8 },
		},
	}, { "rtcm", RTCM_TH_GENERAL_SW_0, RTCM, 4,
		{	/* visbus sel for RTCM macro */
			{ "armm3", 1, 4 },
			{ "qspi", 2, 8 },
			{ "can0", 3, 0 },
			{ "pwrc", 4, 16 },
		},
	}, { "vdifm", VDIFM_TH_GENERAL_SW_0, VDIFM, 7,
		{	/* visbus sel for VDIFM macro */
			{ "dcu", 1, 4 },
			{ "vip", 2, 8 },
			{ "lcd0", 3, 12 },
			{ "lcd1", 4, 16 },
			{ "sdio23", 5, 20 },
			{ "sdio45", 6, 24, },
			{ "sdio67", 7, 28 },
		},
	},
};

#define RCLKC_LEAF_CLK_BASE	0x18620000

#define ROOT_EN0_SET	0x022C

#define EN0_SET	0x0244
#define EN1_SET	0x04A0
#define EN2_SET	0x04B8
#define EN3_SET	0x04D0
#define EN4_SET	0x04E8
#define EN5_SET	0x0500
#define EN6_SET	0x0518
#define EN7_SET	0x0530
#define EN8_SET	0x0548

#define VAL_EN0_SET	0x0F
#define VAL_EN1_SET	0xE2FFFF
#define VAL_EN2_SET	0xFFFFF
#define VAL_EN3_SET	0x1F7FF
#define VAL_EN4_SET	0xFFFF
#define VAL_EN5_SET	0x0F
#define VAL_EN6_SET	0x1F
#define VAL_EN7_SET	0x07
#define VAL_EN8_SET	0xFF

static int atlas7_visbus_enable_clock(struct visbus *vis)
{
	void __iomem *clkc_base;

	clkc_base = devm_ioremap(&vis->dev, RCLKC_LEAF_CLK_BASE, PAGE_SIZE);
	if (!clkc_base)
		return -ENOMEM;

#if 0
	writel(VAL_EN0_SET, clkc_base + EN0_SET);
	writel(VAL_EN1_SET, clkc_base + EN1_SET);
	writel(VAL_EN2_SET, clkc_base + EN2_SET);
	writel(VAL_EN3_SET, clkc_base + EN3_SET);
	writel(VAL_EN4_SET, clkc_base + EN4_SET);
	writel(VAL_EN5_SET, clkc_base + EN5_SET);
	writel(VAL_EN6_SET, clkc_base + EN6_SET);
	writel(VAL_EN7_SET, clkc_base + EN7_SET);
	writel(VAL_EN8_SET, clkc_base + EN8_SET);
#endif

	/* io_clks */
	writel((1 << 17) | (1 << 14) | (1 << 13) | (1 << 12) | (1 << 11) |
		(1 << 2) | (1 << 1), clkc_base + ROOT_EN0_SET);
	/* rsc */
	writel(1 << 5, clkc_base + EN1_SET);
	/* adumscm th */
	writel(1 << 22, clkc_base + EN1_SET);
	/* vdifm th */
	writel(1 << 19, clkc_base + EN2_SET);
	/* mediam th */
	writel(1 << 15, clkc_base + EN4_SET);
	/* gnssm th */
	writel(1 << 14, clkc_base + EN3_SET);
	/* btm th */
	writel((1 << 7) | (1 << 6) | (1 << 5) |
		(1 << 1) | (1 << 0), clkc_base + EN8_SET);
	/* cgum th */
	writel(1 << 3, clkc_base + EN0_SET);
	/* gpum th */
	writel(1 << 2, clkc_base + EN7_SET);
	/* cpum th */
	writel(1 << 4, clkc_base + EN6_SET);
	/* ddrm th */
	writel((1 << 3) | (1 << 2), clkc_base + EN5_SET);

	vis->clk_enable = true;
	return 0;
}

static int atlas7_visbus_enable_pins(struct visbus *vis)
{
	int idx, ret;
	ulong regv;
	struct atlas7_pmx *pmx = vis->pmx;
	const struct atlas7_pad_mux *mux;
	struct atlas7_pad_config *conf;

	if (vis->pins_enable)
		return 0;

	/* backup visbus pins' current mux function */
	for (idx = 0; idx < vis->grp_mux->pad_mux_count; idx++) {
		mux = &vis->grp_mux->pad_mux_list[idx];
		conf = &pmx->pctl_data->confs[mux->pin];

		/* Save Current pin function status */
		regv = readl(pmx->regs[mux->bank] + conf->mux_reg);
		regv = (regv >> conf->mux_bit) & FUNC_CLEAR_MASK;
		vis->pin_mux_funcs[idx] = (int)regv;
	}

	for (idx = 0; idx < vis->grp_mux->pad_mux_count; idx++) {
		mux = &vis->grp_mux->pad_mux_list[idx];
		__atlas7_pmx_pin_input_disable_set(pmx, mux);
		ret = __atlas7_pmx_pin_enable(pmx, mux->pin, mux->func);
		if (ret) {
			dev_err(pmx->dev,
				"PIN#%d MUX_FUNC:%d failed, ret=%d\n",
				mux->pin, mux->func, ret);
			BUG_ON(1);
		}
		__atlas7_pmx_pin_input_disable_clr(pmx, mux);
	}

	vis->pins_enable = true;

	return 0;
}

static int atlas7_visbus_disable_pins(struct visbus *vis)
{
	int idx, func, ret;
	struct atlas7_pmx *pmx = vis->pmx;
	const struct atlas7_pad_mux *mux;

	if (!vis->pins_enable)
		return 0;

	for (idx = 0; idx < vis->grp_mux->pad_mux_count; idx++) {
		func = vis->pin_mux_funcs[idx];
		mux = &vis->grp_mux->pad_mux_list[idx];
		__atlas7_pmx_pin_input_disable_set(pmx, mux);
		ret = __atlas7_pmx_pin_enable(pmx, mux->pin, func);
		if (ret) {
			dev_err(pmx->dev,
				"PIN#%d MUX_FUNC:%d failed, ret=%d\n",
				mux->pin, func, ret);
			BUG_ON(1);
		}
		__atlas7_pmx_pin_input_disable_clr(pmx, mux);
	}

	vis->pins_enable = false;

	return 0;
}

static int atlas7_visbus_get_macro_by_name(struct visbus *vis,
						char *name)
{
	int idx;
	struct visbus_macro *macro;

	for (idx = 0; idx < ALL_MACROS - 1; idx++) {
		macro = &vis->macros[idx];
		if (!strcmp(macro->desc->name, name))
			return macro->desc->sel;
	}

	return NO_MACRO;
}

static int atlas7_visbus_get_block_by_name(struct visbus *vis,
				int macro_sel, char *name)
{
	int idx = macro_sel - 1;
	struct visbus_macro *macro;
	struct visbus_block *block;

	macro = &vis->macros[idx];
	for (idx = 0; idx < macro->desc->used; idx++) {
		block = &macro->blocks[idx];
		if (!strcmp(block->desc->name, name))
			return block->desc->sel;
	}

	return NO_BLOCK;
}

static int atlas7_visbus_enable_macro(struct visbus *vis, int sel)
{
	struct visbus_macro *macro;

	writel(sel & VISBUS_MACRO_MASK, vis->base);
	if (sel == ALL_MACROS) {
		int idx;

		for (idx = 0; idx < ALL_MACROS - 1; idx++) {
			macro = &vis->macros[idx];
			macro->status = M_ACTIVE;
		}
	} else {
		macro = &vis->macros[sel - 1];
		macro->status = M_ACTIVE;
	}

	return 0;
}

static int atlas7_visbus_disable_macro(struct visbus *vis, int sel)
{
	int idx;
	struct visbus_macro *macro;

	writel(NO_MACRO & VISBUS_MACRO_MASK, vis->base);
	for (idx = 0; idx < ALL_MACROS - 1; idx++) {
		macro = &vis->macros[idx];
		macro->status = M_INACTIVE;
	}

	return 0;
}

static int atlas7_visbus_enable_block(struct visbus *vis,
				int macro_sel, int sel, int opt)
{
	int idx = macro_sel - 1;
	ulong regv;
	struct visbus_macro *macro;
	struct visbus_block *block;

	macro = &vis->macros[idx];
	for (idx = 0; idx < macro->desc->used; idx++) {
		block = &macro->blocks[idx];
		if (block->desc->sel == sel) {
			regv = sel & VISBUS_BLOCK_MASK;
			if (opt != -1 && block->desc->in_bit)
				regv |= ((opt & VISBUS_BLOCK_MASK) <<
					 block->desc->in_bit);
			writel(regv, macro->base);
			block->status = B_ACTIVE;
		} else
			block->status = B_INACTIVE;
	}

	return 0;
}

static int atlas7_visbus_disable_block(struct visbus *vis,
					int macro_sel, int sel)
{
	int idx = macro_sel - 1;
	struct visbus_macro *macro;
	struct visbus_block *block;

	macro = &vis->macros[idx];
	writel(0, macro->base);
	for (idx = 0; idx < macro->desc->used; idx++) {
		block = &macro->blocks[idx];
		block->status = B_INACTIVE;
	}

	return 0;
}

#if 0
static void atlas7_visbus_dump_regs(struct visbus *vis)
{
	int idx;
	struct visbus_macro *macro;

	dev_info(&vis->dev, "DUMP VISBUS CONFIG REGISTERS:\n");
	dev_info(&vis->dev, "RSC\tREGVAL:0x%08x\n", readl(vis->base));
	for (idx = 0; idx < ALL_MACROS - 1; idx++) {
		macro = &vis->macros[idx];
		dev_info(&vis->dev, "%s\tREGVAL:0x%08x\n",
			macro->desc->name, readl(macro->base));
	}
	dev_info(&vis->dev, "\n");
}
#endif

static int atlas7_visbus_config(struct visbus *vis, int opcode,
				int macro, int block, int option)
{
	int ret;

	if (macro == NO_MACRO)
		return -EINVAL;

	/* config pins */
	if (macro == OUTPINS) {
		if (opcode)
			ret = atlas7_visbus_enable_pins(vis);
		else
			ret = atlas7_visbus_disable_pins(vis);

		return ret;
	}

	/* config clks */
	if (macro == CLOCKS) {
		if (opcode)
			ret = atlas7_visbus_enable_clock(vis);
		else
			ret = -EINVAL;
		return ret;
	}

	if (!vis->clk_enable) {
		dev_err(&vis->dev,
			"The clocks for Visbus hadn't been enabled!\n");
		ret = -EPERM;
		goto failed;
	}

	/* select marcos */
	if (opcode)
		ret = atlas7_visbus_enable_macro(vis, macro);
	else
		ret = atlas7_visbus_disable_macro(vis, macro);
	if (ret)
		goto failed;

	/* select all macros, other parameters are ignored. */
	if (macro == ALL_MACROS)
		return 0;

	/* select blocks */
	if (opcode)
		ret = atlas7_visbus_enable_block(vis, macro, block, option);
	else
		ret = atlas7_visbus_disable_block(vis, macro, block);
	if (ret)
		goto failed;

failed:
	return ret;
}

static ssize_t usage_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;

	ret = snprintf(buf, 4096, "visbus help:\n"
		"\n\rcat vconf\n"
		"\r	To show the Visbus status.\n"
		"\n\recho enable clocks > vconf\n"
		"\r	Enable Visbus clocks to access registers.\n"
		"\n\recho enable out_pins > vconf\n"
		"\r	Enable Visbus output, enable all visbus out pins.\n"
		"\n\recho enable all_macros > vconf\n"
		"\r	Enable Visbus on all macros.\n"
		"\n\recho enable <macro_name> > vconf\n"
		"\r	Enable Visbus on specified macro.\n"
		"\n\recho enable <macro_name> <block_name> [option] > vconf\n"
		"\r	Enable Visbus for the block of specified macro.\n"
		"\r	Only one block can be enable at a time for a macro.\n"
		"\n\recho disable <macro_name> <block_name> > vconf\n"
		"\r	Disable Visbus for the block of specified macro.\n"
		"\n\recho disable <macro_name> > vconf\n"
		"\r	Disable Visbus for specified macro.\n"
		"\n\recho disable all_macros > vconf\n"
		"\r	Disable Visbus for all macros.\n"
		"\n\recho disable out_pins > vconf\n"
		"\r	Disable Visbus output, enable all visbus out pins.\n"
		);
	return ret;
}
static DEVICE_ATTR_RO(usage);

static ssize_t vconf_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int i, idx, cnt;
	ulong regv = 0;
	struct visbus *vis;
	struct visbus_macro *macro;
	struct visbus_block *block;

	vis = dev_get_drvdata(dev);
	cnt = 0;

	spin_lock(&vis->lock);

	if (vis->clk_enable)
		regv = readl(vis->base);
	cnt += snprintf(buf + cnt, 4096 - cnt,
		"RSC_VIS_SEL:0x%lx\n", regv & VISBUS_MACRO_MASK);

	for (idx = 0; idx < ALL_MACROS - 1; idx++) {
		macro = &vis->macros[idx];
		if (vis->clk_enable)
			regv = readl(macro->base);
		cnt += snprintf(buf + cnt, 4096 - cnt,
		"MACRO:%s SEL:%d IOMEM:0x%lx VALUE:0x%lx STATUS:%s\n",
				macro->desc->name,
				macro->desc->sel,
				macro->desc->reg,
				regv,
				(macro->status == M_INACTIVE)
				? "INACTIVE" : "ACTIVE");
		for (i = 0; i < MAX_VISBUS_BLOCK_PER_MACRO; i++) {
			block = &macro->blocks[i];
			if (block->status != B_UNUSED)
				cnt += snprintf(buf + cnt, 4096 - cnt,
				"\tBLOCK#%d:%s SEL:%d INBIT:%d STATUS:%s\n",
				i, block->desc->name,
				block->desc->sel,
				block->desc->in_bit,
				(block->status == B_INACTIVE)
				? "INACTIVE" : "ACTIVE");
		}
		cnt += snprintf(buf + cnt, 4096 - cnt, "\n");
	}

	spin_unlock(&vis->lock);

	return cnt;
}


static ssize_t vconf_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t len)
{
	struct visbus *vis;
	char *arg0, *arg1, *arg2;
	int ret, argc, arg3 = -1;
	int opcode = -1, macro = NO_MACRO, block = NO_BLOCK;

	vis = dev_get_drvdata(dev);

	arg0 = kzalloc(len + 1, GFP_KERNEL);
	if (!arg0)
		return -ENOMEM;
	arg1 = kzalloc(len + 1, GFP_KERNEL);
	if (!arg1) {
		kfree(arg0);
		return -ENOMEM;
	}
	arg2 = kzalloc(len + 1, GFP_KERNEL);
	if (!arg2) {
		kfree(arg1);
		kfree(arg0);
		return -ENOMEM;
	}

	argc = sscanf(buf, "%s %s %s %d", arg0, arg1, arg2, &arg3);
	spin_lock(&vis->lock);

	if (argc > 0) {
		if (!strcmp(arg0, "enable"))
			opcode = 1;
		else if (!strcmp(arg0, "disable"))
			opcode = 0;
		else {
			ret = -EINVAL;
			goto failed;
		}
	}

	if (argc > 1) {
		if (!strcmp(arg1, "out_pins")) {
			macro = OUTPINS;
			goto config_visbus;
		} else if (!strcmp(arg1, "clocks")) {
			macro = CLOCKS;
			goto config_visbus;
		} else if (!strcmp(arg1, "all_macros")) {
			macro = ALL_MACROS;
			goto config_visbus;
		} else {
			macro = atlas7_visbus_get_macro_by_name(vis, arg1);
			if (macro == NO_MACRO) {
				ret = -EINVAL;
				goto failed;
			}
		}
	}

	block = ALL_ZERO;
	if (argc > 2) {
		block = atlas7_visbus_get_block_by_name(vis, macro, arg2);
		if (block == NO_BLOCK) {
			ret = -EINVAL;
			goto failed;
		}
	}

config_visbus:
	ret = atlas7_visbus_config(vis, opcode, macro, block, arg3);

failed:
	spin_unlock(&vis->lock);
	kfree(arg2);
	kfree(arg1);
	kfree(arg0);
	if (ret)
		dev_err(dev, "Invalied parameters! Error=%d\n", ret);
	return len;
}
static DEVICE_ATTR_RW(vconf);

static int atlas7_visbus_macro_init(struct visbus *vis,
					int macro_id)
{
	int idx;
	struct visbus_macro_desc *m_desc;
	struct visbus_macro *macro;
	struct visbus_block *block;

	idx = macro_id - 1;

	if (idx >= ARRAY_SIZE(atlas7_visbus_macro_descs))
		return -EINVAL;

	m_desc = &atlas7_visbus_macro_descs[idx];
	if (m_desc->sel != macro_id) {
		dev_err(&vis->dev, "Mismatched Visbus Desc table!\n");
		BUG_ON(1);
	}

	macro = &vis->macros[idx];
	macro->desc = m_desc;
	macro->status = M_INACTIVE;
	macro->base = devm_ioremap(&vis->dev, m_desc->reg, PAGE_SIZE);
	if (!macro->base)
		return -ENOMEM;

	/* Init used blocks */
	for (idx = 0; idx < m_desc->used; idx++) {
		block = &macro->blocks[idx];
		block->desc = &m_desc->block_descs[idx];
		block->status = B_INACTIVE;
	}

	/* Init unused blocks */
	for (idx = m_desc->used; idx < MAX_VISBUS_BLOCK_PER_MACRO; idx++) {
		block = &macro->blocks[idx];
		block->desc = NULL;
		block->status = B_UNUSED;
	}

	return 0;
}

static void visbus_device_release(struct device *dev)
{
	struct visbus *vis = dev_get_drvdata(dev);

	kfree(vis);
}

static int atlas7_visbus_initialize(struct atlas7_pmx *pmx)
{
	struct visbus *vis;
	int idx, ret;

	vis = devm_kzalloc(pmx->dev, sizeof(*vis), GFP_KERNEL);
	if (!vis)
		return -ENOMEM;

	/* very simple device indexing plumbing which is enough for now */
	dev_set_name(&vis->dev, "visbus");

	vis->dev.parent = NULL;
	vis->dev.bus = NULL;
	vis->dev.release = visbus_device_release;
	ret = device_register(&vis->dev);
	if (ret) {
		dev_err(pmx->dev, "device_register failed: %d\n", ret);
		put_device(&vis->dev);
		return ret;
	}

	vis->base = devm_ioremap(&vis->dev, RSC_BASE_PHY_ADDR, PAGE_SIZE);
	if (!vis->base) {
		ret = -ENOMEM;
		goto failed;
	}

	vis->base += RSC_VISBUS_SEL;
	vis->pmx = pmx;
	vis->grp_mux = &visbus_dout_grp_mux;
	vis->pin_mux_funcs = devm_kzalloc(&vis->dev,
			sizeof(int) * vis->grp_mux->pad_mux_count,
			GFP_KERNEL);
	if (!vis->pin_mux_funcs) {
		ret = -ENOMEM;
		goto failed;
	}

	/* Initialize each macro */
	for (idx = AUDMSCM; idx < ALL_MACROS; idx++) {
		ret = atlas7_visbus_macro_init(vis, idx);
		if (ret)
			goto failed;
	}

	ret = device_create_file(&vis->dev, &dev_attr_usage);
	if (ret) {
		dev_err(&vis->dev,
			"failed to create visbus attr, %d\n",
			ret);
		return ret;
	}

	ret = device_create_file(&vis->dev, &dev_attr_vconf);
	if (ret) {
		dev_err(&vis->dev,
			"failed to create visbus attr, %d\n",
			ret);
		return ret;
	}

	spin_lock_init(&vis->lock);
	vis->pins_enable = false;
	vis->clk_enable = false;
	dev_set_drvdata(&vis->dev, vis);

	pr_info("atlas7 visbus initialized OK.\n");
	return 0;
failed:
	device_unregister(&vis->dev);
	pr_info("atlas7 visbus initialized failed! error=%d\n", ret);
	return ret;
}


#endif /* __PINCTRL_ATLAS7_DEBUG__ */
