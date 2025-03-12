#!/bin/bash -v
cat /proc/interrupts | grep ethernet
echo 1 > /proc/irq/101/smp_affinity                                                                                                                             
echo 2 > /proc/irq/102/smp_affinity                                                                                                                             
echo 4 > /proc/irq/103/smp_affinity                                                                                                                             
echo 8 > /proc/irq/104/smp_affinity                                                                                                                             

echo 2 > /proc/irq/106/smp_affinity                                                                                                                             

echo 0 > /sys/devices/platform/soc/15100000.ethernet/net/eth0/queues/rx-0/rps_cpus                                                                             
echo 0 > /sys/devices/platform/soc/15100000.ethernet/net/eth1/queues/rx-0/rps_cpus                                                                             
echo 0 > /sys/devices/platform/soc/15100000.ethernet/net/eth2/queues/rx-0/rps_cpus 
