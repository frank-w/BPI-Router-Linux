# Kernel 4.19 for BananaPi R2

<a href="https://travis-ci.com/frank-w/BPI-R2-4.14" target="_blank"><img src="https://travis-ci.com/frank-w/BPI-R2-4.14.svg?branch=4.19-main" alt="Build status 4.19-main"></a>

## Requirements

On a x86/x64-host you need cross compile tools for the armhf architecture (bison and flex-package are needed for kernels >=4.16):
```sh
sudo apt install gcc-arm-linux-gnueabihf libc6-armhf-cross u-boot-tools bc make gcc libc6-dev libncurses5-dev libssl-dev bison flex
```
If you build it directly on the BananaPi-R2 (not recommended) you do not need the crosscompile-packages gcc-arm-linux-gnueabihf and libc6-armhf-cross

## Issues

## Usage

```sh
  ./build.sh importconfig
  ./build.sh config (To configure manually with menuconfig)
  ./build.sh
```
the option "pack" creates a tar.gz-file which contains folders "BPI-BOOT" (content of Boot-partition aka /boot) and BPI-ROOT (content for rootfs aka /). simply backup your existing /boot/bananapi/bpi-r2/linux/uImage and unpack the content of these 2 folders to your system

you can also install direct to sd-card which makes a backup of kernelfile, here you have to change your uEnv.txt if you use a new filename (by default it's containing kernelversion)

## Branch details

Kernel upstream + BPI-R2
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.9-main">4.9-main</a>
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.19-main">4.14-main</a>
* 4.19-main

## Kernel versions

Kernel features by version

| Feature  | 4.4 | 4.9 | 4.14 | 4.19 |
|----------| --- | --- | ---  | ---  |
| PCIe     |  Y  |  Y  |  Y   |  Y   |
| SATA     |  Y  |  Y  |  Y   |  Y   |
| 2 GMAC   |  Y  |  Y  |  Y   |  Y   |
| DSA      |  N  |  Y  |  Y   |  Y   |
| USB      |  Y  |  Y  |  Y   |  Y   |
| VLAN     |     |     |  Y   |      |
| HW NAT   |     |  Y  |  Y   |      |
| HW QOS   |     |  Y  |  ?   |      |
| Crypto   |  Y  |  Y  |  Y   |      |
| WIFI     |     |  Y  |  Y   |  Y   |
| BT       |  N  |  N  |  Y   |  Y   |
| VIDEO    |  Y  |  N  |  Y   |  Y   |
| ACPI     |  ?  |  N  |  Y   |  Y   |
| Other options  |--|--|--|--|
| OpenVPN  |  ?  |  Y  |  Y  |    |
| iptables |  ?  |  Y  |  Y  |  Y  |
| LXC / Docker |  ?  |  ?  |  Y  |    |

Symbols:

|Symbol|Meaning|
|------|-------|
|  ?   |Unsure |
|  ()  |Testing|

(Testing in seperate branch wlan/hdmi/hwnat/hwqos)

* WIFI/BT needs WMT-tools called before
* HW-NAT only works between LAN and WAN (bridge unclear, wifi not supported)
* HW-QoS is merged into 4.14-main, but we do not know how to test it
* ACPI-feature means System is powered off, not only halted (power-consumption ~0.2W, no reboot on reset), reboot-problem with soldered power-switch (see https://github.com/frank-w/BPI-R2-4.14/issues/35). Power-off is also initiated by pressing the power-switch
* VIDEO is hdmi-output (X-server/framebuffer-console)...here some resolutions are not supported by vendor-driver

kernel 4.4 / uboot: https://github.com/frank-w/BPI-R2-4.4

## Links

* BPI-R2: http://www.banana-pi.org/r2.html
* Kernel: https://www.kernel.org/ , Stable-RC: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable-rc.git/
* linux-mediatek: https://patchwork.kernel.org/project/linux-mediatek/list/, Threaded: http://lists.infradead.org/pipermail/linux-mediatek/
* kernelci: https://kernelci.org/boot/mt7623n-bananapi-bpi-r2/
* Forum: http://forum.banana-pi.org/c/Banana-Pi-BPI-R2
* Wiki: http://www.fw-web.de/dokuwiki/doku.php?id=en/bpi-r2/start

License
----
GPL-2.0

*Free Software, Hell Yeah!*
