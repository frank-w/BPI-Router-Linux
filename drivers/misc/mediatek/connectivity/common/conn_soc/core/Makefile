ifeq ($(CONFIG_MTK_COMBO), y)

ccflags-y += \
	-I$(srctree)/$(src)/../linux/include \
	-I$(srctree)/$(src)/../linux/pri/include \
	-I$(srctree)/$(src)/../core/include \
	-I$(srctree)/$(src)/../include \
	-I$(srctree)/$(src)/../../common_detect \
	-I$(srctree)/drivers/misc/mediatek/btif/common/inc

obj-y += wmt_core.o \
         wmt_ctrl.o \
         wmt_func.o \
         wmt_ic_soc.o \
         wmt_lib.o \
         wmt_conf.o \
         btm_core.o \
         dbg_core.o \
         psm_core.o \
         stp_core.o \
         mtk_wcn_stub_alps.o \
         wmt_stp_exp.o

endif
