# Makefile for SiRF framebuffer controller driver.

sirfsoc_fb-objs := sirfsocfb.o CspSocLcd.o CspSocLcdInit.o CspSocVpp.o

ifdef CONFIG_SIRF_BLE
sirfsoc_fb-objs += CspSocBle.o
endif

obj-$(CONFIG_FB_SIRF) += sirfsoc_fb.o
