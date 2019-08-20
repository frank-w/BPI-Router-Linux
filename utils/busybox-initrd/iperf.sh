#!/bin/bash
#VERSION=3.1.7
VERSION=3.7
#CROSS_COMPILE=arm-linux-gnueabihf-
CROSS_COMPILE=aarch64-linux-gnu-

if [ ! -d iperf-${VERSION} ];then
	wget https://downloads.es.net/pub/iperf/iperf-${VERSION}.tar.gz
	tar -xzf iperf-${VERSION}.tar.gz
	cd iperf-${VERSION}
else
	cd iperf-${VERSION}
	make clean
fi
./configure --host=arm CC=${CROSS_COMPILE}gcc CXX=${CROSS_COMPILE}g++ CFLAGS=-static CXXFLAGS=-static --enable-static --prefix=`pwd`/output --without-openssl
make install
file output/bin/iperf3
