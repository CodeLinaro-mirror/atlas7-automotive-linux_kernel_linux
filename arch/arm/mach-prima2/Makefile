obj-y += rstc.o
obj-y += common.o
obj-y += rtciobrg.o
obj-y += camera.o

obj-$(CONFIG_DEBUG_LL) += lluart.o
obj-$(CONFIG_SUSPEND) += pm.o sleep-prima2.o sleep-marco.o
obj-$(CONFIG_SMP) += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)  += hotplug.o

obj-$(CONFIG_VIDEO_CODEC_SIRF)	+= vxd39x.o
CFLAGS_hotplug.o += -march=armv7-a
