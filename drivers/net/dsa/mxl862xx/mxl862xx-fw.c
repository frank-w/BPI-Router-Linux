// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Firmware flash and devlink support for MaxLinear MxL862xx
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 *
 * SB PDI - firmware download interface over clause-22 SMDIO
 * =========================================================
 *
 * The MxL862xx MCUboot loader accepts a firmware image through four "SB PDI"
 * registers in the switch SMDIO register space. It runs whenever no WSP
 * firmware is active: the normal firmware update enters it deliberately - the
 * SYS_MISC_FW_UPDATE API command sets a sticky rescue bit and reboots into
 * MCUboot - and the loader also stays here when the stored WSP firmware fails
 * its boot-time integrity check. This driver drives the loader's 0xc55c
 * "console" download path.
 *
 * SMDIO register access (mxl862xx_smdio_read/write):
 *   MII reg 0x1f := (<sb_pdi_reg> & 0xfff0)   ; page latch
 *   MII reg (<sb_pdi_reg> & 0x000f) := / => <u16 data>
 * so CTRL/ADDR/DATA/STAT (0xe100..0xe103) are MII regs 0/1/2/3 of page
 * 0xe100, not all reg 0x00.
 *
 * SB PDI registers (host name/addr  ->  MCU mailbox):
 *   CTRL 0xe100 -> 0xc0938400   mode: RST=0x00  RD=0x01  WR=0x02
 *   ADDR 0xe101 -> 0xc0938404   SB target word address (SB1 bank = 0x7800)
 *   DATA 0xe102 -> 0xc0938408   16-bit data / reply word
 *   STAT 0xe103 -> 0xc093840c   handshake: a magic (below) or a byte count
 *
 * STAT magics:
 *   READY  0xc55c   loader idle in the console loop        (this driver)
 *   START  0xf48f   host   -> begin download session
 *   ACK    0xf490   loader -> START acknowledged (START + 1)
 *   END    0x3cc3   host   -> finalise now (optional, see below)
 *
 * Console flash path (STAT=0xc55c) - mxl862xx_flash_firmware():
 *
 *   host                                   loader
 *   ----                                   ------
 *   reset (CTRL=ADDR=DATA=0)
 *   read STAT ............................ 0xc55c   (READY, idle)
 *   STAT := START(0xf48f)  -------------->
 *                          <-------------- STAT = 0xf490 (ACK)
 *   CTRL := WR
 *   DATA := hdr[0..9]  (20-byte header: type,size1,crc1,size2,crc2)
 *   reset; STAT := 20 (header len)  -----> parse hdr; r_remain=size1+size2;
 *                                          ERASE target region(s)
 *                          <-------------- STAT=21 (len+1), then STAT=0
 *                                          (erased)
 *   -- payload, streamed in slices: --
 *   CTRL := WR
 *   DATA := word x N ...
 *     at word 16384: CTRL:=RST; ADDR:=0x7800; CTRL:=WR  (half-bank -> SB1)
 *     at word 32760: flush slice:
 *        reset; STAT := <bytes_this_slice> ---> r_remain -= bytes; program
 *                          <------------------- STAT=0  (ready for next slice)
 *   ... repeat until the whole payload is sent ...
 *                          <------------------- STAT=0  image verified
 *                                               (STAT=1: image rejected)
 *   STAT := END(0x3cc3)  ---------------------> finalise and boot
 *
 * The r_remain == 0 rule (critical):
 *   Every host STAT write in the payload phase is a byte count; the loader
 *   does r_remain -= count and stays in the receive loop while r_remain != 0.
 *   It leaves the loop ONLY when r_remain hits EXACTLY 0, and a count larger
 *   than r_remain underflows the 32-bit counter and wedges the loader until a
 *   power cycle. Having left it, the loader verifies the image, publishes the
 *   verdict in STAT (0 good, 1 rejected) and waits 2 s for END before
 *   finalising regardless -- clearing its rescue-enable bit so boot_go boots
 *   the new image -- so END only saves that wait. Hence:
 *     - never send a slice/chunk count larger than what is outstanding;
 *     - a STAT write is a command only once the loader has left the loop;
 *     - the loader leaves the count in STAT while it programs the chunk, so
 *       a lingering count does not distinguish "busy" from "verdict".
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/overflow.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-fw.h"
#include "mxl862xx-host.h"

/* SB PDI registers (clause-22 SMDIO address space) */
#define MXL862XX_SB_PDI_CTRL		0xe100
#define MXL862XX_SB_PDI_ADDR		0xe101
#define MXL862XX_SB_PDI_DATA		0xe102
#define MXL862XX_SB_PDI_STAT		0xe103

/* SB PDI CTRL modes */
#define MXL862XX_SB_PDI_CTRL_RST	0x00
#define MXL862XX_SB_PDI_CTRL_WR		0x02

/* SB PDI handshake magic (published/consumed via STAT) */
#define MXL862XX_SB_PDI_READY		0xc55c	/* loader idle, console loop */
#define MXL862XX_SB_PDI_START		0xf48f
#define MXL862XX_SB_PDI_END		0x3cc3

/* Image verification verdict published in STAT once the receive loop ends */
#define MXL862XX_SB_PDI_VERIFY_OK	0
#define MXL862XX_SB_PDI_VERIFY_BAD	1

/* Firmware transfer geometry */
#define MXL862XX_FW_HDR_SIZE		20
#define MXL862XX_FW_BANK_HALF		16384	/* words per half-bank */
#define MXL862XX_FW_BANK_SLICE		32760	/* words per full slice */
#define MXL862XX_FW_SB1_ADDR		0x7800	/* SB1 word address */

/* Timeouts (generous upper bounds) */
#define MXL862XX_FW_READY_TIMEOUT_MS	3000
#define MXL862XX_FW_ACK_TIMEOUT_MS	5000
#define MXL862XX_FW_ERASE_TIMEOUT_MS	300000
#define MXL862XX_FW_WRITE_TIMEOUT_MS	120000
#define MXL862XX_FW_REBOOT_DELAY_MS	5000
#define MXL862XX_FW_REPROBE_DELAY_MS	500

static int mxl862xx_sb_pdi_reset(struct mxl862xx_priv *priv)
{
	int ret;

	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_CTRL,
				   MXL862XX_SB_PDI_CTRL_RST);
	if (ret < 0)
		return ret;

	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_ADDR,
				   MXL862XX_SB_PDI_CTRL_RST);
	if (ret < 0)
		return ret;

	return mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_DATA,
				    MXL862XX_SB_PDI_CTRL_RST);
}

static int mxl862xx_sb_pdi_poll_stat(struct mxl862xx_priv *priv, u16 expected,
				     unsigned long timeout_ms)
{
	int ret, val;

	ret = read_poll_timeout(mxl862xx_smdio_read, val,
				val < 0 || (u16)val == expected,
				10000, timeout_ms * 1000, false,
				priv, MXL862XX_SB_PDI_STAT);
	if (val < 0)
		return val;
	return ret;
}

static int mxl862xx_sb_pdi_flush_slice(struct mxl862xx_priv *priv,
				       u32 data_written)
{
	int ret;

	ret = mxl862xx_sb_pdi_reset(priv);
	if (ret < 0)
		return ret;

	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_STAT, data_written);
	if (ret < 0)
		return ret;

	return mxl862xx_sb_pdi_poll_stat(priv, 0,
					 MXL862XX_FW_WRITE_TIMEOUT_MS);
}

/* Flush the last slice, which ends the receive loop: the loader verifies the
 * image and replaces the count in STAT with its verdict, so wait for the count
 * to go rather than for a fixed value.
 */
static int mxl862xx_sb_pdi_flush_last(struct mxl862xx_priv *priv,
				      u32 data_written)
{
	int ret, val;

	ret = mxl862xx_sb_pdi_reset(priv);
	if (ret < 0)
		return ret;

	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_STAT, data_written);
	if (ret < 0)
		return ret;

	ret = read_poll_timeout(mxl862xx_smdio_read, val,
				val < 0 || (u16)val != (u16)data_written,
				10000, MXL862XX_FW_WRITE_TIMEOUT_MS * 1000,
				false, priv, MXL862XX_SB_PDI_STAT);
	if (val < 0)
		return val;

	if (!ret && (u16)val == MXL862XX_SB_PDI_VERIFY_OK)
		return 0;

	/* A final count of 1 is indistinguishable from the reject verdict, so
	 * a timeout still holding it lands here too.
	 */
	if ((u16)val == MXL862XX_SB_PDI_VERIFY_BAD) {
		dev_err(&priv->mdiodev->dev,
			"flash: loader rejected the image\n");
		return -EBADMSG;
	}

	return ret ? ret : -EPROTO;
}

static void mxl862xx_flash_notify(struct devlink *dl, const char *status,
				  u32 done, u32 total)
{
	devlink_flash_update_status_notify(dl, status, NULL, done, total);
}

/* Post-flash reprobe. device_reprobe() detaches the driver -- running
 * remove(), which frees priv -- then re-probes, so this work touches only its
 * own device and module references and frees itself. A re-probe failure leaves
 * the device unbound, exactly as a failed initial probe would, so it is only
 * logged. Running from a workqueue keeps device_reprobe() out of the devlink
 * caller's locking and signal context.
 */
struct mxl862xx_reprobe {
	struct delayed_work work;
	struct device *dev;
};

static void mxl862xx_reprobe_work_fn(struct work_struct *work)
{
	struct mxl862xx_reprobe *rp =
		container_of(work, struct mxl862xx_reprobe, work.work);
	struct device *dev = rp->dev;

	if (device_reprobe(dev))
		dev_err(dev, "reprobe failed; device left unbound\n");
	put_device(dev);
	kfree(rp);
	module_put(THIS_MODULE);
}

/* Allocate the reprobe up front, before the switch is disturbed, so an
 * allocation failure aborts cleanly. The caller holds a module and a device
 * reference; the work releases both once it runs. Returns NULL on -ENOMEM.
 */
static struct mxl862xx_reprobe *mxl862xx_reprobe_alloc(struct device *dev)
{
	struct mxl862xx_reprobe *rp;

	rp = kzalloc_obj(*rp);
	if (!rp)
		return NULL;
	rp->dev = dev;
	INIT_DELAYED_WORK(&rp->work, mxl862xx_reprobe_work_fn);
	return rp;
}

/* MCUboot firmware image header */
struct mxl862xx_fw_hdr {
	__le32 image_type;
	__le32 image_size_1;
	__le32 image_checksum_1;
	__le32 image_size_2;
	__le32 image_checksum_2;
} __packed;

static int mxl862xx_flash_validate(struct mxl862xx_priv *priv,
				   const struct firmware *fw,
				   u32 *payload_size)
{
	const struct mxl862xx_fw_hdr *hdr;
	u32 size1, size2, total;
	const u8 *payload;
	u32 crc;

	if (fw->size < MXL862XX_FW_HDR_SIZE)
		return -EINVAL;

	hdr = (const struct mxl862xx_fw_hdr *)fw->data;
	payload = fw->data + MXL862XX_FW_HDR_SIZE;
	size1 = le32_to_cpu(hdr->image_size_1);
	size2 = le32_to_cpu(hdr->image_size_2);

	if (check_add_overflow(size1, size2, &total) ||
	    total > fw->size - MXL862XX_FW_HDR_SIZE) {
		dev_err(&priv->mdiodev->dev,
			"flash: firmware file too small for declared size\n");
		return -EINVAL;
	}

	if (!total) {
		dev_err(&priv->mdiodev->dev,
			"flash: firmware file with empty payload\n");
		return -EINVAL;
	}

	if (size1) {
		crc = ~crc32_le(~0U, payload, size1);
		if (crc != le32_to_cpu(hdr->image_checksum_1)) {
			dev_err(&priv->mdiodev->dev,
				"flash: image 1 CRC mismatch (got %08x, expected %08x)\n",
				crc, le32_to_cpu(hdr->image_checksum_1));
			return -EINVAL;
		}
	}

	if (size2) {
		crc = ~crc32_le(~0U, payload + size1, size2);
		if (crc != le32_to_cpu(hdr->image_checksum_2)) {
			dev_err(&priv->mdiodev->dev,
				"flash: image 2 CRC mismatch (got %08x, expected %08x)\n",
				crc, le32_to_cpu(hdr->image_checksum_2));
			return -EINVAL;
		}
	}

	*payload_size = total;

	return 0;
}

static int mxl862xx_flash_firmware(struct mxl862xx_priv *priv,
				   const struct firmware *fw,
				   u32 payload_size, struct devlink *dl)
{
	const u8 *payload = fw->data + MXL862XX_FW_HDR_SIZE;
	u32 word_idx = 0, data_written = 0, idx = 0;
	unsigned long next_notify = jiffies - 1;
	u16 word, fdata;
	int ret, i;

	/* Step 1: reboot the firmware into MCUboot rescue mode */
	ret = mxl862xx_api_wrap(priv, SYS_MISC_FW_UPDATE, NULL, 0,
				false, false);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: FW_UPDATE command failed: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	/* Step 2: wait for bootloader ready */
	mxl862xx_flash_notify(dl, "Waiting for bootloader", 0, 0);
	ret = mxl862xx_sb_pdi_reset(priv);
	if (ret < 0)
		goto write_err;

	/* Failures from here on end up at no_end, which returns the error
	 * without signalling END -- see there.
	 */
	ret = mxl862xx_sb_pdi_poll_stat(priv, MXL862XX_SB_PDI_READY,
					MXL862XX_FW_READY_TIMEOUT_MS);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: bootloader not ready: %pe\n", ERR_PTR(ret));
		goto no_end;
	}

	/* Step 3: start handshake */
	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_STAT,
				   MXL862XX_SB_PDI_START);
	if (ret < 0)
		goto write_err;

	ret = mxl862xx_sb_pdi_poll_stat(priv, MXL862XX_SB_PDI_START + 1,
					MXL862XX_FW_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: start handshake failed: %pe\n", ERR_PTR(ret));
		goto no_end;
	}

	/* Step 4: transfer image header */
	mxl862xx_flash_notify(dl, "Erasing flash", 0, 0);
	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_CTRL,
				   MXL862XX_SB_PDI_CTRL_WR);
	if (ret < 0)
		goto write_err;

	for (i = 0; i < MXL862XX_FW_HDR_SIZE / 2; i++) {
		word = fw->data[i * 2] |
		       ((u16)fw->data[i * 2 + 1] << 8);
		ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_DATA, word);
		if (ret < 0)
			goto write_err;
	}

	ret = mxl862xx_sb_pdi_reset(priv);
	if (ret < 0)
		goto write_err;

	/* the byte count in STAT triggers the erase */
	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_STAT,
				   MXL862XX_FW_HDR_SIZE);
	if (ret < 0)
		goto write_err;

	/* ACK is byte count + 1 */
	ret = mxl862xx_sb_pdi_poll_stat(priv, MXL862XX_FW_HDR_SIZE + 1,
					MXL862XX_FW_ACK_TIMEOUT_MS);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: header ACK failed: %pe\n", ERR_PTR(ret));
		goto no_end;
	}

	/* Step 5: wait for erase to complete */
	ret = mxl862xx_sb_pdi_poll_stat(priv, 0,
					MXL862XX_FW_ERASE_TIMEOUT_MS);
	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: erase timeout: %pe\n", ERR_PTR(ret));
		goto no_end;
	}

	/* Step 6: transfer payload */
	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_CTRL,
				   MXL862XX_SB_PDI_CTRL_WR);
	if (ret < 0)
		goto write_err;

	while (idx < payload_size) {
		cond_resched();
		if (idx + 1 < payload_size) {
			fdata = payload[idx] |
				((u16)payload[idx + 1] << 8);
			idx += 2;
			data_written += 2;
		} else {
			fdata = payload[idx];
			idx++;
			data_written++;
		}

		ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_DATA, fdata);
		if (ret < 0)
			goto write_err;
		word_idx++;

		if (idx >= payload_size) {
			ret = mxl862xx_sb_pdi_flush_last(priv, data_written);
			break;
		}

		/* Half-bank boundary: switch to SB1 address */
		if (word_idx == MXL862XX_FW_BANK_HALF) {
			ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_CTRL,
						   MXL862XX_SB_PDI_CTRL_RST);
			if (ret < 0)
				goto write_err;

			ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_ADDR,
						   MXL862XX_FW_SB1_ADDR);
			if (ret < 0)
				goto write_err;

			ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_CTRL,
						   MXL862XX_SB_PDI_CTRL_WR);
			if (ret < 0)
				goto write_err;
		} else if (word_idx >= MXL862XX_FW_BANK_SLICE) {
			ret = mxl862xx_sb_pdi_flush_slice(priv, data_written);
			if (ret) {
				dev_err(&priv->mdiodev->dev,
					"flash: write timeout at %u/%u: %pe\n",
					idx, payload_size, ERR_PTR(ret));
				goto no_end;
			}
			word_idx = 0;
			data_written = 0;
			ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_CTRL,
						   MXL862XX_SB_PDI_CTRL_WR);
			if (ret < 0)
				goto write_err;

			if (time_after(jiffies, next_notify)) {
				mxl862xx_flash_notify(dl, "Flashing", idx,
						      payload_size);
				next_notify = jiffies + msecs_to_jiffies(500);
			}
		}
	}

	if (ret) {
		dev_err(&priv->mdiodev->dev,
			"flash: final slice failed: %pe\n", ERR_PTR(ret));
		goto no_end;
	}

	mxl862xx_flash_notify(dl, "Flashing", payload_size, payload_size);

	/* Success: the loader has left the receive loop at r_remain == 0 and
	 * verified the image, so END(0x3cc3) is a finalise/boot request rather
	 * than a byte count. Signal it here -- and only here -- to boot the new
	 * image without waiting out the loader's 2 s END timeout.
	 */
	ret = mxl862xx_smdio_write(priv, MXL862XX_SB_PDI_STAT,
				   MXL862XX_SB_PDI_END);
	msleep(MXL862XX_FW_REBOOT_DELAY_MS);
	return ret;

write_err:
	dev_err(&priv->mdiodev->dev, "flash: SMDIO write failed: %pe\n",
		ERR_PTR(ret));
no_end:
	/* A failure leaves the loader mid transfer; do not signal END (a STAT
	 * write is a byte count then, and END would be misread as one, risking
	 * a receive-counter underflow). Return the error; the caller reprobes.
	 */
	return ret;
}

int mxl862xx_devlink_info_get(struct dsa_switch *ds,
			      struct devlink_info_req *req,
			      struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	char buf[16];
	int ret;

	/* A 0 part number means the CHIP ID read failed or the part is
	 * unfused; omit it rather than publish a bogus "0000" that fwupd
	 * would match firmware against -- it then falls back to the driver
	 * name.
	 */
	if (priv->asic_id) {
		snprintf(buf, sizeof(buf), "%04X", priv->asic_id);
		ret = devlink_info_version_fixed_put(req,
						     DEVLINK_INFO_VERSION_GENERIC_ASIC_ID,
						     buf);
		if (ret)
			return ret;

		snprintf(buf, sizeof(buf), "%u", priv->asic_rev);
		ret = devlink_info_version_fixed_put(req,
						     DEVLINK_INFO_VERSION_GENERIC_ASIC_REV,
						     buf);
		if (ret)
			return ret;
	}

	/* An all-zero version is the cache a failed flash left behind, not a
	 * released firmware; omit it like the part number above.
	 */
	if (!priv->fw_version.major && !priv->fw_version.minor &&
	    !priv->fw_version.revision)
		return 0;

	snprintf(buf, sizeof(buf), "%u.%u.%u",
		 priv->fw_version.major, priv->fw_version.minor,
		 priv->fw_version.revision);

	ret = devlink_info_version_running_put(req,
			DEVLINK_INFO_VERSION_GENERIC_FW, buf);
	if (ret)
		return ret;

	/* boots this image from its own flash: stored == running */
	return devlink_info_version_stored_put(req,
			DEVLINK_INFO_VERSION_GENERIC_FW, buf);
}

int mxl862xx_devlink_flash_update(struct dsa_switch *ds,
				  struct devlink_flash_update_params *params,
				  struct netlink_ext_ack *extack)
{
	struct mxl862xx_reprobe *ko;
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp;
	u32 payload_size;
	int ret, i;

	if (params->component) {
		NL_SET_ERR_MSG_MOD(extack, "component is not supported");
		return -EOPNOTSUPP;
	}

	/* A previous flash is still waiting for its reprobe: the firmware API
	 * is short-circuited, so the raw SB PDI writes below would run against
	 * a switch this driver no longer tracks.
	 */
	if (priv->skip_teardown) {
		NL_SET_ERR_MSG_MOD(extack,
				   "device is reinitializing, retry later");
		return -EBUSY;
	}

	ret = mxl862xx_flash_validate(priv, params->fw, &payload_size);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack, "firmware image validation failed");
		return ret;
	}

	/* The references the reprobe work needs to restore normal operation
	 * must be held before the switch is disturbed; the work itself is
	 * scheduled only once the flash is done (see below).
	 */
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	get_device(ds->dev);

	/* Allocate the reprobe work before disturbing the switch, so an
	 * -ENOMEM here cannot strand it flashed but never reprobed.
	 */
	ko = mxl862xx_reprobe_alloc(ds->dev);
	if (!ko) {
		put_device(ds->dev);
		module_put(THIS_MODULE);
		return -ENOMEM;
	}

	dev_info(ds->dev, "flash: running firmware %u.%u.%u\n",
		 priv->fw_version.major, priv->fw_version.minor,
		 priv->fw_version.revision);

	/* Close ports while the firmware is still alive so the DSA core's
	 * MDB/FDB tracking is drained, and detach user ports so userspace
	 * cannot reopen them during the flash. The conduit is only closed,
	 * not detached: it belongs to the MAC driver. This driver binds a
	 * single switch with a direct host link and no cascade ports, so the
	 * conduit serves only this switch, and flashing it reboots the switch,
	 * which takes the tree down regardless.
	 */
	rtnl_lock();
	dsa_switch_for_each_user_port(dp, ds) {
		if (dp->user) {
			dev_close(dp->user);
			netif_device_detach(dp->user);
		}
	}
	dsa_switch_for_each_cpu_port(dp, ds)
		dev_close(dp->conduit);
	/* The bridge defers the STP state changes triggered by closing
	 * the ports; let them reach the firmware while it is still alive.
	 */
	switchdev_deferred_process();
	rtnl_unlock();

	mutex_lock_nested(&priv->mdiodev->bus->mdio_lock, MDIO_MUTEX_NESTED);
	priv->block_host = true;
	mutex_unlock(&priv->mdiodev->bus->mdio_lock);

	set_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags);
	cancel_delayed_work_sync(&priv->stats_work);
	cancel_work_sync(&priv->crc_err_work);
	for (i = 0; i < ds->num_ports; i++)
		cancel_work_sync(&priv->ports[i].host_flood_work);

	ret = mxl862xx_flash_firmware(priv, params->fw, payload_size,
				      ds->devlink);
	if (ret)
		NL_SET_ERR_MSG_MOD(extack, "firmware transfer failed");

	if (!ret) {
		mutex_lock_nested(&priv->mdiodev->bus->mdio_lock,
				  MDIO_MUTEX_NESTED);
		priv->block_host = false;
		mutex_unlock(&priv->mdiodev->bus->mdio_lock);

		/* Refresh the cached versions so the flash update only
		 * completes once the new firmware is confirmed running and
		 * devlink dev info reports it. Must happen before setting
		 * skip_teardown, which discards all firmware API reads.
		 */
		ret = mxl862xx_wait_ready(ds);
		if (ret)
			NL_SET_ERR_MSG_MOD(extack,
					   "new firmware did not become ready");
	}

	if (ret) {
		/* The switch is in MCUboot with erased or partly written flash;
		 * drop the cached identity so devlink dev info stops reporting
		 * the pre-flash version until the reprobe re-reads the truth.
		 */
		memset(&priv->fw_version, 0, sizeof(priv->fw_version));
		priv->asic_id = 0;
		priv->asic_rev = 0;
	}

	mutex_lock_nested(&priv->mdiodev->bus->mdio_lock, MDIO_MUTEX_NESTED);
	priv->skip_teardown = true;
	mutex_unlock(&priv->mdiodev->bus->mdio_lock);

	/* Queue the reprobe last; the work was allocated up front and its
	 * module and device references are already held.
	 */
	queue_delayed_work(system_long_wq, &ko->work,
			   msecs_to_jiffies(MXL862XX_FW_REPROBE_DELAY_MS));

	return ret;
}
