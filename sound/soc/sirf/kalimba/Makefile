snd-soc-sirf-kalimba-objs := buffer.o license.o ps.o kerror.o \
		$(if $(CONFIG_SND_SOC_SIRF_KALIMBA_KCM),kas-kcm.o,kas.o)
snd-soc-sirf-kalimba-pcm-objs := \
		$(if $(CONFIG_SND_SOC_SIRF_KALIMBA_KCM),pcm-kcm.o,pcm.o kcm.o)
snd-soc-sirf-kalimba-dsp-objs := dsp.o ipc.o ext_amp.o \
		$(if $(CONFIG_SND_SOC_SIRF_KALIMBA_DEBUG),debug.o firmware.o)
snd-soc-sirf-kalimba-iacc-objs := iacc.o
snd-soc-sirf-kalimba-i2s-objs := i2s.o
snd-soc-sirf-kalimba-usp-pcm-objs := usp-pcm.o

obj-$(CONFIG_SND_SOC_SIRF_KALIMBA) +=		\
		snd-soc-sirf-kalimba.o		\
		snd-soc-sirf-kalimba-pcm.o	\
		snd-soc-sirf-kalimba-dsp.o	\
		snd-soc-sirf-kalimba-iacc.o	\
		snd-soc-sirf-kalimba-i2s.o	\
		snd-soc-sirf-kalimba-usp-pcm.o

obj-$(CONFIG_SND_SOC_SIRF_KALIMBA_KCM) += kcm/
