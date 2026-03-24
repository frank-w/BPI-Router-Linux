// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Firmware flash and devlink support for MaxLinear MxL862xx
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 *
 * Usage:
 *   # Query running firmware version:
 *   devlink dev info mdio_bus/<bus>/<addr>
 *
 *   # Flash new firmware (all ports are taken down automatically):
 *   devlink dev flash mdio_bus/<bus>/<addr> file <firmware.bin>
 *
 * The flash process takes approximately 15 minutes. Progress is
 * reported via devlink status notifications. After a successful (or
 * failed) flash the driver reprobes the device automatically.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <net/dsa.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-fw.h"
#include "mxl862xx-host.h"

#define MXL862XX_API_WRITE(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), false, false)
#define MXL862XX_API_READ(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, false)
#define MXL862XX_API_READ_QUIET(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, true)

/* SB PDI register base and offsets (clause-22 SMDIO address space) */
#define MXL862XX_SB_PDI_BASE		0xe100
#define MXL862XX_SB_PDI_CTRL		0	/* + base */
#define MXL862XX_SB_PDI_ADDR		1
#define MXL862XX_SB_PDI_DATA		2
#define MXL862XX_SB_PDI_STAT		3

/* SB PDI handshake magic */
#define MXL862XX_SB_PDI_READY		0xc55c
#define MXL862XX_SB_PDI_START		0xf48f
#define MXL862XX_SB_PDI_START_ACK	0xf490
#define MXL862XX_SB_PDI_END		0x3cc3

/* Firmware transfer geometry */
#define MXL862XX_FW_HDR_SIZE		20
#define MXL862XX_FW_BANK_WORDS		32760	/* 32K bank in 16-bit words */
#define MXL862XX_FW_SB1_OFFSET		0x7800	/* SB1 word offset */

/* Timeouts */
#define MXL862XX_FW_READY_TIMEOUT_MS	30000
#define MXL862XX_FW_ACK_TIMEOUT_MS	5000
#define MXL862XX_FW_ERASE_TIMEOUT_MS	90000
#define MXL862XX_FW_REBOOT_DELAY_MS	5000


static int mxl862xx_sb_pdi_read(struct mxl862xx_priv *priv, int offset)
{
	return mxl862xx_smdio_read(priv, MXL862XX_SB_PDI_BASE + offset);
}

static int mxl862xx_sb_pdi_write(struct mxl862xx_priv *priv, int offset,
				 u16 val)
{
	return mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_BASE + offset, val);
}

static int mxl862xx_sb_pdi_poll_stat(struct mxl862xx_priv *priv, u16 expected,
				     unsigned long timeout_ms)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);
	int ret;

	do {
		ret = mxl862xx_sb_pdi_read(priv, MXL862XX_SB_PDI_STAT);
		if (ret < 0)
			return ret;
		if ((u16)ret == expected)
			return 0;
		msleep(100);
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

/* Reprobe work -- dynamically allocated so it survives remove().
 * device_reprobe() -> remove() frees priv (devm) while work is executing,
 * so the work struct must not live in mxl862xx_priv.
 */
struct mxl862xx_reprobe {
	struct device *dev;
	struct delayed_work dwork;
};

static void mxl862xx_reprobe_work_fn(struct work_struct *work)
{
	struct mxl862xx_reprobe *reprobe =
		container_of(work, struct mxl862xx_reprobe, dwork.work);

	if (device_reprobe(reprobe->dev))
		dev_err(reprobe->dev, "reprobe failed\n");
	put_device(reprobe->dev);
	kfree(reprobe);
	module_put(THIS_MODULE);
}

static int mxl862xx_flash_firmware(struct mxl862xx_priv *priv,
				   const struct firmware *fw,
				   struct devlink *dl)
{
	const u8 *data = fw->data;
	size_t size = fw->size;
	unsigned int bank_bytes = MXL862XX_FW_BANK_WORDS * 2;
	unsigned int slice_bytes = bank_bytes * 2; /* dual-bank = 64K words */
	unsigned int done = 0;
	u8 dummy = 0;
	int ret, i;
	u16 word;

	/* Step 1: Tell firmware to enter MCUboot rescue mode */
	ret = MXL862XX_API_WRITE(priv, SYS_MISC_FW_UPDATE, dummy);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: FW_UPDATE command failed: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Step 2: Wait for bootloader ready */
	devlink_flash_update_status_notify(dl, "Waiting for bootloader",
					   NULL, 0, 0);
	ret = mxl862xx_sb_pdi_poll_stat(priv, MXL862XX_SB_PDI_READY,
					MXL862XX_FW_READY_TIMEOUT_MS);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: bootloader not ready: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* Step 3: Start handshake */
	ret = mxl862xx_sb_pdi_write(priv, MXL862XX_SB_PDI_STAT,
				    MXL862XX_SB_PDI_START);
	if (ret < 0)
		return ret;

	ret = mxl862xx_sb_pdi_poll_stat(priv, MXL862XX_SB_PDI_START_ACK,
					MXL862XX_FW_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: start handshake failed: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* Step 4: Transfer 20-byte header */
	devlink_flash_update_status_notify(dl, "Erasing flash", NULL, 0, 0);
	for (i = 0; i < MXL862XX_FW_HDR_SIZE; i += 2) {
		word = data[i] | ((u16)data[i + 1] << 8);

		ret = mxl862xx_sb_pdi_write(priv, MXL862XX_SB_PDI_ADDR,
					    i / 2);
		if (ret < 0)
			return ret;
		ret = mxl862xx_sb_pdi_write(priv, MXL862XX_SB_PDI_DATA, word);
		if (ret < 0)
			return ret;
	}

	/* Write header byte count to STAT to trigger erase */
	ret = mxl862xx_sb_pdi_write(priv, MXL862XX_SB_PDI_STAT,
				    MXL862XX_FW_HDR_SIZE);
	if (ret < 0)
		return ret;

	/* Step 5: Wait for erase to complete (STAT goes to 0) */
	devlink_flash_update_timeout_notify(dl, "Erasing flash", NULL, 90);
	ret = mxl862xx_sb_pdi_poll_stat(priv, 0,
					MXL862XX_FW_ERASE_TIMEOUT_MS);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: erase timeout: %pe\n", ERR_PTR(ret));
		return ret;
	}

	/* Step 6: Transfer payload in dual-bank slices */
	data += MXL862XX_FW_HDR_SIZE;
	size -= MXL862XX_FW_HDR_SIZE;

	while (done < size) {
		unsigned int chunk = min((unsigned int)(size - done),
					slice_bytes);
		unsigned int pos = 0;
		u16 addr;

		/* Bank 0: first half */
		for (pos = 0; pos < chunk && pos < bank_bytes; pos += 2) {
			addr = pos / 2;
			word = data[done + pos] |
			       ((u16)data[done + pos + 1] << 8);

			ret = mxl862xx_sb_pdi_write(priv,
						    MXL862XX_SB_PDI_ADDR,
						    addr);
			if (ret < 0)
				return ret;
			ret = mxl862xx_sb_pdi_write(priv,
						    MXL862XX_SB_PDI_DATA,
						    word);
			if (ret < 0)
				return ret;
		}

		/* Bank 1: second half at SB1 offset */
		for (; pos < chunk; pos += 2) {
			addr = MXL862XX_FW_SB1_OFFSET +
			       (pos - bank_bytes) / 2;
			word = data[done + pos] |
			       ((u16)data[done + pos + 1] << 8);

			ret = mxl862xx_sb_pdi_write(priv,
						    MXL862XX_SB_PDI_ADDR,
						    addr);
			if (ret < 0)
				return ret;
			ret = mxl862xx_sb_pdi_write(priv,
						    MXL862XX_SB_PDI_DATA,
						    word);
			if (ret < 0)
				return ret;
		}

		/* Signal byte count for this slice, wait for ACK (0) */
		ret = mxl862xx_sb_pdi_write(priv, MXL862XX_SB_PDI_STAT,
					    chunk);
		if (ret < 0)
			return ret;

		ret = mxl862xx_sb_pdi_poll_stat(priv, 0,
						MXL862XX_FW_ERASE_TIMEOUT_MS);
		if (ret) {
			dev_err(&priv->mdiodev->dev,
				"flash: write timeout at offset %u: %pe\n",
				done, ERR_PTR(ret));
			return ret;
		}

		done += chunk;
		devlink_flash_update_status_notify(dl, "Flashing", NULL,
						   done, size);
	}

	/* Step 7: End magic */
	ret = mxl862xx_sb_pdi_write(priv, MXL862XX_SB_PDI_STAT,
				    MXL862XX_SB_PDI_END);
	if (ret < 0)
		return ret;

	/* Step 8: Wait for reboot */
	msleep(MXL862XX_FW_REBOOT_DELAY_MS);

	return 0;
}

int mxl862xx_devlink_info_get(struct dsa_switch *ds,
			      struct devlink_info_req *req,
			      struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	char ver_str[32];

	snprintf(ver_str, sizeof(ver_str), "%u.%u.%u",
		 priv->fw_version.major, priv->fw_version.minor,
		 priv->fw_version.revision);

	return devlink_info_version_running_put(req, "fw", ver_str);
}

int mxl862xx_devlink_flash_update(struct dsa_switch *ds,
				  struct devlink_flash_update_params *params,
				  struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_sys_fw_image_version ver = {};
	struct mxl862xx_reprobe *reprobe;
	struct dsa_port *dp;
	int ret, i;

	if (params->component) {
		NL_SET_ERR_MSG_MOD(extack, "component is not supported");
		return -EOPNOTSUPP;
	}

	dev_info(ds->dev, "flash: running firmware %u.%u.%u\n",
		 priv->fw_version.major, priv->fw_version.minor,
		 priv->fw_version.revision);

	/* Take all ports down and mark every netdev not-present so
	 * userspace cannot bring them back up during the (slow) flash.
	 * We take only RTNL here (not dsa2_mutex) to avoid a lock
	 * inversion with devl_lock.
	 */
	rtnl_lock();
	dsa_switch_for_each_user_port(dp, ds)
		if (dp->user)
			netif_device_detach(dp->user);
	dsa_switch_for_each_cpu_port(dp, ds) {
		dev_close(dp->conduit);
		netif_device_detach(dp->conduit);
	}
	rtnl_unlock();

	/* Stop stats polling and pending host-flood work */
	cancel_delayed_work_sync(&priv->stats_work);
	for (i = 0; i < ds->num_ports; i++)
		cancel_work_sync(&priv->ports[i].host_flood_work);

	ret = mxl862xx_flash_firmware(priv, params->fw, ds->devlink);
	if (ret)
		NL_SET_ERR_MSG_MOD(extack, "firmware transfer failed");

	if (!ret) {
		/* Read new firmware version (switch just rebooted) */
		memset(&ver, 0, sizeof(ver));
		if (!MXL862XX_API_READ_QUIET(priv, SYS_MISC_FW_VERSION, ver)
		    && ver.iv_major)
			dev_info(ds->dev, "flash: new firmware %u.%u.%u\n",
				 ver.iv_major, ver.iv_minor,
				 le16_to_cpu(ver.iv_revision));
	}

	/* Once SYS_MISC_FW_UPDATE has been sent the switch is in
	 * MCUboot mode -- there is no way to restore normal operation
	 * without a reprobe regardless of whether the transfer
	 * succeeded or failed.
	 */
	reprobe = kzalloc(sizeof(*reprobe), GFP_KERNEL);
	if (!reprobe)
		return ret;

	if (!try_module_get(THIS_MODULE)) {
		kfree(reprobe);
		return ret;
	}

	reprobe->dev = get_device(ds->dev);
	INIT_DELAYED_WORK(&reprobe->dwork, mxl862xx_reprobe_work_fn);
	schedule_delayed_work(&reprobe->dwork, msecs_to_jiffies(500));

	return ret;
}
