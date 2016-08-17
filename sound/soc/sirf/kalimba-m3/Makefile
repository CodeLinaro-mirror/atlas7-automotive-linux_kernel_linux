snd-soc-sirf-kalimba-m3-objs := atlas7-kas-card.o \
		kas-kcm.o pcm-kcm.o\
		audio-protocol.o ps.o license.o buffer.o 			\
		$(if $(CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG),debug.o i2s.o iacc.o \
			usp-pcm.o firmware.o)

obj-$(CONFIG_SND_SOC_SIRF_KALIMBA_M3) +=		\
		snd-soc-sirf-kalimba-m3.o		\
i
obj-$(CONFIG_SND_SOC_SIRF_KALIMBA_KCM) += kcm/
