/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: CK Hu <ck.hu@mediatek.com>
 */

#ifndef MTK_DRM_FBDEV_H
#define MTK_DRM_FBDEV_H

#ifdef CONFIG_DRM_FBDEV_EMULATION
int mtk_fbdev_init(struct drm_device *dev);
void mtk_fbdev_fini(struct drm_device *dev);
#else
int mtk_fbdev_init(struct drm_device *dev)
{
	return 0;
}

void mtk_fbdev_fini(struct drm_device *dev)
{

}
#endif /* CONFIG_DRM_FBDEV_EMULATION */

#endif /* MTK_DRM_FBDEV_H */
