obj-$(CONFIG_SIRF_VDSS) += sirfsoc_vdss.o
# Core DSS files
sirfsoc_vdss-y := core.o lcdc.o display.o output.o \
	layer_screen.o vpp.o lvdsc.o \
	screen-sysfs.o dcu_scaler.o dcu_ipp.o \
	dcu_ee.o dcu_nrdnt.o dcu.o \

ccflags-$(CONFIG_SIRF_VDSS_DEBUG) += -DDEBUG
