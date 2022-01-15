#ifndef __RTL8367_MDIO_H
#define __RTL8367_MDIO_H

struct rtk_gsw {
	struct device		*dev;
	struct mii_bus		*bus;
	int			phy_id;
	int			reset_pin;
	unsigned int		num_ports;
	struct switch_chip_t	*switchchip;
	int			chipnum;
};

static struct rtk_gsw *_gsw;

typedef unsigned char		rtk_uint8;
typedef short			rtk_int16;
typedef unsigned short		rtk_uint16;
typedef int			rtk_int32;
typedef unsigned int		rtk_uint32;

//typedef rtk_int16	ret_t;
typedef rtk_int32	ret_t;
//typedef rtk_int16	rtk_api_ret_t;
typedef rtk_int32	rtk_api_ret_t;
typedef rtk_uint32	rtk_pri_t;	// priority value
typedef rtk_uint32	rtk_vlan_t;	// vlan id type
typedef rtk_uint32	rtk_data_t;

#define MDC_MDIO_CTRL0_REG			31
#define MDC_MDIO_START_REG			29
#define MDC_MDIO_CTRL1_REG			21
#define MDC_MDIO_ADDRESS_REG			23
#define MDC_MDIO_DATA_WRITE_REG			24
#define MDC_MDIO_DATA_READ_REG			25
#define MDC_MDIO_PREAMBLE_LEN			32

#define MDC_MDIO_START_OP			0xFFFF
#define MDC_MDIO_ADDR_OP			0x000E
#define MDC_MDIO_READ_OP			0x0001
#define MDC_MDIO_WRITE_OP			0x0003

#define RTK_SWITCH_PORT_NUM			(32)
#define RTL8367C_REGBITLENGTH			16

#define RTL8367C_CHIP_NUMBER_REG		0x1300
#define RTL8367C_CHIP_VER_REG			0x1301
#define RTL8367C_RTL_MAGIC_ID_REG		0x13c2
#define RTL8367C_RTL_MAGIC_ID_VAL		0x0249

#define RTL8367C_REGDATAMAX			0xFFFF
#define PHY_CONTROL_REG				0
#define RTL8367C_PHY_REGNOMAX			0x1F
#define RTL8367C_PHY_BASE			0x2000
#define RTL8367C_PHY_OFFSET			5
#define RTL8367C_REG_GPHY_OCP_MSB_0		0x1d15
#define RTL8367C_CFG_CPU_OCPADR_MSB_MASK	0xFC0
#define RTL8367C_REG_UTP_FIB_DET		0x13eb
#define UNDEFINE_PHY_PORT			(0xFF)
#define RTL8367C_REG_PHY_AD			0x130f
#define RTL8367C_PDNPHY_OFFSET			5
#define RTL8367C_EXTNO				3

#define RTL8367C_REG_DIGITAL_INTERFACE0_FORCE	0x1310
#define RTL8367C_REG_DIGITAL_INTERFACE2_FORCE	0x13c4

#define RTL8367C_REG_REG_TO_ECO4		0x1d41

#define RTL8367C_REG_SDS_MISC			0x1d11
#define RTL8367C_REG_SDS_INDACS_CMD		0x6600
#define RTL8367C_REG_SDS_INDACS_ADR		0x6601
#define RTL8367C_REG_SDS_INDACS_DATA		0x6602

#define RTL8367C_CFG_SGMII_SPD_MASK		0x180
#define RTL8367C_CFG_MAC8_SEL_SGMII_OFFSET	6
#define RTL8367C_CFG_SGMII_LINK_OFFSET		9
#define RTL8367C_CFG_SGMII_FDUP_OFFSET		10
#define RTL8367C_CFG_MAC8_SEL_HSGMII_OFFSET	11
#define RTL8367C_CFG_SGMII_TXFC_OFFSET		13
#define RTL8367C_CFG_SGMII_RXFC_OFFSET		14

#define RTL8367C_REG_PARA_LED_IO_EN1		0x1b24
#define RTL8367C_REG_PARA_LED_IO_EN2		0x1b25
#define RTL8367C_REG_PARA_LED_IO_EN3		0x1b33

#define RTL8367C_REG_CHIP_RESET			0x1322
#define RTL8367C_DW8051_RST_OFFSET		4

#define RTL8367C_REG_MISCELLANEOUS_CONFIGURE0	0x130c
#define RTL8367C_DW8051_EN_OFFSET		5

#define RTL8367C_REG_DW8051_RDY			0x1336
#define RTL8367C_ACS_IROM_ENABLE_OFFSET		1
#define RTL8367C_IROM_MSB_OFFSET		2

#define RTL8367C_REG_EXT0_RGMXF			0x1306
#define RTL8367C_EXT0_RGTX_INV_OFFSET		6
#define RTL8367C_REG_EXT1_RGMXF			0x1307
#define RTL8367C_EXT1_RGTX_INV_OFFSET		6

#define RTL8367C_REG_EXT_TXC_DLY		0x13f9
#define RTL8367C_EXT1_GMII_TX_DELAY_MASK	0x7000
#define RTL8367C_EXT0_GMII_TX_DELAY_MASK	0xE00

#define RTL8367C_REG_BYPASS_LINE_RATE		0x03f7
#define RTL8367C_REG_DIGITAL_INTERFACE_SELECT	0x1305
#define RTL8367C_REG_DIGITAL_INTERFACE_SELECT_1	0x13c3
#define RTL8367C_SELECT_GMII_0_MASK		0xF
#define RTL8367C_SELECT_GMII_1_OFFSET		4
#define RTL8367C_SELECT_GMII_2_MASK		0xF
#define RTL8367C_PORTMASK			0x7FF

#define RTL8367C_REG_EXT2_RGMXF			0x13c5

#define RTL8367C_PORTNO				11
#define RTL8367C_PORTIDMAX			(RTL8367C_PORTNO-1)
#define RTL8367C_FIDMAX				0xF
#define RTL8367C_VIDMAX				0xFFF
#define RTL8367C_EVIDMAX			0x1FFF
#define RTL8367C_CVIDXNO			32
#define RTL8367C_CVIDXMAX			(RTL8367C_CVIDXNO-1)
#define RTL8367C_PRIMAX				7
#define RTL8367C_VLAN_4KTABLE_LEN		(3)
#define RTL8367C_VLAN_MBRCFG_LEN		(4)

#define RTL8367C_PORT0_MISC_CFG_VLAN_EGRESS_MODE_MASK	0x30
#define RTL8367C_VLAN_EGRESS_MODE_MASK		RTL8367C_PORT0_MISC_CFG_VLAN_EGRESS_MODE_MASK

#define RTL8367C_REG_VLAN_PVID_CTRL0		0x0700
#define RTL8367C_VLAN_PVID_CTRL_BASE		RTL8367C_REG_VLAN_PVID_CTRL0
#define RTL8367C_VLAN_PVID_CTRL_REG(port)	(RTL8367C_VLAN_PVID_CTRL_BASE + (port >> 1))

#define RTL8367C_PORT0_VIDX_MASK		0x1F
#define RTL8367C_PORT_VIDX_OFFSET(port)		((port &1)<<3)
#define RTL8367C_PORT_VIDX_MASK(port)		(RTL8367C_PORT0_VIDX_MASK << RTL8367C_PORT_VIDX_OFFSET(port))

#define RTL8367C_REG_VLAN_PORTBASED_PRIORITY_CTRL0	0x0851
#define RTL8367C_VLAN_PORTBASED_PRIORITY_BASE		RTL8367C_REG_VLAN_PORTBASED_PRIORITY_CTRL0
#define RTL8367C_VLAN_PORTBASED_PRIORITY_REG(port)	(RTL8367C_VLAN_PORTBASED_PRIORITY_BASE + (port >> 2))
#define RTL8367C_VLAN_PORTBASED_PRIORITY_OFFSET(port)	((port & 0x3) << 2)
#define RTL8367C_VLAN_PORTBASED_PRIORITY_MASK(port)	(0x7 << RTL8367C_VLAN_PORTBASED_PRIORITY_OFFSET(port))

#define RTL8367C_REG_VLAN_INGRESS		0x07a9
#define RTL8367C_VLAN_INGRESS_REG		RTL8367C_REG_VLAN_INGRESS

#define RTL8367C_REG_VLAN_CTRL			0x07a8
#define RTL8367C_VLAN_CTRL_OFFSET		0

#define RTL8367C_REG_PORT0_MISC_CFG		0x000e
#define RTL8367C_PORT_MISC_CFG_BASE		RTL8367C_REG_PORT0_MISC_CFG
#define RTL8367C_PORT_MISC_CFG_REG(port)	(RTL8367C_PORT_MISC_CFG_BASE + (port << 5))

#define RTL8367C_REG_VLAN_MEMBER_CONFIGURATION0_CTRL0	0x0728
#define RTL8367C_VLAN_MEMBER_CONFIGURATION_BASE		RTL8367C_REG_VLAN_MEMBER_CONFIGURATION0_CTRL0
#define RTL8367C_REG_TABLE_ACCESS_ADDR			0x0501
#define RTL8367C_TABLE_ACCESS_ADDR_REG			RTL8367C_REG_TABLE_ACCESS_ADDR
#define RTL8367C_REG_TABLE_ACCESS_CTRL			0x0500
#define RTL8367C_TABLE_ACCESS_CTRL_REG			RTL8367C_REG_TABLE_ACCESS_CTRL
#define RTL8367C_METERNO			64
#define RTL8367C_METERMAX			(RTL8367C_METERNO-1)
#define RTL8367C_TABLE_TYPE_MASK		0x7
#define RTL8367C_COMMAND_TYPE_MASK		0x8

#define RTL8367C_REG_VLAN_EXT_CTRL2		0x07b6
#define RTL8367C_VLAN_EXT_CTRL2_OFFSET		0
#define RTL8367C_VLAN_BUSY_CHECK_NO		(10)
#define RTL8367C_REG_TABLE_LUT_ADDR		0x0502
#define RTL8367C_TABLE_ACCESS_STATUS_REG	RTL8367C_REG_TABLE_LUT_ADDR
#define RTL8367C_TABLE_LUT_ADDR_BUSY_FLAG_OFFSET	13
#define RTL8367C_REG_TABLE_READ_DATA0		0x0520
#define RTL8367C_TABLE_ACCESS_RDDATA_BASE	RTL8367C_REG_TABLE_READ_DATA0

enum RTL8367C_TABLE_ACCESS_OP
{
    TB_OP_READ = 0,
    TB_OP_WRITE
};

enum RTL8367C_TABLE_ACCESS_TARGET
{
    TB_TARGET_ACLRULE = 1,
    TB_TARGET_ACLACT,
    TB_TARGET_CVLAN,
    TB_TARGET_L2,
    TB_TARGET_IGMP_GROUP
};
#define RTL8367C_TABLE_ACCESS_REG_DATA(op, target)	((op << 3) | target)
#define RTL8367C_REG_TABLE_WRITE_DATA0			0x0510
#define RTL8367C_TABLE_ACCESS_WRDATA_BASE		RTL8367C_REG_TABLE_WRITE_DATA0
#define RTL8367C_TABLE_ACCESS_WRDATA_REG(index)		(RTL8367C_TABLE_ACCESS_WRDATA_BASE + index)

#define SGMII_INIT_SIZE	1183
static rtk_uint8 Sgmii_Init[SGMII_INIT_SIZE] = {
0x02,0x03,0x81,0xE4,0xF5,0xA8,0xD2,0xAF,
0x22,0x00,0x00,0x02,0x04,0x0D,0xC5,0xF0,
0xF8,0xA3,0xE0,0x28,0xF0,0xC5,0xF0,0xF8,
0xE5,0x82,0x15,0x82,0x70,0x02,0x15,0x83,
0xE0,0x38,0xF0,0x22,0x75,0xF0,0x08,0x75,
0x82,0x00,0xEF,0x2F,0xFF,0xEE,0x33,0xFE,
0xCD,0x33,0xCD,0xCC,0x33,0xCC,0xC5,0x82,
0x33,0xC5,0x82,0x9B,0xED,0x9A,0xEC,0x99,
0xE5,0x82,0x98,0x40,0x0C,0xF5,0x82,0xEE,
0x9B,0xFE,0xED,0x9A,0xFD,0xEC,0x99,0xFC,
0x0F,0xD5,0xF0,0xD6,0xE4,0xCE,0xFB,0xE4,
0xCD,0xFA,0xE4,0xCC,0xF9,0xA8,0x82,0x22,
0xB8,0x00,0xC1,0xB9,0x00,0x59,0xBA,0x00,
0x2D,0xEC,0x8B,0xF0,0x84,0xCF,0xCE,0xCD,
0xFC,0xE5,0xF0,0xCB,0xF9,0x78,0x18,0xEF,
0x2F,0xFF,0xEE,0x33,0xFE,0xED,0x33,0xFD,
0xEC,0x33,0xFC,0xEB,0x33,0xFB,0x10,0xD7,
0x03,0x99,0x40,0x04,0xEB,0x99,0xFB,0x0F,
0xD8,0xE5,0xE4,0xF9,0xFA,0x22,0x78,0x18,
0xEF,0x2F,0xFF,0xEE,0x33,0xFE,0xED,0x33,
0xFD,0xEC,0x33,0xFC,0xC9,0x33,0xC9,0x10,
0xD7,0x05,0x9B,0xE9,0x9A,0x40,0x07,0xEC,
0x9B,0xFC,0xE9,0x9A,0xF9,0x0F,0xD8,0xE0,
0xE4,0xC9,0xFA,0xE4,0xCC,0xFB,0x22,0x75,
0xF0,0x10,0xEF,0x2F,0xFF,0xEE,0x33,0xFE,
0xED,0x33,0xFD,0xCC,0x33,0xCC,0xC8,0x33,
0xC8,0x10,0xD7,0x07,0x9B,0xEC,0x9A,0xE8,
0x99,0x40,0x0A,0xED,0x9B,0xFD,0xEC,0x9A,
0xFC,0xE8,0x99,0xF8,0x0F,0xD5,0xF0,0xDA,
0xE4,0xCD,0xFB,0xE4,0xCC,0xFA,0xE4,0xC8,
0xF9,0x22,0xEB,0x9F,0xF5,0xF0,0xEA,0x9E,
0x42,0xF0,0xE9,0x9D,0x42,0xF0,0xE8,0x9C,
0x45,0xF0,0x22,0xE0,0xFC,0xA3,0xE0,0xFD,
0xA3,0xE0,0xFE,0xA3,0xE0,0xFF,0x22,0xE0,
0xF8,0xA3,0xE0,0xF9,0xA3,0xE0,0xFA,0xA3,
0xE0,0xFB,0x22,0xEC,0xF0,0xA3,0xED,0xF0,
0xA3,0xEE,0xF0,0xA3,0xEF,0xF0,0x22,0xE4,
0x90,0x06,0x2C,0xF0,0xFD,0x7C,0x01,0x7F,
0x3F,0x7E,0x1D,0x12,0x04,0x6B,0x7D,0x40,
0x7C,0x00,0x7F,0x36,0x7E,0x13,0x12,0x04,
0x6B,0xE4,0xFF,0xFE,0xFD,0x80,0x25,0xE4,
0x7F,0xFF,0x7E,0xFF,0xFD,0xFC,0x90,0x06,
0x24,0x12,0x01,0x0F,0xC3,0x12,0x00,0xF2,
0x50,0x1B,0x90,0x06,0x24,0x12,0x01,0x03,
0xEF,0x24,0x01,0xFF,0xE4,0x3E,0xFE,0xE4,
0x3D,0xFD,0xE4,0x3C,0xFC,0x90,0x06,0x24,
0x12,0x01,0x1B,0x80,0xD2,0xE4,0xF5,0xA8,
0xD2,0xAF,0x7D,0x1F,0xFC,0x7F,0x49,0x7E,
0x13,0x12,0x04,0x6B,0x12,0x04,0x92,0x7D,
0x41,0x7C,0x00,0x7F,0x36,0x7E,0x13,0x12,
0x04,0x6B,0xE4,0xFF,0xFE,0xFD,0x80,0x25,
0xE4,0x7F,0x20,0x7E,0x4E,0xFD,0xFC,0x90,
0x06,0x24,0x12,0x01,0x0F,0xC3,0x12,0x00,
0xF2,0x50,0x1B,0x90,0x06,0x24,0x12,0x01,
0x03,0xEF,0x24,0x01,0xFF,0xE4,0x3E,0xFE,
0xE4,0x3D,0xFD,0xE4,0x3C,0xFC,0x90,0x06,
0x24,0x12,0x01,0x1B,0x80,0xD2,0xC2,0x00,
0xC2,0x01,0xD2,0xA9,0xD2,0x8C,0x7F,0x01,
0x7E,0x62,0x12,0x04,0x47,0xEF,0x30,0xE2,
0x07,0xE4,0x90,0x06,0x2C,0xF0,0x80,0xEE,
0x90,0x06,0x2C,0xE0,0x70,0x12,0x12,0x02,
0xDB,0x90,0x06,0x2C,0x74,0x01,0xF0,0xE4,
0x90,0x06,0x2F,0xF0,0xA3,0xF0,0x80,0xD6,
0xC3,0x90,0x06,0x30,0xE0,0x94,0x62,0x90,
0x06,0x2F,0xE0,0x94,0x00,0x40,0xC7,0xE4,
0xF0,0xA3,0xF0,0x12,0x02,0xDB,0x90,0x06,
0x2C,0x74,0x01,0xF0,0x80,0xB8,0x75,0x0F,
0x80,0x75,0x0E,0x7E,0x75,0x0D,0xAA,0x75,
0x0C,0x83,0xE4,0xF5,0x10,0x7F,0x36,0x7E,
0x13,0x12,0x04,0x47,0xEE,0xC4,0xF8,0x54,
0xF0,0xC8,0xEF,0xC4,0x54,0x0F,0x48,0x54,
0x07,0xFB,0x7A,0x00,0xEA,0x70,0x4A,0xEB,
0x14,0x60,0x1C,0x14,0x60,0x27,0x24,0xFE,
0x60,0x31,0x14,0x60,0x3C,0x24,0x05,0x70,
0x38,0x75,0x0B,0x00,0x75,0x0A,0xC2,0x75,
0x09,0xEB,0x75,0x08,0x0B,0x80,0x36,0x75,
0x0B,0x40,0x75,0x0A,0x59,0x75,0x09,0x73,
0x75,0x08,0x07,0x80,0x28,0x75,0x0B,0x00,
0x75,0x0A,0xE1,0x75,0x09,0xF5,0x75,0x08,
0x05,0x80,0x1A,0x75,0x0B,0xA0,0x75,0x0A,
0xAC,0x75,0x09,0xB9,0x75,0x08,0x03,0x80,
0x0C,0x75,0x0B,0x00,0x75,0x0A,0x62,0x75,
0x09,0x3D,0x75,0x08,0x01,0x75,0x89,0x11,
0xE4,0x7B,0x60,0x7A,0x09,0xF9,0xF8,0xAF,
0x0B,0xAE,0x0A,0xAD,0x09,0xAC,0x08,0x12,
0x00,0x60,0xAA,0x06,0xAB,0x07,0xC3,0xE4,
0x9B,0xFB,0xE4,0x9A,0xFA,0x78,0x17,0xF6,
0xAF,0x03,0xEF,0x08,0xF6,0x18,0xE6,0xF5,
0x8C,0x08,0xE6,0xF5,0x8A,0x74,0x0D,0x2B,
0xFB,0xE4,0x3A,0x18,0xF6,0xAF,0x03,0xEF,
0x08,0xF6,0x75,0x88,0x10,0x53,0x8E,0xC7,
0xD2,0xA9,0x22,0x7D,0x02,0x7C,0x00,0x7F,
0x4A,0x7E,0x13,0x12,0x04,0x6B,0x7D,0x46,
0x7C,0x71,0x7F,0x02,0x7E,0x66,0x12,0x04,
0x6B,0x7D,0x03,0x7C,0x00,0x7F,0x01,0x7E,
0x66,0x12,0x04,0x6B,0x7D,0xC0,0x7C,0x00,
0x7F,0x00,0x7E,0x66,0x12,0x04,0x6B,0xE4,
0xFF,0xFE,0x0F,0xBF,0x00,0x01,0x0E,0xEF,
0x64,0x64,0x4E,0x70,0xF5,0x7D,0x04,0x7C,
0x00,0x7F,0x02,0x7E,0x66,0x12,0x04,0x6B,
0x7D,0x00,0x7C,0x04,0x7F,0x01,0x7E,0x66,
0x12,0x04,0x6B,0x7D,0xC0,0x7C,0x00,0x7F,
0x00,0x7E,0x66,0x12,0x04,0x6B,0xE4,0xFD,
0xFC,0x7F,0x02,0x7E,0x66,0x12,0x04,0x6B,
0x7D,0x00,0x7C,0x04,0x7F,0x01,0x7E,0x66,
0x12,0x04,0x6B,0x7D,0xC0,0x7C,0x00,0x7F,
0x00,0x7E,0x66,0x12,0x04,0x6B,0xE4,0xFD,
0xFC,0x7F,0x4A,0x7E,0x13,0x12,0x04,0x6B,
0x7D,0x06,0x7C,0x71,0x7F,0x02,0x7E,0x66,
0x12,0x04,0x6B,0x7D,0x03,0x7C,0x00,0x7F,
0x01,0x7E,0x66,0x12,0x04,0x6B,0x7D,0xC0,
0x7C,0x00,0x7F,0x00,0x7E,0x66,0x02,0x04,
0x6B,0x78,0x7F,0xE4,0xF6,0xD8,0xFD,0x75,
0x81,0x3C,0x02,0x03,0xC8,0x02,0x01,0x27,
0xE4,0x93,0xA3,0xF8,0xE4,0x93,0xA3,0x40,
0x03,0xF6,0x80,0x01,0xF2,0x08,0xDF,0xF4,
0x80,0x29,0xE4,0x93,0xA3,0xF8,0x54,0x07,
0x24,0x0C,0xC8,0xC3,0x33,0xC4,0x54,0x0F,
0x44,0x20,0xC8,0x83,0x40,0x04,0xF4,0x56,
0x80,0x01,0x46,0xF6,0xDF,0xE4,0x80,0x0B,
0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,
0x90,0x04,0x87,0xE4,0x7E,0x01,0x93,0x60,
0xBC,0xA3,0xFF,0x54,0x3F,0x30,0xE5,0x09,
0x54,0x1F,0xFE,0xE4,0x93,0xA3,0x60,0x01,
0x0E,0xCF,0x54,0xC0,0x25,0xE0,0x60,0xA8,
0x40,0xB8,0xE4,0x93,0xA3,0xFA,0xE4,0x93,
0xA3,0xF8,0xE4,0x93,0xA3,0xC8,0xC5,0x82,
0xC8,0xCA,0xC5,0x83,0xCA,0xF0,0xA3,0xC8,
0xC5,0x82,0xC8,0xCA,0xC5,0x83,0xCA,0xDF,
0xE9,0xDE,0xE7,0x80,0xBE,0xC0,0xE0,0xC0,
0xF0,0xC0,0x83,0xC0,0x82,0xC0,0xD0,0x75,
0xD0,0x00,0xC0,0x00,0x78,0x17,0xE6,0xF5,
0x8C,0x78,0x18,0xE6,0xF5,0x8A,0x90,0x06,
0x2D,0xE4,0x75,0xF0,0x01,0x12,0x00,0x0E,
0x90,0x06,0x2F,0xE4,0x75,0xF0,0x01,0x12,
0x00,0x0E,0xD0,0x00,0xD0,0xD0,0xD0,0x82,
0xD0,0x83,0xD0,0xF0,0xD0,0xE0,0x32,0xC2,
0xAF,0xAD,0x07,0xAC,0x06,0x8C,0xA2,0x8D,
0xA3,0x75,0xA0,0x01,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xAE,
0xA1,0xBE,0x00,0xF0,0xAE,0xA6,0xAF,0xA7,
0xD2,0xAF,0x22,0xC2,0xAF,0xAB,0x07,0xAA,
0x06,0x8A,0xA2,0x8B,0xA3,0x8C,0xA4,0x8D,
0xA5,0x75,0xA0,0x03,0x00,0x00,0x00,0xAA,
0xA1,0xBA,0x00,0xF8,0xD2,0xAF,0x22,0x42,
0x06,0x2F,0x00,0x00,0x42,0x06,0x2D,0x00,
0x00,0x00,0x12,0x04,0x9B,0x12,0x02,0x16,
0x02,0x00,0x03,0xE4,0xF5,0x8E,0x22,};

typedef enum rtk_enable_e
{
	DISABLED = 0,
	ENABLED,
	RTK_ENABLE_END
} rtk_enable_t;

typedef enum init_state_e
{
	INIT_NOT_COMPLETED = 0,
	INIT_COMPLETED,
	INIT_STATE_END
} init_state_t;
static init_state_t	init_state = INIT_NOT_COMPLETED;

#define RTK_CHK_INIT_STATE()				\
	do						\
	{						\
		if(init_state != INIT_COMPLETED)	\
		{					\
			return RT_ERR_NOT_INIT;		\
		}					\
	}while(0)


typedef enum port_type_e
{
	UTP_PORT = 0,
	EXT_PORT,
	UNKNOWN_PORT = 0xFF,
	PORT_TYPE_END
}port_type_t;

typedef enum rtk_port_e
{
	UTP_PORT0 = 0,
	UTP_PORT1,
	UTP_PORT2,
	UTP_PORT3,
	UTP_PORT4,
	UTP_PORT5,
	UTP_PORT6,
	UTP_PORT7,

	EXT_PORT0 = 16,
	EXT_PORT1,
	EXT_PORT2,

	UNDEFINE_PORT = 30,
	RTK_PORT_MAX = 31
} rtk_port_t;

enum EXTMODE
{
	EXT_DISABLE = 0,
	EXT_RGMII,
	EXT_MII_MAC,
	EXT_MII_PHY,
	EXT_TMII_MAC,
	EXT_TMII_PHY,
	EXT_GMII,
	EXT_RMII_MAC,
	EXT_RMII_PHY,
	EXT_SGMII,
	EXT_HSGMII,
	EXT_1000X_100FX,
	EXT_1000X,
	EXT_100FX,
	EXT_RGMII_2,
	EXT_MII_MAC_2,
	EXT_MII_PHY_2,
	EXT_TMII_MAC_2,
	EXT_TMII_PHY_2,
	EXT_RMII_MAC_2,
	EXT_RMII_PHY_2,
	EXT_END
};
typedef struct rtl8367c_port_ability_s{
	rtk_uint16 forcemode;
	rtk_uint16 mstfault;
	rtk_uint16 mstmode;
	rtk_uint16 nway;
	rtk_uint16 txpause;
	rtk_uint16 rxpause;
	rtk_uint16 link;
	rtk_uint16 duplex;
	rtk_uint16 speed;
}rtl8367c_port_ability_t;

#define RTK_PORTMASK_PORT_SET(__portmask__, __port__)	((__portmask__).bits[0] |= (0x00000001 << __port__))

typedef enum rtk_mode_ext_e
{
	MODE_EXT_DISABLE = 0,
	MODE_EXT_RGMII,
	MODE_EXT_MII_MAC,
	MODE_EXT_MII_PHY,
	MODE_EXT_TMII_MAC,
	MODE_EXT_TMII_PHY,
	MODE_EXT_GMII,
	MODE_EXT_RMII_MAC,
	MODE_EXT_RMII_PHY,
	MODE_EXT_SGMII,
	MODE_EXT_HSGMII,
	MODE_EXT_1000X_100FX,
	MODE_EXT_1000X,
	MODE_EXT_100FX,
	MODE_EXT_RGMII_2,
	MODE_EXT_MII_MAC_2,
	MODE_EXT_MII_PHY_2,
	MODE_EXT_TMII_MAC_2,
	MODE_EXT_TMII_PHY_2,
	MODE_EXT_RMII_MAC_2,
	MODE_EXT_RMII_PHY_2,
	MODE_EXT_END
} rtk_mode_ext_t;

typedef enum rtk_port_duplex_e
{
	PORT_HALF_DUPLEX = 0,
	PORT_FULL_DUPLEX,
	PORT_DUPLEX_END
} rtk_port_duplex_t;

typedef enum rtk_port_linkStatus_e
{
	PORT_LINKDOWN = 0,
	PORT_LINKUP,
	PORT_LINKSTATUS_END
} rtk_port_linkStatus_t;

typedef struct rtk_port_mac_ability_s
{
	rtk_uint32 forcemode;
	rtk_uint32 speed;
	rtk_uint32 duplex;
	rtk_uint32 link;
	rtk_uint32 nway;
	rtk_uint32 txpause;
	rtk_uint32 rxpause;
}rtk_port_mac_ability_t;

typedef enum rtk_port_speed_e
{
	PORT_SPEED_10M = 0,
	PORT_SPEED_100M,
	PORT_SPEED_1000M,//maybe use SPEED_1000??
	PORT_SPEED_500M,
	PORT_SPEED_2500M,//maybe use SPEED_2500??
	PORT_SPEED_END
} rtk_port_speed_t;

typedef rtk_uint32 rtk_port_phy_data_t;     // phy page
typedef enum rtk_port_phy_reg_e
{
	PHY_REG_CONTROL			= 0,
	PHY_REG_STATUS,
	PHY_REG_IDENTIFIER_1,
	PHY_REG_IDENTIFIER_2,
	PHY_REG_AN_ADVERTISEMENT,
	PHY_REG_AN_LINKPARTNER,
	PHY_REG_1000_BASET_CONTROL	= 9,
	PHY_REG_1000_BASET_STATUS,
	PHY_REG_END			= 32
} rtk_port_phy_reg_t;

/* enum for mac link mode */
enum LINKMODE
{
	MAC_NORMAL = 0,
	MAC_FORCE,
};

/* enum for port current link duplex mode */
enum DUPLEXMODE
{
	HALF_DUPLEX = 0,
	FULL_DUPLEX
};

#define RTK_TOTAL_NUM_OF_WORD_FOR_1BIT_PORT_LIST	1

typedef struct rtk_portmask_s
{
	rtk_uint32 bits[RTK_TOTAL_NUM_OF_WORD_FOR_1BIT_PORT_LIST];
} rtk_portmask_t;

typedef struct rtk_vlan_cfg_s
{
	rtk_portmask_t	mbr;
	rtk_portmask_t	untag;
	rtk_uint16	ivl_en;
	rtk_uint16	fid_msti;
	rtk_uint16	envlanpol;
	rtk_uint16	meteridx;
	rtk_uint16	vbpen;
	rtk_uint16	vbpri;
}rtk_vlan_cfg_t;

typedef enum switch_chip_e
{
	CHIP_RTL8367C = 0,
	CHIP_RTL8370B,
	CHIP_RTL8364B,
	CHIP_RTL8363SC_VB,
	CHIP_END
}switch_chip_t;

typedef enum rt_error_code_e
{
	RT_ERR_FAILED = -1,				// General Error

	// 0x0000xxxx for common error code
	RT_ERR_OK = 0,					// 0x00000000, OK
	RT_ERR_INPUT,					// 0x00000001, invalid input parameter
	RT_ERR_UNIT_ID,					// 0x00000002, invalid unit id
	RT_ERR_PORT_ID,					// 0x00000003, invalid port id
	RT_ERR_PORT_MASK,				// 0x00000004, invalid port mask
	RT_ERR_PORT_LINKDOWN,				// 0x00000005, link down port status
	RT_ERR_ENTRY_INDEX,				// 0x00000006, invalid entry index
	RT_ERR_NULL_POINTER,				// 0x00000007, input parameter is null pointer
	RT_ERR_QUEUE_ID,				// 0x00000008, invalid queue id
	RT_ERR_QUEUE_NUM,				// 0x00000009, invalid queue number
	RT_ERR_BUSYWAIT_TIMEOUT,			// 0x0000000a, busy watting time out
	RT_ERR_MAC,					// 0x0000000b, invalid mac address
	RT_ERR_OUT_OF_RANGE,				// 0x0000000c, input parameter out of range
	RT_ERR_CHIP_NOT_SUPPORTED,			// 0x0000000d, functions not supported by this chip model
	RT_ERR_SMI,					// 0x0000000e, SMI error
	RT_ERR_NOT_INIT,				// 0x0000000f, The module is not initial
	RT_ERR_CHIP_NOT_FOUND,				// 0x00000010, The chip can not found
	RT_ERR_NOT_ALLOWED,				// 0x00000011, actions not allowed by the function
	RT_ERR_DRIVER_NOT_FOUND,			// 0x00000012, The driver can not found
	RT_ERR_SEM_LOCK_FAILED,				// 0x00000013, Failed to lock semaphore
	RT_ERR_SEM_UNLOCK_FAILED,			// 0x00000014, Failed to unlock semaphore
	RT_ERR_ENABLE,					// 0x00000015, invalid enable parameter
	RT_ERR_TBL_FULL,				// 0x00000016, input table full

	// 0x0001xxxx for vlan
	RT_ERR_VLAN_VID = 0x00010000,			// 0x00010000, invalid vid
	RT_ERR_VLAN_PRIORITY,				// 0x00010001, invalid 1p priority
	RT_ERR_VLAN_EMPTY_ENTRY,			// 0x00010002, emtpy entry of vlan table
	RT_ERR_VLAN_ACCEPT_FRAME_TYPE,			// 0x00010003, invalid accept frame type
	RT_ERR_VLAN_EXIST,				// 0x00010004, vlan is exist
	RT_ERR_VLAN_ENTRY_NOT_FOUND,			// 0x00010005, specified vlan entry not found
	RT_ERR_VLAN_PORT_MBR_EXIST,			// 0x00010006, member port exist in the specified vlan
	RT_ERR_VLAN_PROTO_AND_PORT,			// 0x00010008, invalid protocol and port based vlan

	// 0x0002xxxx for svlan
	RT_ERR_SVLAN_ENTRY_INDEX = 0x00020000,		// 0x00020000, invalid svid entry no
	RT_ERR_SVLAN_ETHER_TYPE,			// 0x00020001, invalid SVLAN ether type
	RT_ERR_SVLAN_TABLE_FULL,			// 0x00020002, no empty entry in SVLAN table
	RT_ERR_SVLAN_ENTRY_NOT_FOUND,			// 0x00020003, specified svlan entry not found
	RT_ERR_SVLAN_EXIST,				// 0x00020004, SVLAN entry is exist
	RT_ERR_SVLAN_VID,				// 0x00020005, invalid svid

	// 0x0003xxxx for MSTP
	RT_ERR_MSTI = 0x00030000,			// 0x00030000, invalid msti
	RT_ERR_MSTP_STATE,				// 0x00030001, invalid spanning tree status
	RT_ERR_MSTI_EXIST,				// 0x00030002, MSTI exist
	RT_ERR_MSTI_NOT_EXIST,				// 0x00030003, MSTI not exist

	// 0x0004xxxx for BUCKET
	RT_ERR_TIMESLOT = 0x00040000,			// 0x00040000, invalid time slot
	RT_ERR_TOKEN,					// 0x00040001, invalid token amount
	RT_ERR_RATE,					// 0x00040002, invalid rate
	RT_ERR_TICK,					// 0x00040003, invalid tick

	// 0x0005xxxx for RMA
	RT_ERR_RMA_ADDR = 0x00050000,			// 0x00050000, invalid rma mac address
	RT_ERR_RMA_ACTION,				// 0x00050001, invalid rma action

	// 0x0006xxxx for L2
	RT_ERR_L2_HASH_KEY = 0x00060000,		// 0x00060000, invalid L2 Hash key
	RT_ERR_L2_HASH_INDEX,				// 0x00060001, invalid L2 Hash index
	RT_ERR_L2_CAM_INDEX,				// 0x00060002, invalid L2 CAM index
	RT_ERR_L2_ENRTYSEL,				// 0x00060003, invalid EntrySel
	RT_ERR_L2_INDEXTABLE_INDEX,			// 0x00060004, invalid L2 index table(=portMask table) index
	RT_ERR_LIMITED_L2ENTRY_NUM,			// 0x00060005, invalid limited L2 entry number
	RT_ERR_L2_AGGREG_PORT,				// 0x00060006, this aggregated port is not the lowest physical port of its aggregation group
	RT_ERR_L2_FID,					// 0x00060007, invalid fid
	RT_ERR_L2_VID,					// 0x00060008, invalid cvid
	RT_ERR_L2_NO_EMPTY_ENTRY,			// 0x00060009, no empty entry in L2 table
	RT_ERR_L2_ENTRY_NOTFOUND,			// 0x0006000a, specified entry not found
	RT_ERR_L2_INDEXTBL_FULL,			// 0x0006000b, the L2 index table is full
	RT_ERR_L2_INVALID_FLOWTYPE,			// 0x0006000c, invalid L2 flow type
	RT_ERR_L2_L2UNI_PARAM,				// 0x0006000d, invalid L2 unicast parameter
	RT_ERR_L2_L2MULTI_PARAM,			// 0x0006000e, invalid L2 multicast parameter
	RT_ERR_L2_IPMULTI_PARAM,			// 0x0006000f, invalid L2 ip multicast parameter
	RT_ERR_L2_PARTIAL_HASH_KEY,			// 0x00060010, invalid L2 partial Hash key
	RT_ERR_L2_EMPTY_ENTRY,				// 0x00060011, the entry is empty(invalid)
	RT_ERR_L2_FLUSH_TYPE,				// 0x00060012, the flush type is invalid
	RT_ERR_L2_NO_CPU_PORT,				// 0x00060013, CPU port not exist

	// 0x0007xxxx for FILTER (PIE)
	RT_ERR_FILTER_BLOCKNUM = 0x00070000,		// 0x00070000, invalid block number
	RT_ERR_FILTER_ENTRYIDX,				// 0x00070001, invalid entry index
	RT_ERR_FILTER_CUTLINE,				// 0x00070002, invalid cutline value
	RT_ERR_FILTER_FLOWTBLBLOCK,			// 0x00070003, block belongs to flow table
	RT_ERR_FILTER_INACLBLOCK,			// 0x00070004, block belongs to ingress ACL
	RT_ERR_FILTER_ACTION,				// 0x00070005, action doesn't consist to entry type
	RT_ERR_FILTER_INACL_RULENUM,			// 0x00070006, invalid ACL rulenum
	RT_ERR_FILTER_INACL_TYPE,			// 0x00070007, entry type isn't an ingress ACL rule
	RT_ERR_FILTER_INACL_EXIST,			// 0x00070008, ACL entry is already exit
	RT_ERR_FILTER_INACL_EMPTY,			// 0x00070009, ACL entry is empty
	RT_ERR_FILTER_FLOWTBL_TYPE,			// 0x0007000a, entry type isn't an flow table rule
	RT_ERR_FILTER_FLOWTBL_RULENUM,			// 0x0007000b, invalid flow table rulenum
	RT_ERR_FILTER_FLOWTBL_EMPTY,			// 0x0007000c, flow table entry is empty
	RT_ERR_FILTER_FLOWTBL_EXIST,			// 0x0007000d, flow table entry is already exist
	RT_ERR_FILTER_METER_ID,				// 0x0007000e, invalid metering id
	RT_ERR_FILTER_LOG_ID,				// 0x0007000f, invalid log id
	RT_ERR_FILTER_INACL_NONE_BEGIN_IDX,		// 0x00070010, entry index is not starting index of a group of rules
	RT_ERR_FILTER_INACL_ACT_NOT_SUPPORT,		// 0x00070011, action not support
	RT_ERR_FILTER_INACL_RULE_NOT_SUPPORT,		// 0x00070012, rule not support

	// 0x0008xxxx for ACL Rate Limit
	RT_ERR_ACLRL_HTHR = 0x00080000,			// 0x00080000, invalid high threshold
	RT_ERR_ACLRL_TIMESLOT,				// 0x00080001, invalid time slot
	RT_ERR_ACLRL_TOKEN,				// 0x00080002, invalid token amount
	RT_ERR_ACLRL_RATE,				// 0x00080003, invalid rate

	// 0x0009xxxx for Link aggregation
	RT_ERR_LA_CPUPORT = 0x00090000,			// 0x00090000, CPU port can not be aggregated port
	RT_ERR_LA_TRUNK_ID,				// 0x00090001, invalid trunk id
	RT_ERR_LA_PORTMASK,				// 0x00090002, invalid port mask
	RT_ERR_LA_HASHMASK,				// 0x00090003, invalid hash mask
	RT_ERR_LA_DUMB,					// 0x00090004, this API should be used in 802.1ad dumb mode
	RT_ERR_LA_PORTNUM_DUMB,				// 0x00090005, it can only aggregate at most four ports when 802.1ad dumb mode
	RT_ERR_LA_PORTNUM_NORMAL,			// 0x00090006, it can only aggregate at most eight ports when 802.1ad normal mode
	RT_ERR_LA_MEMBER_OVERLAP,			// 0x00090007, the specified port mask is overlapped with other group
	RT_ERR_LA_NOT_MEMBER_PORT,			// 0x00090008, the port is not a member port of the trunk
	RT_ERR_LA_TRUNK_NOT_EXIST,			// 0x00090009, the trunk doesn't exist


	// 0x000axxxx for storm filter
	RT_ERR_SFC_TICK_PERIOD = 0x000a0000,		// 0x000a0000, invalid SFC tick period
	RT_ERR_SFC_UNKNOWN_GROUP,			// 0x000a0001, Unknown Storm filter group

	// 0x000bxxxx for pattern match
	RT_ERR_PM_MASK = 0x000b0000,			// 0x000b0000, invalid pattern length. Pattern length should be 8
	RT_ERR_PM_LENGTH,				// 0x000b0001, invalid pattern match mask, first byte must care
	RT_ERR_PM_MODE,					// 0x000b0002, invalid pattern match mode

	// 0x000cxxxx for input bandwidth control
	RT_ERR_INBW_TICK_PERIOD = 0x000c0000,		// 0x000c0000, invalid tick period for input bandwidth control
	RT_ERR_INBW_TOKEN_AMOUNT,			// 0x000c0001, invalid amount of token for input bandwidth control
	RT_ERR_INBW_FCON_VALUE,				// 0x000c0002, invalid flow control ON threshold value for input bandwidth control
	RT_ERR_INBW_FCOFF_VALUE,			// 0x000c0003, invalid flow control OFF threshold value for input bandwidth control
	RT_ERR_INBW_FC_ALLOWANCE,			// 0x000c0004, invalid allowance of incomming packet for input bandwidth control
	RT_ERR_INBW_RATE,				// 0x000c0005, invalid input bandwidth

	// 0x000dxxxx for QoS
	RT_ERR_QOS_1P_PRIORITY = 0x000d0000,		// 0x000d0000, invalid 802.1P priority
	RT_ERR_QOS_DSCP_VALUE,				// 0x000d0001, invalid DSCP value
	RT_ERR_QOS_INT_PRIORITY,			// 0x000d0002, invalid internal priority
	RT_ERR_QOS_SEL_DSCP_PRI,			// 0x000d0003, invalid DSCP selection priority
	RT_ERR_QOS_SEL_PORT_PRI,			// 0x000d0004, invalid port selection priority
	RT_ERR_QOS_SEL_IN_ACL_PRI,			// 0x000d0005, invalid ingress ACL selection priority
	RT_ERR_QOS_SEL_CLASS_PRI,			// 0x000d0006, invalid classifier selection priority
	RT_ERR_QOS_EBW_RATE,				// 0x000d0007, invalid egress bandwidth rate
	RT_ERR_QOS_SCHE_TYPE,				// 0x000d0008, invalid QoS scheduling type
	RT_ERR_QOS_QUEUE_WEIGHT,			// 0x000d0009, invalid Queue weight
	RT_ERR_QOS_SEL_PRI_SOURCE,			// 0x000d000a, invalid selection of priority source

	// 0x000exxxx for port ability
	RT_ERR_PHY_PAGE_ID = 0x000e0000,		// 0x000e0000, invalid PHY page id
	RT_ERR_PHY_REG_ID,				// 0x000e0001, invalid PHY reg id
	RT_ERR_PHY_DATAMASK,				// 0x000e0002, invalid PHY data mask
	RT_ERR_PHY_AUTO_NEGO_MODE,			// 0x000e0003, invalid PHY auto-negotiation mode
	RT_ERR_PHY_SPEED,				// 0x000e0004, invalid PHY speed setting
	RT_ERR_PHY_DUPLEX,				// 0x000e0005, invalid PHY duplex setting
	RT_ERR_PHY_FORCE_ABILITY,			// 0x000e0006, invalid PHY force mode ability parameter
	RT_ERR_PHY_FORCE_1000,				// 0x000e0007, invalid PHY force mode 1G speed setting
	RT_ERR_PHY_TXRX,				// 0x000e0008, invalid PHY tx/rx
	RT_ERR_PHY_ID,					// 0x000e0009, invalid PHY id
	RT_ERR_PHY_RTCT_NOT_FINISH,			// 0x000e000a, PHY RTCT in progress

	// 0x000fxxxx for mirror
	RT_ERR_MIRROR_DIRECTION = 0x000f0000,		// 0x000f0000, invalid error mirror direction
	RT_ERR_MIRROR_SESSION_FULL,			// 0x000f0001, mirroring session is full
	RT_ERR_MIRROR_SESSION_NOEXIST,			// 0x000f0002, mirroring session not exist
	RT_ERR_MIRROR_PORT_EXIST,			// 0x000f0003, mirroring port already exists
	RT_ERR_MIRROR_PORT_NOT_EXIST,			// 0x000f0004, mirroring port does not exists
	RT_ERR_MIRROR_PORT_FULL,			// 0x000f0005, Exceeds maximum number of supported mirroring port

	// 0x0010xxxx for stat
	RT_ERR_STAT_INVALID_GLOBAL_CNTR = 0x00100000,	// 0x00100000, Invalid Global Counter
	RT_ERR_STAT_INVALID_PORT_CNTR,			// 0x00100001, Invalid Port Counter
	RT_ERR_STAT_GLOBAL_CNTR_FAIL,			// 0x00100002, Could not retrieve/reset Global Counter
	RT_ERR_STAT_PORT_CNTR_FAIL,			// 0x00100003, Could not retrieve/reset Port Counter
	RT_ERR_STAT_INVALID_CNTR,			// 0x00100004, Invalid Counter
	RT_ERR_STAT_CNTR_FAIL,				// 0x00100005, Could not retrieve/reset Counter

	// 0x0011xxxx for dot1x
	RT_ERR_DOT1X_INVALID_DIRECTION = 0x00110000,	// 0x00110000, Invalid Authentication Direction
	RT_ERR_DOT1X_PORTBASEDPNEN,			// 0x00110001, Port-based enable port error
	RT_ERR_DOT1X_PORTBASEDAUTH,			// 0x00110002, Port-based auth port error
	RT_ERR_DOT1X_PORTBASEDOPDIR,			// 0x00110003, Port-based opdir error
	RT_ERR_DOT1X_MACBASEDPNEN,			// 0x00110004, MAC-based enable port error
	RT_ERR_DOT1X_MACBASEDOPDIR,			// 0x00110005, MAC-based opdir error
	RT_ERR_DOT1X_PROC,				// 0x00110006, unauthorized behavior error
	RT_ERR_DOT1X_GVLANIDX,				// 0x00110007, guest vlan index error
	RT_ERR_DOT1X_GVLANTALK,				// 0x00110008, guest vlan OPDIR error
	RT_ERR_DOT1X_MAC_PORT_MISMATCH,			// 0x00110009, Auth MAC and port mismatch eror

	RT_ERR_END					// The symbol is the latest symbol
} rt_error_code_t;

typedef struct rtk_switch_halCtrl_s
{
	switch_chip_t	switch_type;
	rtk_uint32	l2p_port[RTK_SWITCH_PORT_NUM];
	rtk_uint32	p2l_port[RTK_SWITCH_PORT_NUM];
	port_type_t	log_port_type[RTK_SWITCH_PORT_NUM];
	rtk_uint32	ptp_port[RTK_SWITCH_PORT_NUM];
	rtk_uint32	valid_portmask;
	rtk_uint32	valid_utp_portmask;
	rtk_uint32	valid_ext_portmask;
	rtk_uint32	valid_cpu_portmask;
	rtk_uint32	min_phy_port;
	rtk_uint32	max_phy_port;
	rtk_uint32	phy_portmask;
	rtk_uint32	combo_logical_port;
	rtk_uint32	hsg_logical_port;
	rtk_uint32	sg_logical_portmask;
	rtk_uint32	max_meter_id;
	rtk_uint32	max_lut_addr_num;
	rtk_uint32	trunk_group_mask;
}rtk_switch_halCtrl_t;

static rtk_switch_halCtrl_t *halCtrl = NULL;

static rtk_switch_halCtrl_t rtl8367c_hal_Ctrl =
{
	/* Switch Chip */
	CHIP_RTL8367C,

	/* Logical to Physical */
	{0, 1, 2, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	6, 7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },

	/* Physical to Logical */
	{UTP_PORT0, UTP_PORT1, UTP_PORT2, UTP_PORT3, UTP_PORT4, UNDEFINE_PORT, EXT_PORT0, EXT_PORT1,
	UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT,
	UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT,
	UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT, UNDEFINE_PORT},

	/* Port Type */
	{UTP_PORT, UTP_PORT, UTP_PORT, UTP_PORT, UTP_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT,
	UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT,
	EXT_PORT, EXT_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT,
	UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT, UNKNOWN_PORT},

	/* PTP port */
	{1, 1, 1, 1, 1, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0 },

	/* Valid port mask */
	( (0x1 << UTP_PORT0) | (0x1 << UTP_PORT1) | (0x1 << UTP_PORT2) | (0x1 << UTP_PORT3) | (0x1 << UTP_PORT4) | (0x1 << EXT_PORT0) | (0x1 << EXT_PORT1) ),

	/* Valid UTP port mask */
	( (0x1 << UTP_PORT0) | (0x1 << UTP_PORT1) | (0x1 << UTP_PORT2) | (0x1 << UTP_PORT3) | (0x1 << UTP_PORT4) ),

	/* Valid EXT port mask */
	( (0x1 << EXT_PORT0) | (0x1 << EXT_PORT1) ),

	/* Valid CPU port mask */
	0x00,

	/* Minimum physical port number */
	0,

	/* Maxmum physical port number */
	7,

	/* Physical port mask */
	0xDF,

	/* Combo Logical port ID */
	4,

	/* HSG Logical port ID */
	EXT_PORT0,

	/* SGMII Logical portmask */
	(0x1 << EXT_PORT0),

	/* Max Meter ID */
	31,

	/* MAX LUT Address Number */
	2112,

	/* Trunk Group Mask */
	0x03
};

typedef struct VLANCONFIGUSER
{
	rtk_uint16  evid;
	rtk_uint16  mbr;
	rtk_uint16  fid_msti;
	rtk_uint16  envlanpol;
	rtk_uint16  meteridx;
	rtk_uint16  vbpen;
	rtk_uint16  vbpri;
} rtl8367c_vlanconfiguser;

typedef struct USER_VLANTABLE
{
	rtk_uint16  vid;
	rtk_uint16  mbr;
	rtk_uint16  untag;
	rtk_uint16  fid_msti;
	rtk_uint16  envlanpol;
	rtk_uint16  meteridx;
	rtk_uint16  vbpen;
	rtk_uint16  vbpri;
	rtk_uint16  ivl_svl;
} rtl8367c_user_vlan4kentry;

typedef enum vlan_mbrCfgType_e
{
	MBRCFG_UNUSED = 0,
	MBRCFG_USED_BY_VLAN,
	MBRCFG_END
} vlan_mbrCfgType_t;

static rtk_vlan_t		vlan_mbrCfgVid[RTL8367C_CVIDXNO];
static vlan_mbrCfgType_t	vlan_mbrCfgUsage[RTL8367C_CVIDXNO];

typedef enum
{
	EG_TAG_MODE_ORI = 0,
	EG_TAG_MODE_KEEP,
	EG_TAG_MODE_PRI_TAG,
	EG_TAG_MODE_REAL_KEEP,
	EG_TAG_MODE_END
} rtl8367c_egtagmode;

static rtk_api_ret_t rtk_switch_logicalPortCheck(rtk_port_t logicalPort)
{
	if(init_state != INIT_COMPLETED)
		return RT_ERR_NOT_INIT;

	if(logicalPort >= RTK_SWITCH_PORT_NUM)
		return RT_ERR_FAILED;

	if(halCtrl->l2p_port[logicalPort] == 0xFF)
		return RT_ERR_FAILED;

	return RT_ERR_OK;
}

static rtk_uint32 rtk_switch_phyPortMask_get(void)
{
	if(init_state != INIT_COMPLETED)
		return 0x00; /* No port in portmask */

	return (halCtrl->phy_portmask);
}
#define RTK_SCAN_ALL_LOG_PORT(__port__)				for(__port__ = 0; __port__ < RTK_SWITCH_PORT_NUM; __port__++)  if( rtk_switch_logicalPortCheck(__port__) == RT_ERR_OK)
#define RTK_PORTMASK_IS_PORT_SET(__portmask__, __port__)	(((__portmask__).bits[0] & (0x00000001 << __port__)) ? 1 : 0)
#define RTK_PORTMASK_SCAN(__portmask__, __port__)		for(__port__ = 0; __port__ < RTK_SWITCH_PORT_NUM; __port__++)  if(RTK_PORTMASK_IS_PORT_SET(__portmask__, __port__))
#define RTK_PHY_PORTMASK_ALL					(rtk_switch_phyPortMask_get())
#define RTK_SCAN_ALL_PHY_PORTMASK(__port__)			for(__port__ = 0; __port__ < RTK_SWITCH_PORT_NUM; __port__++)  if( (rtk_switch_phyPortMask_get() & (0x00000001 << __port__)))

static rtk_api_ret_t rtk_switch_isExtPort(rtk_port_t logicalPort)
{
	if(init_state != INIT_COMPLETED)
		return RT_ERR_NOT_INIT;

	if(logicalPort >= RTK_SWITCH_PORT_NUM)
		return RT_ERR_FAILED;

	if(halCtrl->log_port_type[logicalPort] == EXT_PORT)
		return RT_ERR_OK;
	else
		return RT_ERR_FAILED;
}

static rtk_api_ret_t rtk_switch_isUtpPort(rtk_port_t logicalPort)
{
	if(init_state != INIT_COMPLETED)
		return RT_ERR_NOT_INIT;

	if(logicalPort >= RTK_SWITCH_PORT_NUM)
		return RT_ERR_FAILED;

	if(halCtrl->log_port_type[logicalPort] == UTP_PORT)
		return RT_ERR_OK;
	else
		return RT_ERR_FAILED;
}

static rtk_api_ret_t rtk_switch_isHsgPort(rtk_port_t logicalPort)
{
	if(init_state != INIT_COMPLETED)
		return RT_ERR_NOT_INIT;

	if(logicalPort >= RTK_SWITCH_PORT_NUM)
		return RT_ERR_FAILED;

	if(logicalPort == halCtrl->hsg_logical_port)
		return RT_ERR_OK;
	else
		return RT_ERR_FAILED;
}

static rtk_api_ret_t rtk_switch_isSgmiiPort(rtk_port_t logicalPort)
{
	if(init_state != INIT_COMPLETED)
		return RT_ERR_NOT_INIT;

	if(logicalPort >= RTK_SWITCH_PORT_NUM)
		return RT_ERR_FAILED;

	if( ((0x01 << logicalPort) & halCtrl->sg_logical_portmask) != 0)
		return RT_ERR_OK;
	else
		return RT_ERR_FAILED;
}

static rtk_uint32 rtk_switch_port_L2P_get(rtk_port_t logicalPort)
{
	if(init_state != INIT_COMPLETED)
		return UNDEFINE_PHY_PORT;

	if(logicalPort >= RTK_SWITCH_PORT_NUM)
		return UNDEFINE_PHY_PORT;

	return (halCtrl->l2p_port[logicalPort]);
}

static rtk_api_ret_t rtk_switch_isPortMaskValid(rtk_portmask_t *pPmask)
{
	if(init_state != INIT_COMPLETED)
		return RT_ERR_NOT_INIT;

	if(NULL == pPmask)
		return RT_ERR_NULL_POINTER;

	if( (pPmask->bits[0] | halCtrl->valid_portmask) != halCtrl->valid_portmask )
		return RT_ERR_FAILED;
	else
		return RT_ERR_OK;
}

static rtk_api_ret_t rtk_switch_portmask_L2P_get(rtk_portmask_t *pLogicalPmask, rtk_uint32 *pPhysicalPortmask)
{
	rtk_uint32 log_port, phy_port;

	if(init_state != INIT_COMPLETED)
		return RT_ERR_NOT_INIT;

	if(NULL == pLogicalPmask)
		return RT_ERR_NULL_POINTER;

	if(NULL == pPhysicalPortmask)
		return RT_ERR_NULL_POINTER;

	if(rtk_switch_isPortMaskValid(pLogicalPmask) != RT_ERR_OK)
		return RT_ERR_PORT_MASK;

	/* reset physical port mask */
	*pPhysicalPortmask = 0;

	RTK_PORTMASK_SCAN((*pLogicalPmask), log_port)
	{
		phy_port = rtk_switch_port_L2P_get((rtk_port_t)log_port);
		*pPhysicalPortmask |= (0x0001 << phy_port);
	}

	return RT_ERR_OK;
}

#define RTK_CHK_PORT_VALID(__port__)					\
	do								\
	{								\
		if(rtk_switch_logicalPortCheck(__port__) != RT_ERR_OK)	\
		{							\
			return RT_ERR_PORT_ID;				\
		}							\
	}while(0)

#define RTK_CHK_PORT_IS_UTP(__port__)					\
	do								\
	{								\
		if(rtk_switch_isUtpPort(__port__) != RT_ERR_OK)		\
		{							\
			return RT_ERR_PORT_ID;				\
		}							\
	}while(0)

#define RTK_CHK_PORT_IS_EXT(__port__)					\
	do								\
	{								\
		if(rtk_switch_isExtPort(__port__) != RT_ERR_OK)		\
		{							\
			return RT_ERR_PORT_ID;				\
		}							\
	}while(0)

#define RTK_CHK_PORTMASK_VALID(__portmask__)					\
	do									\
	{									\
		if(rtk_switch_isPortMaskValid(__portmask__) != RT_ERR_OK)	\
		{								\
			return RT_ERR_PORT_MASK;				\
		}								\
	}while(0)

static rtk_uint32 rtk_switch_maxMeterId_get(void)
{
	if(init_state != INIT_COMPLETED)
		return 0x00;

	return (halCtrl->max_meter_id);
}
#define	RTK_MAX_METER_ID	(rtk_switch_maxMeterId_get())

#endif /* __RTL8367_MDIO_H */
