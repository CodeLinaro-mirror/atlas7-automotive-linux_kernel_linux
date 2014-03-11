/*
 * CSR sirfsoc VPP library interface
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __SIRFSOC_VDSS_VPP_H
#define __SIRFSOC_VDSS_VPP_H


enum vpp_deinterlace_mode {
	VPP_DI_RESERVED = 0,
	VPP_DI_WEAVE,
	VPP_DI_3MEDIAN,
	VPP_DI_VMRI,	/* Vertical Median Ranking Interpolation */
};

enum vpp_output_mode {
	VPP_OUTPUT_P_SINGLE = 0,
	VPP_OUTPUT_INTERLACE,
	VPP_OUTPUT_P_DOUBLE,
};


struct vpp_interlace_data {
	bool in_interlaced;
	enum vpp_output_mode out_mode;
	bool output_top_first;
	bool input_top_first;
	bool di_top;
	enum vpp_deinterlace_mode di_mode;
	u32 field_offset;
};

struct vpp_color_ctrl {
	s16 bright;
	s16 contrast;
	s16 uc;
	s16 vc;
};

struct vpp_parms {
	u32 src_base;			/* IN: Physical address of surface */
	enum vdss_pixelformat src_fmt;	/* IN: Format of surface. Refer to
					   FOURCC format of VDSS_PIXELFORMAT */
	unsigned int src_wstride_pixel;	/* IN: Width stride in pixel unit */
	unsigned int src_hstride_pixel;	/* IN: Heigth stride in pixel unit */
	enum vdss_pixelformat dst_fmt;	/* IN: Output format*/
	u32 dst_base;			/* IN: if =0, pass through mode */
	unsigned int dst_wstride_pixel;	/* IN: Width stride in pixel unit */
	unsigned int dst_hstride_pixel;	/* IN: Heigth stride in pixel unit */
};

struct vpp_coeff {
	u32 ycoeff;
	u32 ucoeff;
	u32 vcoeff;
	u32 offset;
};


struct vdss_vpp_ops {
	void (*init)(void *vpp_regs);
	void (*terminate)(void);
	bool (*lock)(bool continues);
	void (*unlock)(void);
	bool (*alloc_overlay)(struct lcdc_overlay *data);

	/* Set parameters which will be set only once */
	bool (*set_params)(struct vpp_parms *parms);
	void (*set_base)(unsigned long base);
	bool (*set_size)(struct vdss_rect *src_rect,
				struct vdss_rect *dst_rect);
	void (*start)(bool continues);
	void (*stop)(void);
	bool (*is_busy)(void);
	void (*set_interlace)(bool input_mode, int out_mode,
				bool output_top_first,
				bool top_field_reserved,
				int di_mode, bool input_top_first,
				unsigned int field_offset);
	void (*clear_dma_interrupt)(void);
	void (*enable_dma_interrupt)(void);
	void (*disable_dma_interrupt)(void);
	bool (*dma_irq_detected)(void);
	void (*update_format_endian)(bool input_big_endian,
					bool output_big_endian);

	void (*sleep)(void);
	void (*wakeup)(void);
	void (*set_color_ctrl)(struct vpp_color_ctrl *data);
	void (*update_bright)(int brightness);
	void (*update_contrast)(int contrast);
	void (*update_coeff2)(u32 *filter_coef);
	void (*update_yuv2rgb)(struct vpp_coeff *rcoef,
				struct vpp_coeff *gcoef,
				struct vpp_coeff *bcoef);
	void (*print_register)(void);

	/* Internal debug function */
	void (*get_src_info)(unsigned int *width,
				unsigned int *height,
				int *format);
	bool (*get_src_buf)(unsigned char *src);
	void (*get_dst_info)(unsigned int *width,
				unsigned int *height,
				int *format);
	bool (*get_dst_buf)(unsigned char *dst);
	void (*set_usr_mode)(bool usr);
	void (*reset)(void);
};

void vdss_install_vpp_ops(struct vdss_vpp_ops *vpp_ops);


#endif
