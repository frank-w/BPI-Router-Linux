// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Felix Fietkau <nbd@nbd.name>
 */

#include "mt7622.h"
#include "eeprom.h"

static int mt7622_eeprom_load(struct mt7622_dev *dev)
{
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7622_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	return 0;
}

int mt7622_eeprom_get_power_index(struct mt7622_dev *dev,
				  struct ieee80211_channel *chan,
 				  u8 chain_idx)
{
	int index;

	if (chain_idx > 3)
		return -EINVAL;

	index = MT_EE_TX0_2G_TARGET_POWER + chain_idx * 6;

	return index;
}

static int mt7622_check_eeprom(struct mt76_dev *dev)
{
	u16 val = get_unaligned_le16(dev->eeprom.data);

	switch (val) {
	case 0x7622:
		return 0;
	default:
		return -EINVAL;
	}
}

static void mt7622_eeprom_parse_hw_cap(struct mt7622_dev *dev)
{
	u8 val, *eeprom = dev->mt76.eeprom.data;

	val = FIELD_GET(MT_EE_NIC_WIFI_CONF_BAND_SEL,
			eeprom[MT_EE_WIFI_CONF]);
	switch (val) {
	case MT_EE_5GHZ:
		dev->mt76.cap.has_5ghz = true;
		break;
	case MT_EE_2GHZ:
		dev->mt76.cap.has_2ghz = true;
		break;
	default:
		dev->mt76.cap.has_2ghz = true;
		dev->mt76.cap.has_5ghz = true;
		break;
	}
}

int mt7622_eeprom_init(struct mt7622_dev *dev)
{
	int ret;

	ret = mt7622_eeprom_load(dev);
	if (ret < 0)
		return ret;

	ret = mt7622_check_eeprom(&dev->mt76);
	if (ret && dev->mt76.otp.data)
		memcpy(dev->mt76.eeprom.data, dev->mt76.otp.data,
		       MT7622_EEPROM_SIZE);

	mt7622_eeprom_parse_hw_cap(dev);
	memcpy(dev->mt76.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);

	mt76_eeprom_override(&dev->mt76);
	dev_info(dev->mt76.dev, "MAC addr = %pM\n", dev->mt76.macaddr);

	return 0;
}
