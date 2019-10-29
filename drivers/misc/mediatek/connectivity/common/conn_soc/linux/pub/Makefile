ifeq ($(CONFIG_MTK_COMBO), y)

ccflags-y += \
	-I$(srctree)/$(src)/../../linux/include \
	-I$(srctree)/$(src)/../../linux/pri/include \
	-I$(srctree)/$(src)/../../core/include \
	-I$(srctree)/$(src)/../../include \
	-I$(srctree)/$(src)/../include \
	-I$(srctree)/$(src)/../../../common_detect \
	-I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/mach \
	-DMTK_BT_HCI=1

ccflags-y += -DWMT_CREATE_NODE_DYNAMIC=1

ifeq ($(CONFIG_MTK_TC1_FEATURE), y)
    ccflags-y += -DCFG_TC1_FEATURE=1
else
    ccflags-y += -DCFG_TC1_FEATURE=0
endif

obj-y += osal.o \
         bgw_desense.o \
         wmt_idc.o

ifneq ($(CONFIG_MTK_COMBO_BT),)
    obj-y += stp_chrdev_bt.o
endif

ifneq ($(CONFIG_MTK_COMBO_WIFI),)
    obj-y += wmt_chrdev_wifi.o
endif

endif
