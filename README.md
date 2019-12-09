# Kernel 5.4 for BananaPi R64

<a href="https://travis-ci.com/frank-w/BPI-R2-4.14" target="_blank"><img src="https://travis-ci.com/frank-w/BPI-R2-4.14.svg?branch=5.4-r64-main" alt="Build status 5.4-r64-main"></a>

## Requirements

On a x86/x64-host you need cross compile tools for the aarch64 architecture:
```sh
sudo apt install gcc-aarch64-linux-gnu u-boot-tools bc make gcc libc6-dev libncurses5-dev libssl-dev bison flex
```
If you build it directly on the BananaPi-R64 (not recommended) you do not need the crosscompile-package gcc-aarch64-linux-gnu

## Issues

* SGMII-Setup goes in timeout, but works with fixed IP in my tests (should affect LAN-POrts only)

## Usage

edit build.conf to select board version (<v1 uses RTL8367 switch) and uboot (mainline-uboot uses arm64 uImage, 2014 uses armhf=default)

```sh
  ./build.sh importconfig
  ./build.sh config (To configure manually with menuconfig)
  ./build.sh
```
the option "pack" creates a tar.gz-file which contains folders "BPI-BOOT" (content of Boot-partition aka /boot) and BPI-ROOT (content for rootfs aka /). simply backup your existing /boot/bananapi/bpi-r64/linux/uImage and /boot/bananapi/bpi-r64/linux/dtb/bpi-r64.dtb and unpack the content of these 2 folders to your system

you can also install direct to sd-card which makes a backup of kernelfile, here you have to change your uEnv.txt if you use a new filename (by default it's containing kernelversion)

## Branch details

Kernel upstream + BPI-R64
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.19-r64-main">4.19-r64-main</a>
* 5.4-r64-main

## Kernel versions

Kernel features by version

| Feature        | 4.19 | 5.4 |
|----------------| ---  | --- |
| PCIe           |  Y   |  Y  |
| SATA           |  Y   |  Y  |
| RTL8367 (<v1)  |  Y   |  Y  |
| MT7631 (v1+)   |  Y   |  Y  |
| DSA            |  N   |  N  |
| USB            |  Y   |  Y  |
| VLAN           |      |     |
| HW NAT         |      |     |
| HW QOS         |      |     |
| Crypto         |      |     |
| WIFI           |  N   |  Y  |
| BT             |  Y   |  Y  |
| ACPI           |  N   |  N  |

Symbols:

|Symbol|Meaning|
|------|-------|
|  ?   |Unsure |
|  ()  |Testing|

(Testing in seperate branch)

* WIFI/BT needs Firmware in /lib/firmware/mediatek (see utils-folder)
* ACPI-feature means System is powered off, not only halted (power-consumption ~0.2W, no reboot on reset)

uboot (2014-04): https://github.com/frank-w/u-boot/tree/bpi-r64 (binaries: https://drive.google.com/open?id=1JF2te5_ZouXNbS5hj3EqiZq4HCPWlKA1)
uboot (2020-01): https://github.com/frank-w/u-boot/tree/2020-01-bpi-r64-v1

## Links

* BPI-R64: http://www.banana-pi.org/r64.html
* Kernel: https://www.kernel.org/ , Stable-RC: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable-rc.git/
* linux-mediatek: https://patchwork.kernel.org/project/linux-mediatek/list/, Threaded: http://lists.infradead.org/pipermail/linux-mediatek/
* Forum: http://forum.banana-pi.org/c/Banana-Pi-BPI-R2
* Wiki: http://www.fw-web.de/dokuwiki/doku.php?id=en/bpi-r64/start

License
----
GPL-2.0

*Free Software, Hell Yeah!*
