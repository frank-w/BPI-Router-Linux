#!/bin/sh

#$1 = SYSROOT
#$2 = ROOTFS

if [ -z ${1} ]; then
	echo ERROR: SYSROOT\(1\) not set; exit 1
fi

if [ -z ${1} ]; then
	echo ERROR: ROOTFS\(1\) not set; exit 1
fi

# Copy sysroot to rootfs folder
rsync -ua --exclude '*.a' --exclude '*.o' ${1}/* ${2}/

# Basic filesystem requirements
cd ${2}
ln -sf bin/busybox init
mkdir -p dev sys proc run boot
mknod -m 622 dev/console c 5 1
mknod -m 755 dev/null c 1 3
chown root:root bin/busybox init dev/console dev/null

rm -rf usr/include usr/lib/gconv usr/share
${CROSS_COMPILE}strip lib/*.so 2>/dev/null
${CROSS_COMPILE}strip sbin/ldconfig sbin/sln 2>/dev/null

find . | cpio -H newc -o > ../initramfs.cpio
