// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Roy Luo <roychl666@gmail.com>
 *         Ryder Lee <ryder.lee@mediatek.com>
 */

#include <linux/etherdevice.h>
#include "mt7615.h"
#include "mac.h"

static int mt7615_alloc_token(struct mt7615_dev *dev)
{
	struct mt7615_token_queue *q = &dev->tkq;
	int i, size;

	spin_lock_init(&q->lock);

	q->ntoken = MT7615_TOKEN_SIZE + 1;
	q->used = q->ntoken;
	q->tail = 0;
	q->head = 0;
	q->queued = 0;

	size = q->ntoken * sizeof(*q->skb);
	q->skb = devm_kzalloc(dev->mt76.dev, size, GFP_KERNEL);
	if (!q->skb)
		return -ENOMEM;

	size = q->ntoken * sizeof(*q->id);
	q->id = devm_kzalloc(dev->mt76.dev, size, GFP_KERNEL);
	if (!q->id)
		return -ENOMEM;

	for (i = 0; i < q->ntoken; i++)
		q->id[i] = i;

	return 0;
}

static void mt7615_phy_init(struct mt7615_dev *dev)
{
	/* disable band 0 rf low power beacon mode */
	mt76_rmw(dev, MT_WF_PHY_WF2_RFCTRL0, MT_WF_PHY_WF2_RFCTRL0_LPBCN_EN,
		 MT_WF_PHY_WF2_RFCTRL0_LPBCN_EN);
}

static void mt7615_mac_init(struct mt7615_dev *dev)
{
	/* enable band 0 clk */
	mt76_rmw(dev, MT_CFG_CCR,
		 MT_CFG_CCR_MAC_D0_1X_GC_EN | MT_CFG_CCR_MAC_D0_2X_GC_EN,
		 MT_CFG_CCR_MAC_D0_1X_GC_EN | MT_CFG_CCR_MAC_D0_2X_GC_EN);

	/* disable rx hdr trans */
	mt76_rmw(dev, MT_DMA_DCR0,
		 MT_DMA_DCR0_RX_HDR_TRANS_EN, 0);

	mt76_rmw_field(dev, MT_TMAC_CTCR0,
		       MT_TMAC_CTCR0_INS_DDLMT_REFTIME, 0x3f);
	mt76_rmw_field(dev, MT_TMAC_CTCR0,
		       MT_TMAC_CTCR0_INS_DDLMT_DENSITY, 0x3);
	mt76_rmw(dev, MT_TMAC_CTCR0,
		 MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
		 MT_TMAC_CTCR0_INS_DDLMT_EN,
		 MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN |
		 MT_TMAC_CTCR0_INS_DDLMT_EN);

	mt7615_mcu_set_rts_thresh(dev, 0x92b);

	mt76_rmw(dev, MT_AGG_SCR, MT_AGG_SCR_NLNAV_MID_PTEC_DIS,
		 MT_AGG_SCR_NLNAV_MID_PTEC_DIS);

	mt7615_mcu_init_mac(dev);

	mt76_wr(dev, MT_AGG_ARUCR, FIELD_PREP(MT_AGG_ARxCR_LIMIT(0), 7));
	mt76_wr(dev, MT_AGG_ARDCR,
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(0), 0) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(1),
			   max_t(int, 0, MT7615_RATE_RETRY - 2)) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(2), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(3), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(4), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(5), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(6), MT7615_RATE_RETRY - 1) |
		FIELD_PREP(MT_AGG_ARxCR_LIMIT(7), MT7615_RATE_RETRY - 1));

	dev->mt76.global_wcid.idx = MT7615_WTBL_RESERVED;
	dev->mt76.global_wcid.hw_key_idx = -1;
	rcu_assign_pointer(dev->mt76.wcid[MT7615_WTBL_RESERVED],
			   &dev->mt76.global_wcid);
}

static int mt7615_init_hardware(struct mt7615_dev *dev)
{
	int ret;

	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	ret = mt7615_alloc_token(dev);
	if (ret < 0)
		return ret;

	ret = mt7615_eeprom_init(dev);
	if (ret < 0)
		return ret;

	ret = mt7615_dma_init(dev);
	if (ret)
		return ret;

	mt7615_dma_start(dev);
	set_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);

	ret = mt7615_mcu_init(dev);
	if (ret)
		return ret;
	set_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state);

	mt7615_mcu_set_eeprom(dev);
	mt7615_mac_init(dev);
	mt7615_phy_init(dev);
	mt7615_mcu_ctrl_pm_state(dev, 0);
	mt7615_mcu_del_wtbl_all(dev);

	return 0;
}

#define CCK_RATE(_idx, _rate) {						\
	.bitrate = _rate,						\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,				\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | (_idx),			\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (4 + (_idx)),	\
}

#define OFDM_RATE(_idx, _rate) {					\
	.bitrate = _rate,						\
	.hw_value = (MT_PHY_TYPE_OFDM << 8) | (_idx),			\
	.hw_value_short = (MT_PHY_TYPE_OFDM << 8) | (_idx),		\
}

static struct ieee80211_rate mt7615_rates[] = {
	CCK_RATE(0, 10),
	CCK_RATE(1, 20),
	CCK_RATE(2, 55),
	CCK_RATE(3, 110),
	OFDM_RATE(11, 60),
	OFDM_RATE(15, 90),
	OFDM_RATE(10, 120),
	OFDM_RATE(14, 180),
	OFDM_RATE(9,  240),
	OFDM_RATE(13, 360),
	OFDM_RATE(8,  480),
	OFDM_RATE(12, 540),
};

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = MT7615_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_AP)
	}
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = 4,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	}
};

int mt7615_register_device(struct mt7615_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;
	int ret;

	ret = mt7615_init_hardware(dev);
	if (ret)
		return ret;

	hw->queues = 4;

	hw->sta_data_size = sizeof(struct mt7615_sta);
	hw->vif_data_size = sizeof(struct mt7615_vif);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);

	dev->mt76.sband_2g.sband.ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
	dev->mt76.sband_5g.sband.ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
	dev->mt76.sband_5g.sband.vht_cap.cap |=
				IEEE80211_VHT_CAP_SHORT_GI_160;
	dev->mt76.chainmask = 0x404;
	dev->mt76.antenna_mask = 0xf;
	dev->mt76.hwq_quirk = 0;

	wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_AP);

	ret = mt76_register_device(&dev->mt76, true, mt7615_rates,
				   ARRAY_SIZE(mt7615_rates));
	if (ret)
		return ret;

	return 0;
}

void mt7615_unregister_device(struct mt7615_dev *dev)
{
	mt76_unregister_device(&dev->mt76);
	mt7615_mcu_exit(dev);
	mt7615_dma_cleanup(dev);
	ieee80211_free_hw(mt76_hw(dev));
}
