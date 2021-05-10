/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt2701-reg.h  --  Mediatek 2701 audio driver reg definition
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 */

#ifndef _MT2701_REG_H_
#define _MT2701_REG_H_

#define AUDIO_TOP_CON0 0x0000
#define AUDIO_TOP_CON4 0x0010
#define AUDIO_TOP_CON5 0x0014
#define AFE_DAIBT_CON0 0x001c
#define AFE_MRGIF_CON 0x003c
#define ASMI_TIMING_CON1 0x0100
#define ASMO_TIMING_CON1 0x0104
#define PWR1_ASM_CON1 0x0108
#define ASYS_TOP_CON 0x0600
#define ASYS_I2SIN1_CON 0x0604
#define ASYS_I2SIN2_CON 0x0608
#define ASYS_I2SIN3_CON 0x060c
#define ASYS_I2SIN4_CON 0x0610
#define ASYS_I2SIN5_CON 0x0614
#define ASYS_I2SO1_CON 0x061C
#define ASYS_I2SO2_CON 0x0620
#define ASYS_I2SO3_CON 0x0624
#define ASYS_I2SO4_CON 0x0628
#define ASYS_I2SO5_CON 0x062c
#define PWR2_TOP_CON 0x0634
#define AFE_CONN0 0x06c0
#define AFE_CONN1 0x06c4
#define AFE_CONN2 0x06c8
#define AFE_CONN3 0x06cc
#define AFE_CONN14 0x06f8
#define AFE_CONN15 0x06fc
#define AFE_CONN16 0x0700
#define AFE_CONN17 0x0704
#define AFE_CONN18 0x0708
#define AFE_CONN19 0x070c
#define AFE_CONN20 0x0710
#define AFE_CONN21 0x0714
#define AFE_CONN22 0x0718
#define AFE_CONN23 0x071c
#define AFE_CONN24 0x0720
#define AFE_CONN41 0x0764
#define ASYS_IRQ1_CON 0x0780
#define ASYS_IRQ2_CON 0x0784
#define ASYS_IRQ3_CON 0x0788
#define ASYS_IRQ_CLR 0x07c0
#define ASYS_IRQ_STATUS 0x07c4
#define PWR2_ASM_CON1 0x1070
#define AFE_DAC_CON0 0x1200
#define AFE_DAC_CON1 0x1204
#define AFE_DAC_CON2 0x1208
#define AFE_DAC_CON3 0x120c
#define AFE_DAC_CON4 0x1210
#define AFE_MEMIF_HD_CON1 0x121c
#define AFE_MEMIF_PBUF_SIZE 0x1238
#define AFE_MEMIF_HD_CON0 0x123c
#define AFE_DL1_BASE 0x1240
#define AFE_DL1_CUR 0x1244
#define AFE_DL2_BASE 0x1250
#define AFE_DL2_CUR 0x1254
#define AFE_DL3_BASE 0x1260
#define AFE_DL3_CUR 0x1264
#define AFE_DL4_BASE 0x1270
#define AFE_DL4_CUR 0x1274
#define AFE_DL5_BASE 0x1280
#define AFE_DL5_CUR 0x1284
#define AFE_DLMCH_BASE 0x12a0
#define AFE_DLMCH_CUR 0x12a4
#define AFE_ARB1_BASE 0x12b0
#define AFE_ARB1_CUR 0x12b4
#define AFE_VUL_BASE 0x1300
#define AFE_VUL_CUR 0x130c
#define AFE_UL2_BASE 0x1310
#define AFE_UL2_END 0x1318
#define AFE_UL2_CUR 0x131c
#define AFE_UL3_BASE 0x1320
#define AFE_UL3_END 0x1328
#define AFE_UL3_CUR 0x132c
#define AFE_UL4_BASE 0x1330
#define AFE_UL4_END 0x1338
#define AFE_UL4_CUR 0x133c
#define AFE_UL5_BASE 0x1340
#define AFE_UL5_END 0x1348
#define AFE_UL5_CUR 0x134c
#define AFE_DAI_BASE 0x1370
#define AFE_DAI_CUR 0x137c

#define AFE_HDMI_OUT_CON0	0x0370
#define AFE_HDMI_OUT_BASE	0x0374
#define AFE_HDMI_OUT_CUR	0x0378
#define AFE_HDMI_OUT_END	0x037c
#define AFE_HDMI_CONN0		0x0390

#define AFE_IRQ_MCU_CON		0x03a0
#define AFE_IRQ_STATUS		0x03a4
#define AFE_IRQ_CLR            0x03a8
#define AFE_IRQ_CNT1		0x03ac
#define AFE_IRQ_CNT2		0x03b0
#define AFE_IRQ_MCU_EN		0x03b4
#define AFE_IRQ_CNT5		0x03bc
#define AFE_IRQ_CNT7		0x03dc

/* AFE_DAIBT_CON0 (0x001c) */
#define AFE_DAIBT_CON0_DAIBT_EN		(0x1 << 0)
#define AFE_DAIBT_CON0_BT_FUNC_EN	(0x1 << 1)
#define AFE_DAIBT_CON0_BT_FUNC_RDY	(0x1 << 3)
#define AFE_DAIBT_CON0_BT_WIDE_MODE_EN	(0x1 << 9)
#define AFE_DAIBT_CON0_MRG_USE		(0x1 << 12)

/* PWR1_ASM_CON1 (0x0108) */
#define PWR1_ASM_CON1_INIT_VAL		(0x492)

/* AFE_MRGIF_CON (0x003c) */
#define AFE_MRGIF_CON_MRG_EN		(0x1 << 0)
#define AFE_MRGIF_CON_MRG_I2S_EN	(0x1 << 16)
#define AFE_MRGIF_CON_I2S_MODE_MASK	(0xf << 20)
#define AFE_MRGIF_CON_I2S_MODE_32K	(0x4 << 20)

/* ASYS_TOP_CON (0x0600) */
#define ASYS_TOP_CON_ASYS_TIMING_ON		(0x3 << 0)

/* PWR2_ASM_CON1 (0x1070) */
#define PWR2_ASM_CON1_INIT_VAL		(0x492492)

/* AFE_DAC_CON0 (0x1200) */
#define AFE_DAC_CON0_AFE_ON		(0x1 << 0)

/* AUDIO_TOP_CON3 */
#define HDMI_OUT_SPEAKER_BIT    4
#define SPEAKER_OUT_HDMI        5
#define HDMI_2CH_SEL_POS        6
#define HDMI_2CH_SEL_LEN        2

/* AFE_HDMI_OUT_CON0 */
#define HDMI_OUT_ON_MASK        1
#define HDMI_OUT_ON         1
#define HDMI_OUT_OFF        0
#define HDMI_OUT_BIT_WIDTH_MASK      (1 << 1)
#define HDMI_OUT_BIT_WIDTH_16     0
#define HDMI_OUT_BIT_WIDTH_32     (1 << 1)
#define HDMI_OUT_DSD_WDITH_MASK      (0x3 << 2)
#define HDMI_OUT_DSD_32BIT       (0x0 << 2)
#define HDMI_OUT_DSD_16BIT       (0x1 << 2)
#define HDMI_OUT_DSD_8BIT        (0x2 << 2)
#define HDMI_OUT_CH_NUM_MASK     (0xF << 4)
#define HDMI_OUT_CH_NUM_POS      4

/* AFE_HDMI_CONN0 */
#define SPDIF_LRCK_SRC_SEL_MASK  (1 << 30)
#define SPDIF_LRCK_SRC_SEL_SPDIF 0
#define SPDIF_LRCK_SRC_SEL_HDMI  (1 << 30)
#define SPDIF2_LRCK_SRC_SEL_MASK  (1 << 31)
#define SPDIF2_LRCK_SRC_SEL_SPDIF 0
#define SPDIF2_LRCK_SRC_SEL_HDMI  (1 << 31)

/* AFE_8CH_I2S_OUT_CON */
#define HDMI_8CH_I2S_CON_EN_MASK 1
#define HDMI_8CH_I2S_CON_EN  1
#define HDMI_8CH_I2S_CON_DIS 0

#define HDMI_8CH_I2S_CON_BCK_INV_MASK   (1 << 1)
#define HDMI_8CH_I2S_CON_BCK_INV              (1 << 1)
#define HDMI_8CH_I2S_CON_BCK_NO_INV  0

#define HDMI_8CH_I2S_CON_LRCK_INV_MASK   (1 << 2)
#define HDMI_8CH_I2S_CON_LRCK_INV    (1 << 2)
#define HDMI_8CH_I2S_CON_LRCK_NO_INV 0

#define HDMI_8CH_I2S_CON_I2S_DELAY_MASK    (1 << 3)
#define HDMI_8CH_I2S_CON_I2S_DELAY     (1 << 3)
#define HDMI_8CH_I2S_CON_I2S_NO_DELAY  0

#define HDMI_8CH_I2S_CON_I2S_WLEN_MASK     (3 << 4)
#define HDMI_8CH_I2S_CON_I2S_8BIT      0
#define HDMI_8CH_I2S_CON_I2S_16BIT     (1 << 4)
#define HDMI_8CH_I2S_CON_I2S_24BIT     (2 << 4)
#define HDMI_8CH_I2S_CON_I2S_32BIT     (3 << 4)

#define HDMI_8CH_I2S_CON_DSD_MODE_MASK     (0x1 << 6)
#define HDMI_8CH_I2S_CON_DSD           (0x1 << 6)
#define HDMI_8CH_I2S_CON_NON_DSD       (0x0 << 6)
/* AFE_SIDETONE_DEBUG */
#define STF_SRC_SEL_POS     16
#define STF_SRC_SEL_LEN     2
#define STF_I5I6_SEL_POS    19
#define STF_I5I6_SEL_LEN    1

/* AFE_SIDETONE_CON0 */
#define STF_COEFF_VAL_POS       0
#define STF_COEFF_VAL_LEN       16
#define STF_COEFF_ADDRESS_POS   16
#define STF_COEFF_ADDRESS_LEN   5
#define STF_CH_SEL_POS          23
#define STF_CH_SEL_LEN          1
#define STF_COEFF_W_ENABLE_POS  24
#define STF_COEFF_W_ENABLE_LEN  1
#define STF_W_ENABLE_POS        25
#define STF_W_ENABLE_LEN        1
#define STF_COEFF_BIT       0x0000FFFF

/* AFE_SIDETONE_CON1 */
#define STF_TAP_NUM_POS 0
#define STF_TAP_NUM_LEN	5
#define STF_ON_POS      8
#define STF_ON_LEN      1
#define STF_BYPASS_POS  31
#define STF_BYPASS_LEN  1

/* AFE_SGEN_CON0 */
#define SINE_TONE_FREQ_DIV_CH1  0
#define SINE_TONE_AMP_DIV_CH1   5
#define SINE_TONE_MODE_CH1      8
#define SINE_TONE_FREQ_DIV_CH2  12
#define SINE_TONE_AMP_DIV_CH2   17
#define SINE_TONE_MODE_CH2      20
#define SINE_TONE_MUTE_CH1      24
#define SINE_TONE_MUTE_CH2      25
#define SINE_TONE_ENABLE        26
#define SINE_TONE_LOOPBACK_MOD  28


/* AFE_SPDIF_OUT_CON0 */
#define SPDIF_OUT_CLOCK_ON_OFF_MASK  1
#define SPDIF_OUT_CLOCK_ON       1
#define SPDIF_OUT_CLOCK_OFF      0
#define SPDIF_OUT_MEMIF_ON_OFF_MASK  (1 << 1)
#define SPDIF_OUT_MEMIF_ON       (1 << 1)
#define SPDIF_OUT_MEMIF_OFF      0

/* AFE_MEMIF_PBUF_SIZE (0x1238) */
#define AFE_MEMIF_PBUF_SIZE_DLM_MASK		(0x1 << 29)
#define AFE_MEMIF_PBUF_SIZE_PAIR_INTERLEAVE	(0x0 << 29)
#define AFE_MEMIF_PBUF_SIZE_FULL_INTERLEAVE	(0x1 << 29)
#define DLMCH_BIT_WIDTH_MASK			(0x1 << 28)
#define AFE_MEMIF_PBUF_SIZE_DLM_CH_MASK		(0xf << 24)
#define AFE_MEMIF_PBUF_SIZE_DLM_CH(x)		((x) << 24)
#define AFE_MEMIF_PBUF_SIZE_DLM_BYTE_MASK	(0x3 << 12)
#define AFE_MEMIF_PBUF_SIZE_DLM_32BYTES		(0x1 << 12)

/* I2S in/out register bit control */
#define ASYS_I2S_CON_FS			(0x1f << 8)
#define ASYS_I2S_CON_FS_SET(x)		((x) << 8)
#define ASYS_I2S_CON_RESET		(0x1 << 30)
#define ASYS_I2S_CON_I2S_EN		(0x1 << 0)
#define ASYS_I2S_CON_ONE_HEART_MODE	(0x1 << 16)
#define ASYS_I2S_CON_I2S_COUPLE_MODE	(0x1 << 17)
/* 0:EIAJ 1:I2S */
#define ASYS_I2S_CON_I2S_MODE		(0x1 << 3)
#define ASYS_I2S_CON_WIDE_MODE		(0x1 << 1)
#define ASYS_I2S_CON_WIDE_MODE_SET(x)	((x) << 1)
#define ASYS_I2S_IN_PHASE_FIX		(0x1 << 31)

#endif
