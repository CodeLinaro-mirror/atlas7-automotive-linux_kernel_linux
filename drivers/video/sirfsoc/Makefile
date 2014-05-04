# Makefile for SiRF framebuffer controller driver.

sirfsoc_fb-objs := sirfsocfb.o vdss_lcdc.o lcdc_init.o vdss_vpp.o

ifeq ($(BOARD),atlas6cb)
ccflags-y := -DSUPPORT_BLE
sirfsoc_fb-objs += vdss_ble.o
endif

obj-$(CONFIG_FB_SIRF) += sirfsoc_fb.o
