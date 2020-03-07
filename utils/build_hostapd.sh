#!/bin/bash

if [ "$(stat -c %d:%i /)" != "$(stat -c %d:%i /proc/1/root/. 2>/dev/null)" ]; then
	echo "We are chrooted! continue building..."

	apt update
	apt -y install pkg-config libssl-dev libreadline-dev libpcsclite-dev libnl-route-3-dev libnl-genl-3-dev libnl-3-dev libncurses5-dev libdbus-1-dev docbook-utils docbook-to-man
	apt -y install git gcc make
	cd /usr/src
	mkdir hostapd
	cd hostapd/
	git clone http://w1.fi/hostap.git
	cd hostap/hostapd/
	cp defconfig .config
	make

	file hostapd hostapd_cli
else
  echo "no chroot...exiting..."
fi
