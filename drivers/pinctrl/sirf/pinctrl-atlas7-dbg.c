/*
 * pinctrl pads, groups, functions for CSR SiRFatlasVII
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
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
			return "pull_enable";
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
	char str_arg0[64];


	argc = sscanf(buf, "%d %d", &opcode, &pin);
	if (argc <= 0) {
		pr_info("invalied parameters!\n");
		return -EINVAL;
	}

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

		ret = __altas7_pinctrl_pull_sel(pmx->pctl, pin, sel);
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
		ret = __altas7_pinctrl_drive_strength_sel(pmx->pctl,
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

	if (ret) {
		dev_err(dev, "Operation Failed, err=%d\n", ret);
		return ret;
	}

	return len;
}
static DEVICE_ATTR_RW(config);

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

	dev_info(pmx->dev, "atlas7_pinctrl_init_sysfs....\n");
	return 0;

failed:
	kfree(s_sysfs_buf);
	return ret;
}

#endif /* __PINCTRL_ATLAS7_DEBUG__ */

