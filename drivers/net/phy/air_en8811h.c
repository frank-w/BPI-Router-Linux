// SPDX-License-Identifier: GPL-2.0+

/* FILE NAME:  air_en8811h.c
 * PURPOSE:
 *      EN8811H phy driver for Linux
 * NOTES:
 *
 */

/* INCLUDE FILE DECLARATIONS
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/firmware.h>
#include <linux/crc32.h>

#include "air_en8811h.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
#define phydev_mdio_bus(_dev) (_dev->bus)
#define phydev_addr(_dev) (_dev->addr)
#define phydev_dev(_dev) (&_dev->dev)
#else
#define phydev_mdio_bus(_dev) (_dev->mdio.bus)
#define phydev_addr(_dev) (_dev->mdio.addr)
#define phydev_dev(_dev) (&_dev->mdio.dev)
#endif

MODULE_DESCRIPTION("Airoha EN8811H PHY drivers");
MODULE_AUTHOR("Airoha");
MODULE_LICENSE("GPL");

/*
GPIO5  <-> BASE_T_LED0,
GPIO4  <-> BASE_T_LED1,
GPIO3  <-> BASE_T_LED2,
*/
/* User-defined.B */
#define AIR_LED_SUPPORT
#ifdef AIR_LED_SUPPORT
static const AIR_BASE_T_LED_CFG_T led_cfg[3] =
{
    /*
     *    LED Enable,            GPIO,            LED Polarity,            LED ON,               LED Blink
     */
         {LED_ENABLE,      AIR_LED0_GPIO5,       AIR_ACTIVE_HIGH,     BASE_T_LED0_ON_CFG,    BASE_T_LED0_BLK_CFG}, /* BASE-T LED0 */
         {LED_ENABLE,      AIR_LED1_GPIO4,       AIR_ACTIVE_HIGH,     BASE_T_LED1_ON_CFG,    BASE_T_LED1_BLK_CFG}, /* BASE-T LED1 */
         {LED_ENABLE,      AIR_LED2_GPIO3,       AIR_ACTIVE_HIGH,     BASE_T_LED2_ON_CFG,    BASE_T_LED2_BLK_CFG}, /* BASE-T LED2 */
};
static const u16 led_dur = UNIT_LED_BLINK_DURATION << AIR_LED_BLK_DUR_64M;
#endif
/* User-defined.E */

/************************************************************************
*                  F U N C T I O N S
************************************************************************/
#if 0
/* Airoha MII read function */
static int air_mii_cl22_read(struct mii_bus *ebus, unsigned int phy_addr,unsigned int phy_register)
{
    int read_data;
    read_data = mdiobus_read(ebus, phy_addr, phy_register);
    return read_data;
}
#endif
/* Airoha MII write function */
static int air_mii_cl22_write(struct mii_bus *ebus, unsigned int phy_addr, unsigned int phy_register,unsigned int write_data)
{
    int ret = 0;
    ret = mdiobus_write(ebus, phy_addr, phy_register, write_data);
    return ret;
}

static int air_mii_cl45_read(struct phy_device *phydev, int devad, u16 reg)
{
    int ret = 0;
    int data;
    struct device *dev = phydev_dev(phydev);
    ret = phy_write(phydev, MII_MMD_ACC_CTL_REG, devad);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return INVALID_DATA;
    }
    ret = phy_write(phydev, MII_MMD_ADDR_DATA_REG, reg);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return INVALID_DATA;
    }
    ret = phy_write(phydev, MII_MMD_ACC_CTL_REG, MMD_OP_MODE_DATA | devad);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return INVALID_DATA;
    }
    data = phy_read(phydev, MII_MMD_ADDR_DATA_REG);
    return data;
}

static int air_mii_cl45_write(struct phy_device *phydev, int devad, u16 reg, u16 write_data)
{
    int ret = 0;
    struct device *dev = phydev_dev(phydev);
    ret = phy_write(phydev, MII_MMD_ACC_CTL_REG, devad);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, MII_MMD_ADDR_DATA_REG, reg);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, MII_MMD_ACC_CTL_REG, MMD_OP_MODE_DATA | devad);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, MII_MMD_ADDR_DATA_REG, write_data);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    return 0;
}
/* Use default PBUS_PHY_ID */
/* EN8811H PBUS write function */
static int air_pbus_reg_write(struct phy_device *phydev, unsigned long pbus_address, unsigned long pbus_data)
{
    struct mii_bus *mbus = phydev_mdio_bus(phydev);
    int addr = phydev_addr(phydev);
    int ret = 0;
    ret = air_mii_cl22_write(mbus, (addr + 8), 0x1F, (unsigned int)(pbus_address >> 6));
    AIR_RTN_ERR(ret);
    ret = air_mii_cl22_write(mbus, (addr + 8), (unsigned int)((pbus_address >> 2) & 0xf), (unsigned int)(pbus_data & 0xFFFF));
    AIR_RTN_ERR(ret);
    ret = air_mii_cl22_write(mbus, (addr + 8), 0x10, (unsigned int)(pbus_data >> 16));
    AIR_RTN_ERR(ret);
    return 0;
}

/* EN8811H BUCK write function */
static int air_buckpbus_reg_write(struct phy_device *phydev, unsigned long pbus_address, unsigned int pbus_data)
{
    int ret = 0;
    struct device *dev = phydev_dev(phydev);
    ret = phy_write(phydev, 0x1F, (unsigned int)4);        /* page 4 */
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x10, (unsigned int)0);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x11, (unsigned int)((pbus_address >> 16) & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x12, (unsigned int)(pbus_address & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x13, (unsigned int)((pbus_data >> 16) & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x14, (unsigned int)(pbus_data & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x1F, 0);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    return 0;
}

/* EN8811H BUCK read function */
static unsigned int air_buckpbus_reg_read(struct phy_device *phydev, unsigned long pbus_address)
{
    unsigned int pbus_data = 0, pbus_data_low, pbus_data_high;
    int ret = 0;
    struct device *dev = phydev_dev(phydev);
    ret = phy_write(phydev, 0x1F, (unsigned int)4);        /* page 4 */
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return PBUS_INVALID_DATA;
    }
    ret = phy_write(phydev, 0x10, (unsigned int)0);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return PBUS_INVALID_DATA;
    }
    ret = phy_write(phydev, 0x15, (unsigned int)((pbus_address >> 16) & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return PBUS_INVALID_DATA;
    }
    ret = phy_write(phydev, 0x16, (unsigned int)(pbus_address & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return PBUS_INVALID_DATA;
    }

    pbus_data_high = phy_read(phydev, 0x17);
    pbus_data_low = phy_read(phydev, 0x18);
    pbus_data = (pbus_data_high << 16) + pbus_data_low;
    ret = phy_write(phydev, 0x1F, 0);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    return pbus_data;
}

static int MDIOWriteBuf(struct phy_device *phydev, unsigned long address, const struct firmware *fw)
{
    unsigned int write_data, offset ;
    int ret = 0;
    struct device *dev = phydev_dev(phydev);
    ret = phy_write(phydev, 0x1F, (unsigned int)4);            /* page 4 */
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x10, (unsigned int)0x8000);        /* address increment*/
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x11, (unsigned int)((address >> 16) & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    ret = phy_write(phydev, 0x12, (unsigned int)(address & 0xffff));
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }

    for (offset = 0; offset < fw->size; offset += 4)
    {
        write_data = (fw->data[offset + 3] << 8) | fw->data[offset + 2];
        ret = phy_write(phydev, 0x13, write_data);
        if (ret < 0) {
            dev_err(dev, "phy_write, ret: %d\n", ret);
            return ret;
        }
        write_data = (fw->data[offset + 1] << 8) | fw->data[offset];
        ret = phy_write(phydev, 0x14, write_data);
        if (ret < 0) {
            dev_err(dev, "phy_write, ret: %d\n", ret);
            return ret;
        }
    }
    ret = phy_write(phydev, 0x1F, (unsigned int)0);
    if (ret < 0) {
        dev_err(dev, "phy_write, ret: %d\n", ret);
        return ret;
    }
    return 0;
}

static int en8811h_load_firmware(struct phy_device *phydev)
{
    struct device *dev = phydev_dev(phydev);
    const struct firmware *fw;
    const char *firmware;
    int ret = 0;
    unsigned int crc32;
    u32 pbus_value = 0;

    ret = air_buckpbus_reg_write(phydev, 0x0f0018, 0x0);
    AIR_RTN_ERR(ret);
    pbus_value = air_buckpbus_reg_read(phydev, 0x800000);
    pbus_value |= BIT(11);
    ret = air_buckpbus_reg_write(phydev, 0x800000, pbus_value);
    AIR_RTN_ERR(ret);
    firmware = EN8811H_MD32_DM;
    ret = request_firmware_direct(&fw, firmware, dev);
    if (ret < 0) {
        dev_info(dev, "failed to load firmware %s, ret: %d\n", firmware, ret);
        return ret;
    }
    crc32 = ~crc32(~0, fw->data, fw->size);
    dev_info(dev, "%s: crc32=0x%x\n", firmware, crc32);
    /* Download DM */
    ret = MDIOWriteBuf(phydev, 0x00000000, fw);
    if (ret < 0) {
        dev_info(dev, "MDIOWriteBuf 0x00000000 fail, ret: %d\n", ret);
        return ret;
    }
    release_firmware(fw);

    firmware = EN8811H_MD32_DSP;
    ret = request_firmware_direct(&fw, firmware, dev);
    if (ret < 0) {
        dev_info(dev, "failed to load firmware %s, ret: %d\n", firmware, ret);
        return ret;
    }
    crc32 = ~crc32(~0, fw->data, fw->size);
    dev_info(dev, "%s: crc32=0x%x\n", firmware, crc32);
    /* Download PM */
    ret = MDIOWriteBuf(phydev, 0x00100000, fw);
    if (ret < 0) {
            dev_info(dev, "MDIOWriteBuf 0x00100000 fail , ret: %d\n", ret);
            return ret;
    }
    release_firmware(fw);

    pbus_value = air_buckpbus_reg_read(phydev, 0x800000);
    pbus_value &= ~BIT(11);
    ret = air_buckpbus_reg_write(phydev, 0x800000, pbus_value);
    AIR_RTN_ERR(ret);
    ret = air_buckpbus_reg_write(phydev, 0x0f0018, 0x01);
    AIR_RTN_ERR(ret);
    return 0;
}

#ifdef  AIR_LED_SUPPORT
static int airoha_led_set_usr_def(struct phy_device *phydev, u8 entity, int polar,
                                   u16 on_evt, u16 blk_evt)
{
    int ret = 0;
    if (AIR_ACTIVE_HIGH == polar) {
        on_evt |= LED_ON_POL;
    } else {
        on_evt &= ~LED_ON_POL ;
    }
    ret = air_mii_cl45_write(phydev, 0x1f, LED_ON_CTRL(entity), on_evt | LED_ON_EN);
    AIR_RTN_ERR(ret);
    ret = air_mii_cl45_write(phydev, 0x1f, LED_BLK_CTRL(entity), blk_evt);
    AIR_RTN_ERR(ret);
    return 0;
}

static int airoha_led_set_mode(struct phy_device *phydev, u8 mode)
{
    u16 cl45_data;
    int err = 0;
    struct device *dev = phydev_dev(phydev);
    cl45_data = air_mii_cl45_read(phydev, 0x1f, LED_BCR);
    switch (mode) {
    case AIR_LED_MODE_DISABLE:
        cl45_data &= ~LED_BCR_EXT_CTRL;
        cl45_data &= ~LED_BCR_MODE_MASK;
        cl45_data |= LED_BCR_MODE_DISABLE;
        break;
    case AIR_LED_MODE_USER_DEFINE:
        cl45_data |= LED_BCR_EXT_CTRL;
        cl45_data |= LED_BCR_CLK_EN;
        break;
    default:
        dev_err(dev, "LED mode%d is not supported!\n", mode);
        return -EINVAL;
    }
    err = air_mii_cl45_write(phydev, 0x1f, LED_BCR, cl45_data);
    AIR_RTN_ERR(err);
    return 0;
}

static int airoha_led_set_state(struct phy_device *phydev, u8 entity, u8 state)
{
    u16 cl45_data = 0;
    int err;

    cl45_data = air_mii_cl45_read(phydev, 0x1f, LED_ON_CTRL(entity));
    if (LED_ENABLE == state) {
        cl45_data |= LED_ON_EN;
    } else {
        cl45_data &= ~LED_ON_EN;
    }

    err = air_mii_cl45_write(phydev, 0x1f, LED_ON_CTRL(entity), cl45_data);
    AIR_RTN_ERR(err);
    return 0;
}

static int en8811h_led_init(struct phy_device *phydev)
{

    unsigned long led_gpio = 0, reg_value = 0;
    u16 cl45_data = led_dur;
    int ret = 0, led_id;
    struct device *dev = phydev_dev(phydev);
    ret = air_mii_cl45_write(phydev, 0x1f, LED_BLK_DUR, cl45_data);
    AIR_RTN_ERR(ret);
    cl45_data >>= 1;
    ret = air_mii_cl45_write(phydev, 0x1f, LED_ON_DUR, cl45_data);
    AIR_RTN_ERR(ret);
    ret = airoha_led_set_mode(phydev, AIR_LED_MODE_USER_DEFINE);
    if (ret != 0) {
        dev_err(dev, "LED fail to set mode, ret %d !\n", ret);
        return ret;
    }
    for(led_id = 0; led_id < EN8811H_LED_COUNT; led_id++)
    {
        /* LED0 <-> GPIO5, LED1 <-> GPIO4, LED0 <-> GPIO3 */
        if (led_cfg[led_id].gpio != (led_id + (AIR_LED0_GPIO5 - (2 * led_id))))
        {
            dev_err(dev, "LED%d uses incorrect GPIO%d !\n", led_id, led_cfg[led_id].gpio);
            return -EINVAL;
        }
        ret = airoha_led_set_state(phydev, led_id, led_cfg[led_id].en);
        if (ret != 0)
        {
            dev_err(dev, "LED fail to set state, ret %d !\n", ret);
            return ret;
        }
        if (LED_ENABLE == led_cfg[led_id].en)
        {
            led_gpio |= BIT(led_cfg[led_id].gpio);
            ret = airoha_led_set_usr_def(phydev, led_id, led_cfg[led_id].pol, led_cfg[led_id].on_cfg, led_cfg[led_id].blk_cfg);
            if (ret != 0)
            {
                dev_err(dev, "LED fail to set default, ret %d !\n", ret);
                return ret;
            }
        }
    }
    reg_value = air_buckpbus_reg_read(phydev, 0xcf8b8) | led_gpio;
    ret = air_buckpbus_reg_write(phydev, 0xcf8b8, reg_value);
    AIR_RTN_ERR(ret);

    dev_info(dev, "LED initialize OK !\n");
    return 0;
}
#endif /* AIR_LED_SUPPORT */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 5, 0))
static int en8811h_get_features(struct phy_device *phydev)
{
    int ret;
    struct device *dev = phydev_dev(phydev);
    dev_info(dev, "%s()\n", __func__);
    ret = air_pbus_reg_write(phydev, 0xcf928 , 0x0);
    AIR_RTN_ERR(ret);
    ret = genphy_read_abilities(phydev);
    if (ret)
        return ret;

    /* EN8811H supports 100M/1G/2.5G speed. */
    linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
               phydev->supported);
    linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
               phydev->supported);
    linkmode_set_bit(ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
               phydev->supported);
    return 0;
}
#endif
static int en8811h_phy_probe(struct phy_device *phydev)
{
    int ret = 0;
    int reg_value, pid1 = 0, pid2 = 0;
    u32 pbus_value = 0, retry;
    struct device *dev = phydev_dev(phydev);
    ret = air_pbus_reg_write(phydev, 0xcf928 , 0x0);
    AIR_RTN_ERR(ret);
    pid1 = phy_read(phydev, MII_PHYSID1);
    if (pid1 < 0)
        return pid1;
    pid2 = phy_read(phydev, MII_PHYSID2);
    if (pid2 < 0)
        return pid2;
    dev_info(dev, "PHY = %x - %x\n", pid1, pid2);
    if ((EN8811H_PHY_ID1 != pid1) || (EN8811H_PHY_ID2 != pid2))
    {
        dev_err(dev, "EN8811H dose not exist !\n");
        return -ENODEV;
    }
    ret = en8811h_load_firmware(phydev);
    if (ret)
    {
        dev_err(dev,"EN8811H load firmware fail.\n");
        return ret;
    }
    retry = MAX_RETRY;
    do {
        mdelay(300);
        reg_value = air_mii_cl45_read(phydev, 0x1e, 0x8009);
        if (EN8811H_PHY_READY == reg_value)
        {
            dev_info(dev, "EN8811H PHY ready!\n");
            break;
        }
        retry--;
    } while (retry);
    if (0 == retry)
    {
        dev_err(dev, "EN8811H initialize fail ! reg: 0x%x\n", reg_value);
        return -EIO;
    }
    /* Mode selection*/
    dev_info(dev, "EN8811H Mode 1 !\n");
    ret = air_mii_cl45_write(phydev, 0x1e, 0x800c, 0x0);
    AIR_RTN_ERR(ret);
    ret = air_mii_cl45_write(phydev, 0x1e, 0x800d, 0x0);
    AIR_RTN_ERR(ret);
    ret = air_mii_cl45_write(phydev, 0x1e, 0x800e, 0x1101);
    AIR_RTN_ERR(ret);
    ret = air_mii_cl45_write(phydev, 0x1e, 0x800f, 0x0002);
    AIR_RTN_ERR(ret);

    /* Serdes polarity */
    pbus_value = air_buckpbus_reg_read(phydev, 0xca0f8);
    pbus_value = (pbus_value & 0xfffffffc) | EN8811H_RX_POLARITY_NORMAL | EN8811H_TX_POLARITY_NORMAL;
    ret = air_buckpbus_reg_write(phydev, 0xca0f8, pbus_value);
    AIR_RTN_ERR(ret);
    pbus_value = air_buckpbus_reg_read(phydev, 0xca0f8);
    dev_info(dev, "0xca0f8= 0x%x\n", pbus_value);
    pbus_value = air_buckpbus_reg_read(phydev, 0x3b3c);
    dev_info(dev, "Version(0x3b3c)= %x\n", pbus_value);
#if defined(AIR_LED_SUPPORT)
    ret = en8811h_led_init(phydev);
    if (ret < 0)
    {
        dev_err(dev, "en8811h_led_init fail. (ret=%d)\n", ret);
        return ret;
    }
#endif
    dev_info(dev, "EN8811H initialize OK ! (%s)\n", EN8811H_FW_VERSION);
    return 0;
}

static int en8811h_get_autonego(struct phy_device *phydev, int *an)
{
    int reg;
    reg = phy_read(phydev, MII_BMCR);
    if (reg < 0)
        return -EINVAL;
    if (reg & BMCR_ANENABLE)
        *an = AUTONEG_ENABLE;
    else
        *an = AUTONEG_DISABLE;
    return 0;
}

static int en8811h_read_status(struct phy_device *phydev)
{
    int ret = 0, lpagb = 0, lpa = 0, common_adv_gb = 0, common_adv = 0, advgb = 0, adv = 0, reg = 0, an = AUTONEG_DISABLE, bmcr = 0;
    int old_link = phydev->link;
    u32 pbus_value = 0;
    struct device *dev = phydev_dev(phydev);
	ret = genphy_update_link(phydev);
	if (ret)
    {
        dev_err(dev, "ret %d!\n", ret);
		return ret;
    }

	if (old_link && phydev->link)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

    reg = phy_read(phydev, MII_BMSR);
    if (reg < 0)
    {
        dev_err(dev, "MII_BMSR reg %d!\n", reg);
		return reg;
    }
    reg = phy_read(phydev, MII_BMSR);
    if (reg < 0)
    {
        dev_err(dev, "MII_BMSR reg %d!\n", reg);
		return reg;
    }
    if(reg & BMSR_LSTATUS)
    {
        pbus_value = air_buckpbus_reg_read(phydev, 0x109D4);
        if (0x10 & pbus_value) {
            phydev->speed = SPEED_2500;
            phydev->duplex = DUPLEX_FULL;
        }
        else
        {
            ret = en8811h_get_autonego(phydev, &an);
            if ((AUTONEG_ENABLE == an) && (0 == ret))
            {
                dev_dbg(dev, "AN mode!\n");
                dev_dbg(dev, "SPEED 1000/100!\n");
                lpagb = phy_read(phydev, MII_STAT1000);
                if (lpagb < 0 )
                    return lpagb;
                advgb = phy_read(phydev, MII_CTRL1000);
                if (adv < 0 )
                    return adv;
                common_adv_gb = (lpagb & (advgb << 2));

                lpa = phy_read(phydev, MII_LPA);
                if (lpa < 0 )
                    return lpa;
                adv = phy_read(phydev, MII_ADVERTISE);
                if (adv < 0 )
                    return adv;
                common_adv = (lpa & adv);

                phydev->speed = SPEED_10;
                phydev->duplex = DUPLEX_HALF;
                if (common_adv_gb & (LPA_1000FULL | LPA_1000HALF))
                {
                    phydev->speed = SPEED_1000;
                    if (common_adv_gb & LPA_1000FULL)

                        phydev->duplex = DUPLEX_FULL;
                }
                else if (common_adv & (LPA_100FULL | LPA_100HALF))
                {
                    phydev->speed = SPEED_100;
                    if (common_adv & LPA_100FULL)
                        phydev->duplex = DUPLEX_FULL;
                }
                else
                {
                    if (common_adv & LPA_10FULL)
                        phydev->duplex = DUPLEX_FULL;
                }
            }
            else
            {
                dev_dbg(dev, "Force mode!\n");
                bmcr = phy_read(phydev, MII_BMCR);

                if (bmcr < 0)
                    return bmcr;

                if (bmcr & BMCR_FULLDPLX)
                    phydev->duplex = DUPLEX_FULL;
                else
                    phydev->duplex = DUPLEX_HALF;

                if (bmcr & BMCR_SPEED1000)
                    phydev->speed = SPEED_1000;
                else if (bmcr & BMCR_SPEED100)
                    phydev->speed = SPEED_100;
                else
                    phydev->speed = SPEED_UNKNOWN;
            }
        }
    }

	return ret;
}
static struct phy_driver en8811h_driver[] = {
{
    .phy_id         = EN8811H_PHY_ID,
    .name           = "Airoha EN8811H",
    .phy_id_mask    = 0x0ffffff0,
    .probe          = en8811h_phy_probe,
    .read_status    = en8811h_read_status,
#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 5, 0))
    .get_features   = en8811h_get_features,
    .read_mmd       = air_mii_cl45_read,
    .write_mmd      = air_mii_cl45_write,
#endif
} };

int __init en8811h_phy_driver_register(void)
{
    int ret;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
    ret = phy_driver_register(en8811h_driver);
#else
    ret = phy_driver_register(en8811h_driver, THIS_MODULE);
#endif
    if (!ret)
        return 0;

    phy_driver_unregister(en8811h_driver);
    return ret;
}

void __exit en8811h_phy_driver_unregister(void)
{
    phy_driver_unregister(en8811h_driver);
}

module_init(en8811h_phy_driver_register);
module_exit(en8811h_phy_driver_unregister);

static struct mdio_device_id __maybe_unused en8811h_tbl[] = {
	{ PHY_ID_MATCH_EXACT(EN8811H_PHY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, en8811h_tbl);
