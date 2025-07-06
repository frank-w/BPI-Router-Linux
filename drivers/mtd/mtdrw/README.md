mtd-rw - Write-enabler for MTD partitions
=========================================

Sets the `MTD_WRITEABLE` flag on all MTD partitions that are marked readonly.
When unloading, read-only partitions will be restored. This module is intended
for embedded devices where the mtd partition layout may be hard-coded in the
firmware. If, for some reason, you DO have to write to a read-only partition
(which is often a bad idea), this module is the way to go.

The module is currently limited to the first 64 partitions, but this
should suffice for most purposes.

Inspired by dougg3@electronics.stackexchange.com:
https://electronics.stackexchange.com/a/116133/97342

Usage:
```
# insmod mtd-rw.ko i_want_a_brick=1
# dmesg | grep mtd-rw
[52997.620000] mtd-rw: mtd0: setting writeable flag
[52997.630000] mtd-rw: mtd1: setting writeable flag
[52997.640000] mtd-rw: mtd3: setting writeable flag
[52997.650000] mtd-rw: mtd4: setting writeable flag
[52997.660000] mtd-rw: mtd6: setting writeable flag
```
