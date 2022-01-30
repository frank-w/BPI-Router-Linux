#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

#$(call all-subdir-src-or-makefile)
ifeq ($(subst ",,$(CONFIG_MTK_PLATFORM)),)
    $(info CONFIG_MTK_PLATFORM empty...try to set it)
    CONFIG_MTK_PLATFORM = "mt7623"
    $(info CONFIG_MTK_PLATFORM: $(CONFIG_MTK_PLATFORM))
endif

export MTK_PLATFORM = $(subst ",,$(CONFIG_MTK_PLATFORM))

#subdir-ccflags-y += -Werror
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/

obj-$(CONFIG_MTK_COMBO) += connectivity/
obj-$(CONFIG_MTK_BTIF) += btif/
