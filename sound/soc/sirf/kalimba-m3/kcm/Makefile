snd-soc-kcm-objs := kcm.o
snd-soc-kcm-objs += kasdb.o
snd-soc-kcm-objs += kasobj.o
snd-soc-kcm-objs += op/utils.o op/passthr.o op/splitter.o op/resampler.o
snd-soc-kcm-objs += op/mixer.o op/delay.o op/bass.o op/peq.o op/vol-ctrl.o
snd-soc-kcm-objs += op/aec-ref.o op/cvc-send.o op/cvc-recv.o op/source-sync.o
snd-soc-kcm-objs += op/chmixer.o

obj-$(CONFIG_SND_SOC_SIRF_KALIMBA_M3) += snd-soc-kcm.o

ccflags-y += $(if $(CONFIG_SND_SOC_SIRF_KALIMBA_KCM_USER),\
		-I$(srctree)/sound/soc/sirf/kalimba-m3/kcm/db-user,\
		-I$(srctree)/sound/soc/sirf/kalimba-m3/kcm/db-default)
