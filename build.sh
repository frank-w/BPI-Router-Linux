#!/bin/bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export INSTALL_MOD_PATH=output
CFLAGS=-j$(grep ^processor /proc/cpuinfo  | wc -l)

case $1 in
	importconfig) make mt7623n_evb_bpi_defconfig;;
	config) make menuconfig;;
	build) make ${CFLAGS} UIMAGE_LOADADDR=0x80008000 uImage dtbs modules 2> >(tee build.log) &&
		make modules_install
	;;
	*) echo "build.sh [importconfig|config|build]";;
esac
