export ARCH=arm
export EXTRADIR=${PWD}/extra
export CROSS_COMPILE=arm-none-linux-gnueabi-
make marcocb_defconfig
make zImage KALLSYMS_EXTRA_PASS=1 -j8 V=1
make modules -j8
make dtbs
cp arch/arm/boot/zImage extra
cp arch/arm/boot/marco-evb.dtb extra/dtb
cp .config	extra/
