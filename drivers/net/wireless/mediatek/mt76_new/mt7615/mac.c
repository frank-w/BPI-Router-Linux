// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Roy Luo <roychl666@gmail.com>
 */

#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7615.h"
#include "mac.h"

static struct mt76_wcid *mt7615_rx_get_wcid(struct mt7615_dev *dev,
					    u8 idx, bool unicast)
{
	struct mt7615_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7615_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

static int mt7615_get_rate(struct mt7615_dev *dev,
			   struct ieee80211_supported_band *sband,
			   int idx, bool cck)
{
	int offset = 0;
	int len = sband->n_bitrates;
	int i;

	if (cck) {
		if (sband == &dev->mt76.sband_5g.sband)
			return 0;

		idx &= ~BIT(2); /* short preamble */
	} else if (sband == &dev->mt76.sband_2g.sband) {
		offset = 4;
	}

	for (i = offset; i < len; i++) {
		if ((sband->bitrates[i].hw_value & GENMASK(7, 0)) == idx)
			return i;
	}

	return 0;
}

static void mt7615_insert_ccmp_hdr(struct sk_buff *skb, u8 key_id)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	int hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	u8 *pn = status->iv;
	u8 *hdr;

	__skb_push(skb, 8);
	memmove(skb->data, skb->data + 8, hdr_len);
	hdr = skb->data + hdr_len;

	hdr[0] = pn[5];
	hdr[1] = pn[4];
	hdr[2] = 0;
	hdr[3] = 0x20 | (key_id << 6);
	hdr[4] = pn[3];
	hdr[5] = pn[2];
	hdr[6] = pn[1];
	hdr[7] = pn[0];

	status->flag &= ~RX_FLAG_IV_STRIPPED;
}

int mt7615_mac_fill_rx(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;
	__le32 *rxd = (__le32 *)skb->data;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	bool unicast, remove_pad, insert_ccmp_hdr = false;
	int i, idx;

	memset(status, 0, sizeof(*status));

	i = FIELD_GET(MT_RXD1_NORMAL_CH_FREQ, rxd1);
	sband = (i & 1) ? &dev->mt76.sband_5g.sband :
			  &dev->mt76.sband_2g.sband;
	i >>= 1;

	unicast = (rxd1 & MT_RXD1_NORMAL_ADDR_TYPE) == MT_RXD1_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD2_NORMAL_WLAN_IDX, rxd2);
	status->wcid = mt7615_rx_get_wcid(dev, idx, unicast);

	status->band = sband->band;
	if (i < sband->n_channels)
		status->freq = sband->channels[i].center_freq;

	if (rxd2 & MT_RXD2_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd2 & MT_RXD2_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2) != 0 &&
	    !(rxd2 & (MT_RXD2_NORMAL_CLM | MT_RXD2_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	remove_pad = rxd1 & MT_RXD1_NORMAL_HDR_OFFSET;

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	if (!sband->channels)
		return -EINVAL;

	rxd += 4;
	if (rxd0 & MT_RXD0_NORMAL_GROUP_4) {
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			status->iv[0] = data[5];
			status->iv[1] = data[4];
			status->iv[2] = data[3];
			status->iv[3] = data[2];
			status->iv[4] = data[1];
			status->iv[5] = data[0];

			insert_ccmp_hdr = FIELD_GET(MT_RXD2_NORMAL_FRAG, rxd2);
		}
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_2) {
		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_3) {
		/* TODO: rxv */
		rxd += 6;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	skb_pull(skb, (u8 *)rxd - skb->data + 2 * remove_pad);

	if (insert_ccmp_hdr) {
		u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

		mt7615_insert_ccmp_hdr(skb, key_id);
	}

	hdr = (struct ieee80211_hdr *)skb->data;
	if (!status->wcid || !ieee80211_is_data_qos(hdr->frame_control))
		return 0;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(hdr->frame_control);
	status->tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	status->seqno = IEEE80211_SEQ_TO_SN(hdr->seq_ctrl);

	return 0;
}

int mt7615_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  struct sk_buff *skb, struct mt76_queue *q,
			  struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			  u32 *tx_info)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct mt7615_sta *msta = container_of(wcid, struct mt7615_sta, wcid);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	int pid = 0;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	pid = mt76_tx_status_skb_add(mdev, wcid, skb);

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
		spin_lock_bh(&dev->mt76.lock);
		msta->rate_probe = true;
		mt7615_mcu_set_rates(dev, msta, &info->control.rates[0],
				     msta->rates);
		spin_unlock_bh(&dev->mt76.lock);
	}

	mt7615_mac_write_txwi(dev, txwi_ptr, skb, wcid, sta, pid, key);

	return 0;
}

void mt7615_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}

u16 mt7615_mac_tx_rate_val(struct mt7615_dev *dev,
			   const struct ieee80211_tx_rate *rate,
			   bool stbc, u8 *bw)
{
	u8 phy, nss, rate_idx;
	u16 rateval;

	*bw = 0;

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		rate_idx = ieee80211_rate_get_vht_mcs(rate);
		nss = ieee80211_rate_get_vht_nss(rate);
		phy = MT_PHY_TYPE_VHT;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*bw = 1;
		else if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			*bw = 2;
		else if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
			*bw = 3;
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		rate_idx = rate->idx;
		nss = 1 + (rate->idx >> 3);
		phy = MT_PHY_TYPE_HT;
		if (rate->flags & IEEE80211_TX_RC_GREEN_FIELD)
			phy = MT_PHY_TYPE_HT_GF;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*bw = 1;
	} else {
		const struct ieee80211_rate *r;
		int band = dev->mt76.chandef.chan->band;
		u16 val;

		nss = 1;
		r = &mt76_hw(dev)->wiphy->bands[band]->bitrates[rate->idx];
		if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			val = r->hw_value_short;
		else
			val = r->hw_value;

		phy = val >> 8;
		rate_idx = val & 0xff;
	}

	rateval = (FIELD_PREP(MT_TX_RATE_IDX, rate_idx) |
		   FIELD_PREP(MT_TX_RATE_MODE, phy) |
		   FIELD_PREP(MT_TX_RATE_NSS, nss));

	if (stbc && nss == 1)
		rateval |= MT_TX_RATE_STBC;

	return rateval;
}

int mt7615_mac_write_txwi(struct mt7615_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_vif *vif = info->control.vif;
	int tx_count = 8;
	u8 fc_type, fc_stype, p_fmt, q_idx, omac_idx = 0;
	u16 fc = le16_to_cpu(hdr->frame_control);
	u32 val;

	if (vif) {
		struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

		omac_idx = mvif->omac_idx;
	}

	if (sta) {
		struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

		tx_count = msta->rate_count;
	}

	fc_type = (fc & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (fc & IEEE80211_FCTL_STYPE) >> 4;

	if (ieee80211_is_data(fc)) {
		q_idx = skb_get_queue_mapping(skb);
		p_fmt = MT_TX_TYPE_CT;
	} else if (ieee80211_is_beacon(fc)) {
		q_idx = MT_LMAC_BCN0;
		p_fmt = MT_TX_TYPE_FW;
	} else {
		q_idx = MT_LMAC_ALTX0;
		p_fmt = MT_TX_TYPE_CT;
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_P_IDX, MT_TX_PORT_IDX_LMAC) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID,
			 skb->priority & IEEE80211_QOS_CTL_TID_MASK) |
	      FIELD_PREP(MT_TXD1_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);
	txwi[1] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype) |
	      FIELD_PREP(MT_TXD2_MULTICAST,
			 is_multicast_ether_addr(hdr->addr1));
	txwi[2] = cpu_to_le32(val);

	if (!(info->flags & IEEE80211_TX_CTL_AMPDU))
		txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

	txwi[4] = 0;
	txwi[6] = 0;

	if (rate->idx >= 0 && rate->count &&
	    !(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)) {
		bool stbc = info->flags & IEEE80211_TX_CTL_STBC;
		u8 bw;
		u16 rateval = mt7615_mac_tx_rate_val(dev, rate, stbc, &bw);

		txwi[2] |= cpu_to_le32(MT_TXD2_FIX_RATE);

		val = MT_TXD6_FIXED_BW |
		      FIELD_PREP(MT_TXD6_BW, bw) |
		      FIELD_PREP(MT_TXD6_TX_RATE, rateval);
		txwi[6] |= cpu_to_le32(val);

		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			txwi[6] |= cpu_to_le32(MT_TXD6_SGI);

		if (info->flags & IEEE80211_TX_CTL_LDPC)
			txwi[6] |= cpu_to_le32(MT_TXD6_LDPC);

		if (!(rate->flags & IEEE80211_TX_RC_MCS))
			txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

		tx_count = rate->count;
	}

	if (!ieee80211_is_beacon(fc)) {
		val = MT_TXD5_TX_STATUS_HOST | MT_TXD5_SW_POWER_MGMT |
		      FIELD_PREP(MT_TXD5_PID, pid);
		txwi[5] = cpu_to_le32(val);
	} else {
		txwi[5] = 0;
		/* use maximum tx count for beacons */
		tx_count = 0x1f;
	}

	txwi[3] = cpu_to_le32(FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count));

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		txwi[3] |= cpu_to_le32(MT_TXD3_NO_ACK);

	if (key)
		txwi[3] |= cpu_to_le32(MT_TXD3_PROTECT_FRAME);

	txwi[7] = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
		  FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);

	return 0;
}

static int mt7615_token_enqueue(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_token_queue *q = &dev->tkq;
	int token;

	if (q->queued == q->ntoken)
		return  -ENOSPC;

	spin_lock_bh(&q->lock);

	token = q->id[q->head];
	if (token == q->used) {
		token = -ENOSPC;
		goto out;
	}

	q->id[q->head] = q->used;
	q->skb[token] = skb;

	q->head = (q->head + 1) % q->ntoken;
	q->queued++;

out:
	spin_unlock_bh(&q->lock);
	return token;
}

static void mt7615_token_dequeue(struct mt7615_dev *dev, u16 token)
{
	struct mt7615_token_queue *q = &dev->tkq;
	struct mt76_dev *mdev = &dev->mt76;
	struct sk_buff_head list;
	struct sk_buff *skb;

	if (!q->queued)
		return;

	skb = q->skb[token];

	spin_lock_bh(&q->lock);
	q->id[q->tail] = token;
	q->skb[token] = NULL;

	q->tail = (q->tail + 1) % q->ntoken;
	q->queued--;
	spin_unlock_bh(&q->lock);

	mt76_tx_status_lock(mdev, &list);
	__mt76_tx_status_skb_done(mdev, skb, MT_TX_CB_DMA_TX_FREE, &list);
	mt76_tx_status_unlock(mdev, &list);
}

int mt7615_tx_prepare_txp(struct mt76_dev *mdev, void *txwi_ptr,
			  struct sk_buff *skb, struct mt76_queue_buf *buf)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt7615_txp *txp = (struct mt7615_txp *)((__le32 *)txwi_ptr + 8);
	struct sk_buff *iter;
	int n = 0, res = -ENOMEM, len = skb_headlen(skb);
	dma_addr_t addr;

#define MT_CT_PARSE_LEN		72
#define MT_CT_DMA_BUF_NUM	2

	addr = dma_map_single(mdev->dev, skb->data, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(mdev->dev, addr)))
		return -ENOMEM;

	/* pass partial skb header to fw */
	buf->addr = addr;
	buf->len = MT_CT_PARSE_LEN;

	/* concatenate txp buffers */
	txp->buf[n] = cpu_to_le32(addr);
	txp->len[n++] = cpu_to_le32(len);

	skb_walk_frags(skb, iter) {
		if (n == MT_TXP_MAX_BUF_NUM)
			goto unmap;

		addr = dma_map_single(mdev->dev, iter->data, iter->len,
				      DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(mdev->dev, addr)))
			goto unmap;

		txp->buf[n] = cpu_to_le32(addr);
		txp->len[n++] = cpu_to_le32(iter->len);
	}

	txp->nbuf = n;
	txp->flags = cpu_to_le16(MT_CT_INFO_APPLY_TXD);

	if (!key)
		txp->flags |= cpu_to_le16(MT_CT_INFO_NONE_CIPHER_FRAME);

	if (ieee80211_is_mgmt(hdr->frame_control))
		txp->flags |= cpu_to_le16(MT_CT_INFO_MGMT_FRAME);

	if (vif) {
		struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

		txp->bss_idx = mvif->idx;
	}

	res = mt7615_token_enqueue(dev, skb);
	if (res < 0)
		goto unmap;

	txp->token = cpu_to_le16(res);
	txp->rept_wds_wcid = 0xff;

	/* move txd and partial skb header into tx ring */
	return MT_CT_DMA_BUF_NUM;

unmap:
	for (n--; n >= 0; n--)
		dma_unmap_single(mdev->dev, txp->buf[n], txp->len[n],
				 DMA_TO_DEVICE);

	return res;
}

static bool mt7615_fill_txs(struct mt7615_dev *dev, struct mt7615_sta *sta,
			    struct ieee80211_tx_info *info, __le32 *txs_data)
{
	struct ieee80211_supported_band *sband;
	int i, idx, count, final_idx = 0;
	bool fixed_rate, final_mpdu, ack_timeout;
	bool probe, ampdu, cck = false;
	u32 final_rate, final_rate_flags, txs;
	u8 pid;

	fixed_rate = info->status.rates[0].count;
	probe = !!(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE);

	txs = le32_to_cpu(txs_data[1]);
	final_mpdu = txs & MT_TXS1_ACKED_MPDU;
	ampdu = !fixed_rate && (txs & MT_TXS1_AMPDU);

	txs = le32_to_cpu(txs_data[3]);
	count = FIELD_GET(MT_TXS3_TX_COUNT, txs);

	txs = le32_to_cpu(txs_data[0]);
	pid = FIELD_GET(MT_TXS0_PID, txs);
	final_rate = FIELD_GET(MT_TXS0_TX_RATE, txs);
	ack_timeout = txs & MT_TXS0_ACK_TIMEOUT;

	if (!ampdu && (txs & MT_TXS0_RTS_TIMEOUT))
		return false;

	if (txs & MT_TXS0_QUEUE_TIMEOUT)
		return false;

	if (!ack_timeout)
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = !!(info->flags &
					IEEE80211_TX_STAT_ACK);

	if (ampdu || (info->flags & IEEE80211_TX_CTL_AMPDU))
		info->flags |= IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_CTL_AMPDU;

	if (fixed_rate && !probe) {
		info->status.rates[0].count = count;
		goto out;
	}

	for (i = 0, idx = 0; i < ARRAY_SIZE(info->status.rates); i++) {
		int cur_count = min_t(int, count, 2 * MT7615_RATE_RETRY);

		if (!i && probe) {
			cur_count = 1;
		} else {
			info->status.rates[i] = sta->rates[idx];
			idx++;
		}

		if (i && info->status.rates[i].idx < 0) {
			info->status.rates[i - 1].count += count;
			break;
		}

		if (!count) {
			info->status.rates[i].idx = -1;
			break;
		}

		info->status.rates[i].count = cur_count;
		final_idx = i;
		count -= cur_count;
	}

out:
	final_rate_flags = info->status.rates[final_idx].flags;

	switch (FIELD_GET(MT_TX_RATE_MODE, final_rate)) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		/* fall through */
	case MT_PHY_TYPE_OFDM:
		if (dev->mt76.chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &dev->mt76.sband_5g.sband;
		else
			sband = &dev->mt76.sband_2g.sband;
		final_rate &= GENMASK(5, 0);
		final_rate = mt7615_get_rate(dev, sband, final_rate, cck);
		final_rate_flags = 0;
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		final_rate_flags |= IEEE80211_TX_RC_MCS;
		final_rate &= GENMASK(5, 0);
		if (i > 15)
			return false;
		break;
	default:
		return false;
	}

	info->status.rates[final_idx].idx = final_rate;
	info->status.rates[final_idx].flags = final_rate_flags;

	return true;
}

static bool mt7615_mac_add_txs_skb(struct mt7615_dev *dev,
				   struct mt7615_sta *sta, int pid,
				   __le32 *txs_data)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct sk_buff_head list;
	struct sk_buff *skb;

	if (pid < MT_PACKET_ID_FIRST)
		return false;

	mt76_tx_status_lock(mdev, &list);
	skb = mt76_tx_status_skb_get(mdev, &sta->wcid, pid, &list);
	if (skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
			spin_lock_bh(&dev->mt76.lock);
			if (sta->rate_probe) {
				mt7615_mcu_set_rates(dev, sta, NULL,
						     sta->rates);
				sta->rate_probe = false;
			}
			spin_unlock_bh(&dev->mt76.lock);
		}

		if (!mt7615_fill_txs(dev, sta, info, txs_data)) {
			ieee80211_tx_info_clear_status(info);
			info->status.rates[0].idx = -1;
		}

		mt76_tx_status_skb_done(mdev, skb, &list);
	}
	mt76_tx_status_unlock(mdev, &list);

	return !!skb;
}

void mt7615_mac_add_txs(struct mt7615_dev *dev, void *data)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_sta *sta = NULL;
	struct mt7615_sta *msta = NULL;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	u32 txs;
	u8 wcidx;
	u8 pid;

	txs = le32_to_cpu(txs_data[0]);
	pid = FIELD_GET(MT_TXS0_PID, txs);
	txs = le32_to_cpu(txs_data[2]);
	wcidx = FIELD_GET(MT_TXS2_WCID, txs);

	if (pid == MT_PACKET_ID_NO_ACK)
		return;

	if (wcidx >= ARRAY_SIZE(dev->mt76.wcid))
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		goto out;

	msta = container_of(wcid, struct mt7615_sta, wcid);
	sta = wcid_to_sta(wcid);

	if (mt7615_mac_add_txs_skb(dev, msta, pid, txs_data))
		goto out;

	if (wcidx >= MT7615_WTBL_STA || !sta)
		goto out;

	if (mt7615_fill_txs(dev, msta, &info, txs_data))
		ieee80211_tx_status_noskb(mt76_hw(dev), sta, &info);

out:
	rcu_read_unlock();
}

void mt7615_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			    struct mt76_queue_entry *e, bool flush)
{
	struct sk_buff *skb = e->skb;
	struct sk_buff_head list;
	u8 flags = MT_TX_CB_DMA_TXD_DONE;

	if (!e->txwi) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (!skb->prev)
		flags |= MT_TX_CB_TXS_DONE;

	mt76_tx_status_lock(mdev, &list);
	__mt76_tx_status_skb_done(mdev, skb, flags, &list);
	mt76_tx_status_unlock(mdev, &list);
}

void mt7615_mac_tx_free(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_tx_free *free;
	u8 i, cnt;

	free = (struct mt7615_tx_free *)skb->data;
	cnt = FIELD_GET(MT_TX_FREE_MSDU_ID_CNT, le16_to_cpu(free->ctrl));

	for (i = 0; i < cnt; i++)
		mt7615_token_dequeue(dev, le16_to_cpu(free->token[i]));

	dev_kfree_skb(skb);
}
