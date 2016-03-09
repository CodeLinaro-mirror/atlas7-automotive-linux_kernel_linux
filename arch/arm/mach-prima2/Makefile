obj-y += rstc.o
obj-y += common.o
obj-y += rtciobrg.o

obj-$(CONFIG_ATLAS7DA_CSRVISOR_TEST) += csrvisor_test.o
obj-$(CONFIG_SUSPEND) += pm.o sleep-prima2.o sleep-atlas7.o
obj-$(CONFIG_SMP) += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)  += hotplug.o

obj-$(CONFIG_VIDEO_CODEC_SIRF)	+= vxd39x.o
CFLAGS_hotplug.o += -march=armv7-a
