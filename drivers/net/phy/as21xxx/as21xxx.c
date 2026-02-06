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
#define AN_STATES1_ADDR	0x8005
#define AN_STATES1_ARB_MASK	0xF000
#define AN_STATES1_ARB_OFST	12
#define LINK_GOOD 9

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

/* IPC_CFG_PARAM_DIRECT_CU_AN sub command */
#define IPC_CMD_CFG_CU_AN_RESTART	0xa
#define IPC_CMD_CFG_CU_AN_TOP_SPD	0xc

/* Sub command of CMD_TEMP_MON */
#define IPC_CMD_TEMP_MON_GET		0x4
#define AS21XXX_MDIO_AN_C22		0xffe0
#define AEON_MEM_DEFAULT_ADDR (0x300100 >> 1)
#define MEM_WORD_SIZE 4
#define MAX_WDATA_SIZE 16
#define PHY_MAX_ADDR 32
#define MDI_CFG_SPD_T10 0x2
#define MDI_CFG_SPD_T100 0x4
#define MDI_CFG_SPD_T1G 0x8
#define MDI_CFG_SPD_T2P5G 0x10
#define MDI_CFG_SPD_T5G 0x20
#define MDI_CFG_SPD_T10G 0x40

static int param1 = 1;
module_param(param1, int, 0444);
MODULE_PARM_DESC(param1, "First parameter");

#define LED_NUM 5
#define LED_PARAM 7

#define LED_BLINK_RATE_15_625Hz 0x1
#define LED_BLINK_RATE_7_8125Hz 0x2
#define LED_BLINK_RATE_3_9063Hz 0x3
#define LED_BLINK_RATE_1_9531Hz 0x4
#define LED_BLINK_RATE_0_97656Hz 0x5
#define LED_BLINK_RATE_0_48828Hz 0x6

enum as21xxx_driver_event {
	LED_MODE_OFF = 0x0,
	LED_ON_NG_BLINK_ACT,
	LED_ON_FE_GE_BLINK_ACT,
	LED_LINK_EST,
	LED_TX_RX_ACT,
	LED_LINK_EST_BLINK_ACT,
	LED_ON_NG_BLINK_FE_GE,
	LED_ON_FE_GE,
	LED_ON_NG,
	LED_ON_FD,
	LED_ON_COLL,
	LED_TX_ACT,
	LED_RX_ACT,
	LED_ON_2P5G,
	LED_ON_1000BT,
	LED_ON_5G,
	LED_LINK_EST_BLINK_RX,
	LED_ON_100TX,
	LED_ON_10BT,
	LED_ON_10G,
	LED_ON_FD_BLINK_COLL,
	LED_MODE_ON,
};

#define LED_POLARITY_OFF 0x0

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


struct as21xxx_led_map_info {
	u16 mode;
	u16 map;
};

static struct as21xxx_led_map_info as21xxx_led_map[] = {
	{
		.mode = LED_ON_10BT,
		.map = VEND1_LED_REG_A_EVENT_ON_10
	},
	{
		.mode = LED_ON_100TX,
		.map = VEND1_LED_REG_A_EVENT_ON_100
	},
	{
		.mode = LED_ON_1000BT,
		.map = VEND1_LED_REG_A_EVENT_ON_1000
	},
	{
		.mode = LED_ON_2P5G,
		.map = VEND1_LED_REG_A_EVENT_ON_2500
	},
	{
		.mode = LED_ON_5G,
		.map = VEND1_LED_REG_A_EVENT_ON_5000
	},
	{
		.mode = LED_ON_10G,
		.map = VEND1_LED_REG_A_EVENT_ON_10000
	},
	{
		.mode = LED_ON_FE_GE,
		.map = VEND1_LED_REG_A_EVENT_ON_FE_GE
	},
	{
		.mode = LED_ON_NG,
		.map = VEND1_LED_REG_A_EVENT_ON_NG
	},
	{
		.mode = LED_ON_FD,
		.map = VEND1_LED_REG_A_EVENT_ON_FULL_DUPLEX
	},
	{
		.mode = LED_ON_COLL,
		.map = VEND1_LED_REG_A_EVENT_ON_COLLISION
	},
	{
		.mode = LED_TX_ACT,
		.map = VEND1_LED_REG_A_EVENT_BLINK_TX
	},
	{
		.mode = LED_RX_ACT,
		.map = VEND1_LED_REG_A_EVENT_BLINK_RX
	},
	{
		.mode = LED_TX_RX_ACT,
		.map = VEND1_LED_REG_A_EVENT_BLINK_ACT
	},
	{
		.mode = LED_LINK_EST,
		.map = VEND1_LED_REG_A_EVENT_ON_LINK
	},
	{
		.mode = LED_LINK_EST_BLINK_ACT,
		.map = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_ACT
	},
	{
		.mode = LED_LINK_EST_BLINK_RX,
		.map = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_RX
	},
	{
		.mode = LED_ON_FE_GE_BLINK_ACT,
		.map = VEND1_LED_REG_A_EVENT_ON_FE_GE_BLINK_ACT
	},
	{
		.mode = LED_ON_NG_BLINK_ACT,
		.map = VEND1_LED_REG_A_EVENT_ON_NG_BLINK_ACT
	},
	{
		.mode = LED_ON_NG_BLINK_FE_GE,
		.map = VEND1_LED_REG_A_EVENT_ON_NG_BLINK_FE_GE
	},
	{
		.mode = LED_ON_FD_BLINK_COLL,
		.map = VEND1_LED_REG_A_EVENT_ON_FD_BLINK_COLLISION
	},
	{
		.mode = LED_MODE_ON,
		.map = VEND1_LED_REG_A_EVENT_ON
	},
	{
		.mode = LED_MODE_OFF,
		.map = VEND1_LED_REG_A_EVENT_OFF
	},
	{
		.mode = LED_ON_NG_BLINK_ACT,
		.map = VEND1_LED_REG_A_EVENT_BLINK_RX
	},
};

u16 custome_cfg[LED_PARAM] = {LED_ON_NG_BLINK_ACT, LED_ON_FE_GE_BLINK_ACT, LED_LINK_EST,
	LED_TX_RX_ACT, LED_LINK_EST_BLINK_ACT, LED_POLARITY_OFF, LED_BLINK_RATE_3_9063Hz};
EXPORT_SYMBOL(custome_cfg);

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
#define PHY_ID_AS21XXX			0x75009410
#define PHY_ID_AS22XXX			0x750094a0
#define AEON_MAX_LEDS			5
#define AEON_IPC_DELAY			10000
#define AEON_IPC_TIMEOUT		(AEON_IPC_DELAY * 100)
#define AEON_IPC_DATA_NUM_REGISTERS	8
#define AEON_IPC_DATA_MAX		(AEON_IPC_DATA_NUM_REGISTERS * sizeof(u16))
#define AEON_BOOT_ADDR			0x1000
#define AEON_CPU_BOOT_ADDR		0x2000
#define AEON_CPU_CTRL_FW_LOAD		(BIT(4) | BIT(2) | BIT(1) | BIT(0))
#define AEON_CPU_CTRL_FW_START		BIT(0)

enum as21xxx_led_num {
	AEON_LED0 = 0,
	AEON_LED1,
	AEON_LED2,
	AEON_LED3,
	AEON_LED4,
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

int aeon_cl45_read(struct phy_device *phydev, int dev_addr,
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
EXPORT_SYMBOL(aeon_cl45_read);

void aeon_cl45_write(struct phy_device *phydev, int dev_addr,
		     unsigned int phy_reg, unsigned short phy_data)
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
EXPORT_SYMBOL(aeon_cl45_write);

static int aeon_mdio_read(struct phy_device *phydev, int dev_addr,
			  unsigned short phy_reg)
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

	ret = __mdiobus_c45_read(bus, phy_addr, dev_addr, phy_reg);
	__mdiobus_c45_write(bus, 30, 0x1, 0x1, 0x1);

	return ret;
}

static int aeon_mdio_write(struct phy_device *phydev, int dev_addr,
			   unsigned short phy_reg, unsigned short val)
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

	ret = __mdiobus_c45_write(bus, phy_addr, dev_addr, phy_reg, val);
	__mdiobus_c45_write(bus, 30, 0x1, 0x1, 0x1);

	return ret;
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
	u16 val;

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

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1,
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
	if (!wdata)
		return -ENOMEM;

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

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL); //GLB_REG_CPU_CTRL
	val |= 0x12;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL, val);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_FW_START_ADDR,
			(u16)(AEON_MEM_DEFAULT_ADDR & 0xFFFF));
	phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD,
			0x3ffc, 0xc000);
	aeon_cl45_write_burst(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_LOAD,
			(unsigned char *)wdata, wdata_count*2);
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD); //GLB_REG_MDIO_INDIRECT_ADDRCMD
	val &= 0x3FFF;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD, val);
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL); //GLB_REG_CPU_CTRL
	val &= 0xFFED;
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL, val);

cleanup:
	kfree(wdata);
	return 0;
}

static void aeon_set_fast_mdc_timing(struct phy_device *phydev)
{
	if (param1) {
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x53, 0xFFFF);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x54, 0xFFFF);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x55, 0xFFFF);
	} else {
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x53, 0);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x54, 0);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x55, 0);
	}
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
	unsigned int val;

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
	ret = read_poll_timeout(phy_read_mmd, val,
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
		return -EINVAL;

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
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i));
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
	if (ret != -EINVAL)
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
	u16 ret_data[AEON_IPC_DATA_NUM_REGISTERS], data[1];
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

	/* Make sure FW version is NULL terminated */
	ret_data[DIV_ROUND_UP(ret, sizeof(u16))] = '\0';
	phydev_info(phydev, "Firmware Version: %s\n", (char *)ret_data);

	return 0;
}

static int aeon_dpc_ra_enable(struct phy_device *phydev)
{
	u16 data[2];
	u16 ret_sts;

	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_DPC_RA;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), &ret_sts);
}

static int aeon_set_eth_speed(struct phy_device *phydev, unsigned short speed)
{
	u16 data[8];
	u16 ret_sts;

	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_CU_AN;
	data[2] = IPC_CMD_CFG_CU_AN_TOP_SPD;
	data[3] = speed;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), &ret_sts);
}

static int aeon_restart_an(struct phy_device *phydev)
{
	u16 data[8];
	u16 ret_sts;

	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_CU_AN;
	data[2] = IPC_CMD_CFG_CU_AN_RESTART;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), &ret_sts);
}

static int aeon_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
				   u16 mask, u16 set)
{
	int new, ret;

	ret = phy_read_mmd(phydev, devad, regnum);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	if (set & MDIO_AN_10GBT_CTRL_ADV10G) {
		ret = aeon_ipc_sync_parity(phydev, phydev->priv);
		if (ret)
			return ret;
		ret = aeon_set_eth_speed(phydev, MDI_CFG_SPD_T10G);
		if (ret)
			return ret;
	} else if (set & ADVERTISE_1000FULL) {
		ret = aeon_ipc_sync_parity(phydev, phydev->priv);
		if (ret)
			return ret;
		ret = aeon_set_eth_speed(phydev, MDI_CFG_SPD_T1G);
		if (ret)
			return ret;
	} else if (set & ADVERTISE_100FULL) {
		ret = aeon_ipc_sync_parity(phydev, phydev->priv);
		if (ret)
			return ret;
		ret = aeon_set_eth_speed(phydev, MDI_CFG_SPD_T100);
		if (ret)
			return ret;
	}

	return 1;
}

static int aeon_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_pma_read_abilities(phydev);
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
	/* AS21xxx does not support C22 registers */
	phydev->c45_ids.devices_in_package &= ~BIT(0);

	return 0;
}

static int aeon_gen1_probe(struct phy_device *phydev)
{
	struct as21xxx_priv *priv;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev,
			    sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;
	aeon_set_fast_mdc_timing(phydev);
	aeon_set_default_value(phydev);
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

	ret = as21xxx_debugfs_init(phydev);
	if (ret)
		return ret;

	/* Enable PTP clk if not already Enabled */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK,
			       VEND1_PTP_CLK_EN);
	if (ret)
		return ret;

	return 0;
}

static int aeon_gen2_probe(struct phy_device *phydev)
{
	struct as21xxx_priv *priv;
	int ret = 0;

	priv = devm_kzalloc(&phydev->mdio.dev,
				sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;

	ret = aeon_firmware_load(phydev);
	if (ret) {
		phydev_err(phydev, "AS22XX load firmware fail.\n");
		return ret;
	}

	mutex_init(&priv->ipc_lock);
	ret = aeon_ipc_sync_parity(phydev, priv);
	if (ret)
		return ret;

	ret = aeon_ipc_get_fw_version(phydev);
	if (ret)
		return ret;

	return 0;
}

static int aeon_update_link(struct phy_device *phydev)
{
	int status = 0, bmcr;
	bool link_up;

	bmcr = phy_read_mmd(phydev, MDIO_MMD_AN, AS21XXX_MDIO_AN_C22 + MII_BMCR);
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
		status = phy_read_mmd(phydev, MDIO_MMD_AN, AN_STATES1_ADDR);
		if (status < 0)
			return status;
		else if (status & AN_STATES1_ARB_MASK)
			goto done;
	}

	/* Read link and autonegotiation status */
	status = phy_read_mmd(phydev, MDIO_MMD_AN, AN_STATES1_ADDR);
	if (status < 0)
		return status;
done:
	link_up = ((status & AN_STATES1_ARB_MASK) >> AN_STATES1_ARB_OFST) == LINK_GOOD;
	phydev->link = link_up;
	phydev->autoneg_complete = link_up;
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
			lpagb = phy_read_mmd(phydev, MDIO_MMD_AN,
					     AS21XXX_MDIO_AN_C22 + MII_STAT1000);
			if (lpagb < 0)
				return lpagb;
			if (lpagb & LPA_1000MSFAIL) {
				int adv = phy_read_mmd(phydev, MDIO_MMD_AN,
						       AS21XXX_MDIO_AN_C22 + MII_CTRL1000);
				if (adv < 0)
					return adv;
				if (adv & CTL1000_ENABLE_MASTER)
					phydev_err(phydev,
						"Master/Slave resolution failed, maybe conflicting manual settings?\n");
				else
					phydev_err(phydev,
						"Master/Slave resolution failed\n");
				return -ENOLINK;
			}
			mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising,
							lpagb);
		}

		lpa = phy_read_mmd(phydev, MDIO_MMD_AN, AS21XXX_MDIO_AN_C22 + MII_LPA);
		if (lpa < 0)
			return lpa;
		mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);
		/* Read the link partner's 10G advertisement */
		lpa = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
		if (lpa < 0)
			return lpa;
		mii_10gbt_stat_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);
	} else {
		linkmode_zero(phydev->lp_advertising);
	}
	return 0;
}

static void aeon_read_speed(struct phy_device *phydev)
{
	int bmcr, speed;

	bmcr = phy_read_mmd(phydev, MDIO_MMD_AN, AS21XXX_MDIO_AN_C22 + MII_BMCR);
	if (bmcr < 0)
		return;

	speed = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_SPEED_STATUS);
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

static int aeon_read_status(struct phy_device *phydev)
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

static int aeon_ipc_set_led_cfg(unsigned short led0, unsigned short led1,
				unsigned short led2, unsigned short led3,
				unsigned short led4, unsigned short polarity,
				unsigned short blink, struct phy_device *phydev)
{
	u16 ret_size;
	u16 cfg[7] = {
		led0, led1, led2, led3, led4, polarity, blink
	};

	ret_size = aeon_ipc_sync_parity(phydev, phydev->priv);
	if (ret_size)
		return ret_size;

	aeon_ipc_send_msg(phydev, IPC_CMD_SET_LED, cfg, sizeof(cfg), &ret_size);

	return 1;
}


static int aeon_led_brightness_set(struct phy_device *phydev,
				   u8 index, enum led_brightness value)
{
	u16 val = LED_MODE_OFF;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;
	if (value)
		val = LED_MODE_ON;

	if (index == AEON_LED0)
		custome_cfg[0] = val;
	else if (index == AEON_LED1)
		custome_cfg[1] = val;
	else if (index == AEON_LED2)
		custome_cfg[2] = val;
	else if (index == AEON_LED3)
		custome_cfg[3] = val;
	else if (index == AEON_LED4)
		custome_cfg[4] = val;
	else
		phydev_dbg(phydev, "AEON support five leds, check index\r\n");

	aeon_ipc_set_led_cfg(custome_cfg[0], custome_cfg[1], custome_cfg[2], custome_cfg[3],
			     custome_cfg[4], custome_cfg[5], custome_cfg[6], phydev);

	return 1;
}

static int aeon_led_hw_is_supported(struct phy_device *phydev, u8 index,
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

static int aeon_led_hw_control_get(struct phy_device *phydev, u8 index,
				   unsigned long *rules)
{
	int i, val;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	if (index == AEON_LED0) {
		for (i = 0; i < ARRAY_SIZE(as21xxx_led_map); i++)
			if (custome_cfg[0] == as21xxx_led_map[i].mode)
				val = as21xxx_led_map[i].map;
	} else if (index == AEON_LED1) {
		for (i = 0; i < ARRAY_SIZE(as21xxx_led_map); i++)
			if (custome_cfg[1] == as21xxx_led_map[i].mode)
				val = as21xxx_led_map[i].map;
	} else if (index == AEON_LED2) {
		for (i = 0; i < ARRAY_SIZE(as21xxx_led_map); i++)
			if (custome_cfg[2] == as21xxx_led_map[i].mode)
				val = as21xxx_led_map[i].map;
	} else if (index == AEON_LED3) {
		for (i = 0; i < ARRAY_SIZE(as21xxx_led_map); i++)
			if (custome_cfg[3] == as21xxx_led_map[i].mode)
				val = as21xxx_led_map[i].map;
	} else if (index == AEON_LED4) {
		for (i = 0; i < ARRAY_SIZE(as21xxx_led_map); i++)
			if (custome_cfg[4] == as21xxx_led_map[i].mode)
				val = as21xxx_led_map[i].map;
	} else
		phydev_dbg(phydev, "AEON support five leds, check index\r\n");

	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (val == as21xxx_led_supported_pattern[i].val) {
			*rules = as21xxx_led_supported_pattern[i].pattern;
			return 0;
		}

	/* Should be impossible */
	return -EINVAL;
}

static int aeon_led_hw_control_set(struct phy_device *phydev, u8 index,
				   unsigned long rules)
{
	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	if (index == AEON_LED0)
		custome_cfg[0] = rules;
	else if (index == AEON_LED1)
		custome_cfg[1] = rules;
	else if (index == AEON_LED2)
		custome_cfg[2] = rules;
	else if (index == AEON_LED3)
		custome_cfg[3] = rules;
	else if (index == AEON_LED4)
		custome_cfg[4] = rules;
	else
		phydev_dbg(phydev, "AEON support five leds, check index\r\n");

	aeon_ipc_set_led_cfg(custome_cfg[0], custome_cfg[1], custome_cfg[2], custome_cfg[3],
			     custome_cfg[4], custome_cfg[5], custome_cfg[6], phydev);

	return 1;
}

static int aeon_led_polarity_set(struct phy_device *phydev, int index,
				 unsigned long modes)
{
	bool led_active_low = false;
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

	if (led_active_low)
		custome_cfg[5] |= (1 << index);

	aeon_ipc_set_led_cfg(custome_cfg[0], custome_cfg[1], custome_cfg[2], custome_cfg[3],
			     custome_cfg[4], custome_cfg[5], custome_cfg[6], phydev);

	return 0;
}

static int aeon_gen1_read_pid(struct phy_device *phydev)
{
	int pid1 = 0, pid2 = 0, pid = 0;

	pid1 = aeon_cl45_read(phydev, MDIO_MMD_PMAPMD, 2);
	if (pid1 < 0)
		return pid1;

	if (pid1 == 0x7500 && param1) {
		aeon_cl45_write(phydev, MDIO_MMD_VEND1, 0x53, 0xFFFF);
		aeon_cl45_write(phydev, MDIO_MMD_VEND1, 0x54, 0xFFFF);
		aeon_cl45_write(phydev, MDIO_MMD_VEND1, 0x55, 0xFFFF);
	}

	pid2 = aeon_cl45_read(phydev, MDIO_MMD_PMAPMD, 3);
	if (pid2 < 0)
		return pid2;

	phydev_dbg(phydev, "%s aeonsemi PHY = %x - %x\n", __func__, pid1, pid2);
	pid = ((pid1 & 0xffff) << 16) | (pid2 & 0xffff);

	return pid;
}

static int aeon_gen2_read_pid(struct phy_device *phydev)
{
	int pid1 = 0, pid2 = 0, pid = 0;

	pid1 = aeon_cl45_read(phydev, MDIO_MMD_PMAPMD, 2);
	if (pid1 < 0)
		return pid1;

	pid2 = aeon_cl45_read(phydev, MDIO_MMD_PMAPMD, 3);
	if (pid2 < 0)
		return pid2;

	phydev_dbg(phydev, "%s aeonsemi PHY = %x - %x\n", __func__, pid1, pid2);
	pid = ((pid1 & 0xffff) << 16) | (pid2 & 0xffff);

	return pid;
}

static int aeon_config_led(struct phy_device *phydev)
{
	int ret;
	/* LED0 */
	ret = aeon_led_hw_control_set(phydev, AEON_LED0, LED_LINK_EST);
	if (ret < 0)
		return ret;

	/* LED1 */
	return aeon_led_hw_control_set(phydev, AEON_LED1, LED_LINK_EST_BLINK_ACT);
}

static int aeon_gen1_match_phy_device(struct phy_device *phydev)
{
	u32 phy_id = aeon_gen1_read_pid(phydev);

	if (phy_id != PHY_ID_AS21XXX)
		return 0;

	phydev->phy_id = phy_id;
	aeon_cl45_write(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK, 0x48);

	return 1;
}

static int aeon_gen2_match_phy_device(struct phy_device *phydev)
{
	/* AEONSEMI get pid. */
	phydev->phy_id = aeon_gen2_read_pid(phydev);

	if (phydev->phy_id == PHY_ID_AS22XXX)
		return 1;

	return 0;
}

static void aeon_gen1_remove(struct phy_device *phydev)
{
	as21xxx_debugfs_remove(phydev);
}

static int aeon_wait_reset_complete(struct phy_device *phydev)
{
	int val;

	return read_poll_timeout(aeon_ipc_get_fw_version, val,
				 val == 0, 10000, 2000000, false, phydev);
}

static int aeon_gen1_config_init(struct phy_device *phydev)
{
	int ret = aeon_wait_reset_complete(phydev);

	if (ret) {
		aeon_cl45_write(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK, 0x48);
		ret = aeon_firmware_load(phydev);
		if (ret)
			return ret;

		ret = aeon_wait_reset_complete(phydev);
		if (!ret) {
			/* Enable PTP clk if not already Enabled */
			ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK,
					       VEND1_PTP_CLK_EN);
			if (ret)
				return ret;
		} else
			return -ENODEV;
	}

	aeon_config_led(phydev);
	if (phydev->interface == PHY_INTERFACE_MODE_USXGMII)
		ret = aeon_dpc_ra_enable(phydev);

	return ret;
}

static int aeon_gen2_config_init(struct phy_device *phydev)
{
	int ret = aeon_wait_reset_complete(phydev);

	if (ret) {
		ret = aeon_firmware_load(phydev);
		if (ret)
			return ret;

		ret = aeon_wait_reset_complete(phydev);
		if (ret)
			return -ENODEV;
	}

	aeon_config_led(phydev);
	if (phydev->interface == PHY_INTERFACE_MODE_USXGMII)
		ret = aeon_dpc_ra_enable(phydev);

	return ret;
}

static int aeon_gen1_c45_an_config_aneg(struct phy_device *phydev)
{
	int changed, ret;
	u32 adv;

	linkmode_and(phydev->advertising, phydev->advertising,
		     phydev->supported);
	// changed = genphy_c45_an_config_eee_aneg(phydev);

	adv = linkmode_adv_to_mii_adv_t(phydev->advertising);
	ret = aeon_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_ADVERTISE,
				      ADVERTISE_ALL | ADVERTISE_100BASE4 |
				      ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM,
				      adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;
	if (ret == 0) {
		if (adv & ADVERTISE_100FULL) {
			ret = aeon_ipc_sync_parity(phydev, phydev->priv);
			if (ret)
				return ret;
			ret = aeon_set_eth_speed(phydev, MDI_CFG_SPD_T100);
			if (ret)
				return ret;
		}
	}

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = aeon_modify_mmd_changed(phydev, MDIO_MMD_AN, AS21XXX_MDIO_AN_C22 + MII_CTRL1000,
				      ADVERTISE_1000FULL | ADVERTISE_1000HALF,
				      adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;
	if (ret == 0) {
		if (adv & ADVERTISE_1000FULL) {
			ret = aeon_ipc_sync_parity(phydev, phydev->priv);
			if (ret)
				return ret;
			ret = aeon_set_eth_speed(phydev, MDI_CFG_SPD_T1G);
			if (ret)
				return ret;
		}
	}

	adv = linkmode_adv_to_mii_10gbt_adv_t(phydev->advertising);
	ret = aeon_modify_mmd_changed(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_CTRL,
				      MDIO_AN_10GBT_CTRL_ADV10G |
				      MDIO_AN_10GBT_CTRL_ADV5G |
				      MDIO_AN_10GBT_CTRL_ADV2_5G, adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = 1;
	if (ret == 0) {
		if (adv & MDIO_AN_10GBT_CTRL_ADV10G) {
			ret = aeon_ipc_sync_parity(phydev, phydev->priv);
			if (ret)
				return ret;
			ret = aeon_set_eth_speed(phydev, MDI_CFG_SPD_T10G);
			if (ret)
				return ret;
		}
	}

	return changed;
}

static int aeon_gen1_c45_restart_aneg(struct phy_device *phydev)
{
	int ret = 0;

	ret = aeon_ipc_sync_parity(phydev, phydev->priv);
	if (ret)
		return ret;

	ret = aeon_restart_an(phydev);
	if (ret)
		return ret;

	return 1;
}

static int aeon_gen1_c45_check_and_restart_aneg(struct phy_device *phydev, bool restart)
{
	int ret = 0;

	if (!restart) {
		/* Configure and restart aneg if it wasn't set before */
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
		if (ret < 0)
			return ret;
		if (!(ret & MDIO_AN_CTRL1_ENABLE))
			restart = true;
	}

	if (restart)
		ret = aeon_gen1_c45_restart_aneg(phydev);

	return ret;
}

static int aeon_gen1_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	int ret;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_pma_setup_forced(phydev);
	ret = aeon_gen1_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return aeon_gen1_c45_check_and_restart_aneg(phydev, changed);
}

static struct phy_driver aeon_drivers[] = {
	{
		/* PHY expose in C45 as 0x7500 0x9410
		 * before firmware is loaded.
		 * This driver entry must be attempted first to load
		 * the firmware and thus update the ID registers.
		 */
		PHY_ID_MATCH_EXACT(PHY_ID_AS21XXX),
		.name		= "Aeonsemi AS21xxx",
		.match_phy_device = aeon_gen1_match_phy_device,
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.config_init	= aeon_gen1_config_init,
		.config_aneg	= aeon_gen1_config_aneg,
		.read_mmd	= aeon_mdio_read,
		.write_mmd	= aeon_mdio_write,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011JB1),
		.name		= "Aeonsemi AS21011JB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.read_status	= aeon_read_status,
		.get_features	= aeon_get_features,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011PB1),
		.name		= "Aeonsemi AS21011PB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.read_status	= aeon_read_status,
		.get_features	= aeon_get_features,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010PB1),
		.name		= "Aeonsemi AS21010PB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.match_phy_device = aeon_gen1_match_phy_device,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010JB1),
		.name		= "Aeonsemi AS21010JB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210PB1),
		.name		= "Aeonsemi AS21210PB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510JB1),
		.name		= "Aeonsemi AS21510JB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510PB1),
		.name		= "Aeonsemi AS21510PB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511JB1),
		.name		= "Aeonsemi AS21511JB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210JB1),
		.name		= "Aeonsemi AS21210JB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511PB1),
		.name		= "Aeonsemi AS21511PB1",
		.probe		= aeon_gen1_probe,
		.remove		= aeon_gen1_remove,
		.match_phy_device = aeon_gen1_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS22XXX),
		.name		= "Aeonsemi AS22XXX",
		.probe		= aeon_gen2_probe,
		.match_phy_device = aeon_gen2_match_phy_device,
		.get_features	= aeon_get_features,
		.read_status	= aeon_read_status,
		.config_init	= aeon_gen2_config_init,
		.led_brightness_set = aeon_led_brightness_set,
		.led_hw_is_supported = aeon_led_hw_is_supported,
		.led_hw_control_set = aeon_led_hw_control_set,
		.led_hw_control_get = aeon_led_hw_control_get,
		.led_polarity_set = aeon_led_polarity_set,
	},
};
module_phy_driver(aeon_drivers);

static struct mdio_device_id __maybe_unused aeon_tbl[] = {
	{ PHY_ID_MATCH_VENDOR(PHY_VENDOR_AEONSEMI) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, aeon_tbl);

MODULE_DESCRIPTION("Aeonsemi AS21xxx PHY driver");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");
