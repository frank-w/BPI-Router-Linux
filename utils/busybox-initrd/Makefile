#export ARCH:=arm
#export CROSS_COMPILE:=arm-linux-gnueabihf-
export ARCH:=arm64
export CROSS_COMPILE:=aarch64-linux-gnu-
#TODO-cross-compile-prefix
#export PATH:=
#TODO-absolute-path-to-your-cross-compiler-binaries:${PATH}
#SYSROOT=/usr/arm-linux-gnueabihf
SYSROOT=/usr/aarch64-linux-gnu/
#TODO-abolute-path-to-your-cross-compiled-sysroot-see-genfs.sh
BUSYBOX=busybox-1.28.2

kerneldir=../../

all: busybox initramfs
#output/initramfs.cpio

linux-modules:
	# TODO: build linux modules and install to ./output/rootfs
	#INSTALL_MOD_PATH=${pwd}/output/rootfs/lib/modules
	rm -r $(shell pwd)/output/rootfs/lib/modules
	make -C ${kerneldir} modules_install INSTALL_MOD_PATH=$(shell pwd)/output/rootfs/
.PHONY: busybox
busybox:
	wget https://busybox.net/downloads/${BUSYBOX}.tar.bz2
	tar xjf ${BUSYBOX}.tar.bz2
	$(MAKE) -C ${BUSYBOX} defconfig
	$(MAKE) -C ${BUSYBOX} CONFIG_PREFIX=$(realpath .)/output/rootfs install
	rsync -ua rootfs-overlay/* output/rootfs

output/initramfs.cpio: busybox #TODO-enable-linux-modules
	fakeroot /bin/sh genfs.sh $(SYSROOT) output/rootfs

initramfs:
	fakeroot /bin/sh genfs.sh $(SYSROOT) output/rootfs
