snd-soc-sirf-kalimba-objs := kas.o buffer.o
snd-soc-sirf-kalimba-pcm-objs := pcm.o kcm.o dma-hack.o
snd-soc-sirf-kalimba-dsp-objs := dsp.o ipc.o \
		$(if $(CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG),debug.o firmware.o)
snd-soc-sirf-kalimba-iacc-objs := iacc.o
snd-soc-sirf-kalimba-i2s-objs := i2s.o

obj-$(CONFIG_SND_SOC_SIRF_KALIMBA) +=		\
		snd-soc-sirf-kalimba.o		\
		snd-soc-sirf-kalimba-pcm.o	\
		snd-soc-sirf-kalimba-dsp.o	\
		snd-soc-sirf-kalimba-iacc.o	\
		snd-soc-sirf-kalimba-i2s.o
