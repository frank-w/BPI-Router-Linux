/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_FW_H
#define __MXL862XX_FW_H

#include <net/dsa.h>

struct mxl862xx_priv;
struct work_struct;

int mxl862xx_rescue_mode_detect(struct mxl862xx_priv *priv);
void mxl862xx_rescue_heal_work_fn(struct work_struct *work);
int mxl862xx_devlink_info_get(struct dsa_switch *ds,
			      struct devlink_info_req *req,
			      struct netlink_ext_ack *extack);
int mxl862xx_devlink_flash_update(struct dsa_switch *ds,
				  struct devlink_flash_update_params *params,
				  struct netlink_ext_ack *extack);

#endif /* __MXL862XX_FW_H */
