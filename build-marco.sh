export ARCH=arm
export EXTRADIR=${PWD}/extra
export CROSS_COMPILE=arm-none-linux-gnueabi-
make marcocb_defconfig
make uImage KALLSYMS_EXTRA_PASS=1 -j8
make modules -j8
make dtbs
cp arch/arm/boot/uImage extra
cp arch/arm/boot/marco-cb.dtb extra/dtb
cp .config	extra/
