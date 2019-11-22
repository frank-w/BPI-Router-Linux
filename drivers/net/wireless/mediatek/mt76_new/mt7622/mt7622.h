/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7622_H
#define __MT7622_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include "../mt76.h"
#include "regs.h"

#define MT7622_MAX_INTERFACES		4
#define MT7622_MAX_WMM_SETS		4
#define MT7622_WTBL_SIZE		128
#define MT7622_WTBL_RESERVED		(MT7622_WTBL_SIZE - 1)
#define MT7622_WTBL_STA			(MT7622_WTBL_RESERVED - \
					 MT7622_MAX_INTERFACES)

#define MT7622_WATCHDOG_TIME		(HZ / 10)
#define MT7622_RATE_RETRY		2

#define MT7622_TX_RING_SIZE		512
#define MT7622_TX_MCU_RING_SIZE		128
#define MT7622_TX_FWDL_RING_SIZE	128

#define MT7622_RX_RING_SIZE		512
#define MT7622_RX_MCU_RING_SIZE		512

#define MT7622_FIRMWARE_N9		"mediatek/mt7622_n9.bin"
#define MT7622_ROM_PATCH		"mediatek/mt7622_rom_patch.bin"

#define MT7622_EEPROM_SIZE		1024
#define MT7622_TOKEN_SIZE		4096

struct mt7622_vif;
struct mt7622_sta;

enum mt7622_hw_txq_id {
	MT7622_TXQ_MAIN,
	MT7622_TXQ_EXT,
	MT7622_TXQ_FWDL = 3,
	MT7622_TXQ_MGMT = 5,
	MT7622_TXQ_MCU = 15,
};

struct mt7622_rate_set {
	struct ieee80211_tx_rate probe_rate;
	struct ieee80211_tx_rate rates[4];
};

struct mt7622_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt7622_vif *vif;

	struct ieee80211_tx_rate rates[4];

	struct mt7622_rate_set rateset[2];
	u32 rate_set_tsf;

	u8 rate_count;
	u8 n_rates;

	u8 rate_probe;
};

struct mt7622_vif {
	u8 idx;
	u8 omac_idx;
	u8 band_idx;
	u8 wmm_idx;

	struct mt7622_sta sta;
};

struct mt7622_dev {
	struct mt76_dev mt76; /* must be first */
	u32 vif_mask;
	u32 omac_mask;

	__le32 rx_ampdu_ts;

	int false_cca_ofdm, false_cca_cck;
	unsigned long last_cca_adj;
	u8 mac_work_count;
	s8 ofdm_sensitivity;
	s8 cck_sensitivity;
	bool scs_en;

	spinlock_t token_lock;
	struct idr token;

	const u32 *regs;
};

struct mt7622_rate_desc {
	int wcid;
	u8 bw;
	u8 bw_idx;
	u16 val[4];
	u16 probe_val;
	bool rateset;

	struct mt7622_sta *sta;
	struct list_head node;
};

enum {
	HW_BSSID_0 = 0x0,
	HW_BSSID_1,
	HW_BSSID_2,
	HW_BSSID_3,
	HW_BSSID_MAX,
	EXT_BSSID_START = 0x10,
	EXT_BSSID_1,
	EXT_BSSID_2,
	EXT_BSSID_3,
	EXT_BSSID_4,
	EXT_BSSID_5,
	EXT_BSSID_6,
	EXT_BSSID_7,
	EXT_BSSID_8,
	EXT_BSSID_9,
	EXT_BSSID_10,
	EXT_BSSID_11,
	EXT_BSSID_12,
	EXT_BSSID_13,
	EXT_BSSID_14,
	EXT_BSSID_15,
	EXT_BSSID_END
};

enum {
	MT_RX_SEL0,
	MT_RX_SEL1,
};

extern const struct ieee80211_ops mt7622_ops;

int mt7622_register_device(struct mt7622_dev *dev);
void mt7622_unregister_device(struct mt7622_dev *dev);
int mt7622_eeprom_init(struct mt7622_dev *dev);
int mt7622_eeprom_get_power_index(struct mt7622_dev *dev,
				  struct ieee80211_channel *chan,
 				  u8 chain_idx);
int mt7622_dma_sched_init(struct mt7622_dev *dev);
int mt7622_dma_init(struct mt7622_dev *dev);
void mt7622_dma_cleanup(struct mt7622_dev *dev);
int mt7622_mcu_init(struct mt7622_dev *dev);
int mt7622_mcu_set_dev_info(struct mt7622_dev *dev,
			    struct ieee80211_vif *vif, bool enable);
int mt7622_mcu_set_bss_info(struct mt7622_dev *dev, struct ieee80211_vif *vif,
			    int en);
void mt7622_mac_set_scs(struct mt7622_dev *dev, bool enable);
void mt7622_mac_set_rates(struct mt7622_dev *dev, struct mt7622_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates);
int mt7622_mcu_wtbl_bmc(struct mt7622_dev *dev, struct ieee80211_vif *vif,
			bool enable);
int mt7622_mcu_add_wtbl(struct mt7622_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);
int mt7622_mcu_del_wtbl(struct mt7622_dev *dev, struct ieee80211_sta *sta);
int mt7622_mcu_del_wtbl_all(struct mt7622_dev *dev);
int mt7622_mcu_set_sta_rec_bmc(struct mt7622_dev *dev,
			       struct ieee80211_vif *vif, bool en);
int mt7622_mcu_set_sta_rec(struct mt7622_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, bool en);
int mt7622_mcu_set_bcn(struct mt7622_dev *dev, struct ieee80211_vif *vif,
		       int en);
int mt7622_mcu_set_channel(struct mt7622_dev *dev);
int mt7622_mcu_set_wmm(struct mt7622_dev *dev, u8 queue,
		       const struct ieee80211_tx_queue_params *params);
int mt7622_mcu_set_tx_ba(struct mt7622_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7622_mcu_set_rx_ba(struct mt7622_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool add);
int mt7622_mcu_set_ht_cap(struct mt7622_dev *dev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta);
void mt7622_mcu_rx_event(struct mt7622_dev *dev, struct sk_buff *skb);

static inline void mt7622_irq_enable(struct mt7622_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR(dev), 0, mask);
}

static inline void mt7622_irq_disable(struct mt7622_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR(dev), mask, 0);
}

void mt7622_update_channel(struct mt76_dev *mdev);
void mt7622_mac_cca_stats_reset(struct mt7622_dev *dev);

int mt7622_mac_write_txwi(struct mt7622_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, enum mt76_txq_id qid,
			  struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key);
int mt7622_mac_fill_rx(struct mt7622_dev *dev, struct sk_buff *skb);
void mt7622_mac_add_txs(struct mt7622_dev *dev, void *data);
void mt7622_mac_tx_free(struct mt7622_dev *dev, struct sk_buff *skb);
int mt7622_mac_wtbl_set_key(struct mt7622_dev *dev, struct mt76_wcid *wcid,
			    struct ieee80211_key_conf *key,
			    enum set_key_cmd cmd);

bool mt7622_mac_wtbl_update(struct mt7622_dev *dev, int idx, u32 mask);
int mt7622_mcu_set_eeprom(struct mt7622_dev *dev);
int mt7622_mcu_init_mac(struct mt7622_dev *dev);
int mt7622_mcu_set_rts_thresh(struct mt7622_dev *dev, u32 val);
int mt7622_mcu_ctrl_pm_state(struct mt7622_dev *dev, int enter);
int mt7622_mcu_get_temperature(struct mt7622_dev *dev, int index);
int mt7622_mcu_set_tx_power(struct mt7622_dev *dev);
void mt7622_mcu_exit(struct mt7622_dev *dev);

int mt7622_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);

void mt7622_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			    struct mt76_queue_entry *e);

void mt7622_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);
void mt7622_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps);
int mt7622_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
void mt7622_sta_assoc(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);
void mt7622_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt7622_mac_work(struct work_struct *work);
void mt7622_txp_skb_unmap(struct mt76_dev *dev,
			  struct mt76_txwi_cache *txwi);

int mt7622_init_debugfs(struct mt7622_dev *dev);
#endif
