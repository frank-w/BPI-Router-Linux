#!/bin/bash
export LANG=C
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export INSTALL_MOD_PATH=output
CFLAGS=-j$(grep ^processor /proc/cpuinfo  | wc -l)

uploaduser=$USER
uploadserver=192.168.0.10
uploaddir=/var/lib/tftp

case $1 in
	"dtsi") nano arch/arm/boot/dts/mt7623.dtsi;;
	"dts") nano arch/arm/boot/dts/mt7623n-bpi-r2.dts;;
	"defconfig") nano arch/arm/configs/mt7623n_evb_bpi_defconfig;;
	"importconfig") make mt7623n_evb_bpi_defconfig;;
	"config") make menuconfig;;
	"build") make ${CFLAGS} UIMAGE_LOADADDR=0x80008000 uImage dtbs modules 2> >(tee build.log);;
	"install")
		cp arch/arm/boot/uImage $INSTALL_MOD_PATH/uImage_$(make kernelversion)
		make modules_install
	;;
	"upload")
		targetfile=uImage_$(make kernelversion)
		echo "uploading $targetfile to ${uploadserver}..."
		scp arch/arm/boot/uImage ${uploaduser}@${uploadserver}:/${uploaddir}/${targetfile};;
	*) echo "build.sh [importconfig|config|build|install]";;
esac
