obj-$(CONFIG_ATLAS7_NOC) += noc.o noc_errlog.o noc_probe.o noc_qos.o

obj-$(CONFIG_ATLAS7_NOC_TRACING) += trace.o
obj-$(CONFIG_ATLAS7_NOC_FW) += noc_fw.o

# for tracing framework to find trace.h
CFLAGS_trace.o := -I$(src)
