/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __CONNAC_REGS_DBG_H
#define __CONNAC_REGS_DBG_H

#define MT_WTBL_BTCR_N		MT_WTBL_ON(0x100)
#define MT_WTBL_BTBCR_N		MT_WTBL_ON(0x110)
#define MT_WTBL_BRCR_N		MT_WTBL_ON(0x120)
#define MT_WTBL_BRBCR_N		MT_WTBL_ON(0x130)
#define MT_WTBL_BTDCR_N		MT_WTBL_ON(0x140)
#define MT_WTBL_BRDCR_N		MT_WTBL_ON(0x150)

#define MT_WTBL_MBTCR_N		MT_WTBL_ON(0x200)
#define MT_WTBL_MBTBCR_N	MT_WTBL_ON(0x240)
#define MT_WTBL_MBRCR_N		MT_WTBL_ON(0x280)
#define MT_WTBL_MBRBCR_N	MT_WTBL_ON(0x2C0)

#define WF_MIB_BASE		0x2d000
#define MT_MIB(_n)			(WF_MIB_BASE + (_n))

#define MT_MIB_M0SDR0			MT_MIB(0x10)
#define MT_MIB_M0SDR0_BCN_TXCNT		GENMASK(15, 0)

#define MT_MIB_M0DR0			MT_MIB(0xa0)
#define MT_MIB_M0DR0_20MHZ_TXCNT	GENMASK(15, 0)
#define MT_MIB_M0DR0_40MHZ_TXCNT	GENMASK(31, 16)

#define MIB_M0SCR0	(WF_MIB_BASE + 0x00)
#define MIB_M0PBSCR	(WF_MIB_BASE + 0x04)
#define MIB_M0SCR1	(WF_MIB_BASE + 0x08)
#define M0_MISC_CR	(WF_MIB_BASE + 0x0c)

#define MIB_M0SDR0      (WF_MIB_BASE + 0x10)
#define MIB_M0SDR3      (WF_MIB_BASE + 0x14)
#define MIB_M0SDR4      (WF_MIB_BASE + 0x18)
#define MIB_M0SDR5      (WF_MIB_BASE + 0x1c)
#define MIB_M0SDR6      (WF_MIB_BASE + 0x20)
#define MIB_M0SDR7      (WF_MIB_BASE + 0x24)
#define MIB_M0SDR8      (WF_MIB_BASE + 0x28)
#define MIB_M0SDR9      (WF_MIB_BASE + 0x2c)
#define MIB_M0SDR10     (WF_MIB_BASE + 0x30)
#define MIB_M0SDR11     (WF_MIB_BASE + 0x34)
#define MIB_M0SDR12     (WF_MIB_BASE + 0x38)
#define MIB_M0SDR14     (WF_MIB_BASE + 0x40)
#define MIB_M0SDR15     (WF_MIB_BASE + 0x44)
#define MIB_M0SDR16     (WF_MIB_BASE + 0x48)
#define MIB_M0SDR17     (WF_MIB_BASE + 0x4c)
#define MIB_M0SDR18     (WF_MIB_BASE + 0x50)
#define MIB_M0SDR19     (WF_MIB_BASE + 0x54)
#define MIB_M0SDR20     (WF_MIB_BASE + 0x58)
#define MIB_M0SDR21     (WF_MIB_BASE + 0x5c)
#define MIB_M0SDR22     (WF_MIB_BASE + 0x60)
#define MIB_M0SDR23     (WF_MIB_BASE + 0x64)
#define MIB_M0SDR29     (WF_MIB_BASE + 0x7c)

#define MIB_M0SDR34 (WF_MIB_BASE + 0x90)
#define MIB_M0SDR35 (WF_MIB_BASE + 0x94)

#define MIB_M0SDR36      (WF_MIB_BASE + 0x98)
#define MIB_M0SDR37      (WF_MIB_BASE + 0x9c)
#define MIB_M0SDR51      (WF_MIB_BASE + 0x1E0)

#define MIB_M0DR0       (WF_MIB_BASE + 0xa0)
#define MIB_M0DR1       (WF_MIB_BASE + 0xa4)
#define MIB_M0DR2       (WF_MIB_BASE + 0xa8)
#define MIB_M0DR3       (WF_MIB_BASE + 0xac)
#define MIB_M0DR4       (WF_MIB_BASE + 0xb0)
#define MIB_M0DR5       (WF_MIB_BASE + 0xb4)

#define MIB_M0DR6       (WF_MIB_BASE + 0xb8)

#define TX_DDLMT_RNG1_CNT_MASK (0xff)
#define GET_TX_DDLMT_RNG1_CNT(p) (((p) & TX_DDLMT_RNG1_CNT_MASK))
#define TX_DDLMT_RNG2_CNT_MASK (0xff << 16)
#define GET_TX_DDLMT_RNG2_CNT(p) (((p) & TX_DDLMT_RNG2_CNT_MASK) >> 16)

#define MIB_M0DR7	(WF_MIB_BASE + 0xbc)
#define TX_DDLMT_RNG3_CNT_MASK (0xff)
#define GET_TX_DDLMT_RNG3_CNT(p) (((p) & TX_DDLMT_RNG3_CNT_MASK))
#define TX_DDLMT_RNG4_CNT_MASK (0xff << 16)
#define GET_TX_DDLMT_RNG4_CNT(p) (((p) & TX_DDLMT_RNG4_CNT_MASK) >> 16)

#define MIB_M0DR8	(WF_MIB_BASE + 0xc0)
#define MIB_M0DR9	(WF_MIB_BASE + 0xc4)
#define MIB_M0DR10	(WF_MIB_BASE + 0xc8)
#define MIB_M0DR11	(WF_MIB_BASE + 0xcc)

#define MIB_MB0SDR0	(WF_MIB_BASE + 0x100)
#define MIB_MB1SDR0	(WF_MIB_BASE + 0x110)
#define MIB_MB2SDR0	(WF_MIB_BASE + 0x120)
#define MIB_MB3SDR0	(WF_MIB_BASE + 0x130)

#define MIB_MB0SDR1	(WF_MIB_BASE + 0x104)
#define MIB_MB1SDR1	(WF_MIB_BASE + 0x114)
#define MIB_MB2SDR1	(WF_MIB_BASE + 0x124)
#define MIB_MB3SDR1	(WF_MIB_BASE + 0x134)

#define MIB_MB0SDR2	(WF_MIB_BASE + 0x108)
#define MIB_MB1SDR2	(WF_MIB_BASE + 0x118)
#define MIB_MB2SDR2	(WF_MIB_BASE + 0x128)
#define MIB_MB3SDR2	(WF_MIB_BASE + 0x138)

#define MIB_MB0SDR3	(WF_MIB_BASE + 0x10c)
#define MIB_MB1SDR3	(WF_MIB_BASE + 0x11c)
#define MIB_MB2SDR3	(WF_MIB_BASE + 0x12c)
#define MIB_MB3SDR3	(WF_MIB_BASE + 0x13c)

#define MIB_MPDU_SR0	(WF_MIB_BASE + 0x190)
#define MIB_MPDU_SR1	(WF_MIB_BASE + 0x194)

#define MIB_M1SCR	(WF_MIB_BASE + 0x200)
#define MIB_M1PBSCR	(WF_MIB_BASE + 0x204)
#define MIB_M1SCR1	(WF_MIB_BASE + 0x208)
#if 1//def TXRX_STAT_SUPPORT
#define MIB_M1SDR0	(WF_MIB_BASE + 0x210)
#endif
#define MIB_M1SDR3	(WF_MIB_BASE + 0x214)
#define MIB_M1SDR4	(WF_MIB_BASE + 0x218)
#define MIB_M1SDR10	(WF_MIB_BASE + 0x230)
#define MIB_M1SDR11	(WF_MIB_BASE + 0x234)
#define MIB_M1SDR16	(WF_MIB_BASE + 0x248)
#define MIB_M1SDR17	(WF_MIB_BASE + 0x24c)
#define MIB_M1SDR18	(WF_MIB_BASE + 0x250)
#define MIB_M1SDR20	(WF_MIB_BASE + 0x258)
#define MIB_M1SDR34	(WF_MIB_BASE + 0x290) /* 820FD290 */
#define MIB_M1SDR36	(WF_MIB_BASE + 0x298) /* 820FD298 */
#define MIB_M1SDR37	(WF_MIB_BASE + 0x29c) /* 820FD29c */

#define PSE_BASE		0xc000

/* PSE Reset Control Register */
#define PSE_RESET (PSE_BASE + 0x00)
#define PSE_ALL_RST	BIT(0)
#define PSE_LOGICAL_RST	BIT(1)
#define PSE_INIT_DONE	BIT(2)
#define GET_PSE_INIT_DONE(p) (((p) & PSE_INIT_DONE) >> 2)

/* PSE Buffer Control Register */
#define PSE_PBUF_CTRL (PSE_BASE + 0x14)
#define PSE_TOTAL_PAGE_NUM_MASK (0xfff)
#define PSE_GET_TOTAL_PAGE_NUM(p) (((p) & 0xfff))
#define PSE_TOTAL_PAGE_CFG_MASK (0xf << 16)
#define PSE_GET_TOTAL_PAGE_CFG(p) (((p) & PSE_TOTAL_PAGE_CFG_MASK) >> 16)

#define PSE_PBUF_OFFSET_MASK (0xf << 20)
#define GET_PSE_PBUF_OFFSET(p) (((p) & PSE_PBUF_OFFSET_MASK) >> 20)

#define PSE_INT_N9_ERR_STS			(PSE_BASE + 0x28)

/* Queue Empty */
#define PSE_QUEUE_EMPTY			(PSE_BASE + 0xb0)

/* Page Flow Control */
#define PSE_FREEPG_CNT				(PSE_BASE + 0x100)

#define PSE_FREEPG_HEAD_TAIL		(PSE_BASE + 0x104)

#define PSE_PG_HIF0_GROUP			(PSE_BASE + 0x110)
#define PSE_HIF0_PG_INFO			(PSE_BASE + 0x114)
#define PSE_PG_HIF1_GROUP			(PSE_BASE + 0x118)
#define PSE_HIF1_PG_INFO			(PSE_BASE + 0x11c)

#define PSE_PG_CPU_GROUP			(PSE_BASE + 0x150)
#define PSE_CPU_PG_INFO			    (PSE_BASE + 0x154)

#define PSE_PG_LMAC0_GROUP			(PSE_BASE + 0x170)
#define PSE_LMAC0_PG_INFO			(PSE_BASE + 0x174)
#define PSE_PG_LMAC1_GROUP			(PSE_BASE + 0x178)
#define PSE_LMAC1_PG_INFO			(PSE_BASE + 0x17c)
#define PSE_PG_LMAC2_GROUP			(PSE_BASE + 0x180)
#define PSE_LMAC2_PG_INFO			(PSE_BASE + 0x184)
#define PSE_PG_PLE_GROUP			(PSE_BASE + 0x190)
#define PSE_PLE_PG_INFO			    (PSE_BASE + 0x194)

/* Indirect path for read/write */
#define PSE_FL_QUE_CTRL_0			(PSE_BASE + 0x1b0)
#define PSE_FL_QUE_CTRL_1			(PSE_BASE + 0x1b4)
#define PSE_FL_QUE_CTRL_2			(PSE_BASE + 0x1b8)
#define PSE_FL_QUE_CTRL_3			(PSE_BASE + 0x1bc)
#define PSE_PL_QUE_CTRL_0			(PSE_BASE + 0x1c0)

/* PSE spare dummy CR */
#define PSE_SPARE_DUMMY_CR1			(PSE_BASE +  0x1e4)
#define PSE_SPARE_DUMMY_CR2			(PSE_BASE +  0x1e8)
#define PSE_SPARE_DUMMY_CR3			(PSE_BASE +  0x2e8)
#define PSE_SPARE_DUMMY_CR4			(PSE_BASE +  0x2ec)

#define PSE_SEEK_CR_00			(PSE_BASE + 0x3d0)
#define PSE_SEEK_CR_01			(PSE_BASE + 0x3d4)
#define PSE_SEEK_CR_02			(PSE_BASE + 0x3d8)
#define PSE_SEEK_CR_03			(PSE_BASE + 0x3dc)
#define PSE_SEEK_CR_04			(PSE_BASE + 0x3e0)
#define PSE_SEEK_CR_05			(PSE_BASE + 0x3e4)
#define PSE_SEEK_CR_06			(PSE_BASE + 0x3e8)
#define PSE_SEEK_CR_08			(PSE_BASE + 0x3f0)

/* CPU Interface Get First Frame ID Control Regitser */
#define C_GFF (PSE_BASE + 0x24)
#define GET_FIRST_FID_MASK (0xfff)
#define GET_FIRST_FID(p) (((p) & GET_FIRST_FID_MASK))
#define GET_QID_MASK (0x1f << 16)
#define SET_GET_QID(p) (((p) & 0x1f) << 16)
#define GET_QID(p) (((p) & GET_QID_MASK) >> 16)
#define GET_PID_MASK (0x3 << 21)
#define SET_GET_PID(p) (((p) & 0x3) << 21)
#define GET_PID(p) (((p) & GET_PID_MASK) >> 21)
#define GET_RVSE_MASK	BIT(23)
#define SET_GET_RVSE(p) (((p) & 0x1) << 23)
#define GET_RVSE(p) (((p) & GET_RVSE_MASK) >> 23)

/* CPU Interface Get Frame ID Control Register */
#define C_GF (PSE_BASE + 0x28)
#define GET_RETURN_FID_MASK (0xfff)
#define GET_RETURN_FID(p) (((p) & GET_RETURN_FID_MASK))
#define CURR_FID_MASK (0xfff << 16)
#define SET_CURR_FID(p) (((p) & 0xfff) << 16)
#define GET_CURR_FID(p) (((p) & CURR_FID_MASK) >> 16)
#define GET_PREV_MASK	BIT(28)
#define SET_GET_PREV(p) (((p) & 0x1) << 28)
#define GET_PREV(p) (((p) & GET_PREV_MASK) >> 28)
#define GET_AUTO_MASK	BIT(29)
#define SET_GET_AUTO(p) (((p) & 0x1) >> 29)
#define GET_AUTO(p) (((p) & GET_AUTO_MASK) >> 29)

/* Get Queue Length Control Register */
#define GQC (PSE_BASE + 0x118)
#define QLEN_RETURN_VALUE_MASK (0xfff)
#define GET_QLEN_RETURN_VALUE(p) (((p) & QLEN_RETURN_VALUE_MASK))
#define GET_QLEN_QID_MASK (0x1f << 16)
#define SET_GET_QLEN_QID(p) (((p) & 0x1f) << 16)
#define GET_QLEN_QID(p) (((p) & GET_QLEN_QID_MASK) >> 16)
#define GET_QLEN_PID_MASK (0x3 << 21)
#define SET_GET_QLEN_PID(p) (((p) & 0x3) << 21)
#define GET_QLEN_PID(p) (((p) & GET_QLEN_PID_MASK) >> 21)
#define QLEN_IN_PAGE	BIT(23)
#define GET_QLEN_IN_PAGE(p) (((p) & QLEN_IN_PAGE) >> 23)

/* Flow Control P0 Control Register */
#define FC_P0 (PSE_BASE + 0x120)
#define MIN_RSRV_P0_MASK (0xfff)
#define MIN_RSRV_P0(p) (((p) & 0xfff))
#define GET_MIN_RSRV_P0(p) (((p) & MIN_RSRV_P0_MASK))
#define MAX_QUOTA_P0_MASK (0xfff << 16)
#define MAX_QUOTA_P0(p) (((p) & 0xfff) << 16)
#define GET_MAX_QUOTA_P0(p) (((p) & MAX_QUOTA_P0_MASK) >> 16)

/* Flow Control P1 Control Register */
#define FC_P1 (PSE_BASE + 0x124)
#define MIN_RSRV_P1_MASK (0xfff)
#define MIN_RSRV_P1(p) (((p) & 0xfff))
#define GET_MIN_RSRV_P1(p) (((p) & MIN_RSRV_P1_MASK))
#define MAX_QUOTA_P1_MASK (0xfff << 16)
#define MAX_QUOTA_P1(p) (((p) & 0xfff) << 16)
#define GET_MAX_QUOTA_P1(p) (((p) & MAX_QUOTA_P1_MASK) >> 16)

/* Flow Control P2_RQ0 Control Register */
#define FC_P2Q0 (PSE_BASE + 0x128)
#define MIN_RSRV_P2_RQ0_MASK (0xfff)
#define MIN_RSRV_P2_RQ0(p) (((p) & 0xfff))
#define GET_MIN_RSRV_P2_RQ0(p) (((p) & MIN_RSRV_P2_RQ0_MASK))
#define MAX_QUOTA_P2_RQ0_MASK (0xfff << 16)
#define MAX_QUOTA_P2_RQ0(p) (((p) & 0xfff) << 16)
#define GET_MAX_QUOTA_P2_RQ0(p) (((p) & MAX_QUOTA_P2_RQ0_MASK) >> 16)

/* Flow Control P2_RQ1 Control Register */
#define FC_P2Q1 (PSE_BASE + 0x12c)
#define MIN_RSRV_P2_RQ1_MASK (0xfff)
#define MIN_RSRV_P2_RQ1(p) (((p) & 0xfff))
#define GET_MIN_RSRV_P2_RQ1(p) (((p) & MIN_RSRV_P2_RQ1_MASK))
#define MAX_QUOTA_P2_RQ1_MASK (0xfff << 16)
#define MAX_QUOTA_P2_RQ1(p) (((p) & 0xfff) << 16)
#define GET_MAX_QUOTA_P2_RQ1(p) (((p) & MAX_QUOTA_P2_RQ1_MASK) >> 16)

/* Flow Control P2_RQ2 Control Register */
#define FC_P2Q2 (PSE_BASE + 0x130)
#define MIN_RSRV_P2_RQ2_MASK (0xfff)
#define MIN_RSRV_P2_RQ2(p) (((p) & 0xfff))
#define GET_MIN_RSRV_P2_RQ2(p) (((p) & MIN_RSRV_P2_RQ2_MASK))
#define MAX_QUOTA_P2_RQ2_MASK (0xfff << 16)
#define MAX_QUOTA_P2_RQ2(p) (((p) & 0xfff) << 16)
#define GET_MAX_QUOTA_P2_RQ2(p) (((p) & MAX_QUOTA_P2_RQ2_MASK) >> 16)

/* Flow Control Free for All and Free Page Counter Register */
#define FC_FFC (PSE_BASE + 0x134)
#define FREE_PAGE_CNT_MASK (0xfff)
#define GET_FREE_PAGE_CNT(p) (((p) & FREE_PAGE_CNT_MASK))
#define FFA_CNT_MASK (0xfff << 16)
#define GET_FFA_CNT(p) (((p) & FFA_CNT_MASK) >> 16)

/* Flow Control Reserve from FFA priority Control Register */
#define FC_FRP (PSE_BASE + 0x138)
#define RSRV_PRI_P0_MASK (0x7)
#define RSRV_PRI_P0(p) (((p) & 0x7))
#define GET_RSRV_PRI_P0(p) (((p) & RSRV_PRI_P0_MASK))
#define RSRV_PRI_P1_MASK (0x7 << 3)
#define RSRV_PRI_P1(p) (((p) & 0x7) << 3)
#define GET_RSRV_PRI_P1(p) (((p) & RSRV_PRI_P1_MASK) >> 3)
#define RSRV_PRI_P2_RQ0_MASK (0x7 << 6)
#define RSRV_PRI_P2_RQ0(p) (((p) & 0x7) << 6)
#define GET_RSRV_PRI_P2_RQ0(p) (((p) & RSRV_PRI_P2_RQ0_MASK) >> 6)
#define RSRV_PRI_P2_RQ1_MASK (0x7 << 9)
#define RSRV_PRI_P2_RQ1(p) (((p) & 0x7) << 9)
#define GET_RSRV_PRI_P2_RQ1(p) (((p) & RSRV_PRI_P2_RQ1_MASK) >> 9)
#define RSRV_PRI_P2_RQ2_MASK (0x7 << 12)
#define RSRV_PRI_P2_RQ2(p) (((p) & 0x7) << 12)
#define GET_RSRV_PRI_P2_RQ2(p) (((p) & RSRV_PRI_P2_RQ2_MASK) >> 12)

/* Flow Control P0 and P1 Reserve Counter Register */
#define FC_RP0P1 (PSE_BASE + 0x13c)
#define RSRV_CNT_P0_MASK (0xfff)
#define RSRV_CNT_P0(p) (((p) & 0xfff))
#define GET_RSRV_CNT_P0(p) (((p) & RSRV_CNT_P0_MASK))
#define RSRV_CNT_P1_MASK (0xfff << 16)
#define RSRV_CNT_P1(p) (((p) & 0xfff) << 16)
#define GET_RSRV_CNT_P1(p) (((p) & RSRV_CNT_P1_MASK) >> 16)

/* Flow Control P2_RQ0 and P2_RQ1 Reserve Counter Register */
#define FC_RP2Q0Q1 (PSE_BASE + 0x140)
#define RSRV_CNT_P2_RQ0_MASK (0xfff)
#define RSRV_CNT_P2_RQ0(p) (((p) & 0xfff))
#define GET_RSRV_CNT_P2_RQ0(p) (((p) & RSRV_CNT_P2_RQ0_MASK))
#define RSRV_CNT_P2_RQ1_MASK (0xfff << 16)
#define RSRV_CNT_P2_RQ1(p) (((p) & 0xfff) << 16)
#define GET_RSRV_CNT_P2_RQ1(p) (((p) & RSRV_CNT_P2_RQ1_MASK) >> 16)

/* Flow Control P2_RQ2 Reserve Counter Register */
#define FC_RP2Q2 (PSE_BASE + 0x144)
#define RSRV_CNT_P2_RQ2_MASK (0xfff)
#define RSRV_CNT_P2_RQ2(p) (((p) & 0xfff))
#define GET_RSRV_CNT_P2_RQ2(p) (((p) & RSRV_CNT_P2_RQ2_MASK))

/* Flow Control P0 and P1 Source Counter Register */
#define FC_SP0P1 (PSE_BASE + 0x148)
#define SRC_CNT_P0_MASK (0xfff)
#define GET_SRC_CNT_P0(p) (((p) & SRC_CNT_P0_MASK))
#define SRC_CNT_P1_MASK (0xfff << 16)
#define GET_SRC_CNT_P1(p) (((p) & SRC_CNT_P1_MASK) >> 16)

/* FLow Control P2_RQ0 and P2_RQ1 Source Counter Register */
#define FC_SP2Q0Q1 (PSE_BASE + 0x14c)
#define SRC_CNT_P2_RQ0_MASK (0xfff)
#define GET_SRC_CNT_P2_RQ0(p) (((p) & SRC_CNT_P2_RQ0_MASK))
#define SRC_CNT_P2_RQ1_MASK (0xfff << 16)
#define GET_SRC_CNT_P2_RQ1(p) (((p) & SRC_CNT_P2_RQ1_MASK) >> 16)

/* Flow Control P2_RQ2 Source Counter Register */
#define FC_SP2Q2 (PSE_BASE + 0x150)
#define SRC_CNT_P2_RQ2_MASK (0xfff)
#define GET_SRC_CNT_P2_RQ2(p) (((p) & 0xfff))

#define PSE_RTA (PSE_BASE + 0x194)
#define PSE_RTA_RD_RULE_QID_MASK (0x1f)
#define PSE_RTA_RD_RULE_QID(p) (((p) & 0x1f))
#define GET_PSE_RTA_RD_RULE_QID(p) (((p) & PSE_RTA_RD_RULE_QID_MASK))
#define PSE_RTA_RD_RULE_PID_MASK (0x3 << 5)
#define PSE_RTA_RD_RULE_PID(p) (((p) & 0x3) << 5)
#define GET_PSE_RTA_RD_RULE_PID(p) (((p) & PSE_RTA_RD_RULE_PID_MASK) >> 5)
#define PSE_RTA_RD_RULE_F	BIT(7)
#define GET_PSE_RTA_RD_RULE_F(p) (((p) & PSE_RTA_RD_RULE_F) >> 7)
#define PSE_RTA_TAG_MASK (0xff << 8)
#define PSE_RTA_TAG(p) (((p) & 0xff) << 8)
#define GET_PSE_RTA_TAG(p) (((p) & PSE_RTA_TAG_MASK) >> 8)
#define PSE_RTA_RD_RW	BIT(16)
#define PSE_RTA_RD_KICK_BUSY	BIT(31)
#define GET_PSE_RTA_RD_KICK_BUSY(p) (((p) & PSE_RTA_RD_KICK_BUSY) >> 31)

#define PLE_BASE		0x8000

/* PLE Buffer Control Register */
#define PLE_PBUF_CTRL				(PLE_BASE + 0x14)
#define PLE_TOTAL_PAGE_NUM_MASK	(0xfff)
#define PLE_GET_TOTAL_PAGE_NUM(p) (((p) & 0xfff))
#define PLE_TOTAL_PAGE_CFG_MASK	(0xf << 16)
#define PLE_GET_TOTAL_PAGE_CFG(p) (((p) & PSE_TOTAL_PAGE_CFG_MASK) >> 16)
#define PLE_PBUF_OFFSET_MASK		(0xf << 20)
#define GET_PLE_PBUF_OFFSET(p)	(((p) & PSE_PBUF_OFFSET_MASK) >> 20)

/* Release Control */
#define PLE_RELEASE_CTRL				(PLE_BASE + 0x30)
#define PLE_NORMAL_TX_RLS_QID_MASK	(0x1f)
#define GET_PLE_NORMAL_TX_RLS_QID(p)	(((p) & GET_FIRST_FID_MASK))
#define PLE_NORMAL_TX_RLS_PID_MASK	(0x3 << 6)
#define GET_PLE_NORMAL_TX_RLS_PID(p) (((p) & PLE_NORMAL_TX_RLS_PID_MASK) >> 6)

#define BCNX_RLS_QID_MASK    (0x1f)
#define BCNX_RLS_PID_MASK    (0x3)
#define SET_BCN0_RLS_QID(p)  (((p) & BCNX_RLS_QID_MASK) << 16)
#define SET_BCN0_RLS_PID(p)  (((p) & BCNX_RLS_PID_MASK) << 22)
#define SET_BCN1_RLS_QID(p)  (((p) & BCNX_RLS_QID_MASK) << 24)
#define SET_BCN1_RLS_PID(p)  (((p) & BCNX_RLS_PID_MASK) << 30)

#define PLE_INT_N9_ERR_STS (PLE_BASE + 0x28)

/* HIF Report Control */
#define PLE_HIF_REPORT				(PLE_BASE + 0x34)

/* CPU Interface get frame ID Control */
#define PLE_C_GET_FID_0				(PLE_BASE + 0x40)

/* CPU Interface get frame ID Control */
#define PLE_C_GET_FID_1				(PLE_BASE + 0x44)

#define PLE_C_EN_QUEUE_0                (PLE_BASE + 0x60)
#define PLE_C_EN_QUEUE_1                (PLE_BASE + 0x64)
#define PLE_C_EN_QUEUE_2                (PLE_BASE + 0x68)

#define PLE_C_DE_QUEUE_0                (PLE_BASE + 0x80)
#define PLE_C_DE_QUEUE_1                (PLE_BASE + 0x84)
#define PLE_C_DE_QUEUE_2                (PLE_BASE + 0x88)
#define PLE_C_DE_QUEUE_3                (PLE_BASE + 0x8c)

#define PLE_TO_N9_INT                   (PLE_BASE + 0xf0)
#define PLE_TO_N9_INT_TOGGLE_MASK 0x80000000

/* Queue Empty */
#define PLE_QUEUE_EMPTY			(PLE_BASE + 0xb0)

#define WF_PLE_TOP_TP_LMAC_EN	(PLE_BASE + 0x220)

#define PLE_AC0_QUEUE_EMPTY_0		(PLE_BASE + 0x300)
#define PLE_AC0_QUEUE_EMPTY_1		(PLE_BASE + 0x304)
#define PLE_AC0_QUEUE_EMPTY_2		(PLE_BASE + 0x308)
#define PLE_AC0_QUEUE_EMPTY_3		(PLE_BASE + 0x30c)

#define PLE_AC1_QUEUE_EMPTY_0		(PLE_BASE + 0x310)
#define PLE_AC1_QUEUE_EMPTY_1		(PLE_BASE + 0x314)
#define PLE_AC1_QUEUE_EMPTY_2		(PLE_BASE + 0x318)
#define PLE_AC1_QUEUE_EMPTY_3		(PLE_BASE + 0x31c)

#define PLE_AC2_QUEUE_EMPTY_0		(PLE_BASE + 0x320)
#define PLE_AC2_QUEUE_EMPTY_1		(PLE_BASE + 0x324)
#define PLE_AC2_QUEUE_EMPTY_2		(PLE_BASE + 0x328)
#define PLE_AC2_QUEUE_EMPTY_3		(PLE_BASE + 0x32c)

#define PLE_AC3_QUEUE_EMPTY_0		(PLE_BASE + 0x330)
#define PLE_AC3_QUEUE_EMPTY_1		(PLE_BASE + 0x334)
#define PLE_AC3_QUEUE_EMPTY_2		(PLE_BASE + 0x338)
#define PLE_AC3_QUEUE_EMPTY_3		(PLE_BASE + 0x33c)
#define PLE_WF_PLE_PEEK_CR_0		(PLE_BASE + 0x3D0)
#define PLE_WF_PLE_PEEK_CR_1		(PLE_BASE + 0x3D4)
#define PLE_WF_PLE_PEEK_CR_2		(PLE_BASE + 0x3D8)
#define PLE_WF_PLE_PEEK_CR_3		(PLE_BASE + 0x3DC)
#define PLE_WF_PLE_PEEK_CR_4		(PLE_BASE + 0x3E0)
#define PLE_WF_PLE_PEEK_CR_5		(PLE_BASE + 0x3E4)
#define PLE_WF_PLE_PEEK_CR_6		(PLE_BASE + 0x3E8)
#define PLE_WF_PLE_PEEK_CR_7		(PLE_BASE + 0x3EC)
#define PLE_WF_PLE_PEEK_CR_8		(PLE_BASE + 0x3F0)
#define PLE_WF_PLE_PEEK_CR_9		(PLE_BASE + 0x3F4)
#define PLE_WF_PLE_PEEK_CR_10		(PLE_BASE + 0x3F8)
#define PLE_WF_PLE_PEEK_CR_11		(PLE_BASE + 0x3FC)

#define PLE_AC0_QUEUE_EMPTY_4           (PLE_BASE + 0x600)
#define PLE_AC0_QUEUE_EMPTY_5           (PLE_BASE + 0x604)
#define PLE_AC0_QUEUE_EMPTY_6           (PLE_BASE + 0x608)
#define PLE_AC0_QUEUE_EMPTY_7           (PLE_BASE + 0x60c)

#define PLE_AC1_QUEUE_EMPTY_4           (PLE_BASE + 0x610)
#define PLE_AC1_QUEUE_EMPTY_5           (PLE_BASE + 0x614)
#define PLE_AC1_QUEUE_EMPTY_6           (PLE_BASE + 0x618)
#define PLE_AC1_QUEUE_EMPTY_7           (PLE_BASE + 0x61c)

#define PLE_AC2_QUEUE_EMPTY_4           (PLE_BASE + 0x620)
#define PLE_AC2_QUEUE_EMPTY_5           (PLE_BASE + 0x624)
#define PLE_AC2_QUEUE_EMPTY_6           (PLE_BASE + 0x628)
#define PLE_AC2_QUEUE_EMPTY_7           (PLE_BASE + 0x62c)

#define PLE_AC3_QUEUE_EMPTY_4           (PLE_BASE + 0x630)
#define PLE_AC3_QUEUE_EMPTY_5           (PLE_BASE + 0x634)
#define PLE_AC3_QUEUE_EMPTY_6           (PLE_BASE + 0x638)
#define PLE_AC3_QUEUE_EMPTY_7           (PLE_BASE + 0x63c)

#define PLE_STATION_PAUSE0		(PLE_BASE + 0x360)
#define PLE_STATION_PAUSE1		(PLE_BASE + 0x364)
#define PLE_STATION_PAUSE2		(PLE_BASE + 0x368)
#define PLE_STATION_PAUSE3		(PLE_BASE + 0x36C)

/* Page Flow Control */
#define PLE_FREEPG_CNT				(PLE_BASE + 0x100)

#define PLE_FREEPG_HEAD_TAIL		(PLE_BASE + 0x104)

#define PLE_PG_HIF_GROUP			(PLE_BASE + 0x110)
#define PLE_HIF_PG_INFO				(PLE_BASE + 0x114)

#define PLE_PG_CPU_GROUP			(PLE_BASE + 0x150)
#define PLE_CPU_PG_INFO			(PLE_BASE + 0x154)

/* Indirect path for read/write */
#define PLE_FL_QUE_CTRL_0			(PLE_BASE + 0x1b0)
#define PLE_FL_QUE_CTRL_1			(PLE_BASE + 0x1b4)
#define PLE_FL_QUE_CTRL_2			(PLE_BASE + 0x1b8)
#define PLE_FL_QUE_CTRL_3			(PLE_BASE + 0x1bc)
#define PLE_PL_QUE_CTRL_0			(PLE_BASE + 0x1c0)

/* Disable Station control */
#define DIS_STA_MAP0			(PLE_BASE + 0x260)
#define DIS_STA_MAP1			(PLE_BASE + 0x264)
#define DIS_STA_MAP2			(PLE_BASE + 0x268)
#define DIS_STA_MAP3			(PLE_BASE + 0x26c)

#define DIS_STA_MAP4                    (PLE_BASE + 0x270)
#define DIS_STA_MAP5                    (PLE_BASE + 0x274)
#define DIS_STA_MAP6                    (PLE_BASE + 0x278)
#define DIS_STA_MAP7                    (PLE_BASE + 0x27c)

/* Station Pause control register */
#define STATION_PAUSE0			(PLE_BASE + 0x360)
#define STATION_PAUSE1			(PLE_BASE + 0x364)
#define STATION_PAUSE2			(PLE_BASE + 0x368)
#define STATION_PAUSE3			(PLE_BASE + 0x36c)

#define STATION_PAUSE4                  (PLE_BASE + 0x660)
#define STATION_PAUSE5                  (PLE_BASE + 0x664)
#define STATION_PAUSE6                  (PLE_BASE + 0x668)
#define STATION_PAUSE7                  (PLE_BASE + 0x66c)

/* VOW Ctrl */
#define VOW_RESET_DISABLE	BIT(26)
#define STA_MAX_DEFICIT_MASK    (0x0000FFFF)
#define VOW_DBDC_BW_GROUP_CTRL  (PLE_BASE + 0x2ec)
#define VOW_CONTROL             (PLE_BASE + 0x370)
#define AIRTIME_DRR_SIZE        (PLE_BASE + 0x374)

#define PLE_TX_DELAY_CTRL		(PLE_BASE + 0x440)
#define PLE_TX_DELAY_TIME_THRES_MASK	(0xff)
#define PLE_TX_DELAY_PAGE_THRES_MASK	(0xfff)
#define SET_PLE_TX_DELAY_TIME_THRES(p)  \
	(((p) & PLE_TX_DELAY_TIME_THRES_MASK) << 16)
#define SET_PLE_TX_DELAY_PAGE_THRES(p)  (((p) & PLE_TX_DELAY_PAGE_THRES_MASK))

//----------------------------------------
// DMASHDL
//----------------------------------------
#define MT_HIF_DMASHDL_SLOT_SET0	(DMASHDL_BASE + 0xc4)
#define MT_HIF_DMASHDL_SLOT_SET1	(DMASHDL_BASE + 0xc8)

#define MT_HIF_DMASHDL_PAGE_SET		(DMASHDL_BASE + 0x0c)

/* User program group sequence type control,
 * 0: user program group sequence order type;
 * 1: pre define each slot group strict order
 */
#define DMASHDL_GROUP_SEQ_ORDER_TYPE   BIT(16)

#define MT_HIF_DMASHDL_CTRL_SIGNAL      (DMASHDL_BASE + 0x18)

/* enable to clear the flag of PLE TXD size greater than ple max. size */
#define DMASHDL_PLE_TXD_GT_MAX_SIZE_FLAG_CLR BIT(0)

/* enable packet in substration action from HIF ask period */
#define DMASHDL_HIF_ASK_SUB_ENA        BIT(16)

/* enable packet in substration action from PLE */
#define DMASHDL_PLE_SUB_ENA            BIT(17)

/* enable terminate refill period when PLE release packet to do addition */
#define DMASHDL_PLE_ADD_INT_REFILL_ENA BIT(29)

/* enable terminate refill period when packet in to do addition */
#define DMASHDL_PDMA_ADD_INT_REFILL_ENA BIT(30)

/* enable terminate refill period when packet in to do substration */
#define DMASHDL_PKTIN_INT_REFILL_ENA   BIT(31)

#define MT_HIF_DMASHDL_ERROR_FLAG_CTRL  (DMASHDL_BASE + 0x9c)

#define MT_HIF_DMASHDL_STATUS_RD        (DMASHDL_BASE + 0x100)
#define MT_HIF_DMASHDL_STATUS_RD_GP0    (DMASHDL_BASE + 0x140)
#define MT_HIF_DMASHDL_STATUS_RD_GP1    (DMASHDL_BASE + 0x144)
#define MT_HIF_DMASHDL_STATUS_RD_GP2    (DMASHDL_BASE + 0x148)
#define MT_HIF_DMASHDL_STATUS_RD_GP3    (DMASHDL_BASE + 0x14c)
#define MT_HIF_DMASHDL_STATUS_RD_GP4    (DMASHDL_BASE + 0x150)
#define MT_HIF_DMASHDL_STATUS_RD_GP5    (DMASHDL_BASE + 0x154)
#define MT_HIF_DMASHDL_STATUS_RD_GP6    (DMASHDL_BASE + 0x158)
#define MT_HIF_DMASHDL_STATUS_RD_GP7    (DMASHDL_BASE + 0x15c)
#define MT_HIF_DMASHDL_STATUS_RD_GP8    (DMASHDL_BASE + 0x160)
#define MT_HIF_DMASHDL_STATUS_RD_GP9    (DMASHDL_BASE + 0x164)
#define MT_HIF_DMASHDL_STATUS_RD_GP10   (DMASHDL_BASE + 0x168)
#define MT_HIF_DMASHDL_STATUS_RD_GP11   (DMASHDL_BASE + 0x16c)
#define MT_HIF_DMASHDL_STATUS_RD_GP12   (DMASHDL_BASE + 0x170)
#define MT_HIF_DMASHDL_STATUS_RD_GP13   (DMASHDL_BASE + 0x174)
#define MT_HIF_DMASHDL_STATUS_RD_GP14   (DMASHDL_BASE + 0x178)
#define MT_HIF_DMASHDL_STATUS_RD_GP15   (DMASHDL_BASE + 0x17c)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_0   (DMASHDL_BASE + 0x180)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_1   (DMASHDL_BASE + 0x184)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_2   (DMASHDL_BASE + 0x188)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_3   (DMASHDL_BASE + 0x18c)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_4   (DMASHDL_BASE + 0x190)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_5   (DMASHDL_BASE + 0x194)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_6   (DMASHDL_BASE + 0x198)
#define MT_HIF_DMASHDLRD_GP_PKT_CNT_7   (DMASHDL_BASE + 0x19c)

#define DMASHDL_RSV_CNT_MASK           (0xfff << 16)
#define DMASHDL_SRC_CNT_MASK           (0xfff << 0)
#define DMASHDL_RSV_CNT_OFFSET         16
#define DMASHDL_SRC_CNT_OFFSET         0
#define DMASHDL_FREE_PG_CNT_MASK       (0xfff << 16)
#define DMASHDL_FFA_CNT_MASK           (0xfff << 0)
#define DMASHDL_FREE_PG_CNT_OFFSET     16
#define DMASHDL_FFA_CNT_OFFSET         0

#define ODD_GROUP_ASK_CN_MASK (0xff << 16)
#define ODD_GROUP_ASK_CN_OFFSET 16
#define GET_ODD_GROUP_ASK_CNT(p) \
	(((p) & ODD_GROUP_ASK_CN_MASK) >> ODD_GROUP_ASK_CN_OFFSET)
#define ODD_GROUP_PKT_IN_CN_MASK (0xff << 24)
#define ODD_GROUP_PKT_IN_CN_OFFSET 24
#define GET_ODD_GROUP_PKT_IN_CNT(p) \
	(((p) & ODD_GROUP_PKT_IN_CN_MASK) >> ODD_GROUP_PKT_IN_CN_OFFSET)
#define EVEN_GROUP_ASK_CN_MASK (0xff << 0)
#define EVEN_GROUP_ASK_CN_OFFSET 0
#define GET_EVEN_GROUP_ASK_CNT(p) \
	(((p) & EVEN_GROUP_ASK_CN_MASK) >> EVEN_GROUP_ASK_CN_OFFSET)
#define EVEN_GROUP_PKT_IN_CN_MASK (0xff << 8)
#define EVEN_GROUP_PKT_IN_CN_OFFSET 8
#define GET_EVEN_GROUP_PKT_IN_CNT(p) \
	(((p) & EVEN_GROUP_PKT_IN_CN_MASK) >> EVEN_GROUP_PKT_IN_CN_OFFSET)

//-----------------------
// HIF
//-----------------------
#define CONN_HIF_PDMA_TX_RING0_BASE		MT_HIF(0x300)
#define CONN_HIF_PDMA_TX_RING1_BASE		MT_HIF(0x310)
#define CONN_HIF_PDMA_TX_RING2_BASE		MT_HIF(0x320)
#define CONN_HIF_PDMA_TX_RING3_BASE		MT_HIF(0x330)
#define CONN_HIF_PDMA_TX_RING4_BASE		MT_HIF(0x340)
#define CONN_HIF_PDMA_TX_RING5_BASE		MT_HIF(0x350)
#define CONN_HIF_PDMA_TX_RING15_BASE	MT_HIF(0x3f0)
#define CONN_HIF_PDMA_RX_RING0_BASE		MT_HIF(0x400)
#define CONN_HIF_PDMA_RX_RING1_BASE		MT_HIF(0x410)
#endif
