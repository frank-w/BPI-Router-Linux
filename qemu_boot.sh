#!/bin/bash

#sudo apt install qemu-efi-aarch64 qemu-system-arm

echo "exit emulation with Ctrl+A X"

qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a53 \
  -smp 2 -m 512 \
  -nographic \
  -kernel arch/arm64/boot/Image \
  -initrd arm64_rootfs.cpio.gz \
  -append "console=ttyAMA0,115200 root=/dev/vda rw earlyprintk" \
  -serial mon:stdio
