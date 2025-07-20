// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Balázs Triszka <info@balika011.hu>
 * Copyright (c) 2014-2015, 2022 MediaTek Inc.
 * Author: Chaotian.Jing <chaotian.jing@mediatek.com>
 */

#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/reset.h>

#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>

#define MAX_BD_NUM          1024
#define MSDC_NR_CLOCKS      3

/*--------------------------------------------------------------------------*/
/* Common Definition                                                        */
/*--------------------------------------------------------------------------*/
#define MSDC_BUS_1BITS          0x0
#define MSDC_BUS_4BITS          0x1
#define MSDC_BUS_8BITS          0x2

#define MSDC_BURST_64B          0x6

/*--------------------------------------------------------------------------*/
/* Register Offset                                                          */
/*--------------------------------------------------------------------------*/
#define MSDC_CFG         0x0
#define MSDC_IOCON       0x04
#define MSDC_PS          0x08
#define MSDC_INT         0x0c
#define MSDC_INTEN       0x10
#define MSDC_FIFOCS      0x14
#define SDC_CFG          0x30
#define SDC_CMD          0x34
#define SDC_ARG          0x38
#define SDC_STS          0x3c
#define SDC_RESP0        0x40
#define SDC_RESP1        0x44
#define SDC_RESP2        0x48
#define SDC_RESP3        0x4c
#define SDC_BLK_NUM      0x50
#define SDC_ADV_CFG0     0x64
#define MSDC_NEW_RX_CFG  0x68
#define EMMC_IOCON       0x7c
#define SDC_ACMD_RESP    0x80
#define DMA_SA_H4BIT     0x8c
#define MSDC_DMA_SA      0x90
#define MSDC_DMA_CTRL    0x98
#define MSDC_DMA_CFG     0x9c
#define MSDC_PATCH_BIT   0xb0
#define MSDC_PATCH_BIT1  0xb4
#define MSDC_PATCH_BIT2  0xb8
#define MSDC_PAD_TUNE    0xec
#define MSDC_PAD_TUNE0   0xf0
#define PAD_DS_TUNE      0x188
#define PAD_CMD_TUNE     0x18c
#define EMMC51_CFG0	 0x204
#define EMMC50_CFG0      0x208
#define EMMC50_CFG1      0x20c
#define EMMC50_CFG2      0x21c
#define EMMC50_CFG3      0x220
#define SDC_FIFO_CFG     0x228
#define CQHCI_SETTING	 0x7fc

/*--------------------------------------------------------------------------*/
/* Top Pad Register Offset                                                  */
/*--------------------------------------------------------------------------*/
#define EMMC_TOP_CONTROL	0x00
#define EMMC_TOP_CMD		0x04
#define EMMC50_PAD_DS_TUNE	0x0c
#define LOOP_TEST_CONTROL	0x30

/*--------------------------------------------------------------------------*/
/* Register Mask                                                            */
/*--------------------------------------------------------------------------*/

/* MSDC_CFG mask */
#define MSDC_CFG_MODE           BIT(0)	/* RW */
#define MSDC_CFG_CKPDN          BIT(1)	/* RW */
#define MSDC_CFG_RST            BIT(2)	/* RW */
#define MSDC_CFG_PIO            BIT(3)	/* RW */
#define MSDC_CFG_CKDRVEN        BIT(4)	/* RW */
#define MSDC_CFG_BV18SDT        BIT(5)	/* RW */
#define MSDC_CFG_BV18PSS        BIT(6)	/* R  */
#define MSDC_CFG_CKSTB          BIT(7)	/* R  */
#define MSDC_CFG_CKDIV          GENMASK(15, 8)	/* RW */
#define MSDC_CFG_CKMOD          GENMASK(17, 16)	/* RW */
#define MSDC_CFG_HS400_CK_MODE  BIT(18)	/* RW */
#define MSDC_CFG_HS400_CK_MODE_EXTRA  BIT(22)	/* RW */
#define MSDC_CFG_CKDIV_EXTRA    GENMASK(19, 8)	/* RW */
#define MSDC_CFG_CKMOD_EXTRA    GENMASK(21, 20)	/* RW */

/* MSDC_IOCON mask */
#define MSDC_IOCON_SDR104CKS    BIT(0)	/* RW */
#define MSDC_IOCON_RSPL         BIT(1)	/* RW */
#define MSDC_IOCON_DSPL         BIT(2)	/* RW */
#define MSDC_IOCON_DDLSEL       BIT(3)	/* RW */
#define MSDC_IOCON_DDR50CKD     BIT(4)	/* RW */
#define MSDC_IOCON_DSPLSEL      BIT(5)	/* RW */
#define MSDC_IOCON_W_DSPL       BIT(8)	/* RW */
#define MSDC_IOCON_D0SPL        BIT(16)	/* RW */
#define MSDC_IOCON_D1SPL        BIT(17)	/* RW */
#define MSDC_IOCON_D2SPL        BIT(18)	/* RW */
#define MSDC_IOCON_D3SPL        BIT(19)	/* RW */
#define MSDC_IOCON_D4SPL        BIT(20)	/* RW */
#define MSDC_IOCON_D5SPL        BIT(21)	/* RW */
#define MSDC_IOCON_D6SPL        BIT(22)	/* RW */
#define MSDC_IOCON_D7SPL        BIT(23)	/* RW */
#define MSDC_IOCON_RISCSZ       GENMASK(25, 24)	/* RW */

/* MSDC_PS mask */
#define MSDC_PS_CDEN            BIT(0)	/* RW */
#define MSDC_PS_CDSTS           BIT(1)	/* R  */
#define MSDC_PS_CDDEBOUNCE      GENMASK(15, 12)	/* RW */
#define MSDC_PS_DAT             GENMASK(23, 16)	/* R  */
#define MSDC_PS_DATA1           BIT(17)	/* R  */
#define MSDC_PS_CMD             BIT(24)	/* R  */
#define MSDC_PS_WP              BIT(31)	/* R  */

/* MSDC_INT mask */
#define MSDC_INT_MMCIRQ         BIT(0)	/* W1C */
#define MSDC_INT_CDSC           BIT(1)	/* W1C */
#define MSDC_INT_ACMDRDY        BIT(3)	/* W1C */
#define MSDC_INT_ACMDTMO        BIT(4)	/* W1C */
#define MSDC_INT_ACMDCRCERR     BIT(5)	/* W1C */
#define MSDC_INT_DMAQ_EMPTY     BIT(6)	/* W1C */
#define MSDC_INT_SDIOIRQ        BIT(7)	/* W1C */
#define MSDC_INT_CMDRDY         BIT(8)	/* W1C */
#define MSDC_INT_CMDTMO         BIT(9)	/* W1C */
#define MSDC_INT_RSPCRCERR      BIT(10)	/* W1C */
#define MSDC_INT_CSTA           BIT(11)	/* R */
#define MSDC_INT_XFER_COMPL     BIT(12)	/* W1C */
#define MSDC_INT_DXFER_DONE     BIT(13)	/* W1C */
#define MSDC_INT_DATTMO         BIT(14)	/* W1C */
#define MSDC_INT_DATCRCERR      BIT(15)	/* W1C */
#define MSDC_INT_ACMD19_DONE    BIT(16)	/* W1C */
#define MSDC_INT_DMA_BDCSERR    BIT(17)	/* W1C */
#define MSDC_INT_DMA_GPDCSERR   BIT(18)	/* W1C */
#define MSDC_INT_DMA_PROTECT    BIT(19)	/* W1C */
#define MSDC_INT_CMDQ           BIT(28)	/* W1C */

/* MSDC_INTEN mask */
#define MSDC_INTEN_MMCIRQ       BIT(0)	/* RW */
#define MSDC_INTEN_CDSC         BIT(1)	/* RW */
#define MSDC_INTEN_ACMDRDY      BIT(3)	/* RW */
#define MSDC_INTEN_ACMDTMO      BIT(4)	/* RW */
#define MSDC_INTEN_ACMDCRCERR   BIT(5)	/* RW */
#define MSDC_INTEN_DMAQ_EMPTY   BIT(6)	/* RW */
#define MSDC_INTEN_SDIOIRQ      BIT(7)	/* RW */
#define MSDC_INTEN_CMDRDY       BIT(8)	/* RW */
#define MSDC_INTEN_CMDTMO       BIT(9)	/* RW */
#define MSDC_INTEN_RSPCRCERR    BIT(10)	/* RW */
#define MSDC_INTEN_CSTA         BIT(11)	/* RW */
#define MSDC_INTEN_XFER_COMPL   BIT(12)	/* RW */
#define MSDC_INTEN_DXFER_DONE   BIT(13)	/* RW */
#define MSDC_INTEN_DATTMO       BIT(14)	/* RW */
#define MSDC_INTEN_DATCRCERR    BIT(15)	/* RW */
#define MSDC_INTEN_ACMD19_DONE  BIT(16)	/* RW */
#define MSDC_INTEN_DMA_BDCSERR  BIT(17)	/* RW */
#define MSDC_INTEN_DMA_GPDCSERR BIT(18)	/* RW */
#define MSDC_INTEN_DMA_PROTECT  BIT(19)	/* RW */

/* MSDC_FIFOCS mask */
#define MSDC_FIFOCS_RXCNT       GENMASK(7, 0)	/* R */
#define MSDC_FIFOCS_TXCNT       GENMASK(23, 16)	/* R */
#define MSDC_FIFOCS_CLR         BIT(31)	/* RW */

/* SDC_CFG mask */
#define SDC_CFG_SDIOINTWKUP     BIT(0)	/* RW */
#define SDC_CFG_INSWKUP         BIT(1)	/* RW */
#define SDC_CFG_WRDTOC          GENMASK(14, 2)  /* RW */
#define SDC_CFG_BUSWIDTH        GENMASK(17, 16)	/* RW */
#define SDC_CFG_SDIO            BIT(19)	/* RW */
#define SDC_CFG_SDIOIDE         BIT(20)	/* RW */
#define SDC_CFG_INTATGAP        BIT(21)	/* RW */
#define SDC_CFG_DTOC            GENMASK(31, 24)	/* RW */

/* SDC_STS mask */
#define SDC_STS_SDCBUSY         BIT(0)	/* RW */
#define SDC_STS_CMDBUSY         BIT(1)	/* RW */
#define SDC_STS_SWR_COMPL       BIT(31)	/* RW */

/* SDC_ADV_CFG0 mask */
#define SDC_DAT1_IRQ_TRIGGER	BIT(19)	/* RW */
#define SDC_RX_ENHANCE_EN	BIT(20)	/* RW */
#define SDC_NEW_TX_EN		BIT(31)	/* RW */

/* MSDC_NEW_RX_CFG mask */
#define MSDC_NEW_RX_PATH_SEL	BIT(0)	/* RW */

/* DMA_SA_H4BIT mask */
#define DMA_ADDR_HIGH_4BIT      GENMASK(3, 0)	/* RW */

/* MSDC_DMA_CTRL mask */
#define MSDC_DMA_CTRL_START     BIT(0)	/* W */
#define MSDC_DMA_CTRL_STOP      BIT(1)	/* W */
#define MSDC_DMA_CTRL_RESUME    BIT(2)	/* W */
#define MSDC_DMA_CTRL_MODE      BIT(8)	/* RW */
#define MSDC_DMA_CTRL_LASTBUF   BIT(10)	/* RW */
#define MSDC_DMA_CTRL_BRUSTSZ   GENMASK(14, 12)	/* RW */

/* MSDC_DMA_CFG mask */
#define MSDC_DMA_CFG_STS        BIT(0)	/* R */
#define MSDC_DMA_CFG_DECSEN     BIT(1)	/* RW */
#define MSDC_DMA_CFG_AHBHPROT2  BIT(9)	/* RW */
#define MSDC_DMA_CFG_ACTIVEEN   BIT(13)	/* RW */
#define MSDC_DMA_CFG_CS12B16B   BIT(16)	/* RW */

/* MSDC_PATCH_BIT mask */
#define MSDC_PATCH_BIT_ODDSUPP    BIT(1)	/* RW */
#define MSDC_PATCH_BIT_DIS_WRMON  BIT(2)	/* RW */
#define MSDC_PATCH_BIT_RD_DAT_SEL BIT(3)	/* RW */
#define MSDC_PATCH_BIT_DESCUP_SEL BIT(6)	/* RW */
#define MSDC_INT_DAT_LATCH_CK_SEL GENMASK(9, 7)
#define MSDC_CKGEN_MSDC_DLY_SEL   GENMASK(14, 10)
#define MSDC_PATCH_BIT_IODSSEL    BIT(16)	/* RW */
#define MSDC_PATCH_BIT_IOINTSEL   BIT(17)	/* RW */
#define MSDC_PATCH_BIT_BUSYDLY    GENMASK(21, 18)	/* RW */
#define MSDC_PATCH_BIT_WDOD       GENMASK(25, 22)	/* RW */
#define MSDC_PATCH_BIT_IDRTSEL    BIT(26)	/* RW */
#define MSDC_PATCH_BIT_CMDFSEL    BIT(27)	/* RW */
#define MSDC_PATCH_BIT_INTDLSEL   BIT(28)	/* RW */
#define MSDC_PATCH_BIT_SPCPUSH    BIT(29)	/* RW */
#define MSDC_PATCH_BIT_DECRCTMO   BIT(30)	/* RW */

/* MSDC_PATCH_BIT1 mask */
#define MSDC_PB1_WRDAT_CRC_TACNTR GENMASK(2, 0)     /* RW */
#define MSDC_PATCH_BIT1_CMDTA     GENMASK(5, 3)     /* RW */
#define MSDC_PB1_BUSY_CHECK_SEL   BIT(7)    /* RW */
#define MSDC_PATCH_BIT1_STOP_DLY  GENMASK(11, 8)    /* RW */
#define MSDC_PB1_DDR_CMD_FIX_SEL  BIT(14)           /* RW */
#define MSDC_PB1_SINGLE_BURST     BIT(16)           /* RW */
#define MSDC_PB1_RSVD20           GENMASK(18, 17)   /* RW */
#define MSDC_PB1_AUTO_SYNCST_CLR  BIT(19)           /* RW */
#define MSDC_PB1_MARK_POP_WATER   BIT(20)           /* RW */
#define MSDC_PB1_LP_DCM_EN        BIT(21)           /* RW */
#define MSDC_PB1_RSVD3            BIT(22)           /* RW */
#define MSDC_PB1_AHB_GDMA_HCLK    BIT(23)           /* RW */
#define MSDC_PB1_MSDC_CLK_ENFEAT  GENMASK(31, 24)   /* RW */

/* MSDC_PATCH_BIT2 mask */
#define MSDC_PATCH_BIT2_CFGRESP   BIT(15)   /* RW */
#define MSDC_PATCH_BIT2_CFGCRCSTS BIT(28)   /* RW */
#define MSDC_PB2_SUPPORT_64G      BIT(1)    /* RW */
#define MSDC_PB2_RESPWAIT         GENMASK(3, 2)   /* RW */
#define MSDC_PB2_RESPSTSENSEL     GENMASK(18, 16) /* RW */
#define MSDC_PB2_POP_EN_CNT       GENMASK(23, 20) /* RW */
#define MSDC_PB2_CFGCRCSTSEDGE    BIT(25)   /* RW */
#define MSDC_PB2_CRCSTSENSEL      GENMASK(31, 29) /* RW */

#define MSDC_PAD_TUNE_DATWRDLY	  GENMASK(4, 0)		/* RW */
#define MSDC_PAD_TUNE_DATRRDLY	  GENMASK(12, 8)	/* RW */
#define MSDC_PAD_TUNE_DATRRDLY2	  GENMASK(12, 8)	/* RW */
#define MSDC_PAD_TUNE_CMDRDLY	  GENMASK(20, 16)	/* RW */
#define MSDC_PAD_TUNE_CMDRDLY2	  GENMASK(20, 16)	/* RW */
#define MSDC_PAD_TUNE_CMDRRDLY	  GENMASK(26, 22)	/* RW */
#define MSDC_PAD_TUNE_CLKTDLY	  GENMASK(31, 27)	/* RW */
#define MSDC_PAD_TUNE_RXDLYSEL	  BIT(15)   /* RW */
#define MSDC_PAD_TUNE_RD_SEL	  BIT(13)   /* RW */
#define MSDC_PAD_TUNE_CMD_SEL	  BIT(21)   /* RW */
#define MSDC_PAD_TUNE_RD2_SEL	  BIT(13)   /* RW */
#define MSDC_PAD_TUNE_CMD2_SEL	  BIT(21)   /* RW */

#define PAD_DS_TUNE_DLY_SEL       BIT(0)	  /* RW */
#define PAD_DS_TUNE_DLY2_SEL      BIT(1)	  /* RW */
#define PAD_DS_TUNE_DLY1	  GENMASK(6, 2)   /* RW */
#define PAD_DS_TUNE_DLY2	  GENMASK(11, 7)  /* RW */
#define PAD_DS_TUNE_DLY3	  GENMASK(16, 12) /* RW */

#define PAD_CMD_TUNE_RX_DLY3	  GENMASK(5, 1)   /* RW */

/* EMMC51_CFG0 mask */
#define CMDQ_RDAT_CNT		  GENMASK(21, 12) /* RW */

#define EMMC50_CFG_PADCMD_LATCHCK BIT(0)   /* RW */
#define EMMC50_CFG_CRCSTS_EDGE    BIT(3)   /* RW */
#define EMMC50_CFG_CFCSTS_SEL     BIT(4)   /* RW */
#define EMMC50_CFG_CMD_RESP_SEL   BIT(9)   /* RW */

/* EMMC50_CFG1 mask */
#define EMMC50_CFG1_DS_CFG        BIT(28)  /* RW */

/* EMMC50_CFG2 mask */
#define EMMC50_CFG2_AXI_SET_LEN   GENMASK(27, 24) /* RW */

#define EMMC50_CFG3_OUTS_WR       GENMASK(4, 0)   /* RW */

#define SDC_FIFO_CFG_WRVALIDSEL   BIT(24)  /* RW */
#define SDC_FIFO_CFG_RDVALIDSEL   BIT(25)  /* RW */

/* CQHCI_SETTING */
#define CQHCI_RD_CMD_WND_SEL	  BIT(14) /* RW */
#define CQHCI_WR_CMD_WND_SEL	  BIT(15) /* RW */

/* EMMC_TOP_CONTROL mask */
#define PAD_RXDLY_SEL           BIT(0)      /* RW */
#define DELAY_EN                BIT(1)      /* RW */
#define PAD_DAT_RD_RXDLY2       GENMASK(6, 2)     /* RW */
#define PAD_DAT_RD_RXDLY        GENMASK(11, 7)    /* RW */
#define PAD_DAT_RD_RXDLY2_SEL   BIT(12)     /* RW */
#define PAD_DAT_RD_RXDLY_SEL    BIT(13)     /* RW */
#define DATA_K_VALUE_SEL        BIT(14)     /* RW */
#define SDC_RX_ENH_EN           BIT(15)     /* TW */

/* EMMC_TOP_CMD mask */
#define PAD_CMD_RXDLY2          GENMASK(4, 0)	/* RW */
#define PAD_CMD_RXDLY           GENMASK(9, 5)	/* RW */
#define PAD_CMD_RD_RXDLY2_SEL   BIT(10)		/* RW */
#define PAD_CMD_RD_RXDLY_SEL    BIT(11)		/* RW */
#define PAD_CMD_TX_DLY          GENMASK(16, 12)	/* RW */

/* EMMC50_PAD_DS_TUNE mask */
#define PAD_DS_DLY_SEL		BIT(16)	/* RW */
#define PAD_DS_DLY2_SEL		BIT(15)	/* RW */
#define PAD_DS_DLY1		GENMASK(14, 10)	/* RW */
#define PAD_DS_DLY3		GENMASK(4, 0)	/* RW */

/* LOOP_TEST_CONTROL mask */
#define TEST_LOOP_DSCLK_MUX_SEL        BIT(0)	/* RW */
#define TEST_LOOP_LATCH_MUX_SEL        BIT(1)	/* RW */
#define LOOP_EN_SEL_CLK                BIT(20)	/* RW */
#define TEST_HS400_CMD_LOOP_MUX_SEL    BIT(31)	/* RW */

#define REQ_CMD_EIO  BIT(0)
#define REQ_CMD_TMO  BIT(1)
#define REQ_DAT_ERR  BIT(2)
#define REQ_STOP_EIO BIT(3)
#define REQ_STOP_TMO BIT(4)
#define REQ_CMD_BUSY BIT(5)

#define MSDC_PREPARE_FLAG BIT(0)
#define MSDC_ASYNC_FLAG BIT(1)
#define MSDC_MMAP_FLAG BIT(2)

#define MTK_MMC_AUTOSUSPEND_DELAY	50
#define CMD_TIMEOUT         (HZ/10 * 5)	/* 100ms x5 */
#define DAT_TIMEOUT         (HZ    * 5)	/* 1000ms x5 */

#define DEFAULT_DEBOUNCE	(8)	/* 8 cycles CD debounce */

#define TUNING_REG2_FIXED_OFFEST	4
#define PAD_DELAY_HALF	32 /* PAD delay cells */
#define PAD_DELAY_FULL	64

/*--------------------------------------------------------------------------*/
/* Descriptor Structure                                                     */
/*--------------------------------------------------------------------------*/
struct mt_gpdma_desc {
	u32 gpd_info;
#define GPDMA_DESC_HWO		BIT(0)
#define GPDMA_DESC_BDP		BIT(1)
#define GPDMA_DESC_CHECKSUM	GENMASK(15, 8)
#define GPDMA_DESC_INT		BIT(16)
#define GPDMA_DESC_NEXT_H4	GENMASK(27, 24)
#define GPDMA_DESC_PTR_H4	GENMASK(31, 28)
	u32 next;
	u32 ptr;
	u32 gpd_data_len;
#define GPDMA_DESC_BUFLEN	GENMASK(15, 0)
#define GPDMA_DESC_EXTLEN	GENMASK(23, 16)
	u32 arg;
	u32 blknum;
	u32 cmd;
};

struct mt_bdma_desc {
	u32 bd_info;
#define BDMA_DESC_EOL		BIT(0)
#define BDMA_DESC_CHECKSUM	GENMASK(15, 8)
#define BDMA_DESC_BLKPAD	BIT(17)
#define BDMA_DESC_DWPAD		BIT(18)
#define BDMA_DESC_NEXT_H4	GENMASK(27, 24)
#define BDMA_DESC_PTR_H4	GENMASK(31, 28)
	u32 next;
	u32 ptr;
	u32 bd_data_len;
#define BDMA_DESC_BUFLEN	GENMASK(15, 0)
#define BDMA_DESC_BUFLEN_EXT	GENMASK(23, 0)
};

struct msdc_dma {
	struct scatterlist *sg;	/* I/O scatter list */
	struct mt_gpdma_desc *gpd;		/* pointer to gpd array */
	struct mt_bdma_desc *bd;		/* pointer to bd array */
	dma_addr_t gpd_addr;	/* the physical address of gpd array */
	dma_addr_t bd_addr;	/* the physical address of bd array */
};

struct msdc_save_para {
	u32 msdc_cfg;
	u32 iocon;
	u32 ps;
	u32 inten;
	u32 sdc_cfg;
	u32 pad_tune;
	u32 patch_bit0;
	u32 patch_bit1;
	u32 patch_bit2;
	u32 pad_ds_tune;
	u32 pad_cmd_tune;
	u32 emmc51_cfg0;
	u32 emmc50_cfg0;
	u32 emmc50_cfg1;
	u32 emmc50_cfg3;
	u32 sdc_fifo_cfg;
	u32 cqhci_setting;
	u32 emmc_top_control;
	u32 emmc_top_cmd;
	u32 emmc50_pad_ds_tune;
	u32 loop_test_control;
};

struct mtk_mmc_compatible {
	u8 clk_div_bits;
	bool recheck_sdio_irq;
	bool hs400_tune; /* only used for MT8173 */
	bool needs_top_base;
	u32 pad_tune_reg;
	bool async_fifo;
	bool data_tune;
	bool busy_check;
	bool stop_clk_fix;
	u8 stop_dly_sel;
	u8 pop_en_cnt;
	bool enhance_rx;
	bool support_64g;
	bool use_internal_cd;
	bool support_new_tx;
	bool support_new_rx;
};

struct msdc_tune_para {
	u32 iocon;
	u32 pad_tune;
	u32 pad_cmd_tune;
	u32 emmc_top_control;
	u32 emmc_top_cmd;
};

struct msdc_delay_phase {
	u8 maxlen;
	u8 start;
	u8 final_phase;
};

struct msdc_drv {
	struct device *dev;
	void __iomem *base;		/* host base address */
	void __iomem *top_base;		/* host top register base address */

	struct pinctrl *pinctrl;

	int irq;		/* host interrupt */

	struct clk *src_clk;	/* msdc source clock */
	struct clk *h_clk;      /* msdc h_clk */
	struct clk *bus_clk;	/* bus clock which used to access register */
	struct clk *src_clk_cg; /* msdc source clock control gate */
	struct clk *sys_clk_cg;	/* msdc subsys clock control gate */
	struct clk *crypto_clk; /* msdc crypto clock control gate */
	struct clk_bulk_data bulk_clks[MSDC_NR_CLOCKS];
	u32 src_clk_freq;	/* source clock frequency */

	u32 max_slot_count;
	struct platform_device **slots;

	spinlock_t active_lock;
	int active_slot_id;
	int activation_count;
};

struct msdc_host {
	u32 id;
	struct device *dev;
	const struct mtk_mmc_compatible *dev_comp;
	int cmd_rsp;

	spinlock_t lock;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;
	int error;

	struct msdc_dma dma;	/* dma channel */
	u64 dma_mask;

	u32 timeout_ns;		/* data timeout ns */
	u32 timeout_clks;	/* data timeout clks */

	const char *pins_default_name;
	const char *pins_uhs_name;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_uhs;
	struct delayed_work req_timeout;
	struct reset_control *reset;

	u32 mclk;		/* mmc subsystem clock frequency */
	unsigned char timing;
	bool vqmmc_enabled;
	u32 latch_ck;
	u32 hs400_ds_delay;
	u32 hs400_ds_dly3;
	u32 hs200_cmd_int_delay; /* cmd internal delay for HS200/SDR104 */
	u32 hs400_cmd_int_delay; /* cmd internal delay for HS400 */
	u32 tuning_step;
	bool hs400_cmd_resp_sel_rising; /* cmd response sample selection for HS400 */
	bool hs400_mode;	/* current eMMC will run at hs400 mode */
	bool hs400_tuning;	/* hs400 mode online tuning */
	bool internal_cd;	/* Use internal card-detect logic */
	struct msdc_save_para save_para; /* used when gate HCLK */
	struct msdc_tune_para def_tune_para; /* default tune setting */
	struct msdc_tune_para saved_tune_para; /* tune result of CMD21/CMD19 */
	int signal_voltage;
};

static const struct mtk_mmc_compatible mt2701_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt2712_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt6779_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt6795_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = false,
	.hs400_tune = true,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt7620_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.use_internal_cd = true,
};

static const struct mtk_mmc_compatible mt7622_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt7986_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.needs_top_base = true,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt8135_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt8173_compat = {
	.clk_div_bits = 8,
	.recheck_sdio_irq = true,
	.hs400_tune = true,
	.pad_tune_reg = MSDC_PAD_TUNE,
	.async_fifo = false,
	.data_tune = false,
	.busy_check = false,
	.stop_clk_fix = false,
	.enhance_rx = false,
	.support_64g = false,
};

static const struct mtk_mmc_compatible mt8183_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.needs_top_base = true,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
	.enhance_rx = true,
	.support_64g = true,
};

static const struct mtk_mmc_compatible mt8516_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = true,
	.hs400_tune = false,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 3,
};

static const struct mtk_mmc_compatible mt8196_compat = {
	.clk_div_bits = 12,
	.recheck_sdio_irq = false,
	.hs400_tune = false,
	.needs_top_base = true,
	.pad_tune_reg = MSDC_PAD_TUNE0,
	.async_fifo = true,
	.data_tune = true,
	.busy_check = true,
	.stop_clk_fix = true,
	.stop_dly_sel = 1,
	.pop_en_cnt = 2,
	.enhance_rx = true,
	.support_64g = true,
	.support_new_tx = true,
	.support_new_rx = true,
};

static const struct of_device_id msdc_of_ids[] = {
	{ .compatible = "mediatek,mt2701-mmc", .data = &mt2701_compat },
	{ .compatible = "mediatek,mt2712-mmc", .data = &mt2712_compat },
	{ .compatible = "mediatek,mt6779-mmc", .data = &mt6779_compat },
	{ .compatible = "mediatek,mt6795-mmc", .data = &mt6795_compat },
	{ .compatible = "mediatek,mt7620-mmc", .data = &mt7620_compat },
	{ .compatible = "mediatek,mt7622-mmc", .data = &mt7622_compat },
	{ .compatible = "mediatek,mt7986-mmc", .data = &mt7986_compat },
	{ .compatible = "mediatek,mt7988-mmc", .data = &mt7986_compat },
	{ .compatible = "mediatek,mt8135-mmc", .data = &mt8135_compat },
	{ .compatible = "mediatek,mt8173-mmc", .data = &mt8173_compat },
	{ .compatible = "mediatek,mt8183-mmc", .data = &mt8183_compat },
	{ .compatible = "mediatek,mt8196-mmc", .data = &mt8196_compat },
	{ .compatible = "mediatek,mt8516-mmc", .data = &mt8516_compat },
	{}
};
MODULE_DEVICE_TABLE(of, msdc_of_ids);

static const struct of_device_id msdc_slot_of_ids[] = {
	{ .compatible = "mediatek,mmc-slot" },
	{}
};
MODULE_DEVICE_TABLE(of, msdc_slot_of_ids);

static void sdr_set_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val |= bs;
	writel(val, reg);
}

static void sdr_clr_bits(void __iomem *reg, u32 bs)
{
	u32 val = readl(reg);

	val &= ~bs;
	writel(val, reg);
}

static void sdr_set_field(void __iomem *reg, u32 field, u32 val)
{
	unsigned int tv = readl(reg);

	tv &= ~field;
	tv |= ((val) << (ffs((unsigned int)field) - 1));
	writel(tv, reg);
}

static void sdr_get_field(void __iomem *reg, u32 field, u32 *val)
{
	unsigned int tv = readl(reg);

	*val = ((tv & field) >> (ffs((unsigned int)field) - 1));
}

static void msdc_reset_hw(struct msdc_drv *drv)
{
	u32 val;

	sdr_set_bits(drv->base + MSDC_CFG, MSDC_CFG_RST);
	readl_poll_timeout_atomic(drv->base + MSDC_CFG, val, !(val & MSDC_CFG_RST), 0, 0);

	sdr_set_bits(drv->base + MSDC_FIFOCS, MSDC_FIFOCS_CLR);
	readl_poll_timeout_atomic(drv->base + MSDC_FIFOCS, val,
				  !(val & MSDC_FIFOCS_CLR), 0, 0);

	val = readl(drv->base + MSDC_INT);
	writel(val, drv->base + MSDC_INT);
}

static void msdc_cmd_next(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd);
static void __msdc_enable_sdio_irq(struct msdc_host *host, int enb);
static int msdc_restore_reg(struct msdc_host *host);
static void msdc_save_reg(struct msdc_host *host);

static const u32 cmd_ints_mask = MSDC_INTEN_CMDRDY | MSDC_INTEN_RSPCRCERR |
			MSDC_INTEN_CMDTMO | MSDC_INTEN_ACMDRDY |
			MSDC_INTEN_ACMDCRCERR | MSDC_INTEN_ACMDTMO;
static const u32 data_ints_mask = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO |
			MSDC_INTEN_DATCRCERR | MSDC_INTEN_DMA_BDCSERR |
			MSDC_INTEN_DMA_GPDCSERR | MSDC_INTEN_DMA_PROTECT;

static u8 msdc_dma_calcs(u8 *buf, u32 len)
{
	u32 i, sum = 0;

	for (i = 0; i < len; i++)
		sum += buf[i];
	return 0xff - (u8) sum;
}

static inline void msdc_dma_setup(struct msdc_host *host, struct msdc_dma *dma,
		struct mmc_data *data)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	unsigned int j, dma_len;
	dma_addr_t dma_address;
	u32 dma_ctrl;
	struct scatterlist *sg;
	struct mt_gpdma_desc *gpd;
	struct mt_bdma_desc *bd;

	sg = data->sg;

	gpd = dma->gpd;
	bd = dma->bd;

	/* modify gpd */
	gpd->gpd_info |= GPDMA_DESC_HWO;
	gpd->gpd_info |= GPDMA_DESC_BDP;
	/* need to clear first. use these bits to calc checksum */
	gpd->gpd_info &= ~GPDMA_DESC_CHECKSUM;
	gpd->gpd_info |= msdc_dma_calcs((u8 *) gpd, 16) << 8;

	/* modify bd */
	for_each_sg(data->sg, sg, data->sg_count, j) {
		dma_address = sg_dma_address(sg);
		dma_len = sg_dma_len(sg);

		/* init bd */
		bd[j].bd_info &= ~BDMA_DESC_BLKPAD;
		bd[j].bd_info &= ~BDMA_DESC_DWPAD;
		bd[j].ptr = lower_32_bits(dma_address);
		if (host->dev_comp->support_64g) {
			bd[j].bd_info &= ~BDMA_DESC_PTR_H4;
			bd[j].bd_info |= (upper_32_bits(dma_address) & 0xf)
					 << 28;
		}

		if (host->dev_comp->support_64g) {
			bd[j].bd_data_len &= ~BDMA_DESC_BUFLEN_EXT;
			bd[j].bd_data_len |= (dma_len & BDMA_DESC_BUFLEN_EXT);
		} else {
			bd[j].bd_data_len &= ~BDMA_DESC_BUFLEN;
			bd[j].bd_data_len |= (dma_len & BDMA_DESC_BUFLEN);
		}

		if (j == data->sg_count - 1) /* the last bd */
			bd[j].bd_info |= BDMA_DESC_EOL;
		else
			bd[j].bd_info &= ~BDMA_DESC_EOL;

		/* checksum need to clear first */
		bd[j].bd_info &= ~BDMA_DESC_CHECKSUM;
		bd[j].bd_info |= msdc_dma_calcs((u8 *)(&bd[j]), 16) << 8;
	}

	sdr_set_field(drv->base + MSDC_DMA_CFG, MSDC_DMA_CFG_DECSEN, 1);
	dma_ctrl = readl_relaxed(drv->base + MSDC_DMA_CTRL);
	dma_ctrl &= ~(MSDC_DMA_CTRL_BRUSTSZ | MSDC_DMA_CTRL_MODE);
	dma_ctrl |= (MSDC_BURST_64B << 12 | BIT(8));
	writel_relaxed(dma_ctrl, drv->base + MSDC_DMA_CTRL);
	if (host->dev_comp->support_64g)
		sdr_set_field(drv->base + DMA_SA_H4BIT, DMA_ADDR_HIGH_4BIT,
			      upper_32_bits(dma->gpd_addr) & 0xf);
	writel(lower_32_bits(dma->gpd_addr), drv->base + MSDC_DMA_SA);
}

static void msdc_prepare_data(struct msdc_host *host, struct mmc_data *data)
{
	if (!(data->host_cookie & MSDC_PREPARE_FLAG)) {
		data->host_cookie |= MSDC_PREPARE_FLAG;
		data->sg_count = dma_map_sg(host->dev, data->sg, data->sg_len,
					    mmc_get_dma_dir(data));
	}
}

static void msdc_unprepare_data(struct msdc_host *host, struct mmc_data *data)
{
	if (data->host_cookie & MSDC_ASYNC_FLAG)
		return;

	if (data->host_cookie & MSDC_PREPARE_FLAG) {
		dma_unmap_sg(host->dev, data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
		data->host_cookie &= ~MSDC_PREPARE_FLAG;
	}
}

static u64 msdc_timeout_cal(struct msdc_host *host, u64 ns, u64 clks)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u64 timeout;
	u32 clk_ns, mode = 0;

	if (mmc->actual_clock == 0) {
		timeout = 0;
	} else {
		clk_ns = 1000000000U / mmc->actual_clock;
		timeout = ns + clk_ns - 1;
		do_div(timeout, clk_ns);
		timeout += clks;
		/* in 1048576 sclk cycle unit */
		timeout = DIV_ROUND_UP(timeout, BIT(20));
		if (host->dev_comp->clk_div_bits == 8)
			sdr_get_field(drv->base + MSDC_CFG,
				      MSDC_CFG_CKMOD, &mode);
		else
			sdr_get_field(drv->base + MSDC_CFG,
				      MSDC_CFG_CKMOD_EXTRA, &mode);
		/*DDR mode will double the clk cycles for data timeout */
		timeout = mode >= 2 ? timeout * 2 : timeout;
		timeout = timeout > 1 ? timeout - 1 : 0;
	}
	return timeout;
}

/* clock control primitives */
static void msdc_set_timeout(struct msdc_host *host, u64 ns, u64 clks)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u64 timeout;

	host->timeout_ns = ns;
	host->timeout_clks = clks;

	timeout = msdc_timeout_cal(host, ns, clks);
	sdr_set_field(drv->base + SDC_CFG, SDC_CFG_DTOC,
		      min_t(u32, timeout, 255));
}

static void msdc_gate_clock(struct msdc_drv *drv)
{
	clk_bulk_disable_unprepare(MSDC_NR_CLOCKS, drv->bulk_clks);
	clk_disable_unprepare(drv->crypto_clk);
	clk_disable_unprepare(drv->src_clk_cg);
	clk_disable_unprepare(drv->src_clk);
	clk_disable_unprepare(drv->bus_clk);
	clk_disable_unprepare(drv->h_clk);
}

static int msdc_ungate_clock(struct msdc_drv *drv)
{
	u32 val;
	int ret;

	clk_prepare_enable(drv->h_clk);
	clk_prepare_enable(drv->bus_clk);
	clk_prepare_enable(drv->src_clk);
	clk_prepare_enable(drv->src_clk_cg);
	clk_prepare_enable(drv->crypto_clk);
	ret = clk_bulk_prepare_enable(MSDC_NR_CLOCKS, drv->bulk_clks);
	if (ret) {
		dev_err(drv->dev, "Cannot enable pclk/axi/ahb clock gates\n");
		return ret;
	}

	return readl_poll_timeout(drv->base + MSDC_CFG, val,
				  (val & MSDC_CFG_CKSTB), 1, 20000);
}

static void msdc_new_tx_setting(struct msdc_host *host)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);

	if (!drv->top_base)
		return;

	sdr_set_bits(drv->top_base + LOOP_TEST_CONTROL,
		     TEST_LOOP_DSCLK_MUX_SEL);
	sdr_set_bits(drv->top_base + LOOP_TEST_CONTROL,
		     TEST_LOOP_LATCH_MUX_SEL);
	sdr_clr_bits(drv->top_base + LOOP_TEST_CONTROL,
		     TEST_HS400_CMD_LOOP_MUX_SEL);

	switch (host->timing) {
	case MMC_TIMING_LEGACY:
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		sdr_clr_bits(drv->top_base + LOOP_TEST_CONTROL,
			     LOOP_EN_SEL_CLK);
		break;
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
	case MMC_TIMING_MMC_HS400:
		sdr_set_bits(drv->top_base + LOOP_TEST_CONTROL,
			     LOOP_EN_SEL_CLK);
		break;
	default:
		break;
	}
}

static void msdc_set_mclk(struct msdc_host *host, unsigned char timing, u32 hz)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 mode;
	u32 flags;
	u32 div;
	u32 sclk;
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	u32 val;
	bool timing_changed;

	if (!hz) {
		dev_dbg(host->dev, "set mclk to 0\n");
		host->mclk = 0;
		mmc->actual_clock = 0;
		sdr_clr_bits(drv->base + MSDC_CFG, MSDC_CFG_CKPDN);
		return;
	}

	if (host->timing != timing)
		timing_changed = true;
	else
		timing_changed = false;

	flags = readl(drv->base + MSDC_INTEN);
	sdr_clr_bits(drv->base + MSDC_INTEN, flags);
	if (host->dev_comp->clk_div_bits == 8)
		sdr_clr_bits(drv->base + MSDC_CFG, MSDC_CFG_HS400_CK_MODE);
	else
		sdr_clr_bits(drv->base + MSDC_CFG,
			     MSDC_CFG_HS400_CK_MODE_EXTRA);
	if (timing == MMC_TIMING_UHS_DDR50 ||
	    timing == MMC_TIMING_MMC_DDR52 ||
	    timing == MMC_TIMING_MMC_HS400) {
		if (timing == MMC_TIMING_MMC_HS400)
			mode = 0x3;
		else
			mode = 0x2; /* ddr mode and use divisor */

		if (hz >= (drv->src_clk_freq >> 2)) {
			div = 0; /* mean div = 1/4 */
			sclk = drv->src_clk_freq >> 2; /* sclk = clk / 4 */
		} else {
			div = (drv->src_clk_freq + ((hz << 2) - 1)) / (hz << 2);
			sclk = (drv->src_clk_freq >> 2) / div;
			div = (div >> 1);
		}

		if (timing == MMC_TIMING_MMC_HS400 &&
		    hz >= (drv->src_clk_freq >> 1)) {
			if (host->dev_comp->clk_div_bits == 8)
				sdr_set_bits(drv->base + MSDC_CFG,
					     MSDC_CFG_HS400_CK_MODE);
			else
				sdr_set_bits(drv->base + MSDC_CFG,
					     MSDC_CFG_HS400_CK_MODE_EXTRA);
			sclk = drv->src_clk_freq >> 1;
			div = 0; /* div is ignore when bit18 is set */
		}
	} else if (hz >= drv->src_clk_freq) {
		mode = 0x1; /* no divisor */
		div = 0;
		sclk = drv->src_clk_freq;
	} else {
		mode = 0x0; /* use divisor */
		if (hz >= (drv->src_clk_freq >> 1)) {
			div = 0; /* mean div = 1/2 */
			sclk = drv->src_clk_freq >> 1; /* sclk = clk / 2 */
		} else {
			div = (drv->src_clk_freq + ((hz << 2) - 1)) / (hz << 2);
			sclk = (drv->src_clk_freq >> 2) / div;
		}
	}
	sdr_clr_bits(drv->base + MSDC_CFG, MSDC_CFG_CKPDN);

	clk_disable_unprepare(drv->src_clk_cg);
	if (host->dev_comp->clk_div_bits == 8)
		sdr_set_field(drv->base + MSDC_CFG,
			      MSDC_CFG_CKMOD | MSDC_CFG_CKDIV,
			      (mode << 8) | div);
	else
		sdr_set_field(drv->base + MSDC_CFG,
			      MSDC_CFG_CKMOD_EXTRA | MSDC_CFG_CKDIV_EXTRA,
			      (mode << 12) | div);

	clk_prepare_enable(drv->src_clk_cg);
	readl_poll_timeout(drv->base + MSDC_CFG, val, (val & MSDC_CFG_CKSTB), 0, 0);
	sdr_set_bits(drv->base + MSDC_CFG, MSDC_CFG_CKPDN);
	mmc->actual_clock = sclk;
	host->mclk = hz;
	host->timing = timing;
	/* need because clk changed. */
	msdc_set_timeout(host, host->timeout_ns, host->timeout_clks);
	sdr_set_bits(drv->base + MSDC_INTEN, flags);

	/*
	 * mmc_select_hs400() will drop to 50Mhz and High speed mode,
	 * tune result of hs200/200Mhz is not suitable for 50Mhz
	 */
	if (mmc->actual_clock <= 52000000) {
		writel(host->def_tune_para.iocon, drv->base + MSDC_IOCON);
		if (drv->top_base) {
			writel(host->def_tune_para.emmc_top_control,
			       drv->top_base + EMMC_TOP_CONTROL);
			writel(host->def_tune_para.emmc_top_cmd,
			       drv->top_base + EMMC_TOP_CMD);
		} else {
			writel(host->def_tune_para.pad_tune,
			       drv->base + tune_reg);
		}
	} else {
		writel(host->saved_tune_para.iocon, drv->base + MSDC_IOCON);
		writel(host->saved_tune_para.pad_cmd_tune,
		       drv->base + PAD_CMD_TUNE);
		if (drv->top_base) {
			writel(host->saved_tune_para.emmc_top_control,
			       drv->top_base + EMMC_TOP_CONTROL);
			writel(host->saved_tune_para.emmc_top_cmd,
			       drv->top_base + EMMC_TOP_CMD);
		} else {
			writel(host->saved_tune_para.pad_tune,
			       drv->base + tune_reg);
		}
	}

	if (timing == MMC_TIMING_MMC_HS400 &&
	    host->dev_comp->hs400_tune)
		sdr_set_field(drv->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs400_cmd_int_delay);
	if (host->dev_comp->support_new_tx && timing_changed)
		msdc_new_tx_setting(host);

	dev_dbg(host->dev, "sclk: %d, timing: %d\n", mmc->actual_clock,
		timing);
}

static inline u32 msdc_cmd_find_resp(struct msdc_host *host,
		struct mmc_command *cmd)
{
	u32 resp;

	switch (mmc_resp_type(cmd)) {
	/* Actually, R1, R5, R6, R7 are the same */
	case MMC_RSP_R1:
		resp = 0x1;
		break;
	case MMC_RSP_R1B:
	case MMC_RSP_R1B_NO_CRC:
		resp = 0x7;
		break;
	case MMC_RSP_R2:
		resp = 0x2;
		break;
	case MMC_RSP_R3:
		resp = 0x3;
		break;
	case MMC_RSP_NONE:
	default:
		resp = 0x0;
		break;
	}

	return resp;
}

static inline u32 msdc_cmd_prepare_raw_cmd(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	/* rawcmd :
	 * vol_swt << 30 | auto_cmd << 28 | blklen << 16 | go_irq << 15 |
	 * stop << 14 | rw << 13 | dtype << 11 | rsptyp << 7 | brk << 6 | opcode
	 */
	u32 opcode = cmd->opcode;
	u32 resp = msdc_cmd_find_resp(host, cmd);
	u32 rawcmd = (opcode & 0x3f) | ((resp & 0x7) << 7);

	host->cmd_rsp = resp;

	if ((opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int) -1) ||
	    opcode == MMC_STOP_TRANSMISSION)
		rawcmd |= BIT(14);
	else if (opcode == SD_SWITCH_VOLTAGE)
		rawcmd |= BIT(30);
	else if (opcode == SD_APP_SEND_SCR ||
		 opcode == SD_APP_SEND_NUM_WR_BLKS ||
		 (opcode == SD_SWITCH && mmc_cmd_type(cmd) == MMC_CMD_ADTC) ||
		 (opcode == SD_APP_SD_STATUS && mmc_cmd_type(cmd) == MMC_CMD_ADTC) ||
		 (opcode == MMC_SEND_EXT_CSD && mmc_cmd_type(cmd) == MMC_CMD_ADTC))
		rawcmd |= BIT(11);

	if (cmd->data) {
		struct mmc_data *data = cmd->data;

		if (mmc_op_multi(opcode)) {
			if (mmc_card_mmc(mmc->card) && mrq->sbc &&
			    !(mrq->sbc->arg & 0xFFFF0000))
				rawcmd |= BIT(29); /* AutoCMD23 */
		}

		rawcmd |= ((data->blksz & 0xFFF) << 16);
		if (data->flags & MMC_DATA_WRITE)
			rawcmd |= BIT(13);
		if (data->blocks > 1)
			rawcmd |= BIT(12);
		else
			rawcmd |= BIT(11);
		/* Always use dma mode */
		sdr_clr_bits(drv->base + MSDC_CFG, MSDC_CFG_PIO);

		if (host->timeout_ns != data->timeout_ns ||
		    host->timeout_clks != data->timeout_clks)
			msdc_set_timeout(host, data->timeout_ns,
					data->timeout_clks);

		writel(data->blocks, drv->base + SDC_BLK_NUM);
	}
	return rawcmd;
}

static void msdc_start_data(struct msdc_host *host, struct mmc_command *cmd,
		struct mmc_data *data)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	bool read;

	WARN_ON(host->data);
	host->data = data;
	read = data->flags & MMC_DATA_READ;

	mod_delayed_work(system_wq, &host->req_timeout, DAT_TIMEOUT);
	msdc_dma_setup(host, &host->dma, data);
	sdr_set_bits(drv->base + MSDC_INTEN, data_ints_mask);
	sdr_set_field(drv->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_START, 1);
	dev_dbg(host->dev, "DMA start\n");
	dev_dbg(host->dev, "%s: cmd=%d DMA data: %d blocks; read=%d\n",
			__func__, cmd->opcode, data->blocks, read);
}

static int msdc_auto_cmd_done(struct msdc_host *host, int events,
		struct mmc_command *cmd)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 *rsp = cmd->resp;

	rsp[0] = readl(drv->base + SDC_ACMD_RESP);

	if (events & MSDC_INT_ACMDRDY) {
		cmd->error = 0;
	} else {
		msdc_reset_hw(drv);
		if (events & MSDC_INT_ACMDCRCERR) {
			cmd->error = -EILSEQ;
			host->error |= REQ_STOP_EIO;
		} else if (events & MSDC_INT_ACMDTMO) {
			cmd->error = -ETIMEDOUT;
			host->error |= REQ_STOP_TMO;
		}
		dev_err(host->dev,
			"%s: AUTO_CMD%d arg=%08X; rsp %08X; cmd_error=%d\n",
			__func__, cmd->opcode, cmd->arg, rsp[0], cmd->error);
	}
	return cmd->error;
}

/*
 * msdc_recheck_sdio_irq - recheck whether the SDIO irq is lost
 *
 * Host controller may lost interrupt in some special case.
 * Add SDIO irq recheck mechanism to make sure all interrupts
 * can be processed immediately
 */
static void msdc_recheck_sdio_irq(struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 reg_int, reg_inten, reg_ps;

	if (mmc->caps & MMC_CAP_SDIO_IRQ) {
		reg_inten = readl(drv->base + MSDC_INTEN);
		if (reg_inten & MSDC_INTEN_SDIOIRQ) {
			reg_int = readl(drv->base + MSDC_INT);
			reg_ps = readl(drv->base + MSDC_PS);
			if (!(reg_int & MSDC_INT_SDIOIRQ ||
			      reg_ps & MSDC_PS_DATA1)) {
				__msdc_enable_sdio_irq(host, 0);
				sdio_signal_irq(mmc);
			}
		}
	}
}

static void msdc_track_cmd_data(struct msdc_host *host, struct mmc_command *cmd)
{
	if (host->error &&
	    ((!mmc_op_tuning(cmd->opcode) && !host->hs400_tuning) ||
	     cmd->error == -ETIMEDOUT))
		dev_warn(host->dev, "%s: cmd=%d arg=%08X; host->error=0x%08X\n",
			 __func__, cmd->opcode, cmd->arg, host->error);
}

static void msdc_request_done(struct msdc_host *host, struct mmc_request *mrq)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	struct mmc_host *mmc = mmc_from_priv(host);
	unsigned long flags;

	/*
	 * No need check the return value of cancel_delayed_work, as only ONE
	 * path will go here!
	 */
	cancel_delayed_work(&host->req_timeout);

	spin_lock_irqsave(&host->lock, flags);
	host->mrq = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	msdc_track_cmd_data(host, mrq->cmd);
	if (mrq->data)
		msdc_unprepare_data(host, mrq->data);
	if (host->error)
		msdc_reset_hw(drv);
	mmc_request_done(mmc, mrq);
	if (host->dev_comp->recheck_sdio_irq)
		msdc_recheck_sdio_irq(host);

	msdc_save_reg(host);
}

/* returns true if command is fully handled; returns false otherwise */
static bool msdc_cmd_done(struct msdc_host *host, int events,
			  struct mmc_request *mrq, struct mmc_command *cmd)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	bool done = false;
	bool sbc_error;
	unsigned long flags;
	u32 *rsp;

	if (mrq->sbc && cmd == mrq->cmd &&
	    (events & (MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR
				   | MSDC_INT_ACMDTMO)))
		msdc_auto_cmd_done(host, events, mrq->sbc);

	sbc_error = mrq->sbc && mrq->sbc->error;

	if (!sbc_error && !(events & (MSDC_INT_CMDRDY
					| MSDC_INT_RSPCRCERR
					| MSDC_INT_CMDTMO)))
		return done;

	spin_lock_irqsave(&host->lock, flags);
	done = !host->cmd;
	host->cmd = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	if (done)
		return true;
	rsp = cmd->resp;

	sdr_clr_bits(drv->base + MSDC_INTEN, cmd_ints_mask);

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			rsp[0] = readl(drv->base + SDC_RESP3);
			rsp[1] = readl(drv->base + SDC_RESP2);
			rsp[2] = readl(drv->base + SDC_RESP1);
			rsp[3] = readl(drv->base + SDC_RESP0);
		} else {
			rsp[0] = readl(drv->base + SDC_RESP0);
		}
	}

	if (!sbc_error && !(events & MSDC_INT_CMDRDY)) {
		if ((events & MSDC_INT_CMDTMO && !host->hs400_tuning) ||
		    (!mmc_op_tuning(cmd->opcode) && !host->hs400_tuning))
			/*
			 * should not clear fifo/interrupt as the tune data
			 * may have already come when cmd19/cmd21 gets response
			 * CRC error.
			 */
			msdc_reset_hw(drv);
		if (events & MSDC_INT_RSPCRCERR &&
		    mmc_resp_type(cmd) != MMC_RSP_R1B_NO_CRC) {
			cmd->error = -EILSEQ;
			host->error |= REQ_CMD_EIO;
		} else if (events & MSDC_INT_CMDTMO) {
			cmd->error = -ETIMEDOUT;
			host->error |= REQ_CMD_TMO;
		}
	}
	if (cmd->error)
		dev_dbg(host->dev,
				"%s: cmd=%d arg=%08X; rsp %08X; cmd_error=%d\n",
				__func__, cmd->opcode, cmd->arg, rsp[0],
				cmd->error);

	msdc_cmd_next(host, mrq, cmd);
	return true;
}

/* It is the core layer's responsibility to ensure card status
 * is correct before issue a request. but host design do below
 * checks recommended.
 */
static inline bool msdc_cmd_is_ready(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 val;
	int ret;

	/* The max busy time we can endure is 20ms */
	ret = readl_poll_timeout_atomic(drv->base + SDC_STS, val,
					!(val & SDC_STS_CMDBUSY), 1, 20000);
	if (ret) {
		dev_err(host->dev, "CMD bus busy detected\n");
		host->error |= REQ_CMD_BUSY;
		msdc_cmd_done(host, MSDC_INT_CMDTMO, mrq, cmd);
		return false;
	}

	if (mmc_resp_type(cmd) == MMC_RSP_R1B || cmd->data) {
		/* R1B or with data, should check SDCBUSY */
		ret = readl_poll_timeout_atomic(drv->base + SDC_STS, val,
						!(val & SDC_STS_SDCBUSY), 1, 20000);
		if (ret) {
			dev_err(host->dev, "Controller busy detected\n");
			host->error |= REQ_CMD_BUSY;
			msdc_cmd_done(host, MSDC_INT_CMDTMO, mrq, cmd);
			return false;
		}
	}
	return true;
}

static void msdc_start_command(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 rawcmd;
	unsigned long flags;

	WARN_ON(host->cmd);
	host->cmd = cmd;

	mod_delayed_work(system_wq, &host->req_timeout, DAT_TIMEOUT);
	if (!msdc_cmd_is_ready(host, mrq, cmd))
		return;

	if ((readl(drv->base + MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16 ||
	    readl(drv->base + MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) {
		dev_err(host->dev, "TX/RX FIFO non-empty before start of IO. Reset\n");
		msdc_reset_hw(drv);
	}

	cmd->error = 0;
	rawcmd = msdc_cmd_prepare_raw_cmd(host, mrq, cmd);

	spin_lock_irqsave(&host->lock, flags);
	sdr_set_bits(drv->base + MSDC_INTEN, cmd_ints_mask);
	spin_unlock_irqrestore(&host->lock, flags);

	writel(cmd->arg, drv->base + SDC_ARG);
	writel(rawcmd, drv->base + SDC_CMD);
}

static void msdc_cmd_next(struct msdc_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	if ((cmd->error && !host->hs400_tuning &&
	     !(cmd->error == -EILSEQ &&
	     mmc_op_tuning(cmd->opcode))) ||
	    (mrq->sbc && mrq->sbc->error))
		msdc_request_done(host, mrq);
	else if (cmd == mrq->sbc)
		msdc_start_command(host, mrq, mrq->cmd);
	else if (!cmd->data)
		msdc_request_done(host, mrq);
	else
		msdc_start_data(host, cmd, cmd->data);
}

static void msdc_ops_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);

	if (msdc_restore_reg(host) < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return;
	}

	host->error = 0;
	WARN_ON(host->mrq);
	host->mrq = mrq;

	if (mrq->data)
		msdc_prepare_data(host, mrq->data);

	/* if SBC is required, we have HW option and SW option.
	 * if HW option is enabled, and SBC does not have "special" flags,
	 * use HW option,  otherwise use SW option
	 */
	if (mrq->sbc && (!mmc_card_mmc(mmc->card) ||
	    (mrq->sbc->arg & 0xFFFF0000)))
		msdc_start_command(host, mrq, mrq->sbc);
	else
		msdc_start_command(host, mrq, mrq->cmd);
}

static void msdc_data_xfer_next(struct msdc_host *host, struct mmc_request *mrq)
{
	if (mmc_op_multi(mrq->cmd->opcode) && mrq->stop && !mrq->stop->error &&
	    !mrq->sbc)
		msdc_start_command(host, mrq, mrq->stop);
	else
		msdc_request_done(host, mrq);
}

static void msdc_data_xfer_done(struct msdc_host *host, u32 events,
				struct mmc_request *mrq, struct mmc_data *data)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	struct mmc_command *stop;
	unsigned long flags;
	bool done;
	unsigned int check_data = events &
	    (MSDC_INT_XFER_COMPL | MSDC_INT_DATCRCERR | MSDC_INT_DATTMO
	     | MSDC_INT_DMA_BDCSERR | MSDC_INT_DMA_GPDCSERR
	     | MSDC_INT_DMA_PROTECT);
	u32 val;
	int ret;

	spin_lock_irqsave(&host->lock, flags);
	done = !host->data;
	if (check_data)
		host->data = NULL;
	spin_unlock_irqrestore(&host->lock, flags);

	if (done)
		return;
	stop = data->stop;

	if (check_data || (stop && stop->error)) {
		dev_dbg(host->dev, "DMA status: 0x%8X\n",
				readl(drv->base + MSDC_DMA_CFG));
		sdr_set_field(drv->base + MSDC_DMA_CTRL, MSDC_DMA_CTRL_STOP,
				1);

		ret = readl_poll_timeout_atomic(drv->base + MSDC_DMA_CTRL, val,
						!(val & MSDC_DMA_CTRL_STOP), 1, 20000);
		if (ret)
			dev_dbg(host->dev, "DMA stop timed out\n");

		ret = readl_poll_timeout_atomic(drv->base + MSDC_DMA_CFG, val,
						!(val & MSDC_DMA_CFG_STS), 1, 20000);
		if (ret)
			dev_dbg(host->dev, "DMA inactive timed out\n");

		sdr_clr_bits(drv->base + MSDC_INTEN, data_ints_mask);
		dev_dbg(host->dev, "DMA stop\n");

		if ((events & MSDC_INT_XFER_COMPL) && (!stop || !stop->error)) {
			data->bytes_xfered = data->blocks * data->blksz;
		} else {
			dev_dbg(host->dev, "interrupt events: %x\n", events);
			msdc_reset_hw(drv);
			host->error |= REQ_DAT_ERR;
			data->bytes_xfered = 0;

			if (events & MSDC_INT_DATTMO)
				data->error = -ETIMEDOUT;
			else if (events & MSDC_INT_DATCRCERR)
				data->error = -EILSEQ;

			dev_dbg(host->dev, "%s: cmd=%d; blocks=%d",
				__func__, mrq->cmd->opcode, data->blocks);
			dev_dbg(host->dev, "data_error=%d xfer_size=%d\n",
				(int)data->error, data->bytes_xfered);
		}

		msdc_data_xfer_next(host, mrq);
	}
}

static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 val = readl(drv->base + SDC_CFG);

	val &= ~SDC_CFG_BUSWIDTH;

	switch (width) {
	default:
	case MMC_BUS_WIDTH_1:
		val |= (MSDC_BUS_1BITS << 16);
		break;
	case MMC_BUS_WIDTH_4:
		val |= (MSDC_BUS_4BITS << 16);
		break;
	case MMC_BUS_WIDTH_8:
		val |= (MSDC_BUS_8BITS << 16);
		break;
	}

	writel(val, drv->base + SDC_CFG);
	dev_dbg(host->dev, "Bus Width = %d", width);
}

static int msdc_ops_switch_volt(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	int ret;

	ret = msdc_restore_reg(host);
	if (ret < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return ret;
	}

	if (!IS_ERR(mmc->supply.vqmmc)) {
		if (ios->signal_voltage != MMC_SIGNAL_VOLTAGE_330 &&
		    ios->signal_voltage != MMC_SIGNAL_VOLTAGE_180) {
			dev_err(host->dev, "Unsupported signal voltage!\n");
			ret = -EINVAL;
			goto out;
		}

		ret = mmc_regulator_set_vqmmc(mmc, ios);
		if (ret < 0) {
			dev_dbg(host->dev, "Regulator set error %d (%d)\n",
				ret, ios->signal_voltage);
			goto out;
		}

		/* Apply different pinctrl settings for different signal voltage */
		if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180)
			pinctrl_select_state(drv->pinctrl, host->pins_uhs);
		else
			pinctrl_select_state(drv->pinctrl, host->pins_default);

		host->signal_voltage = ios->signal_voltage;
	}

	ret = 0;
out:
	msdc_save_reg(host);
	return ret;
}

static int msdc_card_busy(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	int ret;

	ret = msdc_restore_reg(host);
	if (ret < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return ret;
	}

	/* only check if data0 is low */
	ret = !(readl(drv->base + MSDC_PS) & BIT(16));

	msdc_save_reg(host);

	return ret;
}

static void msdc_request_timeout(struct work_struct *work)
{
	struct msdc_host *host = container_of(work, struct msdc_host,
			req_timeout.work);

	/* simulate HW timeout status */
	dev_err(host->dev, "%s: aborting cmd/data/mrq\n", __func__);
	if (host->mrq) {
		dev_err(host->dev, "%s: aborting mrq=%p cmd=%d\n", __func__,
				host->mrq, host->mrq->cmd->opcode);
		if (host->cmd) {
			dev_err(host->dev, "%s: aborting cmd=%d\n",
					__func__, host->cmd->opcode);
			msdc_cmd_done(host, MSDC_INT_CMDTMO, host->mrq,
					host->cmd);
		} else if (host->data) {
			dev_err(host->dev, "%s: abort data: cmd%d; %d blocks\n",
					__func__, host->mrq->cmd->opcode,
					host->data->blocks);
			msdc_data_xfer_done(host, MSDC_INT_DATTMO, host->mrq,
					host->data);
		}
	}
}

static void __msdc_enable_sdio_irq(struct msdc_host *host, int enb)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	if (enb) {
		sdr_set_bits(drv->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_set_bits(drv->base + SDC_CFG, SDC_CFG_SDIOIDE);
		if (host->dev_comp->recheck_sdio_irq)
			msdc_recheck_sdio_irq(host);
	} else {
		sdr_clr_bits(drv->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_clr_bits(drv->base + SDC_CFG, SDC_CFG_SDIOIDE);
	}
}

static void msdc_enable_sdio_irq(struct mmc_host *mmc, int enb)
{
	struct msdc_host *host = mmc_priv(mmc);
	unsigned long flags;

	if (msdc_restore_reg(host) < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return;
	}

	spin_lock_irqsave(&host->lock, flags);
	__msdc_enable_sdio_irq(host, enb);
	spin_unlock_irqrestore(&host->lock, flags);

	msdc_save_reg(host);
}

static irqreturn_t msdc_irq(int irq, void *dev_id)
{
	struct msdc_drv *drv = (struct msdc_drv *) dev_id;
	struct platform_device *host_pdev;
	struct mmc_host *mmc;
	struct msdc_host *host;

	BUG_ON(drv->active_slot_id == -1);

	host_pdev = drv->slots[drv->active_slot_id];

	BUG_ON(!host_pdev);

	mmc = platform_get_drvdata(host_pdev);
	host = mmc_priv(mmc);

	while (true) {
		struct mmc_request *mrq;
		struct mmc_command *cmd;
		struct mmc_data *data;
		u32 events, event_mask;

		spin_lock(&host->lock);
		events = readl(drv->base + MSDC_INT);
		event_mask = readl(drv->base + MSDC_INTEN);
		if ((events & event_mask) & MSDC_INT_SDIOIRQ)
			__msdc_enable_sdio_irq(host, 0);
		/* clear interrupts */
		writel(events & event_mask, drv->base + MSDC_INT);

		mrq = host->mrq;
		cmd = host->cmd;
		data = host->data;
		spin_unlock(&host->lock);

		if ((events & event_mask) & MSDC_INT_SDIOIRQ)
			sdio_signal_irq(mmc);

		if ((events & event_mask) & MSDC_INT_CDSC) {
			if (host->internal_cd)
				mmc_detect_change(mmc, msecs_to_jiffies(20));
			events &= ~MSDC_INT_CDSC;
		}

		if (!(events & (event_mask & ~MSDC_INT_SDIOIRQ)))
			break;

		if (!mrq) {
			dev_err(host->dev,
				"%s: MRQ=NULL; events=%08X; event_mask=%08X\n",
				__func__, events, event_mask);
			WARN_ON(1);
			break;
		}

		dev_dbg(host->dev, "%s: events=%08X\n", __func__, events);

		if (cmd)
			msdc_cmd_done(host, events, mrq, cmd);
		else if (data)
			msdc_data_xfer_done(host, events, mrq, data);
	}

	return IRQ_HANDLED;
}

static void msdc_init_hw(struct msdc_host *host)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 val, pb1_val, pb2_val;

	u32 tune_reg = host->dev_comp->pad_tune_reg;
	struct mmc_host *mmc = mmc_from_priv(host);

	if (host->reset) {
		reset_control_assert(host->reset);
		usleep_range(10, 50);
		reset_control_deassert(host->reset);
	}

	/* New tx/rx enable bit need to be 0->1 for hardware check */
	if (host->dev_comp->support_new_tx) {
		sdr_clr_bits(drv->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
		sdr_set_bits(drv->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
		msdc_new_tx_setting(host);
	}
	if (host->dev_comp->support_new_rx) {
		sdr_clr_bits(drv->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
		sdr_set_bits(drv->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
	}

	/* Configure to MMC/SD mode, clock free running */
	sdr_set_bits(drv->base + MSDC_CFG, MSDC_CFG_MODE | MSDC_CFG_CKPDN);

	/* Reset */
	msdc_reset_hw(drv);

	/* Disable and clear all interrupts */
	writel(0, drv->base + MSDC_INTEN);
	val = readl(drv->base + MSDC_INT);
	writel(val, drv->base + MSDC_INT);

	/* Configure card detection */
	if (host->internal_cd) {
		sdr_set_field(drv->base + MSDC_PS, MSDC_PS_CDDEBOUNCE,
			      DEFAULT_DEBOUNCE);
		sdr_set_bits(drv->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_set_bits(drv->base + MSDC_INTEN, MSDC_INTEN_CDSC);
		sdr_set_bits(drv->base + SDC_CFG, SDC_CFG_INSWKUP);
	} else {
		sdr_clr_bits(drv->base + SDC_CFG, SDC_CFG_INSWKUP);
		sdr_clr_bits(drv->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(drv->base + MSDC_INTEN, MSDC_INTEN_CDSC);
	}

	if (drv->top_base) {
		writel(0, drv->top_base + EMMC_TOP_CONTROL);
		writel(0, drv->top_base + EMMC_TOP_CMD);
	} else {
		writel(0, drv->base + tune_reg);
	}

	writel(0, drv->base + MSDC_IOCON);
	sdr_set_field(drv->base + MSDC_IOCON, MSDC_IOCON_DDLSEL, 0);

	/*
	 * Patch bit 0 and 1 are completely rewritten, but for patch bit 2
	 * defaults are retained and, if necessary, only some bits are fixed
	 * up: read the PB2 register here for later usage in this function.
	 */
	pb2_val = readl(drv->base + MSDC_PATCH_BIT2);

	/* Enable odd number support for 8-bit data bus */
	val = MSDC_PATCH_BIT_ODDSUPP;

	/* Disable SD command register write monitor */
	val |= MSDC_PATCH_BIT_DIS_WRMON;

	/* Issue transfer done interrupt after GPD update */
	val |= MSDC_PATCH_BIT_DESCUP_SEL;

	/* Extend R1B busy detection delay (in clock cycles) */
	val |= FIELD_PREP(MSDC_PATCH_BIT_BUSYDLY, 15);

	/* Enable CRC phase timeout during data write operation */
	val |= MSDC_PATCH_BIT_DECRCTMO;

	/* Set CKGEN delay to one stage */
	val |= FIELD_PREP(MSDC_CKGEN_MSDC_DLY_SEL, 1);

	/* First MSDC_PATCH_BIT setup is done: pull the trigger! */
	writel(val, drv->base + MSDC_PATCH_BIT);

	/* Set wr data, crc status, cmd response turnaround period for UHS104 */
	pb1_val = FIELD_PREP(MSDC_PB1_WRDAT_CRC_TACNTR, 1);
	pb1_val |= FIELD_PREP(MSDC_PATCH_BIT1_CMDTA, 1);
	pb1_val |= MSDC_PB1_DDR_CMD_FIX_SEL;

	/* Support 'single' burst type only when AXI_LEN is 0 */
	sdr_get_field(drv->base + EMMC50_CFG2, EMMC50_CFG2_AXI_SET_LEN, &val);
	if (!val)
		pb1_val |= MSDC_PB1_SINGLE_BURST;

	/* Set auto sync state clear, block gap stop clk */
	pb1_val |= MSDC_PB1_RSVD20 | MSDC_PB1_AUTO_SYNCST_CLR | MSDC_PB1_MARK_POP_WATER;

	/* Set low power DCM, use HCLK for GDMA, use MSDC CLK for everything else */
	pb1_val |= MSDC_PB1_LP_DCM_EN | MSDC_PB1_RSVD3 |
		   MSDC_PB1_AHB_GDMA_HCLK | MSDC_PB1_MSDC_CLK_ENFEAT;

	/* If needed, enable R1b command busy check at controller init time */
	if (!host->dev_comp->busy_check)
		pb1_val |= MSDC_PB1_BUSY_CHECK_SEL;

	if (host->dev_comp->stop_clk_fix) {
		if (host->dev_comp->stop_dly_sel)
			pb1_val |= FIELD_PREP(MSDC_PATCH_BIT1_STOP_DLY,
					      host->dev_comp->stop_dly_sel);

		if (host->dev_comp->pop_en_cnt) {
			pb2_val &= ~MSDC_PB2_POP_EN_CNT;
			pb2_val |= FIELD_PREP(MSDC_PB2_POP_EN_CNT,
					      host->dev_comp->pop_en_cnt);
		}

		sdr_clr_bits(drv->base + SDC_FIFO_CFG, SDC_FIFO_CFG_WRVALIDSEL);
		sdr_clr_bits(drv->base + SDC_FIFO_CFG, SDC_FIFO_CFG_RDVALIDSEL);
	}

	if (host->dev_comp->async_fifo) {
		/* Set CMD response timeout multiplier to 65 + (16 * 3) cycles */
		pb2_val &= ~MSDC_PB2_RESPWAIT;
		pb2_val |= FIELD_PREP(MSDC_PB2_RESPWAIT, 3);

		/* eMMC4.5: Select async FIFO path for CMD resp and CRC status */
		pb2_val &= ~MSDC_PATCH_BIT2_CFGRESP;
		pb2_val |= MSDC_PATCH_BIT2_CFGCRCSTS;

		if (!host->dev_comp->enhance_rx) {
			/* eMMC4.5: Delay 2T for CMD resp and CRC status EN signals */
			pb2_val &= ~(MSDC_PB2_RESPSTSENSEL | MSDC_PB2_CRCSTSENSEL);
			pb2_val |= FIELD_PREP(MSDC_PB2_RESPSTSENSEL, 2);
			pb2_val |= FIELD_PREP(MSDC_PB2_CRCSTSENSEL, 2);
		} else if (drv->top_base) {
			sdr_set_bits(drv->top_base + EMMC_TOP_CONTROL, SDC_RX_ENH_EN);
		} else {
			sdr_set_bits(drv->base + SDC_ADV_CFG0, SDC_RX_ENHANCE_EN);
		}
	}

	if (host->dev_comp->support_64g)
		pb2_val |= MSDC_PB2_SUPPORT_64G;

	/* Patch Bit 1/2 setup is done: pull the trigger! */
	writel(pb1_val, drv->base + MSDC_PATCH_BIT1);
	writel(pb2_val, drv->base + MSDC_PATCH_BIT2);
	sdr_set_bits(drv->base + EMMC50_CFG0, EMMC50_CFG_CFCSTS_SEL);

	if (host->dev_comp->data_tune) {
		if (drv->top_base) {
			u32 top_ctl_val = readl(drv->top_base + EMMC_TOP_CONTROL);
			u32 top_cmd_val = readl(drv->top_base + EMMC_TOP_CMD);

			top_cmd_val |= PAD_CMD_RD_RXDLY_SEL;
			top_ctl_val |= PAD_DAT_RD_RXDLY_SEL;
			top_ctl_val &= ~DATA_K_VALUE_SEL;
			if (host->tuning_step > PAD_DELAY_HALF) {
				top_cmd_val |= PAD_CMD_RD_RXDLY2_SEL;
				top_ctl_val |= PAD_DAT_RD_RXDLY2_SEL;
			}

			writel(top_ctl_val, drv->top_base + EMMC_TOP_CONTROL);
			writel(top_cmd_val, drv->top_base + EMMC_TOP_CMD);
		} else {
			sdr_set_bits(drv->base + tune_reg,
				     MSDC_PAD_TUNE_RD_SEL |
				     MSDC_PAD_TUNE_CMD_SEL);
			if (host->tuning_step > PAD_DELAY_HALF)
				sdr_set_bits(drv->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
					     MSDC_PAD_TUNE_RD2_SEL |
					     MSDC_PAD_TUNE_CMD2_SEL);
		}
	} else {
		/* choose clock tune */
		if (drv->top_base)
			sdr_set_bits(drv->top_base + EMMC_TOP_CONTROL,
				     PAD_RXDLY_SEL);
		else
			sdr_set_bits(drv->base + tune_reg,
				     MSDC_PAD_TUNE_RXDLYSEL);
	}

	if (mmc->caps2 & MMC_CAP2_NO_SDIO) {
		sdr_clr_bits(drv->base + SDC_CFG, SDC_CFG_SDIO);
		sdr_clr_bits(drv->base + MSDC_INTEN, MSDC_INTEN_SDIOIRQ);
		sdr_clr_bits(drv->base + SDC_ADV_CFG0, SDC_DAT1_IRQ_TRIGGER);
	} else {
		/* Configure to enable SDIO mode, otherwise SDIO CMD5 fails */
		sdr_set_bits(drv->base + SDC_CFG, SDC_CFG_SDIO);

		/* Config SDIO device detect interrupt function */
		sdr_clr_bits(drv->base + SDC_CFG, SDC_CFG_SDIOIDE);
		sdr_set_bits(drv->base + SDC_ADV_CFG0, SDC_DAT1_IRQ_TRIGGER);
	}

	/* Configure to default data timeout */
	sdr_set_field(drv->base + SDC_CFG, SDC_CFG_DTOC, 3);

	host->def_tune_para.iocon = readl(drv->base + MSDC_IOCON);
	host->saved_tune_para.iocon = readl(drv->base + MSDC_IOCON);
	if (drv->top_base) {
		host->def_tune_para.emmc_top_control =
			readl(drv->top_base + EMMC_TOP_CONTROL);
		host->def_tune_para.emmc_top_cmd =
			readl(drv->top_base + EMMC_TOP_CMD);
		host->saved_tune_para.emmc_top_control =
			readl(drv->top_base + EMMC_TOP_CONTROL);
		host->saved_tune_para.emmc_top_cmd =
			readl(drv->top_base + EMMC_TOP_CMD);
	} else {
		host->def_tune_para.pad_tune = readl(drv->base + tune_reg);
		host->saved_tune_para.pad_tune = readl(drv->base + tune_reg);
	}
	dev_dbg(host->dev, "init hardware done!");
}

static void msdc_deinit_hw(struct msdc_host *host)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 val;

	if (host->internal_cd) {
		/* Disabled card-detect */
		sdr_clr_bits(drv->base + MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(drv->base + SDC_CFG, SDC_CFG_INSWKUP);
	}

	/* Disable and clear all interrupts */
	writel(0, drv->base + MSDC_INTEN);

	val = readl(drv->base + MSDC_INT);
	writel(val, drv->base + MSDC_INT);
}

/* init gpd and bd list in msdc_drv_probe */
static void msdc_init_gpd_bd(struct msdc_host *host, struct msdc_dma *dma)
{
	struct mt_gpdma_desc *gpd = dma->gpd;
	struct mt_bdma_desc *bd = dma->bd;
	dma_addr_t dma_addr;
	int i;

	memset(gpd, 0, sizeof(struct mt_gpdma_desc) * 2);

	dma_addr = dma->gpd_addr + sizeof(struct mt_gpdma_desc);
	gpd->gpd_info = GPDMA_DESC_BDP; /* hwo, cs, bd pointer */
	/* gpd->next is must set for desc DMA
	 * That's why must alloc 2 gpd structure.
	 */
	gpd->next = lower_32_bits(dma_addr);
	if (host->dev_comp->support_64g)
		gpd->gpd_info |= (upper_32_bits(dma_addr) & 0xf) << 24;

	dma_addr = dma->bd_addr;
	gpd->ptr = lower_32_bits(dma->bd_addr); /* physical address */
	if (host->dev_comp->support_64g)
		gpd->gpd_info |= (upper_32_bits(dma_addr) & 0xf) << 28;

	memset(bd, 0, sizeof(struct mt_bdma_desc) * MAX_BD_NUM);
	for (i = 0; i < (MAX_BD_NUM - 1); i++) {
		dma_addr = dma->bd_addr + sizeof(*bd) * (i + 1);
		bd[i].next = lower_32_bits(dma_addr);
		if (host->dev_comp->support_64g)
			bd[i].bd_info |= (upper_32_bits(dma_addr) & 0xf) << 24;
	}
}

static void msdc_ops_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	int ret;

	if (msdc_restore_reg(host) < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return;
	}

	msdc_set_buswidth(host, ios->bus_width);

	/* Suspend/Resume will do power off/on */
	switch (ios->power_mode) {
	case MMC_POWER_UP:
		if (!IS_ERR(mmc->supply.vmmc)) {
			msdc_init_hw(host);
			ret = mmc_regulator_set_ocr(mmc, mmc->supply.vmmc,
					ios->vdd);
			if (ret) {
				dev_err(host->dev, "Failed to set vmmc power!\n");
				goto out;
			}
		}
		break;
	case MMC_POWER_ON:
		if (!IS_ERR(mmc->supply.vqmmc) && !host->vqmmc_enabled) {
			ret = regulator_enable(mmc->supply.vqmmc);
			if (ret)
				dev_err(host->dev, "Failed to set vqmmc power!\n");
			else
				host->vqmmc_enabled = true;
		}
		break;
	case MMC_POWER_OFF:
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);

		if (!IS_ERR(mmc->supply.vqmmc) && host->vqmmc_enabled) {
			regulator_disable(mmc->supply.vqmmc);
			host->vqmmc_enabled = false;
		}
		break;
	default:
		break;
	}

	if (host->mclk != ios->clock || host->timing != ios->timing)
		msdc_set_mclk(host, ios->timing, ios->clock);
	
out:
	msdc_save_reg(host);
}

static u64 test_delay_bit(u64 delay, u32 bit)
{
	bit %= PAD_DELAY_FULL;
	return delay & BIT_ULL(bit);
}

static int get_delay_len(u64 delay, u32 start_bit)
{
	int i;

	for (i = 0; i < (PAD_DELAY_FULL - start_bit); i++) {
		if (test_delay_bit(delay, start_bit + i) == 0)
			return i;
	}
	return PAD_DELAY_FULL - start_bit;
}

static struct msdc_delay_phase get_best_delay(struct msdc_host *host, u64 delay)
{
	int start = 0, len = 0;
	int start_final = 0, len_final = 0;
	u8 final_phase = 0xff;
	struct msdc_delay_phase delay_phase = { 0, };

	if (delay == 0) {
		dev_err(host->dev, "phase error: [map:%016llx]\n", delay);
		delay_phase.final_phase = final_phase;
		return delay_phase;
	}

	while (start < PAD_DELAY_FULL) {
		len = get_delay_len(delay, start);
		if (len_final < len) {
			start_final = start;
			len_final = len;
		}
		start += len ? len : 1;
		if (!upper_32_bits(delay) && len >= 12 && start_final < 4)
			break;
	}

	/* The rule is that to find the smallest delay cell */
	if (start_final == 0)
		final_phase = (start_final + len_final / 3) % PAD_DELAY_FULL;
	else
		final_phase = (start_final + len_final / 2) % PAD_DELAY_FULL;
	dev_dbg(host->dev, "phase: [map:%016llx] [maxlen:%d] [final:%d]\n",
		delay, len_final, final_phase);

	delay_phase.maxlen = len_final;
	delay_phase.start = start_final;
	delay_phase.final_phase = final_phase;
	return delay_phase;
}

static inline void msdc_set_cmd_delay(struct msdc_host *host, u32 value)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (drv->top_base) {
		u32 regval = readl(drv->top_base + EMMC_TOP_CMD);

		regval &= ~(PAD_CMD_RXDLY | PAD_CMD_RXDLY2);

		if (value < PAD_DELAY_HALF) {
			regval |= FIELD_PREP(PAD_CMD_RXDLY, value);
		} else {
			regval |= FIELD_PREP(PAD_CMD_RXDLY, PAD_DELAY_HALF - 1);
			regval |= FIELD_PREP(PAD_CMD_RXDLY2, value - PAD_DELAY_HALF);
		}
		writel(regval, drv->top_base + EMMC_TOP_CMD);
	} else {
		if (value < PAD_DELAY_HALF) {
			sdr_set_field(drv->base + tune_reg, MSDC_PAD_TUNE_CMDRDLY, value);
			sdr_set_field(drv->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_CMDRDLY2, 0);
		} else {
			sdr_set_field(drv->base + tune_reg, MSDC_PAD_TUNE_CMDRDLY,
				      PAD_DELAY_HALF - 1);
			sdr_set_field(drv->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_CMDRDLY2, value - PAD_DELAY_HALF);
		}
	}
}

static inline void msdc_set_data_delay(struct msdc_host *host, u32 value)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	if (drv->top_base) {
		u32 regval = readl(drv->top_base + EMMC_TOP_CONTROL);

		regval &= ~(PAD_DAT_RD_RXDLY | PAD_DAT_RD_RXDLY2);

		if (value < PAD_DELAY_HALF) {
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY, value);
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY2, value);
		} else {
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY, PAD_DELAY_HALF - 1);
			regval |= FIELD_PREP(PAD_DAT_RD_RXDLY2, value - PAD_DELAY_HALF);
		}
		writel(regval, drv->top_base + EMMC_TOP_CONTROL);
	} else {
		if (value < PAD_DELAY_HALF) {
			sdr_set_field(drv->base + tune_reg, MSDC_PAD_TUNE_DATRRDLY, value);
			sdr_set_field(drv->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_DATRRDLY2, 0);
		} else {
			sdr_set_field(drv->base + tune_reg, MSDC_PAD_TUNE_DATRRDLY,
				      PAD_DELAY_HALF - 1);
			sdr_set_field(drv->base + tune_reg + TUNING_REG2_FIXED_OFFEST,
				      MSDC_PAD_TUNE_DATRRDLY2, value - PAD_DELAY_HALF);
		}
	}
}

static inline void msdc_set_data_sample_edge(struct msdc_host *host, bool rising)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 value = rising ? 0 : 1;

	if (host->dev_comp->support_new_rx) {
		sdr_set_field(drv->base + MSDC_PATCH_BIT, MSDC_PATCH_BIT_RD_DAT_SEL, value);
		sdr_set_field(drv->base + MSDC_PATCH_BIT2, MSDC_PB2_CFGCRCSTSEDGE, value);
	} else {
		sdr_set_field(drv->base + MSDC_IOCON, MSDC_IOCON_DSPL, value);
		sdr_set_field(drv->base + MSDC_IOCON, MSDC_IOCON_W_DSPL, value);
	}
}

static int msdc_tune_response(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u64 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	struct msdc_delay_phase internal_delay_phase;
	u8 final_delay, final_maxlen;
	u32 internal_delay = 0;
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	int cmd_err;
	int i, j;

	if (mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    mmc->ios.timing == MMC_TIMING_UHS_SDR104)
		sdr_set_field(drv->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs200_cmd_int_delay);

	sdr_clr_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				rise_delay |= BIT_ULL(i);
			} else {
				rise_delay &= ~BIT_ULL(i);
				break;
			}
		}
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	sdr_set_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				fall_delay |= BIT_ULL(i);
			} else {
				fall_delay &= ~BIT_ULL(i);
				break;
			}
		}
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_fall_delay.maxlen >= 12 && final_fall_delay.start < 4)
		final_maxlen = final_fall_delay.maxlen;
	if (final_maxlen == final_rise_delay.maxlen) {
		sdr_clr_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		final_delay = final_rise_delay.final_phase;
	} else {
		sdr_set_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		final_delay = final_fall_delay.final_phase;
	}
	msdc_set_cmd_delay(host, final_delay);

	if (host->dev_comp->async_fifo || host->hs200_cmd_int_delay)
		goto skip_internal;

	for (i = 0; i < host->tuning_step; i++) {
		sdr_set_field(drv->base + tune_reg,
			      MSDC_PAD_TUNE_CMDRRDLY, i);
		mmc_send_tuning(mmc, opcode, &cmd_err);
		if (!cmd_err)
			internal_delay |= BIT_ULL(i);
	}
	dev_dbg(host->dev, "Final internal delay: 0x%x\n", internal_delay);
	internal_delay_phase = get_best_delay(host, internal_delay);
	sdr_set_field(drv->base + tune_reg, MSDC_PAD_TUNE_CMDRRDLY,
		      internal_delay_phase.final_phase);
skip_internal:
	dev_dbg(host->dev, "Final cmd pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int hs400_tune_response(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 cmd_delay = 0;
	struct msdc_delay_phase final_cmd_delay = { 0,};
	u8 final_delay;
	int cmd_err;
	int i, j;

	/* select EMMC50 PAD CMD tune */
	sdr_set_bits(drv->base + PAD_CMD_TUNE, BIT(0));
	sdr_set_field(drv->base + MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMDTA, 2);

	if (mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    mmc->ios.timing == MMC_TIMING_UHS_SDR104)
		sdr_set_field(drv->base + MSDC_PAD_TUNE,
			      MSDC_PAD_TUNE_CMDRRDLY,
			      host->hs200_cmd_int_delay);

	if (host->hs400_cmd_resp_sel_rising)
		sdr_clr_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	else
		sdr_set_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);

	for (i = 0; i < PAD_DELAY_HALF; i++) {
		sdr_set_field(drv->base + PAD_CMD_TUNE,
			      PAD_CMD_TUNE_RX_DLY3, i);
		/*
		 * Using the same parameters, it may sometimes pass the test,
		 * but sometimes it may fail. To make sure the parameters are
		 * more stable, we test each set of parameters 3 times.
		 */
		for (j = 0; j < 3; j++) {
			mmc_send_tuning(mmc, opcode, &cmd_err);
			if (!cmd_err) {
				cmd_delay |= BIT(i);
			} else {
				cmd_delay &= ~BIT(i);
				break;
			}
		}
	}
	final_cmd_delay = get_best_delay(host, cmd_delay);
	sdr_set_field(drv->base + PAD_CMD_TUNE, PAD_CMD_TUNE_RX_DLY3,
		      final_cmd_delay.final_phase);
	final_delay = final_cmd_delay.final_phase;

	dev_dbg(host->dev, "Final cmd pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_tune_data(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u64 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	u8 final_delay, final_maxlen;
	int i, ret;

	sdr_set_field(drv->base + MSDC_PATCH_BIT, MSDC_INT_DAT_LATCH_CK_SEL,
		      host->latch_ck);
	msdc_set_data_sample_edge(host, true);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			rise_delay |= BIT_ULL(i);
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	msdc_set_data_sample_edge(host, false);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			fall_delay |= BIT_ULL(i);
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		msdc_set_data_sample_edge(host, true);
		final_delay = final_rise_delay.final_phase;
	} else {
		msdc_set_data_sample_edge(host, false);
		final_delay = final_fall_delay.final_phase;
	}
	msdc_set_data_delay(host, final_delay);

	dev_dbg(host->dev, "Final data pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

/*
 * MSDC IP which supports data tune + async fifo can do CMD/DAT tune
 * together, which can save the tuning time.
 */
static int msdc_tune_together(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u64 rise_delay = 0, fall_delay = 0;
	struct msdc_delay_phase final_rise_delay, final_fall_delay = { 0,};
	u8 final_delay, final_maxlen;
	int i, ret;

	sdr_set_field(drv->base + MSDC_PATCH_BIT, MSDC_INT_DAT_LATCH_CK_SEL,
		      host->latch_ck);

	sdr_clr_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	msdc_set_data_sample_edge(host, true);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			rise_delay |= BIT_ULL(i);
	}
	final_rise_delay = get_best_delay(host, rise_delay);
	/* if rising edge has enough margin, then do not scan falling edge */
	if (final_rise_delay.maxlen >= 12 ||
	    (final_rise_delay.start == 0 && final_rise_delay.maxlen >= 4))
		goto skip_fall;

	sdr_set_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
	msdc_set_data_sample_edge(host, false);
	for (i = 0; i < host->tuning_step; i++) {
		msdc_set_cmd_delay(host, i);
		msdc_set_data_delay(host, i);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (!ret)
			fall_delay |= BIT_ULL(i);
	}
	final_fall_delay = get_best_delay(host, fall_delay);

skip_fall:
	final_maxlen = max(final_rise_delay.maxlen, final_fall_delay.maxlen);
	if (final_maxlen == final_rise_delay.maxlen) {
		sdr_clr_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		msdc_set_data_sample_edge(host, true);
		final_delay = final_rise_delay.final_phase;
	} else {
		sdr_set_bits(drv->base + MSDC_IOCON, MSDC_IOCON_RSPL);
		msdc_set_data_sample_edge(host, false);
		final_delay = final_fall_delay.final_phase;
	}

	msdc_set_cmd_delay(host, final_delay);
	msdc_set_data_delay(host, final_delay);

	dev_dbg(host->dev, "Final pad delay: %x\n", final_delay);
	return final_delay == 0xff ? -EIO : 0;
}

static int msdc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	int ret;
	u32 tune_reg = host->dev_comp->pad_tune_reg;

	ret = msdc_restore_reg(host);
	if (ret < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return ret;
	}

	if (host->dev_comp->data_tune && host->dev_comp->async_fifo) {
		ret = msdc_tune_together(mmc, opcode);
		if (host->hs400_mode) {
			msdc_set_data_sample_edge(host, true);
			msdc_set_data_delay(host, 0);
		}
		goto tune_done;
	}
	if (host->hs400_mode &&
	    host->dev_comp->hs400_tune)
		ret = hs400_tune_response(mmc, opcode);
	else
		ret = msdc_tune_response(mmc, opcode);
	if (ret == -EIO) {
		dev_err(host->dev, "Tune response fail!\n");
		goto out;
	}
	if (host->hs400_mode == false) {
		ret = msdc_tune_data(mmc, opcode);
		if (ret == -EIO)
			dev_err(host->dev, "Tune data fail!\n");
	}

tune_done:
	host->saved_tune_para.iocon = readl(drv->base + MSDC_IOCON);
	host->saved_tune_para.pad_tune = readl(drv->base + tune_reg);
	host->saved_tune_para.pad_cmd_tune = readl(drv->base + PAD_CMD_TUNE);
	if (drv->top_base) {
		host->saved_tune_para.emmc_top_control = readl(drv->top_base +
				EMMC_TOP_CONTROL);
		host->saved_tune_para.emmc_top_cmd = readl(drv->top_base +
				EMMC_TOP_CMD);
	}

out:
	msdc_save_reg(host);
	return ret;
}

static int msdc_prepare_hs400_tuning(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);

	int ret;

	ret = msdc_restore_reg(host);
	if (ret < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return ret;
	}

	host->hs400_mode = true;

	if (drv->top_base) {
		if (host->hs400_ds_dly3)
			sdr_set_field(drv->top_base + EMMC50_PAD_DS_TUNE,
				      PAD_DS_DLY3, host->hs400_ds_dly3);
		if (host->hs400_ds_delay)
			writel(host->hs400_ds_delay,
			       drv->top_base + EMMC50_PAD_DS_TUNE);
	} else {
		if (host->hs400_ds_dly3)
			sdr_set_field(drv->base + PAD_DS_TUNE,
				      PAD_DS_TUNE_DLY3, host->hs400_ds_dly3);
		if (host->hs400_ds_delay)
			writel(host->hs400_ds_delay, drv->base + PAD_DS_TUNE);
	}

	/* hs400 mode must set it to 0 */
	sdr_clr_bits(drv->base + MSDC_PATCH_BIT2, MSDC_PATCH_BIT2_CFGCRCSTS);
	/* to improve read performance, set outstanding to 2 */
	sdr_set_field(drv->base + EMMC50_CFG3, EMMC50_CFG3_OUTS_WR, 2);

	msdc_save_reg(host);
	return 0;
}

static int msdc_execute_hs400_tuning(struct mmc_host *mmc, struct mmc_card *card)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	struct msdc_delay_phase dly1_delay;
	u32 val, result_dly1 = 0;
	u8 *ext_csd;
	int i, ret;

	ret = msdc_restore_reg(host);
	if (ret < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return ret;
	}

	if (drv->top_base) {
		sdr_set_bits(drv->top_base + EMMC50_PAD_DS_TUNE,
			     PAD_DS_DLY_SEL);
		sdr_clr_bits(drv->top_base + EMMC50_PAD_DS_TUNE,
			     PAD_DS_DLY2_SEL);
		if (host->hs400_ds_dly3)
			sdr_set_field(drv->top_base + EMMC50_PAD_DS_TUNE,
				      PAD_DS_DLY3, host->hs400_ds_dly3);
	} else {
		sdr_set_bits(drv->base + PAD_DS_TUNE, PAD_DS_TUNE_DLY_SEL);
		if (host->hs400_ds_dly3)
			sdr_set_field(drv->base + PAD_DS_TUNE,
				      PAD_DS_TUNE_DLY3, host->hs400_ds_dly3);
	}

	host->hs400_tuning = true;
	for (i = 0; i < PAD_DELAY_HALF; i++) {
		if (drv->top_base)
			sdr_set_field(drv->top_base + EMMC50_PAD_DS_TUNE,
				      PAD_DS_DLY1, i);
		else
			sdr_set_field(drv->base + PAD_DS_TUNE,
				      PAD_DS_TUNE_DLY1, i);
		ret = mmc_get_ext_csd(card, &ext_csd);
		if (!ret) {
			result_dly1 |= BIT(i);
			kfree(ext_csd);
		}
	}
	host->hs400_tuning = false;

	dly1_delay = get_best_delay(host, result_dly1);
	if (dly1_delay.maxlen == 0) {
		dev_err(host->dev, "Failed to get DLY1 delay!\n");
		goto fail;
	}
	if (drv->top_base)
		sdr_set_field(drv->top_base + EMMC50_PAD_DS_TUNE,
			      PAD_DS_DLY1, dly1_delay.final_phase);
	else
		sdr_set_field(drv->base + PAD_DS_TUNE,
			      PAD_DS_TUNE_DLY1, dly1_delay.final_phase);

	if (drv->top_base)
		val = readl(drv->top_base + EMMC50_PAD_DS_TUNE);
	else
		val = readl(drv->base + PAD_DS_TUNE);

	dev_info(host->dev, "Final PAD_DS_TUNE: 0x%x\n", val);

	msdc_save_reg(host);
	return 0;

fail:
	dev_err(host->dev, "Failed to tuning DS pin delay!\n");
	msdc_save_reg(host);
	return -EIO;
}

static void msdc_hw_reset(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);

	if (msdc_restore_reg(host) < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return;
	}

	sdr_set_bits(drv->base + EMMC_IOCON, 1);
	udelay(10); /* 10us is enough */
	sdr_clr_bits(drv->base + EMMC_IOCON, 1);

	msdc_save_reg(host);
}

static void msdc_ack_sdio_irq(struct mmc_host *mmc)
{
	unsigned long flags;
	struct msdc_host *host = mmc_priv(mmc);

	if (msdc_restore_reg(host) < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return;
	}
	spin_lock_irqsave(&host->lock, flags);
	__msdc_enable_sdio_irq(host, 1);
	spin_unlock_irqrestore(&host->lock, flags);

	msdc_save_reg(host);
}

static int msdc_get_cd(struct mmc_host *mmc)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	int val, ret;

	ret = msdc_restore_reg(host);
	if (ret < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return ret;
	}

	if (mmc->caps & MMC_CAP_NONREMOVABLE) {
		ret = 1;
		goto out;
	}

	if (!host->internal_cd) {
		ret = mmc_gpio_get_cd(mmc);
		goto out;
	}

	val = readl(drv->base + MSDC_PS) & MSDC_PS_CDSTS;
	if (mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH) {
		ret = !!val;
		goto out;
	} else {
		ret = !val;
		goto out;
	}

out:
	msdc_save_reg(host);
	return ret;
}

static void msdc_hs400_enhanced_strobe(struct mmc_host *mmc,
				       struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);

	if (msdc_restore_reg(host) < 0) {
		dev_err(host->dev, "%s: controller is busy with another slot\n",
			__func__);
		return;
	}

	if (ios->enhanced_strobe) {
		msdc_prepare_hs400_tuning(mmc, ios);
		sdr_set_field(drv->base + EMMC50_CFG0, EMMC50_CFG_PADCMD_LATCHCK, 1);
		sdr_set_field(drv->base + EMMC50_CFG0, EMMC50_CFG_CMD_RESP_SEL, 1);
		sdr_set_field(drv->base + EMMC50_CFG1, EMMC50_CFG1_DS_CFG, 1);

		sdr_clr_bits(drv->base + CQHCI_SETTING, CQHCI_RD_CMD_WND_SEL);
		sdr_clr_bits(drv->base + CQHCI_SETTING, CQHCI_WR_CMD_WND_SEL);
		sdr_clr_bits(drv->base + EMMC51_CFG0, CMDQ_RDAT_CNT);
	} else {
		sdr_set_field(drv->base + EMMC50_CFG0, EMMC50_CFG_PADCMD_LATCHCK, 0);
		sdr_set_field(drv->base + EMMC50_CFG0, EMMC50_CFG_CMD_RESP_SEL, 0);
		sdr_set_field(drv->base + EMMC50_CFG1, EMMC50_CFG1_DS_CFG, 0);

		sdr_set_bits(drv->base + CQHCI_SETTING, CQHCI_RD_CMD_WND_SEL);
		sdr_set_bits(drv->base + CQHCI_SETTING, CQHCI_WR_CMD_WND_SEL);
		sdr_set_field(drv->base + EMMC51_CFG0, CMDQ_RDAT_CNT, 0xb4);
	}

	msdc_save_reg(host);
}

static const struct mmc_host_ops mt_msdc_ops = {
	.request = msdc_ops_request,
	.set_ios = msdc_ops_set_ios,
	.get_ro = mmc_gpio_get_ro,
	.get_cd = msdc_get_cd,
	.enable_sdio_irq = msdc_enable_sdio_irq,
	.ack_sdio_irq = msdc_ack_sdio_irq,
	.start_signal_voltage_switch = msdc_ops_switch_volt,
	.card_busy = msdc_card_busy,
	.execute_tuning = msdc_execute_tuning,
	.prepare_hs400_tuning = msdc_prepare_hs400_tuning,
	.execute_hs400_tuning = msdc_execute_hs400_tuning,
	.hs400_enhanced_strobe = msdc_hs400_enhanced_strobe,
	.card_hw_reset = msdc_hw_reset,
};

static void msdc_of_property_parse(struct device_node *np,
				   struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);

	of_property_read_u32(np, "reg", &host->id);

	of_property_read_u32(np, "mediatek,latch-ck",
			     &host->latch_ck);

	of_property_read_u32(np, "hs400-ds-delay",
			     &host->hs400_ds_delay);

	of_property_read_u32(np, "mediatek,hs400-ds-dly3",
			     &host->hs400_ds_dly3);

	of_property_read_u32(np, "mediatek,hs200-cmd-int-delay",
			     &host->hs200_cmd_int_delay);

	of_property_read_u32(np, "mediatek,hs400-cmd-int-delay",
			     &host->hs400_cmd_int_delay);

	if (of_property_read_bool(np,
				  "mediatek,hs400-cmd-resp-sel-rising"))
		host->hs400_cmd_resp_sel_rising = true;
	else
		host->hs400_cmd_resp_sel_rising = false;

	if (of_property_read_u32(np, "mediatek,tuning-step",
				 &host->tuning_step)) {
		if (mmc->caps2 & MMC_CAP2_NO_MMC)
			host->tuning_step = PAD_DELAY_FULL;
		else
			host->tuning_step = PAD_DELAY_HALF;
	}

	of_property_read_string(np, "pinctrl-default",
			     &host->pins_default_name);
	of_property_read_string(np, "pinctrl-state_uhs",
			     &host->pins_uhs_name);
}

static int msdc_of_clock_parse(struct platform_device *pdev,
			       struct msdc_drv *drv)
{
	int ret;

	drv->src_clk = devm_clk_get(&pdev->dev, "source");
	if (IS_ERR(drv->src_clk))
		return PTR_ERR(drv->src_clk);

	drv->h_clk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(drv->h_clk))
		return PTR_ERR(drv->h_clk);

	drv->bus_clk = devm_clk_get_optional(&pdev->dev, "bus_clk");
	if (IS_ERR(drv->bus_clk))
		drv->bus_clk = NULL;

	/*source clock control gate is optional clock*/
	drv->src_clk_cg = devm_clk_get_optional(&pdev->dev, "source_cg");
	if (IS_ERR(drv->src_clk_cg))
		return PTR_ERR(drv->src_clk_cg);

	/*
	 * Fallback for legacy device-trees: src_clk and HCLK use the same
	 * bit to control gating but they are parented to a different mux,
	 * hence if our intention is to gate only the source, required
	 * during a clk mode switch to avoid hw hangs, we need to gate
	 * its parent (specified as a different clock only on new DTs).
	 */
	if (!drv->src_clk_cg) {
		drv->src_clk_cg = clk_get_parent(drv->src_clk);
		if (IS_ERR(drv->src_clk_cg))
			return PTR_ERR(drv->src_clk_cg);
	}

	/* If present, always enable for this clock gate */
	drv->sys_clk_cg = devm_clk_get_optional_enabled(&pdev->dev, "sys_cg");
	if (IS_ERR(drv->sys_clk_cg))
		drv->sys_clk_cg = NULL;

	drv->bulk_clks[0].id = "pclk_cg";
	drv->bulk_clks[1].id = "axi_cg";
	drv->bulk_clks[2].id = "ahb_cg";
	ret = devm_clk_bulk_get_optional(&pdev->dev, MSDC_NR_CLOCKS,
					 drv->bulk_clks);
	if (ret) {
		dev_err(&pdev->dev, "Cannot get pclk/axi/ahb clock gates\n");
		return ret;
	}

	drv->crypto_clk = devm_clk_get_optional(&pdev->dev, "crypto");
	if (IS_ERR(drv->crypto_clk))
		return PTR_ERR(drv->crypto_clk);

	drv->src_clk_freq = clk_get_rate(drv->src_clk);

	return 0;
}

static int msdc_slot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mmc_host *mmc;
	struct msdc_host *host;
	struct msdc_drv *drv;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	dev_err(&pdev->dev, "%s\n", __func__);

	drv = dev_get_drvdata(pdev->dev.parent);

	/* Allocate MMC host for this device */
	mmc = devm_mmc_alloc_host(&pdev->dev, sizeof(struct msdc_host));
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	msdc_of_property_parse(np, host);
	ret = mmc_of_parse(mmc);
	if (ret)
		return ret;

	drv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drv->base))
		return PTR_ERR(drv->base);

	host->dev_comp = of_device_get_match_data(&pdev->dev);

	if (host->dev_comp->needs_top_base) {
		drv->top_base = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(drv->top_base))
			return PTR_ERR(drv->top_base);
	}

	ret = mmc_regulator_get_supply(mmc);
	if (ret)
		return ret;

	host->reset = devm_reset_control_get_optional_exclusive(&pdev->dev,
								"hrst");
	if (IS_ERR(host->reset))
		return PTR_ERR(host->reset);

	/* only eMMC has crypto property */
	if (!(mmc->caps2 & MMC_CAP2_NO_MMC)) {
		if (drv->crypto_clk)
			mmc->caps2 |= MMC_CAP2_CRYPTO;
	}

	if (!host->pins_default_name) {
		dev_err(&pdev->dev, "Missing default pinctrl name!\n");
		return -EINVAL;
	}

	if (IS_ERR(drv->pinctrl))
		return dev_err_probe(&pdev->dev, PTR_ERR(drv->pinctrl),
				     "Cannot find pinctrl");

	host->pins_default = pinctrl_lookup_state(drv->pinctrl, host->pins_default_name);
	if (IS_ERR(host->pins_default)) {
		dev_err(&pdev->dev, "Cannot find pinctrl default!\n");
		return PTR_ERR(host->pins_default);
	}

	if (!host->pins_uhs_name) {
		dev_err(&pdev->dev, "Missing state_uhs pinctrl name!\n");
		return -EINVAL;
	}

	host->pins_uhs = pinctrl_lookup_state(drv->pinctrl, host->pins_uhs_name);
	if (IS_ERR(host->pins_uhs)) {
		dev_err(&pdev->dev, "Cannot find pinctrl uhs!\n");
		return PTR_ERR(host->pins_uhs);
	}

	host->dev = &pdev->dev;
	host->dev_comp = of_device_get_match_data(drv->dev);
	/* Set host parameters to mmc */
	mmc->ops = &mt_msdc_ops;
	if (host->dev_comp->clk_div_bits == 8)
		mmc->f_min = DIV_ROUND_UP(drv->src_clk_freq, 4 * 255);
	else
		mmc->f_min = DIV_ROUND_UP(drv->src_clk_freq, 4 * 4095);

	if (!(mmc->caps & MMC_CAP_NONREMOVABLE) &&
	    !mmc_host_can_gpio_cd(mmc) &&
	    host->dev_comp->use_internal_cd) {
		/*
		 * Is removable but no GPIO declared, so
		 * use internal functionality.
		 */
		host->internal_cd = true;
	}

	if (mmc->caps & MMC_CAP_SDIO_IRQ)
		mmc->caps2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

	mmc->caps |= MMC_CAP_CMD23;
	/* MMC core transfer sizes tunable parameters */
	mmc->max_segs = MAX_BD_NUM;
	if (host->dev_comp->support_64g)
		mmc->max_seg_size = BDMA_DESC_BUFLEN_EXT;
	else
		mmc->max_seg_size = BDMA_DESC_BUFLEN;
	mmc->max_blk_size = 2048;
	mmc->max_req_size = 512 * 1024;
	mmc->max_blk_count = mmc->max_req_size / 512;
	if (host->dev_comp->support_64g)
		host->dma_mask = DMA_BIT_MASK(36);
	else
		host->dma_mask = DMA_BIT_MASK(32);
	mmc_dev(mmc)->dma_mask = &host->dma_mask;

	host->timeout_clks = 3 * 1048576;
	host->dma.gpd = dma_alloc_coherent(&pdev->dev,
				2 * sizeof(struct mt_gpdma_desc),
				&host->dma.gpd_addr, GFP_KERNEL);
	host->dma.bd = dma_alloc_coherent(&pdev->dev,
				MAX_BD_NUM * sizeof(struct mt_bdma_desc),
				&host->dma.bd_addr, GFP_KERNEL);
	if (!host->dma.gpd || !host->dma.bd) {
		ret = -ENOMEM;
		goto release_mem;
	}
	msdc_init_gpd_bd(host, &host->dma);
	INIT_DELAYED_WORK(&host->req_timeout, msdc_request_timeout);
	spin_lock_init(&host->lock);

	platform_set_drvdata(pdev, mmc);

	msdc_init_hw(host);

	drv->active_slot_id = host->id;
	drv->activation_count = 1;
	msdc_save_reg(host);

	ret = mmc_add_host(mmc);
	if (ret)
		goto release;

	return 0;
release:
	msdc_deinit_hw(host);
	platform_set_drvdata(pdev, NULL);
release_mem:
	device_init_wakeup(&pdev->dev, false);
	if (host->dma.gpd)
		dma_free_coherent(&pdev->dev,
			2 * sizeof(struct mt_gpdma_desc),
			host->dma.gpd, host->dma.gpd_addr);
	if (host->dma.bd)
		dma_free_coherent(&pdev->dev,
				  MAX_BD_NUM * sizeof(struct mt_bdma_desc),
				  host->dma.bd, host->dma.bd_addr);
	return ret;
}

static void msdc_slot_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct msdc_host *host;

	mmc = platform_get_drvdata(pdev);
	host = mmc_priv(mmc);

	platform_set_drvdata(pdev, NULL);
	mmc_remove_host(mmc);
	msdc_deinit_hw(host);

	dma_free_coherent(&pdev->dev,
			2 * sizeof(struct mt_gpdma_desc),
			host->dma.gpd, host->dma.gpd_addr);
	dma_free_coherent(&pdev->dev, MAX_BD_NUM * sizeof(struct mt_bdma_desc),
			  host->dma.bd, host->dma.bd_addr);
	device_init_wakeup(&pdev->dev, false);
}

static void msdc_save_reg(struct msdc_host *host)
{
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	unsigned long flags;

	BUG_ON(drv->active_slot_id != host->id);

	host->save_para.msdc_cfg = readl(drv->base + MSDC_CFG);
	host->save_para.iocon = readl(drv->base + MSDC_IOCON);
	host->save_para.ps = readl(drv->base + MSDC_PS);
	host->save_para.inten = readl(drv->base + MSDC_INTEN);
	host->save_para.sdc_cfg = readl(drv->base + SDC_CFG);
	host->save_para.patch_bit0 = readl(drv->base + MSDC_PATCH_BIT);
	host->save_para.patch_bit1 = readl(drv->base + MSDC_PATCH_BIT1);
	host->save_para.patch_bit2 = readl(drv->base + MSDC_PATCH_BIT2);
	if (!drv->top_base) {
		host->save_para.pad_tune = readl(drv->base + tune_reg);
	}
	host->save_para.pad_ds_tune = readl(drv->base + PAD_DS_TUNE);
	host->save_para.pad_cmd_tune = readl(drv->base + PAD_CMD_TUNE);
	host->save_para.emmc51_cfg0 = readl(drv->base + EMMC51_CFG0);
	host->save_para.emmc50_cfg0 = readl(drv->base + EMMC50_CFG0);
	host->save_para.emmc50_cfg1 = readl(drv->base + EMMC50_CFG1);
	host->save_para.emmc50_cfg3 = readl(drv->base + EMMC50_CFG3);
	host->save_para.sdc_fifo_cfg = readl(drv->base + SDC_FIFO_CFG);
	host->save_para.cqhci_setting = readl(drv->base + CQHCI_SETTING);
	if (drv->top_base) {
		host->save_para.emmc_top_control =
			readl(drv->top_base + EMMC_TOP_CONTROL);
		host->save_para.emmc_top_cmd =
			readl(drv->top_base + EMMC_TOP_CMD);
		host->save_para.emmc50_pad_ds_tune =
			readl(drv->top_base + EMMC50_PAD_DS_TUNE);
		host->save_para.loop_test_control =
			readl(drv->top_base + LOOP_TEST_CONTROL);
	}

	spin_lock_irqsave(&drv->active_lock, flags);
	if (--drv->activation_count <= 0)
		drv->active_slot_id = -1;
	spin_unlock_irqrestore(&drv->active_lock, flags);
}

static int msdc_restore_reg(struct msdc_host *host)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	struct msdc_drv *drv = dev_get_drvdata(host->dev->parent);
	u32 tune_reg = host->dev_comp->pad_tune_reg;
	unsigned long flags;
	int tries = 5000;

	while (--tries) {
		spin_lock_irqsave(&drv->active_lock, flags);
		if (drv->active_slot_id == -1 || drv->active_slot_id == host->id) {
			drv->active_slot_id = host->id;
			++drv->activation_count;
			spin_unlock_irqrestore(&drv->active_lock, flags);
			break;
		}
		spin_unlock_irqrestore(&drv->active_lock, flags);
		usleep_range(1000, 1250);
	}

	if (tries == 0) {
		return -EBUSY;
	}

	if (host->signal_voltage == MMC_SIGNAL_VOLTAGE_180)
		pinctrl_select_state(drv->pinctrl, host->pins_uhs);
	else
		pinctrl_select_state(drv->pinctrl, host->pins_default);

	if (host->dev_comp->support_new_tx) {
		sdr_clr_bits(drv->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
		sdr_set_bits(drv->base + SDC_ADV_CFG0, SDC_NEW_TX_EN);
	}
	if (host->dev_comp->support_new_rx) {
		sdr_clr_bits(drv->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
		sdr_set_bits(drv->base + MSDC_NEW_RX_CFG, MSDC_NEW_RX_PATH_SEL);
	}

	writel(host->save_para.msdc_cfg, drv->base + MSDC_CFG);
	writel(host->save_para.iocon, drv->base + MSDC_IOCON);
	writel(host->save_para.ps, drv->base + MSDC_PS);
	writel(host->save_para.inten, drv->base + MSDC_INTEN);
	writel(host->save_para.sdc_cfg, drv->base + SDC_CFG);
	writel(host->save_para.patch_bit0, drv->base + MSDC_PATCH_BIT);
	writel(host->save_para.patch_bit1, drv->base + MSDC_PATCH_BIT1);
	writel(host->save_para.patch_bit2, drv->base + MSDC_PATCH_BIT2);
	writel(host->save_para.pad_ds_tune, drv->base + PAD_DS_TUNE);
	writel(host->save_para.pad_cmd_tune, drv->base + PAD_CMD_TUNE);
	writel(host->save_para.emmc51_cfg0, drv->base + EMMC51_CFG0);
	writel(host->save_para.emmc50_cfg0, drv->base + EMMC50_CFG0);
	writel(host->save_para.emmc50_cfg1, drv->base + EMMC50_CFG1);
	writel(host->save_para.emmc50_cfg3, drv->base + EMMC50_CFG3);
	writel(host->save_para.sdc_fifo_cfg, drv->base + SDC_FIFO_CFG);
	writel(host->save_para.cqhci_setting, drv->base + CQHCI_SETTING);
	if (drv->top_base) {
		writel(host->save_para.emmc_top_control,
		       drv->top_base + EMMC_TOP_CONTROL);
		writel(host->save_para.emmc_top_cmd,
		       drv->top_base + EMMC_TOP_CMD);
		writel(host->save_para.emmc50_pad_ds_tune,
		       drv->top_base + EMMC50_PAD_DS_TUNE);
		writel(host->save_para.loop_test_control,
		       drv->top_base + LOOP_TEST_CONTROL);
	} else {
		writel(host->save_para.pad_tune, drv->base + tune_reg);
	}

	if (sdio_irq_claimed(mmc))
		__msdc_enable_sdio_irq(host, 1);

	return 0;
}

static int msdc_drv_probe(struct platform_device *pdev)
{
	struct device_node *slot_np;
	struct msdc_drv *drv;
	int ret;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No DT found\n");
		return -EINVAL;
	}

	drv = kzalloc(sizeof(struct msdc_drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;
	drv->dev = &pdev->dev;
	drv->active_slot_id = -1;

	drv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drv->base))
		return PTR_ERR(drv->base);

	drv->top_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(drv->top_base))
		drv->top_base = NULL;

	ret = msdc_of_clock_parse(pdev, drv);
	if (ret)
		return ret;

	drv->irq = platform_get_irq(pdev, 0);
	if (drv->irq < 0)
		return drv->irq;

	drv->pinctrl = devm_pinctrl_get(&pdev->dev);

	platform_set_drvdata(pdev, drv);

	drv->max_slot_count = of_get_child_count(pdev->dev.of_node);

	drv->slots = kzalloc(sizeof(struct platform_device *) * drv->max_slot_count, GFP_KERNEL);
	if (!drv->slots)
		return -ENOMEM;

	ret = msdc_ungate_clock(drv);
	if (ret) {
		dev_err(&pdev->dev, "Cannot ungate clocks!\n");
		goto unclock;
	}

	msdc_reset_hw(drv);

	ret = devm_request_irq(&pdev->dev, drv->irq, msdc_irq,
			       IRQF_TRIGGER_NONE, pdev->name, drv);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		goto unclock;
	}

	for_each_child_of_node(pdev->dev.of_node, slot_np) {
		struct platform_device_info pinfo;
		struct platform_device *slot_pdev;
		u32 id;

		if (!of_device_is_compatible(slot_np,
					     "mediatek,mmc-slot"))
			continue;

		if (!of_device_is_available(slot_np))
			continue;

		if (of_property_read_u32(slot_np, "reg", &id)) {
			dev_err(&pdev->dev, "missing reg");
			continue;
		}

		if (id >= drv->max_slot_count) {
			dev_err(&pdev->dev, "invalid reg");
			continue;
		}

		if (drv->slots[id]) {
			dev_err(&pdev->dev, "slot id already in use");
			continue;
		}

		pinfo.parent = &pdev->dev;
		pinfo.fwnode = of_fwnode_handle(slot_np);
		pinfo.of_node_reused = false;
		pinfo.name = "mtk-msdc-slot";
		pinfo.id = PLATFORM_DEVID_AUTO;
		pinfo.res = pdev->resource;
		pinfo.num_res = pdev->num_resources;
		pinfo.data = NULL;
		pinfo.size_data = 0;
		pinfo.dma_mask = DMA_BIT_MASK(32);

		slot_pdev = platform_device_register_full(&pinfo);
		if (IS_ERR(slot_pdev)) {
			int ret = PTR_ERR(slot_pdev);
			dev_err(&pdev->dev, "failed to register slot: %d\n", ret);
			continue;
		}

		drv->slots[id] = slot_pdev;
	}

	return 0;

unclock:
	msdc_gate_clock(drv);

	return ret;
}

static void msdc_drv_remove(struct platform_device *pdev)
{
	struct msdc_drv *drv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < drv->max_slot_count; i++) {
		struct platform_device *slot_pdev = drv->slots[i];
		if (!slot_pdev)
			continue;
		platform_device_unregister(slot_pdev);
		drv->slots[i] = NULL;
	}

	platform_set_drvdata(pdev, NULL);
	msdc_gate_clock(drv);
}

static struct platform_driver mt_msdc_slot_driver = {
	.probe = msdc_slot_probe,
	.remove = msdc_slot_remove,
	.driver = {
		.name = "mtk-msdc-slot",
		.of_match_table = msdc_slot_of_ids,
	},
};

module_platform_driver(mt_msdc_slot_driver);

static struct platform_driver mt_msdc_driver = {
	.probe = msdc_drv_probe,
	.remove = msdc_drv_remove,
	.driver = {
		.name = "mtk-msdc",
		.of_match_table = msdc_of_ids,
	},
};

module_platform_driver(mt_msdc_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek SD/MMC Card Driver");
