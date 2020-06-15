subdir-ccflags-y += \
	-I$(srctree)/$(src)/linux/include \
	-I$(srctree)/$(src)/linux/pri/include \
	-I$(srctree)/$(src)/core/include \
	-I$(srctree)/$(src)/include \
	-I$(srctree)/drivers/misc/mediatek/btif/common/inc \
	-I$(srctree)/$(src)/$(subst ",,$(CONFIG_MTK_PLATFORM))/include

EXT_FLAG=_soc
COMMON_SRC_PATH := $(src)
COMMON_OBJ_PATH := $(src)

ifeq ($(CONFIG_ARCH_MT6580), y)
subdir-ccflags-y += -D CFG_WMT_READ_EFUSE_VCN33
endif

ifeq ($(CONFIG_MTK_COMBO), m)

# ---------------------------------------------------
# Directory List
# ---------------------------------------------------
CORE_DIR      := core/
LINUX_PRI_DIR := linux/pri/
LINUX_PUB_DIR := linux/pub/

# ---------------------------------------------------
# Objects List
# ---------------------------------------------------
CORE_OBJS := 	$(CORE_DIR)wmt_core.o \
				$(CORE_DIR)wmt_ctrl.o \
				$(CORE_DIR)wmt_func.o \
				$(CORE_DIR)wmt_ic_soc.o \
				$(CORE_DIR)wmt_lib.o \
				$(CORE_DIR)wmt_conf.o \
				$(CORE_DIR)btm_core.o \
				$(CORE_DIR)dbg_core.o \
				$(CORE_DIR)psm_core.o \
				$(CORE_DIR)stp_core.o \
				$(CORE_DIR)wmt_stp_exp.o \
				$(CORE_DIR)mtk_wcn_stub_alps.o


LINUX_PRI_OBJS := 	$(LINUX_PRI_DIR)stp_btif.o \
					$(LINUX_PRI_DIR)stp_dbg.o \
					$(LINUX_PRI_DIR)stp_exp.o \
					$(LINUX_PRI_DIR)wmt_dev.o \
					$(LINUX_PRI_DIR)wmt_exp.o


LINUX_PUB_OBJS :=	$(LINUX_PUB_DIR)osal.o \
					$(LINUX_PUB_DIR)bgw_desense.o \
					$(LINUX_PUB_DIR)wmt_idc.o


ccflags-y += -DWMT_CREATE_NODE_DYNAMIC=1
ccflags-y += -DMTK_BT_HCI=1
ccflags-y += -DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT
ifeq ($(CONFIG_MTK_TC1_FEATURE), y)
    ccflags-y += -DCFG_TC1_FEATURE=1
else
    ccflags-y += -DCFG_TC1_FEATURE=0
endif
ifeq ($(CONFIG_MTK_CONN_LTE_IDC_SUPPORT),y)
    ccflags-y += -DWMT_IDC_SUPPORT=1
else
    ccflags-y += -DWMT_IDC_SUPPORT=0
endif

# WMT LIBRARY DRIVER
obj-$(CONFIG_MTK_COMBO) += mtk_stp_wmt$(EXT_FLAG).o

mtk_stp_wmt$(EXT_FLAG)-objs	:= 	$(CORE_OBJS) \
	 	 	 	 	 	 	 	$(LINUX_PRI_OBJS) \
								$(LINUX_PUB_OBJS)

mtk_stp_wmt$(EXT_FLAG)-objs += $(subst ",,$(CONFIG_MTK_PLATFORM))/wmt_plat_alps.o
mtk_stp_wmt$(EXT_FLAG)-objs += $(subst ",,$(CONFIG_MTK_PLATFORM))/mtk_wcn_consys_hw.o

# WMT LIBRARY BT CONTROL DRIVER
obj-$(CONFIG_MTK_COMBO_BT) 	+= mtk_stp_bt$(EXT_FLAG).o
mtk_stp_bt$(EXT_FLAG)-objs	:= $(LINUX_PUB_DIR)stp_chrdev_bt.o

# WMT LIBRARY WIFI CONTROL DRIVER
obj-$(CONFIG_MTK_COMBO_WIFI) += mtk_wmt_wifi$(EXT_FLAG).o
mtk_wmt_wifi$(EXT_FLAG)-objs := $(LINUX_PUB_DIR)wmt_chrdev_wifi.o

endif

ifeq ($(CONFIG_MTK_COMBO), y)
subdir-ccflags-y += -DMTK_WCN_BUILT_IN_DRIVER
obj-y += core/
obj-y += linux/
obj-y += $(subst ",,$(CONFIG_MTK_PLATFORM))/
endif
