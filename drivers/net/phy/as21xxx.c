// SPDX-License-Identifier: GPL-2.0
/*
 * Aeonsemi AS21XXxX PHY Driver
 *
 * Author: Christian Marangi <ansuelsmth@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include "as21xxx.h"

#define VEND1_GLB_REG_CPU_RESET_ADDR_LO_BASEADDR 0x3
#define VEND1_GLB_REG_CPU_RESET_ADDR_HI_BASEADDR 0x4

#define VEND1_GLB_REG_CPU_CTRL		0xe
#define   VEND1_GLB_CPU_CTRL_MASK	GENMASK(4, 0)
#define   VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK GENMASK(12, 8)
#define   VEND1_GLB_CPU_CTRL_LED_POLARITY(_n) FIELD_PREP(VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK, \
							 BIT(_n))

#define VEND1_FW_START_ADDR		0x100

#define VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD 0x101
#define VEND1_GLB_REG_MDIO_INDIRECT_LOAD 0x102

#define VEND1_GLB_REG_MDIO_INDIRECT_STATUS 0x103

#define VEND1_PTP_CLK			0x142
#define   VEND1_PTP_CLK_EN		BIT(6)

/* 5 LED at step of 0x20
 * FE: Fast-Ethernet (10/100)
 * GE: Gigabit-Ethernet (1000)
 * NG: New-Generation (2500/5000/10000)
 */
#define VEND1_LED_REG(_n)		(0x1800 + ((_n) * 0x10))
#define   VEND1_LED_REG_A_EVENT		GENMASK(15, 11)
#define VEND1_LED_CONF			0x1881
#define   VEND1_LED_CONFG_BLINK		GENMASK(7, 0)

#define VEND1_SPEED_STATUS		0x4002
#define   VEND1_SPEED_MASK		GENMASK(7, 0)
#define   VEND1_SPEED_10000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x3)
#define   VEND1_SPEED_5000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x5)
#define   VEND1_SPEED_2500		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x9)
#define   VEND1_SPEED_1000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x10)
#define   VEND1_SPEED_100		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x20)
#define   VEND1_SPEED_10		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x0)

#define VEND1_IPC_CMD			0x5801
#define   AEON_IPC_CMD_PARITY		BIT(15)
#define   AEON_IPC_CMD_SIZE		GENMASK(10, 6)
#define   AEON_IPC_CMD_OPCODE		GENMASK(5, 0)

#define IPC_CMD_NOOP			0x0  /* Do nothing */
#define IPC_CMD_INFO			0x1  /* Get Firmware Version */
#define IPC_CMD_SYS_CPU			0x2  /* SYS_CPU */
#define IPC_CMD_BULK_DATA		0xa  /* Pass bulk data in ipc registers. */
#define IPC_CMD_BULK_WRITE		0xc  /* Write bulk data to memory */
#define IPC_CMD_CFG_PARAM		0x1a /* Write config parameters to memory */
#define IPC_CMD_NG_TESTMODE		0x1b /* Set NG test mode and tone */
#define IPC_CMD_TEMP_MON		0x15 /* Temperature monitoring function */
#define IPC_CMD_SET_LED			0x23 /* Set led */

#define VEND1_IPC_STS			0x5802
#define   AEON_IPC_STS_PARITY		BIT(15)
#define   AEON_IPC_STS_SIZE		GENMASK(14, 10)
#define   AEON_IPC_STS_OPCODE		GENMASK(9, 4)
#define   AEON_IPC_STS_STATUS		GENMASK(3, 0)
#define   AEON_IPC_STS_STATUS_RCVD	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x1)
#define   AEON_IPC_STS_STATUS_PROCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x2)
#define   AEON_IPC_STS_STATUS_SUCCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x4)
#define   AEON_IPC_STS_STATUS_ERROR	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x8)
#define   AEON_IPC_STS_STATUS_BUSY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xe)
#define   AEON_IPC_STS_STATUS_READY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xf)

#define VEND1_IPC_DATA0			0x5808
#define VEND1_IPC_DATA1			0x5809
#define VEND1_IPC_DATA2			0x580a
#define VEND1_IPC_DATA3			0x580b
#define VEND1_IPC_DATA4			0x580c
#define VEND1_IPC_DATA5			0x580d
#define VEND1_IPC_DATA6			0x580e
#define VEND1_IPC_DATA7			0x580f
#define VEND1_IPC_DATA(_n)		(VEND1_IPC_DATA0 + (_n))

/* Sub command of CMD_INFO */
#define IPC_INFO_VERSION		0x1

/* Sub command of CMD_SYS_CPU */
#define IPC_SYS_CPU_REBOOT		0x3
#define IPC_SYS_CPU_IMAGE_OFST		0x4
#define IPC_SYS_CPU_IMAGE_CHECK		0x5
#define IPC_SYS_CPU_PHY_ENABLE		0x6

/* Sub command of CMD_CFG_PARAM */
#define IPC_CFG_PARAM_DIRECT		0x4

/* CFG DIRECT sub command */
#define IPC_CFG_PARAM_DIRECT_NG_PHYCTRL	0x1
#define IPC_CFG_PARAM_DIRECT_CU_AN	0x2
#define IPC_CFG_PARAM_DIRECT_SDS_PCS	0x3
#define IPC_CFG_PARAM_DIRECT_AUTO_EEE	0x4
#define IPC_CFG_PARAM_DIRECT_SDS_PMA	0x5
#define IPC_CFG_PARAM_DIRECT_DPC_RA	0x6
#define IPC_CFG_PARAM_DIRECT_DPC_PKT_CHK 0x7
#define IPC_CFG_PARAM_DIRECT_DPC_SDS_WAIT_ETH 0x8
#define IPC_CFG_PARAM_DIRECT_WDT	0x9
#define IPC_CFG_PARAM_DIRECT_SDS_RESTART_AN 0x10
#define IPC_CFG_PARAM_DIRECT_TEMP_MON	0x11
#define IPC_CFG_PARAM_DIRECT_WOL	0x12

/* Sub command of CMD_TEMP_MON */
#define IPC_CMD_TEMP_MON_GET		0x4

#define AS21XXX_MDIO_AN_C22		0xffe0

#define PHY_ID_AS21XXX			0x75009410
#define AEON_MEM_DEFAULT_ADDR (0x300100 >> 1)
#define MEM_WORD_SIZE 4
#define MAX_WDATA_SIZE 16
static int param1 = 1;
module_param(param1, int, 0444);
MODULE_PARM_DESC(param1, "First parameter");
/* AS21xxx ID Legend
 * AS21x1xxB1
 *     ^ ^^
 *     | |J: Supports SyncE/PTP
 *     | |P: No SyncE/PTP support
 *     | 1: Supports 2nd Serdes
 *     | 2: Not 2nd Serdes support
 *     0: 10G, 5G, 2.5G
 *     5: 5G, 2.5G
 *     2: 2.5G
 */
#define PHY_ID_AS21011JB1		0x75009402
#define PHY_ID_AS21011PB1		0x75009412
#define PHY_ID_AS21010JB1		0x75009422
#define PHY_ID_AS21010PB1		0x75009432
#define PHY_ID_AS21511JB1		0x75009442
#define PHY_ID_AS21511PB1		0x75009452
#define PHY_ID_AS21510JB1		0x75009462
#define PHY_ID_AS21510PB1		0x75009472
#define PHY_ID_AS21210JB1		0x75009482
#define PHY_ID_AS21210PB1		0x75009492
#define PHY_VENDOR_AEONSEMI		0x75009400

#define AEON_MAX_LEDS			5
#define AEON_IPC_DELAY			10000
#define AEON_IPC_TIMEOUT		(AEON_IPC_DELAY * 100)
#define AEON_IPC_DATA_MAX		(8 * sizeof(u16))
#define AEON_BOOT_ADDR			0x1000
#define AEON_CPU_BOOT_ADDR		0x2000
#define AEON_CPU_CTRL_FW_LOAD		(BIT(4) | BIT(2) | BIT(1) | BIT(0))
#define AEON_CPU_CTRL_FW_START		BIT(0)

enum as21xxx_led_event {
	VEND1_LED_REG_A_EVENT_ON_10 = 0x0,
	VEND1_LED_REG_A_EVENT_ON_100,
	VEND1_LED_REG_A_EVENT_ON_1000,
	VEND1_LED_REG_A_EVENT_ON_2500,
	VEND1_LED_REG_A_EVENT_ON_5000,
	VEND1_LED_REG_A_EVENT_ON_10000,
	VEND1_LED_REG_A_EVENT_ON_FE_GE,
	VEND1_LED_REG_A_EVENT_ON_NG,
	VEND1_LED_REG_A_EVENT_ON_FULL_DUPLEX,
	VEND1_LED_REG_A_EVENT_ON_COLLISION,
	VEND1_LED_REG_A_EVENT_BLINK_TX,
	VEND1_LED_REG_A_EVENT_BLINK_RX,
	VEND1_LED_REG_A_EVENT_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_LINK,
	VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_RX,
	VEND1_LED_REG_A_EVENT_ON_FE_GE_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_NG_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_NG_BLINK_FE_GE,
	VEND1_LED_REG_A_EVENT_ON_FD_BLINK_COLLISION,
	VEND1_LED_REG_A_EVENT_ON,
	VEND1_LED_REG_A_EVENT_OFF,
};

struct as21xxx_led_pattern_info {
	unsigned int pattern;
	u16 val;
};

static struct as21xxx_led_pattern_info as21xxx_led_supported_pattern[] = {
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10),
		.val = VEND1_LED_REG_A_EVENT_ON_10
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_100),
		.val = VEND1_LED_REG_A_EVENT_ON_100
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_1000),
		.val = VEND1_LED_REG_A_EVENT_ON_1000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500),
		.val = VEND1_LED_REG_A_EVENT_ON_2500
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_5000),
		.val = VEND1_LED_REG_A_EVENT_ON_5000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_10000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000),
		.val = VEND1_LED_REG_A_EVENT_ON_FE_GE
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_NG
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_FULL_DUPLEX),
		.val = VEND1_LED_REG_A_EVENT_ON_FULL_DUPLEX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_TX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_TX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_RX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_RX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_FE_GE_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_NG_BLINK_ACT
	}
};

static void aeon_mdio_patch(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct mii_bus *bus = phydev->mdio.bus;
	if (!bus) {
		dev_err(dev, "MDIO bus is NULL\r\n");
		return;
	}
	mutex_lock(&bus->mdio_lock);
	__mdiobus_c45_write(bus, 30, 0x1, 0x1, 0x1);
	mutex_unlock(&bus->mdio_lock);
}

/* AEONSEMI MDIO READ function */
static int aeon_cl45_read(struct phy_device *phydev, unsigned int dev_addr,
		   unsigned int phy_reg)
{
	int data;
	data = phy_read_mmd(phydev, dev_addr, phy_reg);
	aeon_mdio_patch(phydev);
	return data;
}

int aeon_mdio_read(struct phy_device *phydev, unsigned int dev_addr,
		   unsigned int phy_reg)
{
	int ret = 0;
	struct mii_bus *bus = phydev->mdio.bus;
	int phy_addr = phydev->mdio.addr;
	if (!bus) {
		phydev_err(phydev, "MDIO bus is NULL\r\n");
		return -ENODEV;
	}
	if (phy_addr >= PHY_MAX_ADDR) {
		phydev_err(phydev, "Invaild PHY address: %d", phy_addr);
		return -EINVAL;
	}
	mutex_lock(&bus->mdio_lock);
	ret = __mdiobus_c45_read(bus, phy_addr, dev_addr, phy_reg);
	mutex_unlock(&bus->mdio_lock);
	aeon_mdio_patch(phydev);
	return ret;
}

void aeon_mdio_write(struct phy_device *phydev, unsigned int dev_addr,
		     unsigned int phy_reg, unsigned int phy_data)
{
	struct mii_bus *bus = phydev->mdio.bus;
	int phy_addr = phydev->mdio.addr;
	if (!bus) {
		phydev_err(phydev, "MDIO bus is NULL\r\n");
		return;
	}
	if (phy_addr >= PHY_MAX_ADDR) {
		phydev_err(phydev, "Invaild PHY address: %d", phy_addr);
		return;
	}
	mutex_lock(&bus->mdio_lock);
	__mdiobus_c45_write(bus, phy_addr, dev_addr, phy_reg, phy_data);
	mutex_unlock(&bus->mdio_lock);
	aeon_mdio_patch(phydev);
}

/* AEONSEMI burst write for load fw */
static void aeon_cl45_write_burst(struct phy_device *phydev, unsigned int dev_addr,
			   unsigned int phy_reg, const unsigned char *data,
			   int size)
{
	unsigned short write_data = 0, i = 0;
	for (i = 0; i < size; i += 2) {
		write_data = (data[i + 1] << 8) | data[i];
		phy_write_mmd(phydev, dev_addr, phy_reg, write_data);
	}
}

static int aeon_firmware_boot(struct phy_device *phydev, const u8 *data,
			      size_t size)
{
	int i, ret;
	int val;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL,
			     VEND1_GLB_CPU_CTRL_MASK, AEON_CPU_CTRL_FW_LOAD);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_FW_START_ADDR,
			    AEON_BOOT_ADDR);
	if (ret)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			     VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD,
			     0x3ffc, 0xc000);
	if (ret)
		return ret;

	val = aeon_cl45_read(phydev, MDIO_MMD_VEND1,
			   VEND1_GLB_REG_MDIO_INDIRECT_STATUS);
	if (val > 1) {
		phydev_err(phydev, "wrong origin mdio_indirect_status: %x\n", val);
		return -EINVAL;
	}

	/* Firmware is always aligned to u16 */
	for (i = 0; i < size; i += 2) {
		val = data[i + 1] << 8 | data[i];
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				    VEND1_GLB_REG_MDIO_INDIRECT_LOAD, val);
		if (ret)
			return ret;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    VEND1_GLB_REG_CPU_RESET_ADDR_LO_BASEADDR,
			    lower_16_bits(AEON_CPU_BOOT_ADDR));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    VEND1_GLB_REG_CPU_RESET_ADDR_HI_BASEADDR,
			    upper_16_bits(AEON_CPU_BOOT_ADDR));
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL,
			      VEND1_GLB_CPU_CTRL_MASK, AEON_CPU_CTRL_FW_START);
}

static int aeon_set_default_value(struct phy_device *phydev)
{
	static const unsigned char base_data[] = {0x32, 0x30, 0x32, 0x33, 0x30, 0x37, 0x31, 0x34};
	unsigned char bytebuf[16];
	unsigned short *wdata;
	unsigned int mask;
	int byte_count, wdata_count = 0;
	int pos = 0, val, ret = 0, remaining;
	unsigned char padded_bytes[MEM_WORD_SIZE] = {0};
	mask = param1 | 14;
	memcpy(bytebuf, base_data, sizeof(base_data));
	bytebuf[8] = mask & 0xff;
	bytebuf[9] = (mask >> 8) & 0xff;
	byte_count = 10;
	wdata = kmalloc(MAX_WDATA_SIZE * sizeof(unsigned short), GFP_KERNEL);
	if (!wdata) {
		pr_err("Failed to allocate wdata array\n");
		return -ENOMEM;
	}
	while (pos + MEM_WORD_SIZE <= byte_count) {
		if (wdata_count + 2 > MAX_WDATA_SIZE) {
			pr_err("wdata array overflow\n");
			ret = -ENOSPC;
			goto cleanup;
		}
		wdata[wdata_count++] = le16_to_cpu(*(unsigned short *)&bytebuf[pos]);
		wdata[wdata_count++] = le16_to_cpu(*(unsigned short *)&bytebuf[pos + 2]);
		pos += MEM_WORD_SIZE;
	}
	remaining = byte_count - pos;
	if (remaining > 0) {
		if (wdata_count + 2 <= MAX_WDATA_SIZE) {
			// Here we just need padded_bytes once, otherwise we need to read from mem
			memcpy(padded_bytes, &bytebuf[pos], remaining);
			wdata[wdata_count++] = le16_to_cpu(*(unsigned short *)&padded_bytes[0]);
			wdata[wdata_count++] = le16_to_cpu(*(unsigned short *)&padded_bytes[2]);
		}
	}
	val = aeon_cl45_read(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL); //GLB_REG_CPU_CTRL
	val |= 0x12;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL, val);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_FW_START_ADDR,
			(u16)(AEON_MEM_DEFAULT_ADDR & 0xFFFF));
	phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD,
			0x3ffc, 0xc000);
	aeon_cl45_write_burst(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_LOAD,
			(unsigned char *)wdata, wdata_count*2);
	val = aeon_cl45_read(phydev, MDIO_MMD_VEND1,
			VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD); //GLB_REG_MDIO_INDIRECT_ADDRCMD
	val &= 0x3FFF;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD, val);
	val = aeon_cl45_read(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL); //GLB_REG_CPU_CTRL
	val &= 0xFFED;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL, val);
cleanup:
	kfree(wdata);
	return 0;
}

static void aeon_set_fast_mdc_timing(struct phy_device *phydev)
{
	phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x53, 0xFFFF);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x54, 0xFFFF);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x55, 0xFFFF);
}

static int aeon_firmware_load(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	const struct firmware *fw;
	const char *fw_name;
	int ret;
	ret = of_property_read_string(dev->of_node, "firmware-name",
				      &fw_name);
	if (ret)
		return ret;
	ret = request_firmware(&fw, fw_name, dev);
	if (ret) {
		phydev_err(phydev, "failed to find FW file %s (%d)\n",
			   fw_name, ret);
		return ret;
	}
	ret = aeon_firmware_boot(phydev, fw->data, fw->size);
	release_firmware(fw);
	return ret;
}

static int aeon_ipc_send_cmd(struct phy_device *phydev,
			     struct as21xxx_priv *priv,
			     u16 cmd, u16 *ret_sts)
{
	bool curr_parity;
	int ret;
	int val;

	/* The IPC sync by using a single parity bit.
	 * Each CMD have alternately this bit set or clear
	 * to understand correct flow and packet order.
	 */
	curr_parity = priv->parity_status;
	if (priv->parity_status)
		cmd |= AEON_IPC_CMD_PARITY;

	/* Always update parity for next packet */
	priv->parity_status = !priv->parity_status;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_CMD, cmd);
	if (ret)
		return ret;

	/* Wait for packet to be processed */
	usleep_range(AEON_IPC_DELAY, AEON_IPC_DELAY + 5000);

	/* With no ret_sts, ignore waiting for packet completion
	 * (ipc parity bit sync)
	 */
	if (!ret_sts)
		return 0;

	/* Exit condition logic:
	 * - Wait for parity bit equal
	 * - Wait for status success, error OR ready
	 */
	ret = read_poll_timeout(aeon_cl45_read, val,
				(FIELD_GET(AEON_IPC_STS_PARITY, val) == curr_parity &&
				(val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_RCVD &&
				(val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_PROCESS &&
				(val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_BUSY) ||
				(val < 0),
				10000, 2000000, false,
				phydev, MDIO_MMD_VEND1, VEND1_IPC_STS);
	if (val < 0)
		ret = val;

	if (ret)
		phydev_err(phydev, "%s fail to polling status failed: %d\n", __func__, ret);
	*ret_sts = val;

	if ((val & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_SUCCESS)
		return -EFAULT;

	return 0;
}

static int aeon_ipc_send_msg(struct phy_device *phydev,
			     u16 opcode, u16 *data, unsigned int data_len,
			     u16 *ret_sts)
{
	struct as21xxx_priv *priv = phydev->priv;
	u16 cmd;
	int ret;
	int i;

	/* IPC have a max of 8 register to transfer data,
	 * make sure we never exceed this.
	 */
	if (data_len > AEON_IPC_DATA_MAX)
		return -EINVAL;

	mutex_lock(&priv->ipc_lock);
	for (i = 0; i < data_len / sizeof(u16); i++)
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i),
			      data[i]);

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, data_len) |
	      FIELD_PREP(AEON_IPC_CMD_OPCODE, opcode);

	ret = aeon_ipc_send_cmd(phydev, priv, cmd, ret_sts);
	if (ret)
		phydev_err(phydev, "failed to send ipc msg for %x: %d\n",
			   opcode, ret);

	mutex_unlock(&priv->ipc_lock);

	return ret;
}

static int aeon_ipc_rcv_msg(struct phy_device *phydev,
			    u16 ret_sts, u16 *data)
{
	struct as21xxx_priv *priv = phydev->priv;
	unsigned int size;
	int ret;
	int i;

	if ((ret_sts & AEON_IPC_STS_STATUS) == AEON_IPC_STS_STATUS_ERROR)
		return -EINVAL;

	/* Prevent IPC from stack smashing the kernel */
	size = FIELD_GET(AEON_IPC_STS_SIZE, ret_sts);
	if (size > AEON_IPC_DATA_MAX)
		return -EINVAL;

	mutex_lock(&priv->ipc_lock);
	for (i = 0; i < DIV_ROUND_UP(size, sizeof(u16)); i++) {
		ret = aeon_cl45_read(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i));
		if (ret < 0) {
			size = ret;
			goto out;
		}
		data[i] = ret;
	}
out:
	mutex_unlock(&priv->ipc_lock);
	return size;
}

static int aeon_ipc_noop(struct phy_device *phydev,
			 struct as21xxx_priv *priv, u16 *ret_sts)
{
	u16 cmd;

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, 0) |
	      FIELD_PREP(AEON_IPC_CMD_OPCODE, IPC_CMD_NOOP);

	return aeon_ipc_send_cmd(phydev, priv, cmd, ret_sts);
}

/* Logic to sync parity bit with IPC.
 * We send 2 NOP cmd with same partity and we wait for IPC
 * to handle the packet only for the second one. This way
 * we make sure we are sync for every next cmd.
 */
static int aeon_ipc_sync_parity(struct phy_device *phydev,
				struct as21xxx_priv *priv)
{
	u16 ret_sts;
	int ret;

	mutex_lock(&priv->ipc_lock);

	/* Send NOP with no parity */
	aeon_ipc_noop(phydev, priv, NULL);

	/* Reset packet parity */
	priv->parity_status = false;

	/* Send second NOP with no parity */
	ret = aeon_ipc_noop(phydev, priv, &ret_sts);

	mutex_unlock(&priv->ipc_lock);

	/* We expect to return -EINVAL */
	if (ret != -EFAULT)
		return ret;

	if ((ret_sts & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_READY) {
		phydev_err(phydev, "Invalid IPC status on sync parity: %x\n",
			   ret_sts);
		return -EINVAL;
	}

	return 0;
}

static int aeon_ipc_get_fw_version(struct phy_device *phydev)
{
	u16 ret_data[8], data[1];
	u16 ret_sts;
	int ret;

	data[0] = IPC_INFO_VERSION;

	ret = aeon_ipc_send_msg(phydev, IPC_CMD_INFO, data,
				sizeof(data), &ret_sts);
	if (ret)
		return ret;

	ret = aeon_ipc_rcv_msg(phydev, ret_sts, ret_data);
	if (ret < 0)
		return ret;

	phydev_info(phydev, "Firmware Version: %s\n", (char *)ret_data);

	return 0;
}

/*static int aeon_dpc_ra_enable(struct phy_device *phydev)
{
	u16 data[2];
	u16 ret_sts;
	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_DPC_RA;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), &ret_sts);
}*/

static int aeon_read_abilities(struct phy_device *phydev)
{
	int val;

	linkmode_set_bit_array(phy_basic_ports_array,
			       ARRAY_SIZE(phy_basic_ports_array),
			       phydev->supported);
	val = aeon_cl45_read(phydev, 0x7, 0xffe1);

	if (val < 0)
		return val;

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->supported,
			 val & BMSR_ANEGCAPABLE);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, phydev->supported,
			 val & BMSR_100FULL);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, phydev->supported,
			 val & BMSR_100HALF);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT, phydev->supported,
			 val & BMSR_10FULL);
	linkmode_mod_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT, phydev->supported,
			 val & BMSR_10HALF);

	if (val & BMSR_ESTATEN) {
		val = aeon_cl45_read(phydev, 0x7, 0xffef);
		if (val < 0)
			return val;
		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				 phydev->supported, val & ESTATUS_1000_TFULL);
		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
				 phydev->supported, val & ESTATUS_1000_THALF);
		linkmode_mod_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
				 phydev->supported, val & ESTATUS_1000_XFULL);
	}
	/* This is optional functionality. If not supported, we may get an error
	 * which should be ignored.
	 */
	//genphy_c45_read_eee_abilities(phydev);

	return 0;
}

static int as21xxx_get_features(struct phy_device *phydev)
{
	int ret;
	ret = aeon_read_abilities(phydev);
	if (ret)
		return ret;
	/* AS21xxx supports 100M/1G/2.5G/5G/10G speed. */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
			   phydev->supported);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
			   phydev->supported);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
			   phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
			 phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
			 phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
			 phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
			 phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
			 phydev->supported);
	return 0;
}

static int as21xxx_probe(struct phy_device *phydev)
{
	struct as21xxx_priv *priv;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev,
			    sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;
	if (param1) {
		aeon_set_fast_mdc_timing(phydev);
		aeon_set_default_value(phydev);
	}
	ret = aeon_firmware_load(phydev);
	mutex_init(&priv->ipc_lock);
	if (ret)
		return ret;

	ret = aeon_ipc_sync_parity(phydev, priv);
	if (ret)
		return ret;

	ret = aeon_ipc_get_fw_version(phydev);
	if (ret)
		return ret;
	//ret = as21xxx_debugfs_init(phydev);
	//if (ret)
	//	return ret;
	/* Enable PTP clk if not already Enabled */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK,
			       VEND1_PTP_CLK_EN);
	if (ret)
		return ret;
	return 0;
}

static int aeon_update_link(struct phy_device *phydev)
{
	int status = 0, bmcr;
	bmcr = aeon_cl45_read(phydev, 0x7, 0xffe0);
	if (bmcr < 0)
		return bmcr;
	/* Autoneg is being started, therefore disregard BMSR value and
	 * report link as down.
	 */
	if (bmcr & BMCR_ANRESTART)
		goto done;
	/* The link state is latched low so that momentary link
	 * drops can be detected. Do not double-read the status
	 * in polling mode to detect such short link drops.
	 */
	if (!phy_polling_mode(phydev)) {
		status = aeon_cl45_read(phydev, 0x7, 0xffe1);
		if (status < 0)
			return status;
		else if (status & BMSR_LSTATUS)
			goto done;
	}
	/* Read link and autonegotiation status */
	status = aeon_cl45_read(phydev, 0x7, 0xffe1);
	if (status < 0)
		return status;
done:
	phydev->link = status & BMSR_LSTATUS ? 1 : 0;
	phydev->autoneg_complete = status & BMSR_ANEGCOMPLETE ? 1 : 0;
	/* Consider the case that autoneg was started and "aneg complete"
	 * bit has been reset, but "link up" bit not yet.
	 */
	if (phydev->autoneg == AUTONEG_ENABLE && !phydev->autoneg_complete)
		phydev->link = 0;
	return 0;
}

static int aeon_read_lpa(struct phy_device *phydev)
{
	int lpa, lpagb;
	if (phydev->autoneg == AUTONEG_ENABLE) {
		if (!phydev->autoneg_complete) {
			mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising,
							0);
			mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, 0);
			return 0;
		}
		if (phydev->is_gigabit_capable) {
			lpagb = aeon_cl45_read(phydev, 0x7, 0xffea);
			if (lpagb < 0)
				return lpagb;
			if (lpagb & LPA_1000MSFAIL) {
				int adv = aeon_cl45_read(phydev, 0x7, 0xffe9);
				if (adv < 0)
					return adv;
				if (adv & CTL1000_ENABLE_MASTER)
					phydev_err(
						phydev,
						"Master/Slave resolution failed, maybe conflicting manual settings?\n");
				else
					phydev_err(
						phydev,
						"Master/Slave resolution failed\n");
				return -ENOLINK;
			}
			mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising,
							lpagb);
		}
		lpa = aeon_cl45_read(phydev, 0x7, 0xffe5);
		if (lpa < 0)
			return lpa;
		mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);
	} else {
		linkmode_zero(phydev->lp_advertising);
	}
	return 0;
}

static void aeon_read_speed(struct phy_device *phydev)
{
	int bmcr, speed;
	bmcr = aeon_cl45_read(phydev, 0x7, 0xffe0);
	if (bmcr < 0)
		return;
	speed = aeon_cl45_read(phydev, 0x1e, 0x4002);
	if (speed < 0)
		return;
	speed &= 0xff;
	if (speed == 0x3) {
		phydev->speed = SPEED_10000;
		phydev->duplex = DUPLEX_FULL;
	} else if (speed == 0x5) {
		phydev->speed = SPEED_5000;
		phydev->duplex = DUPLEX_FULL;
	} else if (speed == 0x9) {
		phydev->speed = SPEED_2500;
		phydev->duplex = DUPLEX_FULL;
	} else if (speed == 0x10) {
		phydev->speed = SPEED_1000;
		if (bmcr & BMCR_FULLDPLX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;
	} else if (speed == 0x20) {
		phydev->speed = SPEED_100;
		if (bmcr & BMCR_FULLDPLX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;
	} else {
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_FULL;
	}
}

static void aeon_resolve_aneg_linkmode(struct phy_device *phydev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
	linkmode_and(common, phydev->lp_advertising, phydev->advertising);
	phy_resolve_aneg_pause(phydev);
}

static int as21xxx_read_status(struct phy_device *phydev)
{
	int err, old_link = phydev->link;
	/* Update the link, but return if there was an error */
	err = aeon_update_link(phydev);
	if (err)
		return err;
	/* why bother the PHY if nothing can have changed */
	if (phydev->autoneg == AUTONEG_ENABLE && old_link && phydev->link)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	err = aeon_read_lpa(phydev);
	if (err < 0)
		return err;
	if (phydev->autoneg == AUTONEG_ENABLE && phydev->autoneg_complete) {
		aeon_read_speed(phydev);
		aeon_resolve_aneg_linkmode(phydev);
	} else if (phydev->autoneg == AUTONEG_DISABLE) {
		aeon_read_speed(phydev);
	}

	return 0;
}

static int as21xxx_led_brightness_set(struct phy_device *phydev,
				      u8 index, enum led_brightness value)
{
	u16 val = VEND1_LED_REG_A_EVENT_OFF;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	if (value)
		val = VEND1_LED_REG_A_EVENT_ON;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_LED_REG(index),
			      VEND1_LED_REG_A_EVENT,
			      FIELD_PREP(VEND1_LED_REG_A_EVENT, val));
}

static int as21xxx_led_hw_is_supported(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	int i;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (rules == as21xxx_led_supported_pattern[i].pattern)
			return 0;

	return -EOPNOTSUPP;
}

static int as21xxx_led_hw_control_get(struct phy_device *phydev, u8 index,
				      unsigned long *rules)
{
	int i, val;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;
	val = aeon_cl45_read(phydev, MDIO_MMD_VEND1, VEND1_LED_REG(index));
	if (val < 0)
		return val;

	val = FIELD_GET(VEND1_LED_REG_A_EVENT, val);
	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (val == as21xxx_led_supported_pattern[i].val) {
			*rules = as21xxx_led_supported_pattern[i].pattern;
			return 0;
		}

	/* Should be impossible */
	return -EINVAL;
}

static int as21xxx_led_hw_control_set(struct phy_device *phydev, u8 index,
				      unsigned long rules)
{
	u16 val = 0;
	int i;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (rules == as21xxx_led_supported_pattern[i].pattern) {
			val = as21xxx_led_supported_pattern[i].val;
			break;
		}

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_LED_REG(index),
			      VEND1_LED_REG_A_EVENT,
			      FIELD_PREP(VEND1_LED_REG_A_EVENT, val));
}

static int as21xxx_led_polarity_set(struct phy_device *phydev, int index,
				    unsigned long modes)
{
	bool led_active_low = false;
	u16 mask, val = 0;
	u32 mode;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for_each_set_bit(mode, &modes, __PHY_LED_MODES_NUM) {
		switch (mode) {
		case PHY_LED_ACTIVE_LOW:
			led_active_low = true;
			break;
		case PHY_LED_ACTIVE_HIGH: /* default mode */
			led_active_low = false;
			break;
		default:
			return -EINVAL;
		}
	}
	mask = VEND1_GLB_CPU_CTRL_LED_POLARITY(index);
	if (led_active_low)
		val = VEND1_GLB_CPU_CTRL_LED_POLARITY(index);
	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_GLB_REG_CPU_CTRL,
			      mask, val);
}

static int aeon_read_pid(struct phy_device *phydev)
{
	int pid1 = 0, pid2 = 0, pid = 0;
	pid1 = aeon_mdio_read(phydev, 0x1, 2);
	if (pid1 < 0)
		return pid1;
	pid2 = aeon_mdio_read(phydev, 0x1, 3);
	if (pid2 < 0)
		return pid2;
	phydev_dbg(phydev, "%s aeonsemi1 PHY = %x - %x\n", __func__, pid1, pid2);
	pid = ((pid1 & 0xffff) << 16) | (pid2 & 0xffff);
	return pid;
}

static int aeon_c45_an_disable_aneg(struct phy_device *phydev)
{
	return phy_clear_bits_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1,
				       MDIO_AN_CTRL1_ENABLE |
					       MDIO_AN_CTRL1_RESTART);
}

static int aeon_c45_pma_setup_forced(struct phy_device *phydev)
{
	int ctrl1, ctrl2;
	/* Half duplex is not supported */
	if (phydev->duplex != DUPLEX_FULL)
		return -EINVAL;
	ctrl1 = aeon_cl45_read(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
	if (ctrl1 < 0)
		return ctrl1;
	ctrl2 = aeon_cl45_read(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL2);
	if (ctrl2 < 0)
		return ctrl2;
	ctrl1 &= ~MDIO_CTRL1_SPEEDSEL;
	/*
	 * PMA/PMD type selection is 1.7.5:0 not 1.7.3:0.  See 45.2.1.6.1
	 * in 802.3-2012 and 802.3-2015.
	 */
	ctrl2 &= ~(MDIO_PMA_CTRL2_TYPE | 0x30);
	switch (phydev->speed) {
	case SPEED_10:
		ctrl2 |= MDIO_PMA_CTRL2_10BT;
		break;
	case SPEED_100:
		ctrl1 |= MDIO_PMA_CTRL1_SPEED100;
		ctrl2 |= MDIO_PMA_CTRL2_100BTX;
		break;
	case SPEED_1000:
		ctrl1 |= MDIO_PMA_CTRL1_SPEED1000;
		/* Assume 1000base-T */
		ctrl2 |= MDIO_PMA_CTRL2_1000BT;
		break;
	case SPEED_2500:
		ctrl1 |= MDIO_CTRL1_SPEED2_5G;
		/* Assume 2.5Gbase-T */
		ctrl2 |= MDIO_PMA_CTRL2_2_5GBT;
		break;
	case SPEED_5000:
		ctrl1 |= MDIO_CTRL1_SPEED5G;
		/* Assume 5Gbase-T */
		ctrl2 |= MDIO_PMA_CTRL2_5GBT;
		break;
	case SPEED_10000:
		ctrl1 |= MDIO_CTRL1_SPEED10G;
		/* Assume 10Gbase-T */
		ctrl2 |= MDIO_PMA_CTRL2_10GBT;
		break;
	default:
		return -EINVAL;
	}
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1, ctrl1);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL2, ctrl2);
	return aeon_c45_an_disable_aneg(phydev);
}

static int aeon_c45_an_config_aneg(struct phy_device *phydev)
{
	int changed = 0, ret;
	u32 adv;
	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);
	//changed = genphy_config_eee_advert(phydev);
	adv = linkmode_adv_to_mii_adv_t(phydev->advertising);
	ret = phy_modify_mmd(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE,
				  ADVERTISE_ALL | ADVERTISE_100BASE4 |
					  ADVERTISE_PAUSE_CAP |
					  ADVERTISE_PAUSE_ASYM,
				  adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;
	adv = linkmode_adv_to_mii_10gbt_adv_t(phydev->advertising);
	ret = phy_modify_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
				  MDIO_AN_10GBT_CTRL_ADV10G |
					  MDIO_AN_10GBT_CTRL_ADV5G |
					  MDIO_AN_10GBT_CTRL_ADV2_5G,
				  adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;
	return changed;
}

static int aeon_c45_restart_aneg(struct phy_device *phydev)
{
	return phy_set_bits_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1,
				     MDIO_AN_CTRL1_ENABLE |
					     MDIO_AN_CTRL1_RESTART);
}

static int aeon_c45_check_and_restart_aneg(struct phy_device *phydev, bool restart)
{
	int ret = 0;
	if (!restart) {
		/* Configure and restart aneg if it wasn't set before */
		ret = aeon_cl45_read(phydev, MDIO_MMD_AN, MDIO_CTRL1);
		if (ret < 0)
			return ret;
		if (!(ret & MDIO_AN_CTRL1_ENABLE))
			restart = true;
	}
	if (restart)
		ret = aeon_c45_restart_aneg(phydev);
	return ret;
}

static int as21xxx_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	int ret;
	if (phydev->autoneg == AUTONEG_DISABLE)
		return aeon_c45_pma_setup_forced(phydev);
	ret = aeon_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;
	return aeon_c45_check_and_restart_aneg(phydev, changed);
}

static int as21xxx_config_led(struct phy_device *phydev)
{
	int ret;
	/* LED0 */
	ret = as21xxx_led_hw_control_set(phydev, 0,
					 BIT(TRIGGER_NETDEV_LINK));
	if (ret < 0)
		return ret;
	/* LED1 */
	return as21xxx_led_hw_control_set(phydev, 1,
					  BIT(TRIGGER_NETDEV_LINK_10) |
					  BIT(TRIGGER_NETDEV_LINK_100) |
					  BIT(TRIGGER_NETDEV_LINK_1000) |
					  BIT(TRIGGER_NETDEV_LINK_2500) |
					  BIT(TRIGGER_NETDEV_LINK_5000) |
					  BIT(TRIGGER_NETDEV_LINK_10000) |
					  BIT(TRIGGER_NETDEV_TX) |
					  BIT(TRIGGER_NETDEV_RX));
}

static int as21xxx_match_phy_device(struct phy_device *phydev,
				    const struct phy_driver *phydrv)
{
	/* AEONSEMI get pid. */
	phydev->phy_id = aeon_read_pid(phydev);
	if (phydev->phy_id != PHY_ID_AS21XXX)
		return 0;
	aeon_mdio_write(phydev, 0x1E, 0x142, 0x48);
	return 1;
}

static void as21xxx_remove(struct phy_device *phydev)
{
	//as21xxx_debugfs_remove(phydev);
}

static int aeon_wait_reset_complete(struct phy_device *phydev)
{
	int val;
	return read_poll_timeout(aeon_ipc_get_fw_version, val,
				 val == 0, 10000, 2000000, false, phydev);
}

static int as21xxx_config_init(struct phy_device *phydev)
{
	int ret = aeon_wait_reset_complete(phydev);
	if (ret) {
		aeon_mdio_write(phydev, MDIO_MMD_VEND1, 0x142, 0x48);
		ret = aeon_firmware_load(phydev);
		if (ret)
			return ret;
		ret = aeon_wait_reset_complete(phydev);
		if (!ret) {
			/* Enable PTP clk if not already enabled */
			ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK,
					       VEND1_PTP_CLK_EN);
			if (ret)
				return ret;
		} else {
			return -ENODEV;
		}
	}
	as21xxx_config_led(phydev);
	//if (phydev->interface == PHY_INTERFACE_MODE_USXGMII)
	//	ret = aeon_dpc_ra_enable(phydev);
	return ret;
}

static struct phy_driver as21xxx_drivers[] = {
	{
		/* PHY expose in C45 as 0x7500 0x9410
		 * before firmware is loaded.
		 * This driver entry must be attempted first to load
		 * the firmware and thus update the ID registers.
		 */
		PHY_ID_MATCH_EXACT(PHY_ID_AS21XXX),
		.name		= "Aeonsemi AS21xxx",
		.match_phy_device = as21xxx_match_phy_device,
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011JB1),
		.name		= "Aeonsemi AS21011JB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011PB1),
		.name		= "Aeonsemi AS21011PB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010PB1),
		.name		= "Aeonsemi AS21010PB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010JB1),
		.name		= "Aeonsemi AS21010JB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210PB1),
		.name		= "Aeonsemi AS21210PB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510JB1),
		.name		= "Aeonsemi AS21510JB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510PB1),
		.name		= "Aeonsemi AS21510PB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511JB1),
		.name		= "Aeonsemi AS21511JB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210JB1),
		.name		= "Aeonsemi AS21210JB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511PB1),
		.name		= "Aeonsemi AS21511PB1",
		.probe		= as21xxx_probe,
		.remove		= as21xxx_remove,
		.match_phy_device = as21xxx_match_phy_device,
		.config_aneg = as21xxx_config_aneg,
		.get_features	= as21xxx_get_features,
		.read_status	= as21xxx_read_status,
		.read_status	= as21xxx_read_status,
		.config_init    = as21xxx_config_init,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
};
module_phy_driver(as21xxx_drivers);

static struct mdio_device_id __maybe_unused as21xxx_tbl[] = {
	{ PHY_ID_MATCH_VENDOR(PHY_VENDOR_AEONSEMI) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, as21xxx_tbl);

MODULE_DESCRIPTION("Aeonsemi AS21xxx PHY driver");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");
