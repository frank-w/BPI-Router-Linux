#
# Makefile for the Mediatek SoCs built-in ethernet macs
#

ccflags-y=-Werror

obj-$(CONFIG_NET_MEDIATEK_SOC)		+= mtkswitch.o
mtkswitch-objs := gsw_mt7623.o

obj-$(CONFIG_NET_MEDIATEK_SOC)		+= mtketh.o
mtketh-objs := mtk_eth_soc.o mtk_dbg.o

obj-$(CONFIG_MT2712_MAC) += mt2712/
