#!/bin/bash -v
IP="192.168.90.2"
IP="192.168.1.1"

ip a a $IP/24 dev eth2
ip l s eth2 up
#ping 192.168.90.1 -c 1
ethtool -N eth2 flow-type tcp4 dst-ip $IP action 0 loc 0
ethtool -K eth2 lro on
ethtool -k eth2
#iperf3 -s
./bin/iperf3 -s
