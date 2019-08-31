#!/bin/bash
ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- make mt7623_bpi-r2_defconfig
ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- make
