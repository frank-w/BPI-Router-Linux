# Kernel 6.6 for BananaPi R2/R64/R2Pro/R3

![CI](https://github.com/frank-w/BPI-Router-Linux/workflows/CI/badge.svg?branch=6.6-main)

## Requirements

On a x86/x64-host you need cross compile tools for the armhf architecture (bison and flex-package are needed for kernels >=4.16):
```sh
#for r2
sudo apt install gcc-arm-linux-gnueabihf libc6-armhf-cross u-boot-tools bc make ccache gcc libc6-dev libncurses5-dev libssl-dev bison flex
#for r64 / r2pro / r3
sudo apt install gcc-aarch64-linux-gnu u-boot-tools bc make gcc ccache libc6-dev libncurses5-dev libssl-dev bison flex
```
If you build it directly on the BananaPi-R2/R64 (not recommended) you do not need the crosscompile-packages gcc-arm-linux-gnueabihf/gcc-aarch64-linux-gnu and libc6-armhf-cross

## Issues

Ubuntu 22 seems not set update-alternatives for gcc, so build.sh will report crosscompiler is not installed

```sh
for i in $(ls /usr/bin/arm-linux-gnueabihf-gcc-{8..12});do sudo update-alternatives --install /usr/bin/arm-linux-gnueabihf-gcc arm-linux-gnueabihf-gcc $i 50;done
sudo update-alternatives --config arm-linux-gnueabihf-gcc

for i in $(ls /usr/bin/aarch64-linux-gnu-gcc-{8..12});do sudo update-alternatives --install /usr/bin/aarch64-linux-gnu-gcc aarch64-linux-gnu-gcc $i 50;done
sudo update-alternatives --config aarch64-linux-gnu-gcc
```
### R2

internal wifi/bt does not work anymore on 6.0+ as there are internal changes in linux which break mt6625 driver

### R64
* pcie-slot CN8 does not detect gen2-cards due to hardware-issue (missing capacitors)
* some pcie-cards are not detected because of wrong memory-mapping of BAR0

### R2Pro
* mic-switch not working

### R3
* vlan on left SFP not working (6.2 + Fix from Felix works)
  https://patchwork.kernel.org/project/linux-mediatek/list/?series=698421

## Usage

if you want to build for R64/r2pro/R3, change "board" in build.conf first

```sh
  ./build.sh importconfig
  ./build.sh config #To configure manually with menuconfig
  ./build.sh
```
the option "pack" creates a tar.gz-file which contains folders "BPI-BOOT" (content of Boot-partition aka /boot) and BPI-ROOT (content for rootfs aka /). simply backup your existing /boot/bananapi/bpi-r2/linux/uImage and unpack the content of these 2 folders to your system

you can also install direct to sd-card which makes a backup of kernelfile, here you have to change your uEnv.txt if you use a new filename (by default it's containing kernelversion)

### Usage with docker

The Dockerfile in `utils/docker/` provides a build environment without installing the native compilers on the local system.

The local directory will be mounted into the docker container. All changes will also be present in the respository folder.

1. Build the docker container for building once:
    ```sh
    sh ./utils/docker/build_container.sh
    ```

    Note: if you get this error: "Got permission denied while trying to connect to the Docker daemon socket at ..." you have to configure non-root
    https://docs.docker.com/engine/install/linux-postinstall/#manage-docker-as-a-non-root-user
    ```sh
    sudo usermod -aG docker $USER
    newgrp docker
    ```
1. Start and connect to the running docker container: 
    ```sh 
    sh ./utils/docker/run.sh
    ```
1. Now you can use the commands from above:
    ```sh 
    ./build.sh
    ```
1. Close the container with `exit` or `CTRL-D`.
1. Your build artifacts from the build script will be in the folder `./SD/` 


If you want to clean up you can remove all containers (and the associated docker images) with:
```sh
docker rmi bpi-cross-compile:1 --force
```
## Branch details

Kernel upstream + BPI-R2 / R64
* <a href="https://github.com/frank-w/BPI-Router-Linux/tree/4.9-main">4.9-main</a>
* <a href="https://github.com/frank-w/BPI-Router-Linux/tree/4.14-main">4.14-main</a>
* <a href="https://github.com/frank-w/BPI-Router-Linux/tree/4.19-main">4.19-main</a> | <a href="https://github.com/frank-w/BPI-R2-4.14/tree/4.19-r64-main">4.19-r64-main</a>
* <a href="https://github.com/frank-w/BPI-Router-Linux/tree/5.4-main">5.4-main</a>
* <a href="https://github.com/frank-w/BPI-Router-Linux/tree/5.10-main">5.10-main</a>
* <a href="https://github.com/frank-w/BPI-Router-Linux/tree/5.15-main">5.15-main</a>
* <a href="https://github.com/frank-w/BPI-Router-Linux/tree/6.1-main">6.1-main</a>
* 6.6-main

## Kernel versions

Kernel features by version

R2/R64:

| Feature            | 4.4 | 4.9 | 4.14 | 4.19 | 5.4 | 5.10 | 5.15 | 6.1 | 6.6 |
|--------------------| --- | --- | ---  | ---  | --- | ---- | ---- | --- | --- |
| PCIe               |  Y  |  Y  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| SATA               |  Y  |  Y  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| 2 GMAC             |  Y  |  Y  |  Y   |  Y   |  N  |  N   |  N   |  Y  |  Y  |
| DSA                |  N  |  Y  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| USB                |  Y  |  Y  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| VLAN (dsa)         |     |     |  Y   |  N   |  Y  |  Y   |  Y   |  Y  |  Y  |
| VLAN-aware Bridge  |     |     |  N   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| HW NAT (R2)        |     |  Y  |  Y   |      |     |  N   |  Y   |  Y  |  Y  |
| HW QOS (R2)        |     |  Y  |  ?   |      |     |  N   |  N   |  Y  |  Y  |
| Crypto             |  Y  |  Y  |  Y   |      |  Y  |      |      |     |     |
| WIFI (internal)    |     |  Y  |  Y   |  Y   |  Y  |  Y   |  Y   |  N  |  N  |
| BT                 |  N  |  N  |  Y   |  Y   |  Y  |  Y   |  Y   |  N  |  N  |
| VIDEO (R2 only)    |  Y  |  N  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| ACPI (R2)          |  ?  |  N  |  Y   |  Y   |  Y  |      |      |  Y  |  N  |
| IR (R2)            |  ?  |  N  |  N   |  N   |  Y  |  Y   |  ?   |  Y  |  Y  |
| WIFI (R64)         |  N  |  N  |  N   |  N   |  Y  |  Y   |  Y   |  Y  |  Y  |
| BT (R64)           |  N  |  N  |  N   |  N   |  Y  |  Y   |  Y   |  Y  |  Y  |
| Other options      |--|--|--|--|--|--|--|--|
| OpenVPN            |  ?  |  Y  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| iptables (R2)      |  ?  |  Y  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| nftables (R2)      |  ?  |  N  |  N   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |
| LXC / Docker (R2)  |  ?  |  ?  |  Y   |  Y   |  Y  |  Y   |  Y   |  Y  |  Y  |

Symbols:

|Symbol|Meaning|
|------|-------|
|  ?   |Unsure |
|  ()  |Testing|

(Testing in separate branch wlan/hdmi/hwnat/hwqos)

* WIFI/BT on R2 needs WMT-tools called before
* HW-NAT only works between LAN and WAN (bridge unclear, wifi not supported)
* HW-QoS is merged into 4.14-main, but we do not know how to test it
* ACPI-feature means System is powered off, not only halted (power-consumption ~0.2W, no reboot on reset), reboot-problem on R2 with soldered power-switch (see https://github.com/frank-w/BPI-R2-4.14/issues/35). Power-off is also initiated by pressing the power-switch, on R64 not currently not available
* VIDEO is hdmi-output (X-server/framebuffer-console)...here some resolutions are not supported by vendor-driver. R64 does not have HDMI


kernel 4.4 / uboot 2014-04: https://github.com/frank-w/BPI-R2-4.4
mainline-uboot: https://github.com/frank-w/u-boot

R2Pro/R3

| Feature            | 6.1 | 6.6 |
|--------------------| --- | --- |
| USB                |  Y  |  Y  |
| PCIe               |  Y  |  Y  |
| SATA (r2pro)       |  Y  |  Y  |
| DSA                |  Y  |  Y  |
| 2 GMAC             |  Y  |  Y  |
| SFP (R3)           |  Y  |  Y  |
| VLAN               | Y/P | Y/P |
| HW NAT (R3)        |  Y  |  Y  |
| WIFI (r3 internal) |  Y  |  Y  |
| VIDEO (R2pro only) |  Y  |  Y  |
| IR (R2pro)         |  ?  |  ?  |

P=partial (vlan on r3 only works on dsa-ports, not on left SFP)

## Images

R2:
Debian Bullseye: https://forum.banana-pi.org/t/bpi-r2-debian-bullseye-image/12592/2

R64:
Debian Bullseye: https://forum.banana-pi.org/t/bpi-r64-debian-bullseye-image/12797

R2Pro:
Debian Bullseye/Ubuntu 22.04: https://forum.banana-pi.org/t/bpi-r2-pro-debian-bullseye-ubuntu-22-04/13395

R3:
Debian Bullseye: https://forum.banana-pi.org/t/bpi-r3-debian-bullseye-image/14541

## Links

* BPI-R2: http://www.banana-pi.org/r2.html | BPI-R64: http://www.banana-pi.org/r64.html
* BPI-R2Pro: https://wiki.banana-pi.org/Banana_Pi_BPI-R2_Pro | BPI-R3: https://wiki.banana-pi.org/Banana_Pi_BPI-R3
* Kernel: https://www.kernel.org/ , Stable-RC: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable-rc.git/
* linux-mediatek: https://patchwork.kernel.org/project/linux-mediatek/list/, Threaded: http://lists.infradead.org/pipermail/linux-mediatek/
* kernelci: https://kernelci.org/boot/mt7623n-bananapi-bpi-r2/
* Forum: http://forum.banana-pi.org/c/Banana-Pi-BPI-R2 | https://forum.banana-pi.org/c/banana-pi-bpi-r2-pro
* Wiki: https://www.fw-web.de/dokuwiki/doku.php?id=en:start

License
----
GPL-2.0

*Free Software, Hell Yeah!*
