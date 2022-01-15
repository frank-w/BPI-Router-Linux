/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>

#include "rtl8367s_mdio.h"

/*
DTS-properties:

"compatible" = "realtek,rtk-gsw"
"realtek,reset-pin" =<gpio>
"realtek,mdio" = <mdio phandle>

optional:
realtek,port_map="wllll"
realtek,extifconfig="rgmii,rgmii"
realtek,phy-id //default 0
*/

// ====================== Protocol functions (mdio,smi,asic) ====================================
/*mii_mgr_read/mii_mgr_write is the callback API for rtl8367 driver*/
static unsigned int mii_mgr_read(struct mii_bus *bus,unsigned int phy_addr,unsigned int phy_register,unsigned int *read_data)
{
	//struct mii_bus *bus = _gsw->bus;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	*read_data = bus->read(bus, phy_addr, phy_register);

	mutex_unlock(&bus->mdio_lock);

	return 0;
}

static unsigned int mii_mgr_write(struct mii_bus *bus,unsigned int phy_addr,unsigned int phy_register,unsigned int write_data)
{
	//struct mii_bus *bus =  _gsw->bus;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	bus->write(bus, phy_addr, phy_register, write_data);

	mutex_unlock(&bus->mdio_lock);

	return 0;
}
#define MDC_MDIO_PHY_ID		0 //r2pro
//#define MDC_MDIO_PHY_ID		29  /* PHY ID 0 or 29 */ //r64
#define MDC_MDIO_WRITE(preamableLength, phyID, regID, data) mii_mgr_write(_gsw->bus,phyID, regID, data)
#define MDC_MDIO_READ(preamableLength, phyID, regID, pData) mii_mgr_read(_gsw->bus,phyID, regID, pData)

//MDC_MDIO_OPERATION is defined in phy code via makefile
static rtk_int32 smi_read(rtk_uint32 mAddrs, rtk_uint32 *rData)
{
    if(mAddrs > 0xFFFF)
        return RT_ERR_INPUT;

    if(rData == NULL)
        return RT_ERR_NULL_POINTER;

    /* Lock (empty function in phy driver) */
    //rtlglue_drvMutexLock();

    /* Write address control code to register 31 */
    MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_CTRL0_REG, MDC_MDIO_ADDR_OP);

    /* Write address to register 23 */
    MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_ADDRESS_REG, mAddrs);

    /* Write read control code to register 21 */
    MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_CTRL1_REG, MDC_MDIO_READ_OP);

    /* Read data from register 25 */
    MDC_MDIO_READ(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_DATA_READ_REG, rData);

    /* Unlock (empty function in phy driver) */
    //rtlglue_drvMutexUnlock();

    return RT_ERR_OK;
}

static rtk_int32 smi_write(rtk_uint32 mAddrs, rtk_uint32 rData)
{
    if(mAddrs > 0xFFFF)
        return RT_ERR_INPUT;

    if(rData > 0xFFFF)
        return RT_ERR_INPUT;

    /* Lock (empty function in phy driver) */
    //rtlglue_drvMutexLock();

    /* Write address control code to register 31 */
    MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_CTRL0_REG, MDC_MDIO_ADDR_OP);

    /* Write address to register 23 */
    MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_ADDRESS_REG, mAddrs);

    /* Write data to register 24 */
    MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_DATA_WRITE_REG, rData);

    /* Write data control code to register 21 */
    MDC_MDIO_WRITE(MDC_MDIO_PREAMBLE_LEN, _gsw->phy_id, MDC_MDIO_CTRL1_REG, MDC_MDIO_WRITE_OP);

    /* Unlock (empty function in phy driver) */
    //rtlglue_drvMutexUnlock();

    return RT_ERR_OK;
}


// ========================================= Asic functions =================================================
/* rtl8367c_{get,set}AsicReg  Get/Set content of asic register */
static ret_t rtl8367c_setAsicReg(rtk_uint32 reg, rtk_uint32 value)
{
    ret_t retVal;

    retVal = smi_write(reg, value);
    if(retVal != RT_ERR_OK)
        return RT_ERR_SMI;

    return RT_ERR_OK;
}

static ret_t rtl8367c_getAsicReg(rtk_uint32 reg, rtk_uint32 *pValue)
{

    rtk_uint32 regData;
    ret_t retVal;

    retVal = smi_read(reg, &regData);
    if(retVal != RT_ERR_OK)
        return RT_ERR_SMI;

    *pValue = regData;

    return RT_ERR_OK;
}

/* rtl8367c_{get,set} AsicRegBit  Get/Set a bit value of a specified register */
static ret_t rtl8367c_setAsicRegBit(rtk_uint32 reg, rtk_uint32 bit, rtk_uint32 value)
{
	rtk_uint32 regData;
	ret_t retVal;

	if(bit >= RTL8367C_REGBITLENGTH)
		return RT_ERR_INPUT;

	retVal = smi_read(reg, &regData);
	if(retVal != RT_ERR_OK)
		return RT_ERR_SMI;

	if(value)
		regData = regData | (1 << bit);
	else
		regData = regData & (~(1 << bit));

	retVal = smi_write(reg, regData);
	if(retVal != RT_ERR_OK)
		return RT_ERR_SMI;

	return RT_ERR_OK;
}

static ret_t rtl8367c_getAsicRegBit(rtk_uint32 reg, rtk_uint32 bit, rtk_uint32 *pValue)
{
	rtk_uint32 regData;
	ret_t retVal;

	retVal = smi_read(reg, &regData);
	if(retVal != RT_ERR_OK)
		return RT_ERR_SMI;

	*pValue = (regData & (0x1 << bit)) >> bit;

	return RT_ERR_OK;
}

/* rtl8367c_{get,set}AsicRegBits  Get/Set bits value of a specified register */
static ret_t rtl8367c_getAsicRegBits(rtk_uint32 reg, rtk_uint32 bits, rtk_uint32 *pValue)
{

    rtk_uint32 regData;
    ret_t retVal;
    rtk_uint32 bitsShift;

    if(bits>= (1<<RTL8367C_REGBITLENGTH) )
        return RT_ERR_INPUT;

    bitsShift = 0;
    while(!(bits & (1 << bitsShift)))
    {
        bitsShift++;
        if(bitsShift >= RTL8367C_REGBITLENGTH)
            return RT_ERR_INPUT;
    }

    retVal = smi_read(reg, &regData);
    if(retVal != RT_ERR_OK) return RT_ERR_SMI;

    *pValue = (regData & bits) >> bitsShift;

    return RT_ERR_OK;
}

static ret_t rtl8367c_setAsicRegBits(rtk_uint32 reg, rtk_uint32 bits, rtk_uint32 value)
{
    rtk_uint32 regData;
    ret_t retVal;
    rtk_uint32 bitsShift;
    rtk_uint32 valueShifted;

    if(bits >= (1 << RTL8367C_REGBITLENGTH) )
        return RT_ERR_INPUT;

    bitsShift = 0;
    while(!(bits & (1 << bitsShift)))
    {
        bitsShift++;
        if(bitsShift >= RTL8367C_REGBITLENGTH)
            return RT_ERR_INPUT;
    }
    valueShifted = value << bitsShift;

    if(valueShifted > RTL8367C_REGDATAMAX)
        return RT_ERR_INPUT;

    retVal = smi_read(reg, &regData);
    if(retVal != RT_ERR_OK)
        return RT_ERR_SMI;

    regData = regData & (~bits);
    regData = regData | (valueShifted & bits);

    retVal = smi_write(reg, regData);
    if(retVal != RT_ERR_OK)
        return RT_ERR_SMI;

    return RT_ERR_OK;
}

/* rtl8367c_{get,set}AsicPHYOCPReg Get/Set PHY OCP registers */
static ret_t rtl8367c_setAsicPHYOCPReg(rtk_uint32 phyNo, rtk_uint32 ocpAddr, rtk_uint32 ocpData )
{
    ret_t retVal;
    rtk_uint32 regAddr;
    rtk_uint32 ocpAddrPrefix, ocpAddr9_6, ocpAddr5_1;

    /* OCP prefix */
    ocpAddrPrefix = ((ocpAddr & 0xFC00) >> 10);
    if((retVal = rtl8367c_setAsicRegBits(RTL8367C_REG_GPHY_OCP_MSB_0, RTL8367C_CFG_CPU_OCPADR_MSB_MASK, ocpAddrPrefix)) != RT_ERR_OK)
        return retVal;

    /*prepare access address*/
    ocpAddr9_6 = ((ocpAddr >> 6) & 0x000F);
    ocpAddr5_1 = ((ocpAddr >> 1) & 0x001F);
    regAddr = RTL8367C_PHY_BASE | (ocpAddr9_6 << 8) | (phyNo << RTL8367C_PHY_OFFSET) | ocpAddr5_1;
    if((retVal = rtl8367c_setAsicReg(regAddr, ocpData)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static ret_t rtl8367c_getAsicPHYOCPReg(rtk_uint32 phyNo, rtk_uint32 ocpAddr, rtk_uint32 *pRegData )
{
    ret_t retVal;
    rtk_uint32 regAddr;
    rtk_uint32 ocpAddrPrefix, ocpAddr9_6, ocpAddr5_1;
    /* OCP prefix */
    ocpAddrPrefix = ((ocpAddr & 0xFC00) >> 10);
    if((retVal = rtl8367c_setAsicRegBits(RTL8367C_REG_GPHY_OCP_MSB_0, RTL8367C_CFG_CPU_OCPADR_MSB_MASK, ocpAddrPrefix)) != RT_ERR_OK)
        return retVal;

    /*prepare access address*/
    ocpAddr9_6 = ((ocpAddr >> 6) & 0x000F);
    ocpAddr5_1 = ((ocpAddr >> 1) & 0x001F);
    regAddr = RTL8367C_PHY_BASE | (ocpAddr9_6 << 8) | (phyNo << RTL8367C_PHY_OFFSET) | ocpAddr5_1;
    if((retVal = rtl8367c_getAsicReg(regAddr, pRegData)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

/* rtl8367c_{get,set}AiscSdsReg Get/Set Serdes registers */
/*static ret_t rtl8367c_setAsicSdsReg(rtk_uint32 sdsId, rtk_uint32 sdsReg, rtk_uint32 sdsPage,  rtk_uint32 value)
{
    rtk_uint32 retVal;

    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, value)) != RT_ERR_OK)
        return retVal;

    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, (sdsPage<<5) | sdsReg)) != RT_ERR_OK)
        return retVal;

    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0|sdsId)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;

}

static ret_t rtl8367c_getAsicSdsReg(rtk_uint32 sdsId, rtk_uint32 sdsReg, rtk_uint32 sdsPage, rtk_uint32 *value)
{
    rtk_uint32 retVal, busy;

    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, (sdsPage<<5) | sdsReg)) != RT_ERR_OK)
        return retVal;

    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x0080|sdsId)) != RT_ERR_OK)
        return retVal;

    while(1)
    {
        if ((retVal = rtl8367c_getAsicReg(RTL8367C_REG_SDS_INDACS_CMD, &busy))!=RT_ERR_OK)
            return retVal;

        if ((busy & 0x100) == 0)
            break;
    }

    if ((retVal = rtl8367c_getAsicReg(RTL8367C_REG_SDS_INDACS_DATA, value))!=RT_ERR_OK)
            return retVal;

    return RT_ERR_OK;
}*/

/* rtl8367c_{get,set}AsicPHYReg  Get/Set PHY registers */
static ret_t rtl8367c_setAsicPHYReg(rtk_uint32 phyNo, rtk_uint32 phyAddr, rtk_uint32 phyData )
{
    rtk_uint32 ocp_addr;

    if(phyAddr > RTL8367C_PHY_REGNOMAX)
        return RT_ERR_PHY_REG_ID;

    ocp_addr = 0xa400 + phyAddr*2;

    return rtl8367c_setAsicPHYOCPReg(phyNo, ocp_addr, phyData);
}

static ret_t rtl8367c_getAsicPHYReg(rtk_uint32 phyNo, rtk_uint32 phyAddr, rtk_uint32 *pRegData )
{
    rtk_uint32 ocp_addr;

    if(phyAddr > RTL8367C_PHY_REGNOMAX)
        return RT_ERR_PHY_REG_ID;

    ocp_addr = 0xa400 + phyAddr*2;

    return rtl8367c_getAsicPHYOCPReg(phyNo, ocp_addr, pRegData);
}

static ret_t rtl8367c_setAsicPortEnableAll(rtk_uint32 enable)
{
    if(enable >= 2)
        return RT_ERR_INPUT;

    return rtl8367c_setAsicRegBit(RTL8367C_REG_PHY_AD, RTL8367C_PDNPHY_OFFSET, !enable);
}

/* rtl8367c_setAsicPortExtMode  Set external interface mode configuration */
static ret_t rtl8367c_setAsicPortExtMode(rtk_uint32 id, rtk_uint32 mode)
{
    ret_t   retVal;
    rtk_uint32 i, regValue, type, option;
    rtk_uint32 idx;
    rtk_uint32 redData[][2] =   { {0x04D7, 0x0480}, {0xF994, 0x0481}, {0x21A2, 0x0482}, {0x6960, 0x0483}, {0x9728, 0x0484}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x83F2, 0x002E} };
    rtk_uint32 redDataSB[][2] = { {0x04D7, 0x0480}, {0xF994, 0x0481}, {0x31A2, 0x0482}, {0x6960, 0x0483}, {0x9728, 0x0484}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x83F2, 0x002E} };
    rtk_uint32 redData1[][2] =  { {0x82F1, 0x0500}, {0xF195, 0x0501}, {0x31A2, 0x0502}, {0x796C, 0x0503}, {0x9728, 0x0504}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x0F80, 0x0001}, {0x83F2, 0x002E} };
    rtk_uint32 redData5[][2] =  { {0x82F1, 0x0500}, {0xF195, 0x0501}, {0x31A2, 0x0502}, {0x796C, 0x0503}, {0x9728, 0x0504}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x0F80, 0x0001}, {0x83F2, 0x002E} };
    rtk_uint32 redData6[][2] =  { {0x82F1, 0x0500}, {0xF195, 0x0501}, {0x31A2, 0x0502}, {0x796C, 0x0503}, {0x9728, 0x0504}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x0F80, 0x0001}, {0x83F2, 0x002E} };
    rtk_uint32 redData8[][2] =  { {0x82F1, 0x0500}, {0xF995, 0x0501}, {0x31A2, 0x0502}, {0x796C, 0x0503}, {0x9728, 0x0504}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x0F80, 0x0001}, {0x83F2, 0x002E} };
    rtk_uint32 redData9[][2] =  { {0x82F1, 0x0500}, {0xF995, 0x0501}, {0x31A2, 0x0502}, {0x796C, 0x0503}, {0x9728, 0x0504}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x0F80, 0x0001}, {0x83F2, 0x002E} };
    rtk_uint32 redDataHB[][2] = { {0x82F0, 0x0500}, {0xF195, 0x0501}, {0x31A2, 0x0502}, {0x7960, 0x0503}, {0x9728, 0x0504}, {0x9D85, 0x0423}, {0xD810, 0x0424}, {0x0F80, 0x0001}, {0x83F2, 0x002E} };

    if(id >= RTL8367C_EXTNO)
        return RT_ERR_OUT_OF_RANGE;

    if(mode >= EXT_END)
        return RT_ERR_OUT_OF_RANGE;

    /* magic number*/
    if((retVal = rtl8367c_setAsicReg(0x13C2, 0x0249)) != RT_ERR_OK)
        return retVal;
    /* Chip num */
    if((retVal = rtl8367c_getAsicReg(0x1300, &regValue)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x13C2, 0x0000)) != RT_ERR_OK)
        return retVal;

    type = 0;

    switch (regValue)
    {
        case 0x0276:
        case 0x0597:
        case 0x6367:
            type = 1;
            break;
        case 0x0652:
        case 0x6368:
            type = 2;
            break;
        case 0x0801:
        case 0x6511:
            type = 3;
            break;
        default:
            return RT_ERR_FAILED;
    }


    if (1==type)
    {
        if((mode == EXT_1000X_100FX) || (mode == EXT_1000X) || (mode == EXT_100FX))
        {
            if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_REG_TO_ECO4, 5, 1)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_REG_TO_ECO4, 7, 1)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_CHIP_RESET, RTL8367C_DW8051_RST_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_MISCELLANEOUS_CONFIGURE0, RTL8367C_DW8051_EN_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_ACS_IROM_ENABLE_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_IROM_MSB_OFFSET, 0)) != RT_ERR_OK)
                return retVal;
/*
            if(mode == EXT_1000X_100FX)
            {
                for(idx = 0; idx < FIBER2_AUTO_INIT_SIZE; idx++)
                {
                    if ((retVal = rtl8367c_setAsicReg(0xE000 + idx, (rtk_uint32)Fiber2_Auto[idx])) != RT_ERR_OK)
                        return retVal;
                }
            }

            if(mode == EXT_1000X)
            {
                for(idx = 0; idx < FIBER2_1G_INIT_SIZE; idx++)
                {
                    if ((retVal = rtl8367c_setAsicReg(0xE000 + idx, (rtk_uint32)Fiber2_1G[idx])) != RT_ERR_OK)
                        return retVal;
                }
            }

            if(mode == EXT_100FX)
            {
                for(idx = 0; idx < FIBER2_100M_INIT_SIZE; idx++)
                {
                    if ((retVal = rtl8367c_setAsicReg(0xE000 + idx, (rtk_uint32)Fiber2_100M[idx])) != RT_ERR_OK)
                        return retVal;
                }
            }
*/
            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_IROM_MSB_OFFSET, 0)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_ACS_IROM_ENABLE_OFFSET, 0)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_CHIP_RESET, RTL8367C_DW8051_RST_OFFSET, 0)) != RT_ERR_OK)
                return retVal;
        }

        if(mode == EXT_GMII)
        {
            if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_EXT0_RGMXF, RTL8367C_EXT0_RGTX_INV_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_EXT1_RGMXF, RTL8367C_EXT1_RGTX_INV_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if( (retVal = rtl8367c_setAsicRegBits(RTL8367C_REG_EXT_TXC_DLY, RTL8367C_EXT1_GMII_TX_DELAY_MASK, 5)) != RT_ERR_OK)
                return retVal;

            if( (retVal = rtl8367c_setAsicRegBits(RTL8367C_REG_EXT_TXC_DLY, RTL8367C_EXT0_GMII_TX_DELAY_MASK, 6)) != RT_ERR_OK)
                return retVal;
        }

        /* Serdes reset */
        if( (mode == EXT_TMII_MAC) || (mode == EXT_TMII_PHY) )
        {
            if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_BYPASS_LINE_RATE, id, 1)) != RT_ERR_OK)
                return retVal;
        }
        else
        {
            if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_BYPASS_LINE_RATE, id, 0)) != RT_ERR_OK)
                return retVal;
        }

        if( (mode == EXT_SGMII) || (mode == EXT_HSGMII) )
        {
            if(id != 1)
                return RT_ERR_PORT_ID;

            if((retVal = rtl8367c_setAsicReg(0x13C0, 0x0249)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_getAsicReg(0x13C1, &option)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_setAsicReg(0x13C0, 0x0000)) != RT_ERR_OK)
                return retVal;
        }

        if(mode == EXT_SGMII)
        {
            if(option == 0)
            {
                for(i = 0; i <= 7; i++)
                {
                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redData[i][0])) != RT_ERR_OK)
                        return retVal;

                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redData[i][1])) != RT_ERR_OK)
                        return retVal;

                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                        return retVal;
                }
            }
            else
            {
                for(i = 0; i <= 7; i++)
                {
                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redDataSB[i][0])) != RT_ERR_OK)
                        return retVal;

                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redDataSB[i][1])) != RT_ERR_OK)
                        return retVal;

                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                        return retVal;
                }
            }
        }

        if(mode == EXT_HSGMII)
        {
            if(option == 0)
            {
                if( (retVal = rtl8367c_setAsicReg(0x13c2, 0x0249)) != RT_ERR_OK)
                    return retVal;

                if( (retVal = rtl8367c_getAsicReg(0x1301, &regValue)) != RT_ERR_OK)
                    return retVal;

                if( (retVal = rtl8367c_setAsicReg(0x13c2, 0x0000)) != RT_ERR_OK)
                    return retVal;

                if ( ((regValue & 0x00F0) >> 4) == 0x0001)
                {
                    for(i = 0; i <= 8; i++)
                    {
                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redData1[i][0])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redData1[i][1])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                            return retVal;
                    }
                }
                else if ( ((regValue & 0x00F0) >> 4) == 0x0005)
                {
                    for(i = 0; i <= 8; i++)
                    {
                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redData5[i][0])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redData5[i][1])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                            return retVal;
                    }
                }
                else if ( ((regValue & 0x00F0) >> 4) == 0x0006)
                {
                    for(i = 0; i <= 8; i++)
                    {
                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redData6[i][0])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redData6[i][1])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                            return retVal;
                    }
                }
                else if ( ((regValue & 0x00F0) >> 4) == 0x0008)
                {
                    for(i = 0; i <= 8; i++)
                    {
                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redData8[i][0])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redData8[i][1])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                            return retVal;
                    }
                }
                else if ( ((regValue & 0x00F0) >> 4) == 0x0009)
                {
                    for(i = 0; i <= 8; i++)
                    {
                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redData9[i][0])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redData9[i][1])) != RT_ERR_OK)
                            return retVal;

                        if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                            return retVal;
                    }
                }
            }
            else
            {
                for(i = 0; i <= 8; i++)
                {
                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, redDataHB[i][0])) != RT_ERR_OK)
                        return retVal;

                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, redDataHB[i][1])) != RT_ERR_OK)
                        return retVal;

                    if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                        return retVal;
                }
            }
        }

        /* Only one ext port should care SGMII setting */
        if(id == 1)
        {

            if(mode == EXT_SGMII)
            {
                if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_SGMII_OFFSET, 1)) != RT_ERR_OK)
                    return retVal;

                if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_HSGMII_OFFSET, 0)) != RT_ERR_OK)
                    return retVal;
            }
            else if(mode == EXT_HSGMII)
            {
                if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_SGMII_OFFSET, 0)) != RT_ERR_OK)
                    return retVal;

                if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_HSGMII_OFFSET, 1)) != RT_ERR_OK)
                    return retVal;
            }
            else
            {

                if((mode != EXT_1000X_100FX) && (mode != EXT_1000X) && (mode != EXT_100FX))
                {
                    if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_SGMII_OFFSET, 0)) != RT_ERR_OK)
                        return retVal;

                    if( (retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_HSGMII_OFFSET, 0)) != RT_ERR_OK)
                        return retVal;
                }
            }
        }

        if(0 == id || 1 == id)
        {
            if((retVal = rtl8367c_setAsicRegBits(RTL8367C_REG_DIGITAL_INTERFACE_SELECT, RTL8367C_SELECT_GMII_0_MASK << (id * RTL8367C_SELECT_GMII_1_OFFSET), mode)) != RT_ERR_OK)
                return retVal;
        }
        else
        {
            if((retVal = rtl8367c_setAsicRegBits(RTL8367C_REG_DIGITAL_INTERFACE_SELECT_1, RTL8367C_SELECT_GMII_2_MASK, mode)) != RT_ERR_OK)
                return retVal;
        }

        /* Serdes not reset */
        if( (mode == EXT_SGMII) || (mode == EXT_HSGMII) )
        {
            if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_DATA, 0x7106)) != RT_ERR_OK)
                return retVal;

            if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_ADR, 0x0003)) != RT_ERR_OK)
                return retVal;

            if( (retVal = rtl8367c_setAsicReg(RTL8367C_REG_SDS_INDACS_CMD, 0x00C0)) != RT_ERR_OK)
                return retVal;
        }

        if( (mode == EXT_SGMII) || (mode == EXT_HSGMII) )
        {
            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_CHIP_RESET, RTL8367C_DW8051_RST_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_MISCELLANEOUS_CONFIGURE0, RTL8367C_DW8051_EN_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_ACS_IROM_ENABLE_OFFSET, 1)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_IROM_MSB_OFFSET, 0)) != RT_ERR_OK)
                return retVal;

            for(idx = 0; idx < SGMII_INIT_SIZE; idx++)
            {
                if ((retVal = rtl8367c_setAsicReg(0xE000 + idx, (rtk_uint32)Sgmii_Init[idx])) != RT_ERR_OK)
                    return retVal;
            }

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_IROM_MSB_OFFSET, 0)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_DW8051_RDY, RTL8367C_ACS_IROM_ENABLE_OFFSET, 0)) != RT_ERR_OK)
                return retVal;

            if ((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_CHIP_RESET, RTL8367C_DW8051_RST_OFFSET, 0)) != RT_ERR_OK)
                return retVal;
        }
    }

    return RT_ERR_OK;
}

static ret_t rtl8367c_setAsicPortForceLinkExt(rtk_uint32 id, rtl8367c_port_ability_t *pPortAbility)
{
    rtk_uint32 retVal, regValue, type;
    rtk_uint32 reg_data = 0;

    /* Invalid input parameter */
    if(id >= RTL8367C_EXTNO)
        return RT_ERR_OUT_OF_RANGE;

    reg_data |= pPortAbility->forcemode << 12;
    reg_data |= pPortAbility->mstfault << 9;
    reg_data |= pPortAbility->mstmode << 8;
    reg_data |= pPortAbility->nway << 7;
    reg_data |= pPortAbility->txpause << 6;
    reg_data |= pPortAbility->rxpause << 5;
    reg_data |= pPortAbility->link << 4;
    reg_data |= pPortAbility->duplex << 2;
    reg_data |= pPortAbility->speed;

    if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, RTL8367C_RTL_MAGIC_ID_VAL)) != RT_ERR_OK)
        return retVal;
    /*get chip ID */
    if((retVal = rtl8367c_getAsicReg(RTL8367C_CHIP_NUMBER_REG, &regValue)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, 0x0000)) != RT_ERR_OK)
        return retVal;

    type = 0;

    switch (regValue)
    {
        case 0x0276:
        case 0x0597:
        case 0x6367:
            type = 1;
            break;
        case 0x0652:
        case 0x6368:
            type = 2;
            break;
        case 0x0801:
        case 0x6511:
            type = 3;
            break;
        default:
            return RT_ERR_FAILED;
    }

    if (1 == type)
    {
        if(1 == id)
        {
            if ((retVal = rtl8367c_getAsicReg(RTL8367C_REG_REG_TO_ECO4, &regValue)) != RT_ERR_OK)
                return retVal;

            if((regValue & (0x0001 << 5)) && (regValue & (0x0001 << 7)))
            {
                return RT_ERR_OK;
            }

            if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_FDUP_OFFSET, pPortAbility->duplex)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_setAsicRegBits(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_SPD_MASK, pPortAbility->speed)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_LINK_OFFSET, pPortAbility->link)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_TXFC_OFFSET, pPortAbility->txpause)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_RXFC_OFFSET, pPortAbility->rxpause)) != RT_ERR_OK)
                return retVal;
        }

        if(0 == id || 1 == id)
            return rtl8367c_setAsicReg(RTL8367C_REG_DIGITAL_INTERFACE0_FORCE + id, reg_data);
        else
            return rtl8367c_setAsicReg(RTL8367C_REG_DIGITAL_INTERFACE2_FORCE, reg_data);
    }

    return RT_ERR_OK;
}

static ret_t rtl8367c_getAsicPortForceLinkExt(rtk_uint32 id, rtl8367c_port_ability_t *pPortAbility)
{
    rtk_uint32  reg_data, regValue, type;
    rtk_uint32  sgmiiSel;
    rtk_uint32  hsgmiiSel;
    ret_t       retVal;

    /* Invalid input parameter */
    if(id >= RTL8367C_EXTNO)
        return RT_ERR_OUT_OF_RANGE;
    /*cfg_magic_id  &  get chip_id*/
    if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, RTL8367C_RTL_MAGIC_ID_VAL)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_getAsicReg(RTL8367C_CHIP_NUMBER_REG, &regValue)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, 0x0000)) != RT_ERR_OK)
        return retVal;

    type = 0;

    switch (regValue)
    {
        case 0x0276:
        case 0x0597:
        case 0x6367:
            type = 1;
            break;
        case 0x0652:
        case 0x6368:
            type = 2;
            break;
        case 0x0801:
        case 0x6511:
            type = 3;
            break;
        default:
            return RT_ERR_FAILED;
    }

    if (1 == type)
    {
        if(1 == id)
        {
            if((retVal = rtl8367c_getAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_SGMII_OFFSET, &sgmiiSel)) != RT_ERR_OK)
                return retVal;

            if((retVal = rtl8367c_getAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_MAC8_SEL_HSGMII_OFFSET, &hsgmiiSel)) != RT_ERR_OK)
                return retVal;

            if( (sgmiiSel == 1) || (hsgmiiSel == 1) )
            {
                memset(pPortAbility, 0x00, sizeof(rtl8367c_port_ability_t));
                pPortAbility->forcemode = 1;

                if((retVal = rtl8367c_getAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_FDUP_OFFSET, &reg_data)) != RT_ERR_OK)
                    return retVal;

                pPortAbility->duplex = reg_data;

                if((retVal = rtl8367c_getAsicRegBits(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_SPD_MASK, &reg_data)) != RT_ERR_OK)
                    return retVal;

                pPortAbility->speed = reg_data;

                if((retVal = rtl8367c_getAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_LINK_OFFSET, &reg_data)) != RT_ERR_OK)
                    return retVal;

                pPortAbility->link = reg_data;

                if((retVal = rtl8367c_getAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_TXFC_OFFSET, &reg_data)) != RT_ERR_OK)
                    return retVal;

                pPortAbility->txpause = reg_data;

                if((retVal = rtl8367c_getAsicRegBit(RTL8367C_REG_SDS_MISC, RTL8367C_CFG_SGMII_RXFC_OFFSET, &reg_data)) != RT_ERR_OK)
                    return retVal;

                pPortAbility->rxpause = reg_data;

                return RT_ERR_OK;
            }
        }

        if(0 == id || 1 == id)
            retVal = rtl8367c_getAsicReg(RTL8367C_REG_DIGITAL_INTERFACE0_FORCE+id, &reg_data);
        else
            retVal = rtl8367c_getAsicReg(RTL8367C_REG_DIGITAL_INTERFACE2_FORCE, &reg_data);

        if(retVal != RT_ERR_OK)
            return retVal;

        pPortAbility->forcemode = (reg_data >> 12) & 0x0001;
        pPortAbility->mstfault  = (reg_data >> 9) & 0x0001;
        pPortAbility->mstmode   = (reg_data >> 8) & 0x0001;
        pPortAbility->nway      = (reg_data >> 7) & 0x0001;
        pPortAbility->txpause   = (reg_data >> 6) & 0x0001;
        pPortAbility->rxpause   = (reg_data >> 5) & 0x0001;
        pPortAbility->link      = (reg_data >> 4) & 0x0001;
        pPortAbility->duplex    = (reg_data >> 2) & 0x0001;
        pPortAbility->speed     = reg_data & 0x0003;
    }
    return RT_ERR_OK;
}

static void _rtl8367c_Vlan4kStUser2Smi(rtl8367c_user_vlan4kentry *pUserVlan4kEntry, rtk_uint16 *pSmiVlan4kEntry)
{
    pSmiVlan4kEntry[0] |= (pUserVlan4kEntry->mbr & 0x00FF);
    pSmiVlan4kEntry[0] |= (pUserVlan4kEntry->untag & 0x00FF) << 8;

    pSmiVlan4kEntry[1] |= (pUserVlan4kEntry->fid_msti & 0x000F);
    pSmiVlan4kEntry[1] |= (pUserVlan4kEntry->vbpen & 0x0001) << 4;
    pSmiVlan4kEntry[1] |= (pUserVlan4kEntry->vbpri & 0x0007) << 5;
    pSmiVlan4kEntry[1] |= (pUserVlan4kEntry->envlanpol & 0x0001) << 8;
    pSmiVlan4kEntry[1] |= (pUserVlan4kEntry->meteridx & 0x001F) << 9;
    pSmiVlan4kEntry[1] |= (pUserVlan4kEntry->ivl_svl & 0x0001) << 14;

    pSmiVlan4kEntry[2] |= ((pUserVlan4kEntry->mbr & 0x0700) >> 8);
    pSmiVlan4kEntry[2] |= ((pUserVlan4kEntry->untag & 0x0700) >> 8) << 3;
    pSmiVlan4kEntry[2] |= ((pUserVlan4kEntry->meteridx & 0x0020) >> 5) << 6;
}

static void _rtl8367c_Vlan4kStSmi2User(rtk_uint16 *pSmiVlan4kEntry, rtl8367c_user_vlan4kentry *pUserVlan4kEntry)
{
    pUserVlan4kEntry->mbr = (pSmiVlan4kEntry[0] & 0x00FF) | ((pSmiVlan4kEntry[2] & 0x0007) << 8);
    pUserVlan4kEntry->untag = ((pSmiVlan4kEntry[0] & 0xFF00) >> 8) | (((pSmiVlan4kEntry[2] & 0x0038) >> 3) << 8);
    pUserVlan4kEntry->fid_msti = pSmiVlan4kEntry[1] & 0x000F;
    pUserVlan4kEntry->vbpen = (pSmiVlan4kEntry[1] & 0x0010) >> 4;
    pUserVlan4kEntry->vbpri = (pSmiVlan4kEntry[1] & 0x00E0) >> 5;
    pUserVlan4kEntry->envlanpol = (pSmiVlan4kEntry[1] & 0x0100) >> 8;
    pUserVlan4kEntry->meteridx = ((pSmiVlan4kEntry[1] & 0x3E00) >> 9) | (((pSmiVlan4kEntry[2] & 0x0040) >> 6) << 5);
    pUserVlan4kEntry->ivl_svl = (pSmiVlan4kEntry[1] & 0x4000) >> 14;
}

static void _rtl8367c_VlanMCStUser2Smi(rtl8367c_vlanconfiguser *pVlanCg, rtk_uint16 *pSmiVlanCfg)
{
    pSmiVlanCfg[0] |= pVlanCg->mbr & 0x07FF;

    pSmiVlanCfg[1] |= pVlanCg->fid_msti & 0x000F;

    pSmiVlanCfg[2] |= pVlanCg->vbpen & 0x0001;
    pSmiVlanCfg[2] |= (pVlanCg->vbpri & 0x0007) << 1;
    pSmiVlanCfg[2] |= (pVlanCg->envlanpol & 0x0001) << 4;
    pSmiVlanCfg[2] |= (pVlanCg->meteridx & 0x003F) << 5;

    pSmiVlanCfg[3] |= pVlanCg->evid & 0x1FFF;
}

static void _rtl8367c_VlanMCStSmi2User(rtk_uint16 *pSmiVlanCfg, rtl8367c_vlanconfiguser *pVlanCg)
{
    pVlanCg->mbr            = pSmiVlanCfg[0] & 0x07FF;
    pVlanCg->fid_msti       = pSmiVlanCfg[1] & 0x000F;
    pVlanCg->meteridx       = (pSmiVlanCfg[2] >> 5) & 0x003F;
    pVlanCg->envlanpol      = (pSmiVlanCfg[2] >> 4) & 0x0001;
    pVlanCg->vbpri          = (pSmiVlanCfg[2] >> 1) & 0x0007;
    pVlanCg->vbpen          = pSmiVlanCfg[2] & 0x0001;
    pVlanCg->evid           = pSmiVlanCfg[3] & 0x1FFF;
}

static ret_t rtl8367c_getAsicVlan4kEntry(rtl8367c_user_vlan4kentry *pVlan4kEntry )
{
    rtk_uint16                  vlan_4k_entry[RTL8367C_VLAN_4KTABLE_LEN];
    rtk_uint32                  page_idx;
    rtk_uint16                  *tableAddr;
    ret_t                       retVal;
    rtk_uint32                  regData;
    rtk_uint32                  busyCounter;

    if(pVlan4kEntry->vid > RTL8367C_VIDMAX)
        return RT_ERR_VLAN_VID;

    /* Polling status */
    busyCounter = RTL8367C_VLAN_BUSY_CHECK_NO;
    while(busyCounter)
    {
        retVal = rtl8367c_getAsicRegBit(RTL8367C_TABLE_ACCESS_STATUS_REG, RTL8367C_TABLE_LUT_ADDR_BUSY_FLAG_OFFSET,&regData);
        if(retVal != RT_ERR_OK)
            return retVal;

        if(regData == 0)
            break;

        busyCounter --;
        if(busyCounter == 0)
            return RT_ERR_BUSYWAIT_TIMEOUT;
    }

    /* Write Address (VLAN_ID) */
    regData = pVlan4kEntry->vid;
    retVal = rtl8367c_setAsicReg(RTL8367C_TABLE_ACCESS_ADDR_REG, regData);
    if(retVal != RT_ERR_OK)
        return retVal;

    /* Read Command */
    retVal = rtl8367c_setAsicRegBits(RTL8367C_TABLE_ACCESS_CTRL_REG, RTL8367C_TABLE_TYPE_MASK | RTL8367C_COMMAND_TYPE_MASK, RTL8367C_TABLE_ACCESS_REG_DATA(TB_OP_READ,TB_TARGET_CVLAN));
    if(retVal != RT_ERR_OK)
        return retVal;

    /* Polling status */
    busyCounter = RTL8367C_VLAN_BUSY_CHECK_NO;
    while(busyCounter)
    {
        retVal = rtl8367c_getAsicRegBit(RTL8367C_TABLE_ACCESS_STATUS_REG, RTL8367C_TABLE_LUT_ADDR_BUSY_FLAG_OFFSET,&regData);
        if(retVal != RT_ERR_OK)
            return retVal;

        if(regData == 0)
            break;

        busyCounter --;
        if(busyCounter == 0)
            return RT_ERR_BUSYWAIT_TIMEOUT;
    }

    /* Read VLAN data from register */
    tableAddr = vlan_4k_entry;
    for(page_idx = 0; page_idx < RTL8367C_VLAN_4KTABLE_LEN; page_idx++)
    {
        retVal = rtl8367c_getAsicReg(RTL8367C_TABLE_ACCESS_RDDATA_BASE + page_idx, &regData);
        if(retVal != RT_ERR_OK)
            return retVal;

        *tableAddr = regData;
        tableAddr++;
    }

    _rtl8367c_Vlan4kStSmi2User(vlan_4k_entry, pVlan4kEntry);

    return RT_ERR_OK;
}

static ret_t rtl8367c_setAsicVlan4kEntry(rtl8367c_user_vlan4kentry *pVlan4kEntry )
{
    rtk_uint16              vlan_4k_entry[RTL8367C_VLAN_4KTABLE_LEN];
    rtk_uint32                  page_idx;
    rtk_uint16                  *tableAddr;
    ret_t                   retVal;
    rtk_uint32                  regData;

    if(pVlan4kEntry->vid > RTL8367C_VIDMAX)
        return RT_ERR_VLAN_VID;

    if(pVlan4kEntry->mbr > RTL8367C_PORTMASK)
        return RT_ERR_PORT_MASK;

    if(pVlan4kEntry->untag > RTL8367C_PORTMASK)
        return RT_ERR_PORT_MASK;

    if(pVlan4kEntry->fid_msti > RTL8367C_FIDMAX)
        return RT_ERR_L2_FID;

    if(pVlan4kEntry->meteridx > RTL8367C_METERMAX)
        return RT_ERR_FILTER_METER_ID;

    if(pVlan4kEntry->vbpri > RTL8367C_PRIMAX)
        return RT_ERR_QOS_INT_PRIORITY;

    memset(vlan_4k_entry, 0x00, sizeof(rtk_uint16) * RTL8367C_VLAN_4KTABLE_LEN);
    _rtl8367c_Vlan4kStUser2Smi(pVlan4kEntry, vlan_4k_entry);

    /* Prepare Data */
    tableAddr = vlan_4k_entry;
    for(page_idx = 0; page_idx < RTL8367C_VLAN_4KTABLE_LEN; page_idx++)
    {
        regData = *tableAddr;
        retVal = rtl8367c_setAsicReg(RTL8367C_TABLE_ACCESS_WRDATA_BASE + page_idx, regData);
        if(retVal != RT_ERR_OK)
            return retVal;

        tableAddr++;
    }

    /* Write Address (VLAN_ID) */
    regData = pVlan4kEntry->vid;
    retVal = rtl8367c_setAsicReg(RTL8367C_TABLE_ACCESS_ADDR_REG, regData);
    if(retVal != RT_ERR_OK)
        return retVal;

    /* Write Command */
    retVal = rtl8367c_setAsicRegBits(RTL8367C_TABLE_ACCESS_CTRL_REG, RTL8367C_TABLE_TYPE_MASK | RTL8367C_COMMAND_TYPE_MASK,RTL8367C_TABLE_ACCESS_REG_DATA(TB_OP_WRITE,TB_TARGET_CVLAN));
    if(retVal != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static ret_t rtl8367c_getAsicVlanMemberConfig(rtk_uint32 index, rtl8367c_vlanconfiguser *pVlanCg)
{
    ret_t  retVal;
    rtk_uint32 page_idx;
    rtk_uint32 regAddr;
    rtk_uint32 regData;
    rtk_uint16 *tableAddr;
    rtk_uint16 smi_vlancfg[RTL8367C_VLAN_MBRCFG_LEN];

    if(index > RTL8367C_CVIDXMAX)
        return RT_ERR_VLAN_ENTRY_NOT_FOUND;

    memset(smi_vlancfg, 0x00, sizeof(rtk_uint16) * RTL8367C_VLAN_MBRCFG_LEN);
    tableAddr  = smi_vlancfg;

    for(page_idx = 0; page_idx < 4; page_idx++)  /* 4 pages per VLAN Member Config */
    {
        regAddr = RTL8367C_VLAN_MEMBER_CONFIGURATION_BASE + (index * 4) + page_idx;

        retVal = rtl8367c_getAsicReg(regAddr, &regData);
        if(retVal != RT_ERR_OK)
            return retVal;

        *tableAddr = (rtk_uint16)regData;
        tableAddr++;
    }

    _rtl8367c_VlanMCStSmi2User(smi_vlancfg, pVlanCg);
    return RT_ERR_OK;
}

static ret_t rtl8367c_setAsicVlanMemberConfig(rtk_uint32 index, rtl8367c_vlanconfiguser *pVlanCg)
{
    ret_t  retVal;
    rtk_uint32 regAddr;
    rtk_uint32 regData;
    rtk_uint16 *tableAddr;
    rtk_uint32 page_idx;
    rtk_uint16 smi_vlancfg[RTL8367C_VLAN_MBRCFG_LEN];

    /* Error Checking  */
    if(index > RTL8367C_CVIDXMAX)
        return RT_ERR_VLAN_ENTRY_NOT_FOUND;

    if(pVlanCg->evid > RTL8367C_EVIDMAX)
        return RT_ERR_INPUT;


    if(pVlanCg->mbr > RTL8367C_PORTMASK)
        return RT_ERR_PORT_MASK;

    if(pVlanCg->fid_msti > RTL8367C_FIDMAX)
        return RT_ERR_L2_FID;

    if(pVlanCg->meteridx > RTL8367C_METERMAX)
        return RT_ERR_FILTER_METER_ID;

    if(pVlanCg->vbpri > RTL8367C_PRIMAX)
        return RT_ERR_QOS_INT_PRIORITY;

    memset(smi_vlancfg, 0x00, sizeof(rtk_uint16) * RTL8367C_VLAN_MBRCFG_LEN);
    _rtl8367c_VlanMCStUser2Smi(pVlanCg, smi_vlancfg);
    tableAddr = smi_vlancfg;

    for(page_idx = 0; page_idx < 4; page_idx++)  /* 4 pages per VLAN Member Config */
    {
        regAddr = RTL8367C_VLAN_MEMBER_CONFIGURATION_BASE + (index * 4) + page_idx;
        regData = *tableAddr;

        retVal = rtl8367c_setAsicReg(regAddr, regData);
        if(retVal != RT_ERR_OK)
            return retVal;

        tableAddr++;
    }

    return RT_ERR_OK;
}

static ret_t rtl8367c_setAsicVlanEgressTagMode(rtk_uint32 port, rtl8367c_egtagmode tagMode)
{
    if(port > RTL8367C_PORTIDMAX)
        return RT_ERR_PORT_ID;

    if(tagMode >= EG_TAG_MODE_END)
        return RT_ERR_INPUT;

    return rtl8367c_setAsicRegBits(RTL8367C_PORT_MISC_CFG_REG(port), RTL8367C_VLAN_EGRESS_MODE_MASK, tagMode);
}

static ret_t rtl8367c_setAsicVlanPortBasedVID(rtk_uint32 port, rtk_uint32 index, rtk_uint32 pri)
{
    rtk_uint32 regAddr, bit_mask;
    ret_t  retVal;

    if(port > RTL8367C_PORTIDMAX)
        return RT_ERR_PORT_ID;

    if(index > RTL8367C_CVIDXMAX)
        return RT_ERR_VLAN_ENTRY_NOT_FOUND;

    if(pri > RTL8367C_PRIMAX)
        return RT_ERR_QOS_INT_PRIORITY;

    regAddr = RTL8367C_VLAN_PVID_CTRL_REG(port);
    bit_mask = RTL8367C_PORT_VIDX_MASK(port);
    retVal = rtl8367c_setAsicRegBits(regAddr, bit_mask, index);
    if(retVal != RT_ERR_OK)
        return retVal;

    regAddr = RTL8367C_VLAN_PORTBASED_PRIORITY_REG(port);
    bit_mask = RTL8367C_VLAN_PORTBASED_PRIORITY_MASK(port);
    retVal = rtl8367c_setAsicRegBits(regAddr, bit_mask, pri);
    if(retVal != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static ret_t rtl8367c_setAsicVlanIngressFilter(rtk_uint32 port, rtk_uint32 enabled)
{
    if(port > RTL8367C_PORTIDMAX)
        return RT_ERR_PORT_ID;

    return rtl8367c_setAsicRegBit(RTL8367C_VLAN_INGRESS_REG, port, enabled);
}

static ret_t rtl8367c_setAsicVlanFilter(rtk_uint32 enabled)
{
    return rtl8367c_setAsicRegBit(RTL8367C_REG_VLAN_CTRL, RTL8367C_VLAN_CTRL_OFFSET, enabled);
}

//============================================= RTK functions ==========================================================

static rtk_api_ret_t rtk_port_macForceLinkExt_set(rtk_port_t port, rtk_mode_ext_t mode, rtk_port_mac_ability_t *pPortability)
{
    rtk_api_ret_t retVal;
    rtl8367c_port_ability_t ability;
    rtk_uint32 ext_id;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Check Port Valid */
    RTK_CHK_PORT_IS_EXT(port);

    if(NULL == pPortability)
        return RT_ERR_NULL_POINTER;

    if (mode >=MODE_EXT_END)
        return RT_ERR_INPUT;

    if(mode == MODE_EXT_HSGMII)
    {
        if (pPortability->forcemode > 1 || pPortability->speed != PORT_SPEED_2500M || pPortability->duplex != PORT_FULL_DUPLEX ||
           pPortability->link >= PORT_LINKSTATUS_END || pPortability->nway > 1 || pPortability->txpause > 1 || pPortability->rxpause > 1)
            return RT_ERR_INPUT;

        if(rtk_switch_isHsgPort(port) != RT_ERR_OK)
            return RT_ERR_PORT_ID;
    }
    else
    {
        if (pPortability->forcemode > 1 || pPortability->speed > PORT_SPEED_1000M || pPortability->duplex >= PORT_DUPLEX_END ||
           pPortability->link >= PORT_LINKSTATUS_END || pPortability->nway > 1 || pPortability->txpause > 1 || pPortability->rxpause > 1)
            return RT_ERR_INPUT;
    }

    ext_id = port - 15;

    if(mode == MODE_EXT_DISABLE)
    {
        memset(&ability, 0x00, sizeof(rtl8367c_port_ability_t));
        if ((retVal = rtl8367c_setAsicPortForceLinkExt(ext_id, &ability)) != RT_ERR_OK)
            return retVal;

        if ((retVal = rtl8367c_setAsicPortExtMode(ext_id, mode)) != RT_ERR_OK)
            return retVal;
    }
    else
    {
        if ((retVal = rtl8367c_setAsicPortExtMode(ext_id, mode)) != RT_ERR_OK)
            return retVal;

        if ((retVal = rtl8367c_getAsicPortForceLinkExt(ext_id, &ability)) != RT_ERR_OK)
            return retVal;

        ability.forcemode = pPortability->forcemode;
        ability.speed     = (mode == MODE_EXT_HSGMII) ? PORT_SPEED_1000M : pPortability->speed;
        ability.duplex    = pPortability->duplex;
        ability.link      = pPortability->link;
        ability.nway      = pPortability->nway;
        ability.txpause   = pPortability->txpause;
        ability.rxpause   = pPortability->rxpause;

        if ((retVal = rtl8367c_setAsicPortForceLinkExt(ext_id, &ability)) != RT_ERR_OK)
            return retVal;
    }

    return RT_ERR_OK;
}

/* rtk_port_phyReg_{get,set}  Get/Set PHY register data of the specific port. */
static rtk_api_ret_t rtk_port_phyReg_set(rtk_port_t port, rtk_port_phy_reg_t reg, rtk_port_phy_data_t regData)
{
    rtk_api_ret_t retVal;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Check Port Valid */
    RTK_CHK_PORT_IS_UTP(port);

    if ((retVal = rtl8367c_setAsicPHYReg(rtk_switch_port_L2P_get(port), reg, regData)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_port_phyReg_get(rtk_port_t port, rtk_port_phy_reg_t reg, rtk_port_phy_data_t *pData)
{
    rtk_api_ret_t retVal;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Check Port Valid */
    RTK_CHK_PORT_IS_UTP(port);

    if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), reg, pData)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_port_phyEnableAll_set(rtk_enable_t enable)
{
    rtk_api_ret_t retVal;
    rtk_uint32 data;
    rtk_uint32 port;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    if (enable >= RTK_ENABLE_END)
        return RT_ERR_ENABLE;

    if ((retVal = rtl8367c_setAsicPortEnableAll(enable)) != RT_ERR_OK)
        return retVal;

    RTK_SCAN_ALL_LOG_PORT(port)
    {
        if(rtk_switch_isUtpPort(port) == RT_ERR_OK)
        {
            if ((retVal = rtk_port_phyReg_get(port, PHY_CONTROL_REG, &data)) != RT_ERR_OK)
                return retVal;

            if (ENABLED == enable)
            {
                data &= 0xF7FF;
                data |= 0x0200;
            }
            else
            {
                data |= 0x0800;
            }

            if ((retVal = rtk_port_phyReg_set(port, PHY_CONTROL_REG, data)) != RT_ERR_OK)
                return retVal;
        }
    }

    return RT_ERR_OK;

}

static rtk_api_ret_t rtk_port_rgmiiDelayExt_set(rtk_port_t port, rtk_data_t txDelay, rtk_data_t rxDelay)
{
    rtk_api_ret_t retVal;
    rtk_uint32 regAddr, regData;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Check Port Valid */
    RTK_CHK_PORT_IS_EXT(port);

    if ((txDelay > 1) || (rxDelay > 7))
        return RT_ERR_INPUT;

    if(port == EXT_PORT0)
        regAddr = RTL8367C_REG_EXT1_RGMXF;
    else if(port == EXT_PORT1)
        regAddr = RTL8367C_REG_EXT2_RGMXF;
    else
        return RT_ERR_INPUT;

    if ((retVal = rtl8367c_getAsicReg(regAddr, &regData)) != RT_ERR_OK)
        return retVal;

    regData = (regData & 0xFFF0) | ((txDelay << 3) & 0x0008) | (rxDelay & 0x0007);

    if ((retVal = rtl8367c_setAsicReg(regAddr, regData)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static ret_t rtl8367c_setSgmiiNway(rtk_uint32 ext_id, rtk_uint32 state)
{
    rtk_uint32 retVal, regValue, type, running = 0, retVal2;

    if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, RTL8367C_RTL_MAGIC_ID_VAL)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_getAsicReg(RTL8367C_CHIP_NUMBER_REG, &regValue)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, 0x0000)) != RT_ERR_OK)
        return retVal;

    switch (regValue)
    {
        case 0x0276:
        case 0x0597:
        case 0x6367:
            type = 0;
            break;
        case 0x0652:
        case 0x6368:
            type = 1;
            break;
        case 0x0801:
        case 0x6511:
            type = 2;
            break;
        default:
            return RT_ERR_FAILED;
    }

    if(type == 0)
    {
        if (1 == ext_id)
        {
            if ((retVal = rtl8367c_getAsicRegBit(0x130c, 5, &running))!=RT_ERR_OK)
                return retVal;

            if(running == 1)
            {
                if ((retVal = rtl8367c_setAsicRegBit(0x130c, 5, 0))!=RT_ERR_OK)
                    return retVal;
            }

            retVal = rtl8367c_setAsicReg(0x6601, 0x0002);

            if(retVal == RT_ERR_OK)
                retVal = rtl8367c_setAsicReg(0x6600, 0x0080);

            if(retVal == RT_ERR_OK)
                retVal = rtl8367c_getAsicReg(0x6602, &regValue);

            if(retVal == RT_ERR_OK)
            {
                if(state)
                      regValue |= 0x0200;
                else
                      regValue &= ~0x0200;

                regValue |= 0x0100;
            }

            if(retVal == RT_ERR_OK)
                retVal = rtl8367c_setAsicReg(0x6602, regValue);

            if(retVal == RT_ERR_OK)
                retVal = rtl8367c_setAsicReg(0x6601, 0x0002);

            if(retVal == RT_ERR_OK)
                retVal = rtl8367c_setAsicReg(0x6600, 0x00C0);

            if(running == 1)
            {
                if ((retVal2 = rtl8367c_setAsicRegBit(0x130c, 5, 1))!=RT_ERR_OK)
                    return retVal2;
            }

            if(retVal != RT_ERR_OK)
                return retVal;
        }
        else
            return RT_ERR_PORT_ID;
    }
    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_port_sgmiiNway_set(rtk_port_t port, rtk_enable_t state)
{
    rtk_uint32 ext_id;

     /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Check Port Valid */
    if(rtk_switch_isSgmiiPort(port) != RT_ERR_OK)
        return RT_ERR_PORT_ID;

    if(state >= RTK_ENABLE_END)
        return RT_ERR_INPUT;

    ext_id = port - 15;
    return rtl8367c_setSgmiiNway(ext_id, (rtk_uint32)state);
}


// ============================================ RTK VLAN functions ==========================================================
static ret_t rtl8367c_resetVlan(struct rtk_gsw *gsw)
{
    ret_t   retVal;

    if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_VLAN_EXT_CTRL2, RTL8367C_VLAN_EXT_CTRL2_OFFSET, 1)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_vlan_reset(struct rtk_gsw *gsw)
{
    rtk_api_ret_t retVal;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    if ((retVal = rtl8367c_resetVlan(gsw)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_vlan_init(struct rtk_gsw *gsw)
{
    rtk_api_ret_t retVal;
    rtk_uint32 i;
    rtl8367c_user_vlan4kentry vlan4K;
    rtl8367c_vlanconfiguser vlanMC;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Clean Database */
    memset(vlan_mbrCfgVid, 0x00, sizeof(rtk_vlan_t) * RTL8367C_CVIDXNO);
    memset(vlan_mbrCfgUsage, 0x00, sizeof(vlan_mbrCfgType_t) * RTL8367C_CVIDXNO);

    /* clean 32 VLAN member configuration */
    for (i = 0; i <= RTL8367C_CVIDXMAX; i++)
    {
        vlanMC.evid = 0;
        vlanMC.mbr = 0;
        vlanMC.fid_msti = 0;
        vlanMC.envlanpol = 0;
        vlanMC.meteridx = 0;
        vlanMC.vbpen = 0;
        vlanMC.vbpri = 0;
        if ((retVal = rtl8367c_setAsicVlanMemberConfig(i, &vlanMC)) != RT_ERR_OK)
            return retVal;
    }

    /* Set a default VLAN with vid 1 to 4K table for all ports */
    memset(&vlan4K, 0, sizeof(rtl8367c_user_vlan4kentry));
    vlan4K.vid = 1;
    vlan4K.mbr = RTK_PHY_PORTMASK_ALL;
    vlan4K.untag = RTK_PHY_PORTMASK_ALL;
    vlan4K.fid_msti = 0;
    if ((retVal = rtl8367c_setAsicVlan4kEntry(&vlan4K)) != RT_ERR_OK)
        return retVal;

    /* Also set the default VLAN to 32 member configuration index 0 */
    memset(&vlanMC, 0, sizeof(rtl8367c_vlanconfiguser));
    vlanMC.evid = 1;
    vlanMC.mbr = RTK_PHY_PORTMASK_ALL;
    vlanMC.fid_msti = 0;
    if ((retVal = rtl8367c_setAsicVlanMemberConfig(0, &vlanMC)) != RT_ERR_OK)
            return retVal;

    /* Set all ports PVID to default VLAN and tag-mode to original */
    RTK_SCAN_ALL_PHY_PORTMASK(i)
    {
        if ((retVal = rtl8367c_setAsicVlanPortBasedVID(i, 0, 0)) != RT_ERR_OK)
            return retVal;
        if ((retVal = rtl8367c_setAsicVlanEgressTagMode(i, EG_TAG_MODE_ORI)) != RT_ERR_OK)
            return retVal;
    }

    /* Updata Databse */
    vlan_mbrCfgUsage[0] = MBRCFG_USED_BY_VLAN;
    vlan_mbrCfgVid[0] = 1;

    /* Enable Ingress filter */
    RTK_SCAN_ALL_PHY_PORTMASK(i)
    {
        if ((retVal = rtl8367c_setAsicVlanIngressFilter(i, ENABLED)) != RT_ERR_OK)
            return retVal;
    }

    /* enable VLAN */
    if ((retVal = rtl8367c_setAsicVlanFilter(ENABLED)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_vlan_checkAndCreateMbr(rtk_vlan_t vid, rtk_uint32 *pIndex)
{
    rtk_api_ret_t retVal;
    rtl8367c_user_vlan4kentry vlan4K;
    rtl8367c_vlanconfiguser vlanMC;
    rtk_uint32 idx;
    rtk_uint32 empty_idx = 0xFFFF;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* vid must be 0~8191 */
    if (vid > RTL8367C_EVIDMAX)
        return RT_ERR_VLAN_VID;

    /* Null pointer check */
    if(NULL == pIndex)
        return RT_ERR_NULL_POINTER;

    /* Get 4K VLAN */
    if (vid <= RTL8367C_VIDMAX)
    {
        memset(&vlan4K, 0x00, sizeof(rtl8367c_user_vlan4kentry));
        vlan4K.vid = vid;
        if ((retVal = rtl8367c_getAsicVlan4kEntry(&vlan4K)) != RT_ERR_OK)
            return retVal;
    }

    /* Search exist entry */
    for (idx = 0; idx <= RTL8367C_CVIDXMAX; idx++)
    {
        if(vlan_mbrCfgUsage[idx] == MBRCFG_USED_BY_VLAN)
        {
            if(vlan_mbrCfgVid[idx] == vid)
            {
                /* Found! return index */
                *pIndex = idx;
                return RT_ERR_OK;
            }
        }
    }

    /* Not found, Read H/W Member Configuration table to update database */
    for (idx = 0; idx <= RTL8367C_CVIDXMAX; idx++)
    {
        if ((retVal = rtl8367c_getAsicVlanMemberConfig(idx, &vlanMC)) != RT_ERR_OK)
            return retVal;

        if( (vlanMC.evid == 0) && (vlanMC.mbr == 0x00))
        {
            vlan_mbrCfgUsage[idx]   = MBRCFG_UNUSED;
            vlan_mbrCfgVid[idx]     = 0;
        }
        else
        {
            vlan_mbrCfgUsage[idx]   = MBRCFG_USED_BY_VLAN;
            vlan_mbrCfgVid[idx]     = vlanMC.evid;
        }
    }

    /* Search exist entry again */
    for (idx = 0; idx <= RTL8367C_CVIDXMAX; idx++)
    {
        if(vlan_mbrCfgUsage[idx] == MBRCFG_USED_BY_VLAN)
        {
            if(vlan_mbrCfgVid[idx] == vid)
            {
                /* Found! return index */
                *pIndex = idx;
                return RT_ERR_OK;
            }
        }
    }

    /* try to look up an empty index */
    for (idx = 0; idx <= RTL8367C_CVIDXMAX; idx++)
    {
        if(vlan_mbrCfgUsage[idx] == MBRCFG_UNUSED)
        {
            empty_idx = idx;
            break;
        }
    }

    if(empty_idx == 0xFFFF)
    {
        /* No empty index */
        return RT_ERR_TBL_FULL;
    }

    if (vid > RTL8367C_VIDMAX)
    {
        /* > 4K, there is no 4K entry, create on member configuration directly */
        memset(&vlanMC, 0x00, sizeof(rtl8367c_vlanconfiguser));
        vlanMC.evid = vid;
        if ((retVal = rtl8367c_setAsicVlanMemberConfig(empty_idx, &vlanMC)) != RT_ERR_OK)
            return retVal;
    }
    else
    {
        /* Copy from 4K table */
        vlanMC.evid = vid;
        vlanMC.mbr = vlan4K.mbr;
        vlanMC.fid_msti = vlan4K.fid_msti;
        vlanMC.meteridx= vlan4K.meteridx;
        vlanMC.envlanpol= vlan4K.envlanpol;
        vlanMC.vbpen = vlan4K.vbpen;
        vlanMC.vbpri = vlan4K.vbpri;
        if ((retVal = rtl8367c_setAsicVlanMemberConfig(empty_idx, &vlanMC)) != RT_ERR_OK)
            return retVal;
    }

    /* Update Database */
    vlan_mbrCfgUsage[empty_idx] = MBRCFG_USED_BY_VLAN;
    vlan_mbrCfgVid[empty_idx] = vid;

    *pIndex = empty_idx;
    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_vlan_portPvid_set(rtk_port_t port, rtk_vlan_t pvid, rtk_pri_t priority)
{
    rtk_api_ret_t retVal;
    rtk_uint32 index;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Check Port Valid */
    RTK_CHK_PORT_VALID(port);

    /* vid must be 0~8191 */
    if (pvid > RTL8367C_EVIDMAX)
        return RT_ERR_VLAN_VID;

    /* priority must be 0~7 */
    if (priority > RTL8367C_PRIMAX)
        return RT_ERR_VLAN_PRIORITY;

    if((retVal = rtk_vlan_checkAndCreateMbr(pvid, &index)) != RT_ERR_OK)
        return retVal;

    if ((retVal = rtl8367c_setAsicVlanPortBasedVID(rtk_switch_port_L2P_get(port), index, priority)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_vlan_set(rtk_vlan_t vid, rtk_vlan_cfg_t *pVlanCfg)
{
    rtk_api_ret_t retVal;
    rtk_uint32 phyMbrPmask;
    rtk_uint32 phyUntagPmask;
    rtl8367c_user_vlan4kentry vlan4K;
    rtl8367c_vlanconfiguser vlanMC;
    rtk_uint32 idx;
    rtk_uint32 empty_index = 0xffff;
    rtk_uint32 update_evid = 0;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* vid must be 0~8191 */
    if (vid > RTL8367C_EVIDMAX)
        return RT_ERR_VLAN_VID;

    /* Null pointer check */
    if(NULL == pVlanCfg)
        return RT_ERR_NULL_POINTER;

    /* Check port mask valid */
    RTK_CHK_PORTMASK_VALID(&(pVlanCfg->mbr));

    if (vid <= RTL8367C_VIDMAX)
    {
        /* Check untag port mask valid */
        RTK_CHK_PORTMASK_VALID(&(pVlanCfg->untag));
    }

    /* IVL_EN */
    if(pVlanCfg->ivl_en >= RTK_ENABLE_END)
        return RT_ERR_ENABLE;

    /* fid must be 0~15 */
    if(pVlanCfg->fid_msti > RTL8367C_FIDMAX)
        return RT_ERR_L2_FID;

    /* Policing */
    if(pVlanCfg->envlanpol >= RTK_ENABLE_END)
        return RT_ERR_ENABLE;

    /* Meter ID */
    if(pVlanCfg->meteridx > RTK_MAX_METER_ID)
        return RT_ERR_INPUT;

    /* VLAN based priority */
    if(pVlanCfg->vbpen >= RTK_ENABLE_END)
        return RT_ERR_ENABLE;

    /* Priority */
    if(pVlanCfg->vbpri > RTL8367C_PRIMAX)
        return RT_ERR_INPUT;

    /* Get physical port mask */
    if(rtk_switch_portmask_L2P_get(&(pVlanCfg->mbr), &phyMbrPmask) != RT_ERR_OK)
        return RT_ERR_FAILED;

    if(rtk_switch_portmask_L2P_get(&(pVlanCfg->untag), &phyUntagPmask) != RT_ERR_OK)
        return RT_ERR_FAILED;

    if (vid <= RTL8367C_VIDMAX)
    {
        /* update 4K table */
        memset(&vlan4K, 0, sizeof(rtl8367c_user_vlan4kentry));
        vlan4K.vid = vid;

        vlan4K.mbr    = (phyMbrPmask & 0xFFFF);
        vlan4K.untag  = (phyUntagPmask & 0xFFFF);

        vlan4K.ivl_svl      = pVlanCfg->ivl_en;
        vlan4K.fid_msti     = pVlanCfg->fid_msti;
        vlan4K.envlanpol    = pVlanCfg->envlanpol;
        vlan4K.meteridx     = pVlanCfg->meteridx;
        vlan4K.vbpen        = pVlanCfg->vbpen;
        vlan4K.vbpri        = pVlanCfg->vbpri;

        if ((retVal = rtl8367c_setAsicVlan4kEntry(&vlan4K)) != RT_ERR_OK)
            return retVal;

        /* Update Member configuration if exist */
        for (idx = 0; idx <= RTL8367C_CVIDXMAX; idx++)
        {
            if(vlan_mbrCfgUsage[idx] == MBRCFG_USED_BY_VLAN)
            {
                if(vlan_mbrCfgVid[idx] == vid)
                {
                    /* Found! Update */
                    if(phyMbrPmask == 0x00)
                    {
                        /* Member port = 0x00, delete this VLAN from Member Configuration */
                        memset(&vlanMC, 0x00, sizeof(rtl8367c_vlanconfiguser));
                        if ((retVal = rtl8367c_setAsicVlanMemberConfig(idx, &vlanMC)) != RT_ERR_OK)
                            return retVal;

                        /* Clear Database */
                        vlan_mbrCfgUsage[idx] = MBRCFG_UNUSED;
                        vlan_mbrCfgVid[idx]   = 0;
                    }
                    else
                    {
                        /* Normal VLAN config, update to member configuration */
                        vlanMC.evid = vid;
                        vlanMC.mbr = vlan4K.mbr;
                        vlanMC.fid_msti = vlan4K.fid_msti;
                        vlanMC.meteridx = vlan4K.meteridx;
                        vlanMC.envlanpol= vlan4K.envlanpol;
                        vlanMC.vbpen = vlan4K.vbpen;
                        vlanMC.vbpri = vlan4K.vbpri;
                        if ((retVal = rtl8367c_setAsicVlanMemberConfig(idx, &vlanMC)) != RT_ERR_OK)
                            return retVal;
                    }

                    break;
                }
            }
        }
    }
    else
    {
        /* vid > 4095 */
        for (idx = 0; idx <= RTL8367C_CVIDXMAX; idx++)
        {
            if(vlan_mbrCfgUsage[idx] == MBRCFG_USED_BY_VLAN)
            {
                if(vlan_mbrCfgVid[idx] == vid)
                {
                    /* Found! Update */
                    if(phyMbrPmask == 0x00)
                    {
                        /* Member port = 0x00, delete this VLAN from Member Configuration */
                        memset(&vlanMC, 0x00, sizeof(rtl8367c_vlanconfiguser));
                        if ((retVal = rtl8367c_setAsicVlanMemberConfig(idx, &vlanMC)) != RT_ERR_OK)
                            return retVal;

                        /* Clear Database */
                        vlan_mbrCfgUsage[idx] = MBRCFG_UNUSED;
                        vlan_mbrCfgVid[idx]   = 0;
                    }
                    else
                    {
                        /* Normal VLAN config, update to member configuration */
                        vlanMC.evid = vid;
                        vlanMC.mbr = phyMbrPmask;
                        vlanMC.fid_msti = pVlanCfg->fid_msti;
                        vlanMC.meteridx = pVlanCfg->meteridx;
                        vlanMC.envlanpol= pVlanCfg->envlanpol;
                        vlanMC.vbpen = pVlanCfg->vbpen;
                        vlanMC.vbpri = pVlanCfg->vbpri;
                        if ((retVal = rtl8367c_setAsicVlanMemberConfig(idx, &vlanMC)) != RT_ERR_OK)
                            return retVal;

                        break;
                    }

                    update_evid = 1;
                }
            }

            if(vlan_mbrCfgUsage[idx] == MBRCFG_UNUSED)
            {
                if(0xffff == empty_index)
                    empty_index = idx;
            }
        }

        /* doesn't find out same EVID entry and there is empty index in member configuration */
        if( (phyMbrPmask != 0x00) && (update_evid == 0) && (empty_index != 0xFFFF) )
        {
            vlanMC.evid = vid;
            vlanMC.mbr = phyMbrPmask;
            vlanMC.fid_msti = pVlanCfg->fid_msti;
            vlanMC.meteridx = pVlanCfg->meteridx;
            vlanMC.envlanpol= pVlanCfg->envlanpol;
            vlanMC.vbpen = pVlanCfg->vbpen;
            vlanMC.vbpri = pVlanCfg->vbpri;
            if ((retVal = rtl8367c_setAsicVlanMemberConfig(empty_index, &vlanMC)) != RT_ERR_OK)
                return retVal;

            vlan_mbrCfgUsage[empty_index] = MBRCFG_USED_BY_VLAN;
            vlan_mbrCfgVid[empty_index] = vid;

        }
    }

    return RT_ERR_OK;
}

static int rtl8367s_vlan_config(int want_at_p0)
{
	rtk_vlan_cfg_t vlan1, vlan2;

	/* Set LAN/WAN VLAN partition */
	memset(&vlan1, 0x00, sizeof(rtk_vlan_cfg_t));

	RTK_PORTMASK_PORT_SET(vlan1.mbr, EXT_PORT0);
	RTK_PORTMASK_PORT_SET(vlan1.mbr, UTP_PORT1);
	RTK_PORTMASK_PORT_SET(vlan1.mbr, UTP_PORT2);
	RTK_PORTMASK_PORT_SET(vlan1.mbr, UTP_PORT3);
	RTK_PORTMASK_PORT_SET(vlan1.untag, EXT_PORT0);
	RTK_PORTMASK_PORT_SET(vlan1.untag, UTP_PORT1);
	RTK_PORTMASK_PORT_SET(vlan1.untag, UTP_PORT2);
	RTK_PORTMASK_PORT_SET(vlan1.untag, UTP_PORT3);

	if (want_at_p0) {
		RTK_PORTMASK_PORT_SET(vlan1.mbr, UTP_PORT4);
		RTK_PORTMASK_PORT_SET(vlan1.untag, UTP_PORT4);
	} else {
		RTK_PORTMASK_PORT_SET(vlan1.mbr, UTP_PORT0);
		RTK_PORTMASK_PORT_SET(vlan1.untag, UTP_PORT0);
	}

	vlan1.ivl_en = 1;

	rtk_vlan_set(1, &vlan1);

	memset(&vlan2, 0x00, sizeof(rtk_vlan_cfg_t));

	RTK_PORTMASK_PORT_SET(vlan2.mbr, EXT_PORT1);
	RTK_PORTMASK_PORT_SET(vlan2.untag, EXT_PORT1);

	if (want_at_p0) {
		RTK_PORTMASK_PORT_SET(vlan2.mbr, UTP_PORT0);
		RTK_PORTMASK_PORT_SET(vlan2.untag, UTP_PORT0);
	} else {
		RTK_PORTMASK_PORT_SET(vlan2.mbr, UTP_PORT4);
		RTK_PORTMASK_PORT_SET(vlan2.untag, UTP_PORT4);
	}

	vlan2.ivl_en = 1;
	rtk_vlan_set(2, &vlan2);

	rtk_vlan_portPvid_set(EXT_PORT0, 1, 0);
	rtk_vlan_portPvid_set(UTP_PORT1, 1, 0);
	rtk_vlan_portPvid_set(UTP_PORT2, 1, 0);
	rtk_vlan_portPvid_set(UTP_PORT3, 1, 0);
	rtk_vlan_portPvid_set(EXT_PORT1, 2, 0);

	if (want_at_p0) {
		rtk_vlan_portPvid_set(UTP_PORT0, 2, 0);
		rtk_vlan_portPvid_set(UTP_PORT4, 1, 0);
	} else {
		rtk_vlan_portPvid_set(UTP_PORT0, 1, 0);
		rtk_vlan_portPvid_set(UTP_PORT4, 2, 0);
	}

	return 0;
}

//====================== Init functions ===============================================
static rtk_api_ret_t _rtk_switch_init_8367c(void)
{
    rtk_port_t port;
    rtk_uint32 retVal;
    rtk_uint32 regData;
    rtk_uint32 regValue;

    if( (retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, RTL8367C_RTL_MAGIC_ID_VAL)) != RT_ERR_OK)
        return retVal;

    if( (retVal = rtl8367c_getAsicReg(RTL8367C_CHIP_VER_REG, &regValue)) != RT_ERR_OK)
        return retVal;

    if( (retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, 0x0000)) != RT_ERR_OK)
        return retVal;

    RTK_SCAN_ALL_LOG_PORT(port)
    {
         if(rtk_switch_isUtpPort(port) == RT_ERR_OK)
         {
             /*if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_PORT0_EEECFG + (0x20 * port), RTL8367C_PORT0_EEECFG_EEE_100M_OFFSET, 1)) != RT_ERR_OK)
                 return retVal;

             if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_PORT0_EEECFG + (0x20 * port), RTL8367C_PORT0_EEECFG_EEE_GIGA_500M_OFFSET, 1)) != RT_ERR_OK)
                 return retVal;

             if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_PORT0_EEECFG + (0x20 * port), RTL8367C_PORT0_EEECFG_EEE_TX_OFFSET, 1)) != RT_ERR_OK)
                 return retVal;

             if((retVal = rtl8367c_setAsicRegBit(RTL8367C_REG_PORT0_EEECFG + (0x20 * port), RTL8367C_PORT0_EEECFG_EEE_RX_OFFSET, 1)) != RT_ERR_OK)
                 return retVal;*/

             if((retVal = rtl8367c_getAsicPHYOCPReg(port, 0xA428, &regData)) != RT_ERR_OK)
                return retVal;

             regData &= ~(0x0200);
             if((retVal = rtl8367c_setAsicPHYOCPReg(port, 0xA428, regData)) != RT_ERR_OK)
                 return retVal;

             if((regValue & 0x00F0) == 0x00A0)
             {
                 if((retVal = rtl8367c_getAsicPHYOCPReg(port, 0xA5D0, &regData)) != RT_ERR_OK)
                     return retVal;

                 regData |= 0x0006;
                 if((retVal = rtl8367c_setAsicPHYOCPReg(port, 0xA5D0, regData)) != RT_ERR_OK)
                     return retVal;
             }
         }
    }

    if((retVal = rtl8367c_setAsicReg(RTL8367C_REG_UTP_FIB_DET, 0x15BB)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x1303, 0x06D6)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x1304, 0x0700)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x13E2, 0x003F)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x13F9, 0x0090)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x121e, 0x03CA)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x1233, 0x0352)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x1237, 0x00a0)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x123a, 0x0030)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x1239, 0x0084)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x0301, 0x1000)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicReg(0x1349, 0x001F)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicRegBit(0x18e0, 0, 0)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicRegBit(0x122b, 14, 1)) != RT_ERR_OK)
        return retVal;

    if((retVal = rtl8367c_setAsicRegBits(0x1305, 0xC000, 3)) != RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_switch_probe(switch_chip_t *pSwitchChip)
{
	rtk_uint32 retVal;
	rtk_uint32 data, regValue;

	if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, RTL8367C_RTL_MAGIC_ID_VAL)) != RT_ERR_OK)
		return retVal;

	if((retVal = rtl8367c_getAsicReg(RTL8367C_CHIP_NUMBER_REG, &data)) != RT_ERR_OK)
		return retVal;

	if((retVal = rtl8367c_getAsicReg(RTL8367C_CHIP_VER_REG, &regValue)) != RT_ERR_OK)
		return retVal;

	if((retVal = rtl8367c_setAsicReg(RTL8367C_RTL_MAGIC_ID_REG, 0x0000)) != RT_ERR_OK)
		return retVal;

	printk(KERN_ALERT "DEBUG: Passed %s %d chip: 0x%x\n",__FUNCTION__,__LINE__,data); //0x6367 on r64 v0.1

	switch (data)
	{
		case 0x0276:
		case 0x0597:
		case 0x6367:
			*pSwitchChip = CHIP_RTL8367C;
			halCtrl = &rtl8367c_hal_Ctrl;
			break;
		case 0x0652:
		case 0x6368:
			*pSwitchChip = CHIP_RTL8370B;
			break;
		case 0x0801:
		case 0x6511:
			if( (regValue & 0x00F0) == 0x0080)
			{
				*pSwitchChip = CHIP_RTL8363SC_VB;
			}
			else
			{
				*pSwitchChip = CHIP_RTL8364B;
			}
			break;
		default:
			return RT_ERR_FAILED;
	}

	return RT_ERR_OK;
}

static rtk_api_ret_t rtk_switch_initialState_set(init_state_t state)
{
    if(state >= INIT_STATE_END)
        return RT_ERR_FAILED;

    init_state = state;
    return RT_ERR_OK;
}

static rtk_api_ret_t rtk_switch_init(struct rtk_gsw *gsw)
{
	rtk_uint32  retVal;
	//rtl8367c_rma_t rmaCfg;
	switch_chip_t   switchChip;

	/* probe switch */
	if ((retVal = rtk_switch_probe(&switchChip)) != RT_ERR_OK)
		return retVal;

	/* Set initial state */
	if((retVal = rtk_switch_initialState_set(INIT_COMPLETED)) != RT_ERR_OK)
		return retVal;

	/* Initial */
	switch(switchChip)
	{
		case CHIP_RTL8367C:
			if((retVal = _rtk_switch_init_8367c()) != RT_ERR_OK)
				return retVal;
			break;
		default:
			return RT_ERR_CHIP_NOT_FOUND;
	}

	//some more missing here

	return 0;
}

static int rtl8367s_hw_reset(struct rtk_gsw *gsw)
{
	//struct rtk_gsw *gsw = _gsw;
	int ret;

	ret = devm_gpio_request(gsw->dev, gsw->reset_pin, "realtek,reset-pin");

	if (ret)
                printk("fail to devm_gpio_request\n");

	gpio_direction_output(gsw->reset_pin, 0);

	//usleep_range(1000, 1100); //r64
	usleep_range(100000, 110000); //r2pro

	printk(KERN_ALERT "DEBUG: Passed %s %d do reset!\n",__FUNCTION__,__LINE__);
	gpio_set_value(gsw->reset_pin, 1);

	mdelay(500);

	devm_gpio_free(gsw->dev, gsw->reset_pin);

	return 0;
}


// =================================================== RTL8367s init functions ====================================================
static int rtl8367s_hw_init(struct rtk_gsw *gsw)
{
	rtl8367s_hw_reset(gsw);

	if(rtk_switch_init(gsw))
		return -1;

	mdelay(500);

	if (rtk_vlan_reset(gsw))
		return -1;

	if (rtk_vlan_init(gsw))
		return -1;

	return 0;
}

static void set_rtl8367s_sgmii(struct rtk_gsw *gsw)
{
	rtk_port_mac_ability_t mac_cfg;
	rtk_mode_ext_t mode;

	mode = MODE_EXT_HSGMII;
	mac_cfg.forcemode = MAC_FORCE;
	mac_cfg.speed = PORT_SPEED_2500M;
	mac_cfg.duplex = PORT_FULL_DUPLEX;
	mac_cfg.link = PORT_LINKUP;
	mac_cfg.nway = DISABLED;
	mac_cfg.txpause = ENABLED;
	mac_cfg.rxpause = ENABLED;
	rtk_port_macForceLinkExt_set(EXT_PORT0, mode, &mac_cfg);
	rtk_port_sgmiiNway_set(EXT_PORT0, DISABLED);
	rtk_port_phyEnableAll_set(ENABLED);
}

static void set_rtl8367s_rgmii(struct rtk_gsw *gsw)
{
	rtk_port_mac_ability_t mac_cfg;
	rtk_mode_ext_t mode;

	mode = MODE_EXT_RGMII;
	mac_cfg.forcemode = MAC_FORCE;
	mac_cfg.speed = PORT_SPEED_1000M;
	mac_cfg.duplex = PORT_FULL_DUPLEX;
	mac_cfg.link = PORT_LINKUP;
	mac_cfg.nway = DISABLED;
	mac_cfg.txpause = ENABLED;
	mac_cfg.rxpause = ENABLED;
	rtk_port_macForceLinkExt_set(EXT_PORT1, mode, &mac_cfg);
	rtk_port_rgmiiDelayExt_set(EXT_PORT1, 1, 3);
	rtk_port_phyEnableAll_set(ENABLED);
}

static void set_rtl8367s_rgmii_ep0(struct rtk_gsw *gsw)
{
	rtk_port_mac_ability_t mac_cfg;
	rtk_mode_ext_t mode;

	mode = MODE_EXT_RGMII;
	mac_cfg.forcemode = MAC_FORCE;
	mac_cfg.speed = PORT_SPEED_1000M;
	mac_cfg.duplex = PORT_FULL_DUPLEX;
	mac_cfg.link = PORT_LINKUP;
	mac_cfg.nway = DISABLED;
	mac_cfg.txpause = ENABLED;
	mac_cfg.rxpause = ENABLED;
	rtk_port_macForceLinkExt_set(EXT_PORT0, mode, &mac_cfg);
	/* rtk_port_rgmiiDelayExt_set(EXT_PORT0, 1, 3); */
	rtk_port_rgmiiDelayExt_set(EXT_PORT0, 0, 0);
	rtk_port_phyEnableAll_set(ENABLED);
}

/*static void set_rtl8367s_rgmii_ep1(struct rtk_gsw *gsw)
{
	rtk_port_mac_ability_t mac_cfg;
	rtk_mode_ext_t mode;

	mode = MODE_EXT_RGMII;
	mac_cfg.forcemode = MAC_FORCE;
	mac_cfg.speed = PORT_SPEED_1000M;
	mac_cfg.duplex = PORT_FULL_DUPLEX;
	mac_cfg.link = PORT_LINKUP;
	mac_cfg.nway = DISABLED;
	mac_cfg.txpause = ENABLED;
	mac_cfg.rxpause = ENABLED;
	rtk_port_macForceLinkExt_set(EXT_PORT1, mode, &mac_cfg);
	// rtk_port_rgmiiDelayExt_set(EXT_PORT1, 1, 3);
	rtk_port_rgmiiDelayExt_set(EXT_PORT1, 0, 0);
	rtk_port_phyEnableAll_set(ENABLED);
}*/

static void init_gsw(struct rtk_gsw *gsw)
{
	int ret;
	const char* pm;

	if (rtl8367s_hw_init(gsw))
		printk("RTL8367RB Switch Init Failure !!!\n");
	else
		printk("RTL8367RB Switch Init Successfully !!!\n");

	ret=of_property_read_string(gsw->dev->of_node,"realtek,extifconfig", &pm);
	if (!ret && (!strcasecmp(pm, "rgmii,rgmii")))
	{
		//r2pro
		printk(KERN_ALERT "DEBUG: Passed %s %d rgmii+rgmii\n",__FUNCTION__,__LINE__);
		set_rtl8367s_rgmii_ep0(gsw);
		//set_rtl8367s_rgmii_ep1(gsw);
	} else
	{
		//r64v0.1
		printk(KERN_ALERT "DEBUG: Passed %s %d sgmii+rgmii \n",__FUNCTION__,__LINE__);
		set_rtl8367s_sgmii(gsw);
		set_rtl8367s_rgmii(gsw);
	}
}

// bleow are platform driver
static const struct of_device_id rtk_gsw_match[] = {
	{ .compatible = "realtek,rtk-gsw" },
	{},
};

MODULE_DEVICE_TABLE(of, rtk_gsw_match);

static int rtk_gsw_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	//struct device *dev = &pdev->dev;
	struct device_node *mdio;
	struct mii_bus *mdio_bus;
	struct rtk_gsw *gsw;
	u32 phyid;
	const char *pm;
	int ret;

	mdio = of_parse_phandle(np, "realtek,mdio", 0);

	if (!mdio)
		return -EINVAL;

	mdio_bus = of_mdio_find_bus(mdio);

	if (!mdio_bus)
		return -EPROBE_DEFER;

	gsw = devm_kzalloc(&pdev->dev, sizeof(struct rtk_gsw), GFP_KERNEL);

	if (!gsw)
		return -ENOMEM;

	gsw->dev = &pdev->dev;

	gsw->bus = mdio_bus;

	ret = of_property_read_u32(np, "realtek,phy-id", &phyid);
	if (!ret)
		gsw->phy_id=phyid;
	else
		gsw->phy_id=0;

	gsw->reset_pin = of_get_named_gpio(np, "realtek,reset-pin", 0);

	if (gsw->reset_pin < 0)
		return -1;

	_gsw = gsw;

	init_gsw(gsw);

	//init default vlan for wan
	ret = of_property_read_string(pdev->dev.of_node,
					"realtek,port_map", &pm);

	if (!ret && !strcasecmp(pm, "wllll"))
		rtl8367s_vlan_config(1);
	else
		rtl8367s_vlan_config(0);

	//gsw_debug_proc_init();

	platform_set_drvdata(pdev, gsw);

	return 0;
}

static int rtk_gsw_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	//gsw_debug_proc_exit();

	return 0;
}

static struct platform_driver gsw_driver = {
	.probe = rtk_gsw_probe,
	.remove = rtk_gsw_remove,
	.driver = {
		.name = "rtk-gsw-reduced",
		.owner = THIS_MODULE,
		.of_match_table = rtk_gsw_match,
	},
};

module_platform_driver(gsw_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frank Wunderlich <frank-w@public-files.de>");
MODULE_DESCRIPTION("rtl8367c dsa switch driver for MT7622 based on phy driver by Mark Lee");

