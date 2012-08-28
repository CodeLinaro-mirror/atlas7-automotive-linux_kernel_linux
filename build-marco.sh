export ARCH=arm
export EXTRADIR=${PWD}/extra
export CROSS_COMPILE=arm-none-linux-gnueabi-
make marcocb_defconfig
make uImage KALLSYMS_EXTRA_PASS=1 UIMAGE_TYPE=kernel_noload UIMAGE_ENTRYADDR=0x0 -j8 V=1
make modules -j8
make dtbs
cp arch/arm/boot/uImage extra
cp arch/arm/boot/marco-evb.dtb extra/dtb
cp .config	extra/
