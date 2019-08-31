/* Copyright  2016 MediaTek Inc.
 * Author: Nelson Chang <nelson.chang@mediatek.com>
 * Author: Carlos Huang <carlos.huang@mediatek.com>
 * Author: Harry Huang <harry.huang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef RAETH_CONFIG_H
#define RAETH_CONFIG_H

/* compile flag for features */
#define DELAY_INT
#define CONFIG_RAETH_TX_RX_INT_SEPARATION
#define CONFIG_RAETH_RW_PDMAPTR_FROM_VAR

/*#define CONFIG_RAETH_NAPI_TX_RX*/
#define CONFIG_RAETH_NAPI_RX_ONLY

/* definitions */
#ifdef	DELAY_INT
#define FE_DLY_INT	BIT(0)
#else
#define FE_DLY_INT	(0)
#endif
#ifdef	CONFIG_RAETH_HW_LRO
#define FE_HW_LRO	BIT(1)
#else
#define FE_HW_LRO	(0)
#endif
#ifdef	CONFIG_RAETH_HW_LRO_FORCE
#define FE_HW_LRO_FPORT	BIT(2)
#else
#define FE_HW_LRO_FPORT	(0)
#endif
#ifdef	CONFIG_RAETH_LRO
#define FE_SW_LRO	BIT(3)
#else
#define FE_SW_LRO	(0)
#endif
#ifdef	CONFIG_RAETH_QDMA
#define FE_QDMA		BIT(4)
#else
#define FE_QDMA		(0)
#endif
#ifdef	CONFIG_RAETH_NAPI
#define FE_INT_NAPI	BIT(5)
#else
#define FE_INT_NAPI	(0)
#endif
#ifdef	CONFIG_RA_NETWORK_WORKQUEUE_BH
#define FE_INT_WORKQ	BIT(6)
#else
#define FE_INT_WORKQ	(0)
#endif
#ifdef	CONFIG_RA_NETWORK_TASKLET_BH
#define FE_INT_TASKLET	BIT(7)
#else
#define FE_INT_TASKLET	(0)
#endif
#ifdef	CONFIG_RAETH_TX_RX_INT_SEPARATION
#define FE_IRQ_SEPARATE	BIT(8)
#else
#define FE_IRQ_SEPARATE	(0)
#endif
#ifdef	CONFIG_RAETH_GMAC2
#define FE_GE2_SUPPORT	BIT(9)
#else
#define FE_GE2_SUPPORT	(0)
#endif
#ifdef	CONFIG_RAETH_ETHTOOL
#define FE_ETHTOOL	BIT(10)
#else
#define FE_ETHTOOL	(0)
#endif
#ifdef	CONFIG_RAETH_CHECKSUM_OFFLOAD
#define FE_CSUM_OFFLOAD	BIT(11)
#else
#define FE_CSUM_OFFLOAD	(0)
#endif
#ifdef	CONFIG_RAETH_TSO
#define FE_TSO		BIT(12)
#else
#define FE_TSO		(0)
#endif
#ifdef	CONFIG_RAETH_TSOV6
#define FE_TSO_V6	BIT(13)
#else
#define FE_TSO_V6	(0)
#endif
#ifdef	CONFIG_RAETH_HW_VLAN_TX
#define FE_HW_VLAN_TX	BIT(14)
#else
#define FE_HW_VLAN_TX	(0)
#endif
#ifdef	CONFIG_RAETH_HW_VLAN_RX
#define FE_HW_VLAN_RX	BIT(15)
#else
#define FE_HW_VLAN_RX	(0)
#endif
#ifdef	CONFIG_RAETH_QDMA
#define FE_QDMA_TX	BIT(16)
#else
#define FE_QDMA_TX	(0)
#endif
#ifdef	CONFIG_RAETH_QDMATX_QDMARX
#define FE_QDMA_RX	BIT(17)
#else
#define FE_QDMA_RX	(0)
#endif
#ifdef	CONFIG_HW_SFQ
#define FE_HW_SFQ	BIT(18)
#else
#define FE_HW_SFQ	(0)
#endif
#ifdef	CONFIG_RAETH_HW_IOCOHERENT
#define FE_HW_IOCOHERENT BIT(19)
#else
#define FE_HW_IOCOHERENT (0)
#endif
#ifdef	CONFIG_MTK_FPGA
#define FE_FPGA_MODE	BIT(20)
#else
#define FE_FPGA_MODE	(0)
#endif
#ifdef	CONFIG_RAETH_HW_LRO_REASON_DBG
#define FE_HW_LRO_DBG	BIT(21)
#else
#define FE_HW_LRO_DBG	(0)
#endif
#ifdef CONFIG_RAETH_INT_DBG
#define FE_RAETH_INT_DBG	BIT(22)
#else
#define FE_RAETH_INT_DBG	(0)
#endif
#ifdef CONFIG_USER_SNMPD
#define USER_SNMPD	BIT(23)
#else
#define USER_SNMPD	(0)
#endif
#ifdef CONFIG_TASKLET_WORKQUEUE_SW
#define TASKLET_WORKQUEUE_SW	BIT(24)
#else
#define TASKLET_WORKQUEUE_SW	(0)
#endif
#if defined(CONFIG_RA_HW_NAT) || defined(CONFIG_RA_HW_NAT_MODULE)
#define FE_HW_NAT	BIT(25)
#else
#define FE_HW_NAT	(0)
#endif
#ifdef	CONFIG_RAETH_NAPI_TX_RX
#define FE_INT_NAPI_TX_RX	BIT(26)
#else
#define FE_INT_NAPI_TX_RX	(0)
#endif
#ifdef	CONFIG_QDMA_MQ
#define QDMA_MQ       BIT(27)
#else
#define QDMA_MQ       (0)
#endif
#ifdef	CONFIG_RAETH_NAPI_RX_ONLY
#define FE_INT_NAPI_RX_ONLY	BIT(28)
#else
#define FE_INT_NAPI_RX_ONLY	(0)
#endif
#ifdef	CONFIG_QDMA_SUPPORT_QOS
#define FE_QDMA_FQOS	BIT(29)
#else
#define FE_QDMA_FQOS	(0)
#endif

#define MT7623_FE	(7623)
#define MT7622_FE	(7622)

#ifdef CONFIG_RAETH_GMAC2
#define GMAC2 BIT(0)
#else
#define GMAC2 (0)
#endif
#ifdef CONFIG_LAN_WAN_SUPPORT
#define LAN_WAN_SUPPORT BIT(1)
#else
#define LAN_WAN_SUPPORT (0)
#endif
#ifdef CONFIG_WAN_AT_P0
#define WAN_AT_P0 BIT(2)
#else
#define WAN_AT_P0 (0)
#endif
#ifdef CONFIG_WAN_AT_P4
#define WAN_AT_P4 BIT(3)
#else
#define WAN_AT_P4 (0)
#endif
#if defined(CONFIG_GE1_RGMII_FORCE_1000)
#define    GE1_RGMII_FORCE_1000		BIT(4)
#define    GE1_TRGMII_FORCE_2000	(0)
#define    GE1_TRGMII_FORCE_2600	(0)
#define    MT7530_TRGMII_PLL_25M	(0x0A00)
#define    MT7530_TRGMII_PLL_40M	(0x0640)
#elif defined(CONFIG_GE1_TRGMII_FORCE_2000)
#define    GE1_TRGMII_FORCE_2000	BIT(5)
#define    GE1_RGMII_FORCE_1000		(0)
#define    GE1_TRGMII_FORCE_2600	(0)
#define    MT7530_TRGMII_PLL_25M	(0x1400)
#define    MT7530_TRGMII_PLL_40M	(0x0C80)
#elif defined(CONFIG_GE1_TRGMII_FORCE_2600)
#define    GE1_TRGMII_FORCE_2600	BIT(6)
#define    GE1_RGMII_FORCE_1000		(0)
#define    GE1_TRGMII_FORCE_2000	(0)
#define    MT7530_TRGMII_PLL_25M	(0x1A00)
#define    MT7530_TRGMII_PLL_40M	(0x1040)
#define    TRGMII
#else
#define    GE1_RGMII_FORCE_1000		(0)
#define    GE1_TRGMII_FORCE_2000	(0)
#define    GE1_TRGMII_FORCE_2600	(0)
#define    MT7530_TRGMII_PLL_25M	(0)
#define    MT7530_TRGMII_PLL_40M	(0)
#endif
#ifdef CONFIG_GE1_RGMII_AN
#define    GE1_RGMII_AN    BIT(7)
#else
#define    GE1_RGMII_AN    (0)
#endif
#ifdef CONFIG_GE1_SGMII_AN
#define    GE1_SGMII_AN    BIT(8)
#else
#define    GE1_SGMII_AN    (0)
#endif
#ifdef CONFIG_GE1_SGMII_FORCE_2500
#define    GE1_SGMII_FORCE_2500    BIT(9)
#else
#define    GE1_SGMII_FORCE_2500    (0)
#endif
#ifdef CONFIG_GE1_RGMII_ONE_EPHY
#define    GE1_RGMII_ONE_EPHY    BIT(10)
#else
#define    GE1_RGMII_ONE_EPHY    (0)
#endif
#ifdef CONFIG_RAETH_ESW
#define    RAETH_ESW    BIT(11)
#define    CONFIG_MT7622_EPHY   (1)
#else
#define    RAETH_ESW    (0)
#endif
#ifdef CONFIG_GE1_RGMII_NONE
#define    GE1_RGMII_NONE    BIT(12)
#else
#define    GE1_RGMII_NONE    (0)
#endif
#ifdef CONFIG_GE2_RGMII_FORCE_1000
#define    GE2_RGMII_FORCE_1000    BIT(13)
#else
#define    GE2_RGMII_FORCE_1000    (0)
#endif
#ifdef CONFIG_GE2_RGMII_AN
#define    GE2_RGMII_AN    BIT(14)
#else
#define    GE2_RGMII_AN    (0)
#endif
#ifdef CONFIG_GE2_INTERNAL_GPHY
#define    GE2_INTERNAL_GPHY    BIT(15)
#else
#define    GE2_INTERNAL_GPHY    (0)
#endif
#ifdef CONFIG_GE2_SGMII_AN
#define    GE2_SGMII_AN    BIT(16)
#else
#define    GE2_SGMII_AN    (0)
#endif
#ifdef CONFIG_GE2_SGMII_FORCE_2500
#define    GE2_SGMII_FORCE_2500    BIT(17)
#else
#define    GE2_SGMII_FORCE_2500    (0)
#endif
#ifdef CONFIG_MT7622_EPHY
#define    MT7622_EPHY    BIT(18)
#else
#define    MT7622_EPHY    (0)
#endif
#ifdef CONFIG_RAETH_SGMII
#define    RAETH_SGMII	BIT(19)
#else
#define    RAETH_SGMII	(0)
#endif

#ifndef CONFIG_MAC_TO_GIGAPHY_MODE_ADDR
#define CONFIG_MAC_TO_GIGAPHY_MODE_ADDR (0)
#endif
#ifndef CONFIG_MAC_TO_GIGAPHY_MODE_ADDR2
#define CONFIG_MAC_TO_GIGAPHY_MODE_ADDR2 (0)
#endif

/* macros */
#define fe_features_config(end_device)	\
{					\
end_device->features = 0;		\
end_device->features |= FE_DLY_INT;	\
end_device->features |= FE_HW_LRO;	\
end_device->features |= FE_HW_LRO_FPORT;\
end_device->features |= FE_HW_LRO_DBG;	\
end_device->features |= FE_SW_LRO;	\
end_device->features |= FE_QDMA;	\
end_device->features |= FE_INT_NAPI;	\
end_device->features |= FE_INT_WORKQ;	\
end_device->features |= FE_INT_TASKLET;	\
end_device->features |= FE_IRQ_SEPARATE;\
end_device->features |= FE_GE2_SUPPORT;	\
end_device->features |= FE_ETHTOOL;	\
end_device->features |= FE_CSUM_OFFLOAD;\
end_device->features |= FE_TSO;		\
end_device->features |= FE_TSO_V6;	\
end_device->features |= FE_HW_VLAN_TX;	\
end_device->features |= FE_HW_VLAN_RX;	\
end_device->features |= FE_QDMA_TX;	\
end_device->features |= FE_QDMA_RX;	\
end_device->features |= FE_HW_SFQ;	\
end_device->features |= FE_HW_IOCOHERENT; \
end_device->features |= FE_FPGA_MODE;	\
end_device->features |= FE_HW_NAT;	\
end_device->features |= FE_INT_NAPI_TX_RX; \
end_device->features |= FE_INT_NAPI_RX_ONLY; \
end_device->features |= FE_QDMA_FQOS;	\
}

#define fe_architecture_config(end_device)              \
{                                                       \
end_device->architecture = 0;                           \
end_device->architecture |= GMAC2;                      \
end_device->architecture |= LAN_WAN_SUPPORT;            \
end_device->architecture |= WAN_AT_P0;                  \
end_device->architecture |= WAN_AT_P4;                  \
end_device->architecture |= GE1_RGMII_FORCE_1000;       \
end_device->architecture |= GE1_TRGMII_FORCE_2000;      \
end_device->architecture |= GE1_TRGMII_FORCE_2600;      \
end_device->architecture |= GE1_RGMII_AN;               \
end_device->architecture |= GE1_SGMII_AN;               \
end_device->architecture |= GE1_SGMII_FORCE_2500;       \
end_device->architecture |= GE1_RGMII_ONE_EPHY;         \
end_device->architecture |= RAETH_ESW;                  \
end_device->architecture |= GE1_RGMII_NONE;             \
end_device->architecture |= GE2_RGMII_FORCE_1000;       \
end_device->architecture |= GE2_RGMII_AN;               \
end_device->architecture |= GE2_INTERNAL_GPHY;          \
end_device->architecture |= GE2_SGMII_AN;               \
end_device->architecture |= GE2_SGMII_FORCE_2500;       \
end_device->architecture |= MT7622_EPHY;		\
end_device->architecture |= RAETH_SGMII;		\
}
#endif
