# Kernel 5.0 (rc) for BPI-R2

<a href="https://travis-ci.com/frank-w/BPI-R2-4.14" target="_blank"><img src="https://travis-ci.com/frank-w/BPI-R2-4.14.svg?branch=4.19-main" alt="Build status 4.19-main"></a>

## Requirements

On x86/x64-host you need cross compile tools for the armhf architecture (bison and flex-package are needed for kernels >=4.16):
```sh
sudo apt-get install gcc-arm-linux-gnueabihf libc6-armhf-cross u-boot-tools bc make gcc libc6-dev libncurses5-dev libssl-dev bison flex
```
if you build directly on r2 (not recommended) you do not need the crosscompile-packages gcc-arm-linux-gnueabihf and libc6-armhf-cross

## Issues

missing features like hdmi, wifi, 2nd gmac ;)


````
[    5.170597] WARNING: CPU: 3 PID: 1 at drivers/net/phy/phy.c:548 phy_start_aneg+0x110/0x144
[    5.178826] called from state READY
....
[    5.264111] [<c0629fd4>] (phy_start_aneg) from [<c0e3e720>] (mtk_init+0x414/0x47c)
[    5.271630]  r7:df5f5eec r6:c0f08c48 r5:00000000 r4:dea67800
[    5.277256] [<c0e3e30c>] (mtk_init) from [<c07dabbc>] (register_netdevice+0x98/0x51c)
[    5.285035]  r8:00000000 r7:00000000 r6:c0f97080 r5:c0f08c48 r4:dea67800
[    5.291693] [<c07dab24>] (register_netdevice) from [<c07db06c>] (register_netdev+0x2c/0x44)
[    5.299989]  r8:00000000 r7:dea2e608 r6:deacea00 r5:dea2e604 r4:dea67800
[    5.306646] [<c07db040>] (register_netdev) from [<c06326d8>] (mtk_probe+0x668/0x7ac)
[    5.314336]  r5:dea2e604 r4:dea2e040
[    5.317890] [<c0632070>] (mtk_probe) from [<c05a78fc>] (platform_drv_probe+0x58/0xa8)
[    5.325670]  r10:c0f86bac r9:00000000 r8:c0fbe578 r7:00000000 r6:c0f86bac r5:00000000
[    5.333445]  r4:deacea10
[    5.335963] [<c05a78a4>] (platform_drv_probe) from [<c05a5248>] (really_probe+0x2d8/0x424)
````

## Usage

```sh
  ./build.sh importconfig
  ./build.sh config
  ./build.sh
```

## Branch details

Kernel upstream + BPI-R2
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.14-main">4.14-main</a> (last LTS)
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.9-main">4.9-main</a>
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.16-main">4.16-main</a> (EOL)
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.17-main">4.17-main</a> (EOL)
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.18-main">4.18-main</a>
* <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.19-rc">4.19-rc</a> (next LTS)

## Kernel version

Kernel breakdown features by version

|          | 4.4 | 4.9 | 4.14 | 4.16 | 4.17 | 4.18 | 4.19 (rc) |
|----------| --- | --- | --- | --- | --- | --- | --- |
| PCIe     |  Y  |  Y  |  Y  |  Y  |     |   ?  |    |
| SATA     |  Y  |  Y  |  Y  |  Y?  |     |  Y   |    |
| 2 GMAC   |  Y  |  Y  |  Y  |  N  |     |     |    |
| DSA      |  N  |  Y  |  Y  |  Y  |  Y  |   Y  |  Y  |
| USB      |  Y  |  Y  |  Y  |  Y?  |     |  ?   |  Y  |
| VLAN     |     |     |  Y  |     |     |  ?   |    |
| HW NAT   |     |  Y  |  Y |     |     |     |    |
| HW QOS   |     |  Y  |  ? |     |     |     |    |
| Crypto   |  Y  |  Y  |  Y  |  Y?  |     |     |    |
| WIFI     |     |  Y  |  Y  |  Y |  Y  |   Y  |  Y  |
| BT       |     |     |     |     |     |     |    |
| VIDEO    |  Y  |  N  |  Y  |  Y  |     |     |  (Y)  |
| ACPI |  ?  |  N  |  Y  |  N  |     |     |  Y  |
||| other Options ||||     |    |
| OpenVPN  |  ?  |  Y  |  Y  |  ?  |     |   ?  |    |
| iptables |  ?  |  Y  |  Y  |  ?  |     |   ?  |  Y  |
| LXC / Docker |  ?  |  ?  |  Y  |  ?  |     |  ?   |    |

? = unsure

() = testing (separate Branch wlan/hdmi/hwnat/hwqos)

* HW-NAT only works between LAN and WAN (bridge unclear, wifi not working)
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

**Free Software, Hell Yeah!**
