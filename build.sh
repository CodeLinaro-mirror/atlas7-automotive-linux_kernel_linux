export ARCH=arm
export EXTRADIR=${PWD}/extra
make prima2cb_defconfig
make uImage
make modules
make dtbs
cp arch/arm/boot/uImage extra
cp arch/arm/boot/prima2-cb.dtb	extra/dtb
cp .config	extra/
