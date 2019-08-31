#!/bin/bash
ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- make mt7623n_evb_bpi_defconfig
ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- make INSTALL_MOD_PATH=output UIMAGE_LOADADDR=0x80008000 uImage dtbs
