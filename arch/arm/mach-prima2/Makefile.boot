ifeq ($(CONFIG_ARCH_PRIMA2),y)
zreladdr-y		+= 0x00008000
params_phys-y		:= 0x00000100
initrd_phys-y		:= 0x00800000
else
zreladdr-y		+= 0x40008000
params_phys-y		:= 0x40000100
initrd_phys-y		:= 0x40800000
endif

dtb-$(CONFIG_ARCH_PRIMA2) += prima2-evb.dtb
dtb-$(CONFIG_ARCH_MARCO) += marco-evb.dtb
