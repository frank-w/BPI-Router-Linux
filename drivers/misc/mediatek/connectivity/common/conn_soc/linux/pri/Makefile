ifeq ($(CONFIG_MTK_COMBO), y)

ccflags-y += \
	-I$(srctree)/$(src)/../../linux/include \
	-I$(srctree)/$(src)/../../linux/pri/include \
	-I$(srctree)/$(src)/../../core/include \
	-I$(srctree)/$(src)/../../include \
	-I$(srctree)/$(src)/../include \
	-I$(srctree)/$(src)/../../../common_detect \
	-I$(srctree)/drivers/misc/mediatek/btif/common/inc \
	-I$(srctree)/drivers/misc/mediatek/mach/$(MTK_PLATFORM)/include/mach

ccflags-y += -DWMT_CREATE_NODE_DYNAMIC=1

obj-y += stp_btif.o \
         stp_dbg.o \
         stp_exp.o \
         wmt_dev.o \
         wmt_exp.o

endif
