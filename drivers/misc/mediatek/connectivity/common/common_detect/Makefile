subdir-ccflags-y += -I$(srctree)/arch/arm/mach-$(MTK_PLATFORM)/$(ARCH_MTK_PROJECT)/dct/dct

COMBO_CHIP_SUPPORT := false
ifneq ($(filter "MT6620E3",$(CONFIG_MTK_COMBO_CHIP)),)
	COMBO_CHIP_SUPPORT := true
endif
ifneq ($(filter "MT6628",$(CONFIG_MTK_COMBO_CHIP)),)
	COMBO_CHIP_SUPPORT := true
endif
ifneq ($(filter "MT6630",$(CONFIG_MTK_COMBO_CHIP)),)
	COMBO_CHIP_SUPPORT := true
endif
ifeq ($(COMBO_CHIP_SUPPORT), true)
	subdir-ccflags-y += -D MTK_WCN_COMBO_CHIP_SUPPORT
	ccflags-y += -I$(srctree)/$(src)/../combo/linux/include
endif

ifneq ($(filter "CONSYS_%",$(CONFIG_MTK_COMBO_CHIP)),)
	subdir-ccflags-y += -D MTK_WCN_SOC_CHIP_SUPPORT
	ccflags-y += -I$(srctree)/$(src)/../conn_soc/linux/include
endif


ifeq ($(CONFIG_MTK_COMBO),y)
	ccflags-y += -I$(srctree)/$(src)/drv_init/inc
	obj-y += wmt_gpio.o

	obj-y += wmt_detect.o
	obj-y += sdio_detect.o
	obj-y += wmt_detect_pwr.o

	obj-y += drv_init/
endif

ifeq ($(CONFIG_MTK_COMBO),m)
	ccflags-y += -I$(srctree)/$(src)/drv_init/inc
	ccflags-y += -I$(srctree)/$(src)

ifneq ($(filter "MT6630",$(CONFIG_MTK_COMBO_CHIP)),)
	ccflags-y += -D MTK_WCN_WLAN_GEN3
endif
ifneq ($(filter "CONSYS_6797",$(CONFIG_MTK_COMBO_CHIP)),)
	ccflags-y += -D MTK_WCN_WLAN_GEN3
else ifneq ($(filter "CONSYS_%",$(CONFIG_MTK_COMBO_CHIP)),)
	ccflags-y += -D MTK_WCN_WLAN_GEN2
endif


	DRV_INIT_DIR := drv_init/

	DRV_INIT_OBJS := \
						$(DRV_INIT_DIR)common_drv_init.o \
						$(DRV_INIT_DIR)bluetooth_drv_init.o \
						$(DRV_INIT_DIR)conn_drv_init.o \
						$(DRV_INIT_DIR)wlan_drv_init.o
#						$(DRV_INIT_DIR)gps_drv_init.o \
#						$(DRV_INIT_DIR)fm_drv_init.o \

#	obj-($(CONFIG_MTK_COMBO_ANT)) += ant_drv_init.o

	obj-$(CONFIG_MTK_COMBO) += mtk_wmt_detect.o

#							wmt_stp_exp.o \
#							mtk_wcn_stub_alps.o \

	mtk_wmt_detect-objs := \
							wmt_detect.o \
							sdio_detect.o \
							wmt_detect_pwr.o \
							wmt_gpio.o

	mtk_wmt_detect-objs += $(DRV_INIT_OBJS)
endif
