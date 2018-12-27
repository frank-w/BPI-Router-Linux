# Kernel 4.19 for BPI-R64 (not ready yet)

My Kernel is currently untested...repo is existing for testing only ;)

## Requirements

On x86/x64-host you need cross compile tools for the armhf architecture (bison and flex-package are needed for kernels >=4.16):
```sh
sudo apt-get install gcc-arm-linux-gnueabihf libc6-armhf-cross u-boot-tools bc make gcc libc6-dev libncurses5-dev libssl-dev bison flex
```
if you build directly on r2 (not recommended) you do not need the crosscompile-packages gcc-arm-linux-gnueabihf and libc6-armhf-cross

## Issues

```
arch/arm64/Makefile:27: ld does not support --fix-cortex-a53-843419; kernel may be susceptible to erratum
arch/arm64/Makefile:40: LSE atomics not supported by binutils
arch/arm64/Makefile:48: Detected assembler with broken .inst; disassembly will be unreliable
```
on modules_install

## Usage

```sh
  ./build.sh importconfig
  ./build.sh config
  ./build.sh
```

## Branch details

## Kernel version

Kernel breakdown features by version

|          | 4.4 | 4.19 |
|----------| --- | --- |
| PCIe     |     |     |
| SATA     |     |     |
| 2 GMAC   |     |     |
| DSA      |     |     |
| USB      |     |     |
| VLAN     |     |     |
| HW NAT   |     |     |
| HW QOS   |     |     |
| Crypto   |     |     |
| WIFI     |     |     |
| BT       |     |     |
| ACPI     |     |     |
|| other Options ||
| OpenVPN  |     |     |
| iptables |     |     |
| LXC / Docker |     |     |

? = unsure

() = testing (separate Branch wlan/hdmi/hwnat/hwqos)

* HW-NAT only works between LAN and WAN (bridge unclear, wifi not working)
* HW-QoS is merged into 4.14-main, but we do not know how to test it
* ACPI-feature means System is powered off, not only halted (power-consumption ~0.2W, no reboot on reset), reboot-problem with soldered power-switch (see https://github.com/frank-w/BPI-R2-4.14/issues/35). Power-off is also initiated by pressing the power-switch

kernel 4.4 / uboot: https://github.com/frank-w/BPI-R64-bsp

## Links

* BPI-R64: http://www.banana-pi.org/r64.html
* official Wiki: http://wiki.banana-pi.org/Banana_Pi_BPI-R64 / http://wiki.banana-pi.org/Getting_Started_with_R64
* Kernel: https://www.kernel.org/ , Stable-RC: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable-rc.git/
* linux-mediatek: https://patchwork.kernel.org/project/linux-mediatek/list/, Threaded: http://lists.infradead.org/pipermail/linux-mediatek/
* kernelci: https://kernelci.org/boot/mt7622-rfb1/
* Forum: http://forum.banana-pi.org/c/Banana-Pi-BPI-R2
* my Wiki: http://www.fw-web.de/dokuwiki/doku.php?id=en/bpi-r64/start

License
----

GPL-2.0

**Free Software, Hell Yeah!**
