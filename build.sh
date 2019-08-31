#!/bin/bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export INSTALL_MOD_PATH=output

case $1 in
	importconfig) make mt7623n_evb_bpi_defconfig;;
	config) make menuconfig;;
	build) make UIMAGE_LOADADDR=0x80008000 uImage dtbs modules modules_install;;
	*) echo "build.sh [importconfig|config|build]";;
esac
