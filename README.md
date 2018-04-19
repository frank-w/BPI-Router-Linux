
Kernel 4.16 with patchwork from mediatek for BPI-R2

## Requirements

Need cross compile tools for the armhf architecture and additional bison and flex-package:
```sh
sudo apt-get install gcc-arm-linux-gnueabihf libc6-armhf-cross u-boot-tools bc make gcc libc6-dev libncurses5-dev libssl-dev bison flex
```

## Usage

  ./build.sh importconfig
  ./build.sh config
  ./build.sh

## Branch details

Kernel upstream branch are:
 * 4.16, 4.14, 4.9

Kernel upstream + BPI-R2
* 4.16_main, 4.14_main, 4.9_main

## Kernel version

Kernel breakdown features by version

|          | 4.4 | 4.9 | 4.14 | 4.16|
|----------| --- | --- | --- | ---|
| PCIe     | Y   | Y | Y | Y | Y |
| SATA     | Y   | Y | Y | Y | Y |
| 2 GMAC   | Y   | Y | N | N | N |
| DSA      | N   | Y | Y | Y | Y |
| VLAN     |     |   |   |   |   |
| HW NAT   |     | Y | Y |   |   |
| HW QOS   |     | Y | Y |   |   |
| Crypto   | Y   | Y | Y | Y | Y |
| WIFI     |     |   |   |   |   |
| BT       |     |   |   |   |   |
| VIDEO    | Y   | N | N | N | N |
| AUDIO    | Y   | N | N | N | N |

## Links

* BPI-R2: http://www.banana-pi.org/r2.html
* Kernel: https://www.kernel.org/
* linux-mediatek: https://patchwork.kernel.org/project/linux-mediatek/list/
* kernelci: https://kernelci.org/boot/mt7623n-bananapi-bpi-r2/

License
----

GPL-2.0

**Free Software, Hell Yeah!**
