// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Roy Luo <roychl666@gmail.com>
 */

#include "mt7615.h"
#include "../dma.h"
#include "mac.h"

void mt7615_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;

	type = FIELD_GET(MT_RXD0_PKT_TYPE, le32_to_cpu(rxd[0]));

	switch (type) {
	case PKT_TYPE_TXS:
		for (rxd++; rxd + 7 <= end; rxd += 7)
			mt7615_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_TXRX_NOTIFY:
		mt7615_mac_tx_free(dev, skb);
		return;
	case PKT_TYPE_RX_EVENT:
		mt76_mcu_rx_event(&dev->mt76, skb);
		return;
	case PKT_TYPE_NORMAL:
		if (!mt7615_mac_fill_rx(dev, skb)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		/* fall through */
	default:
		dev_kfree_skb(skb);
		break;
	}
}

static void mt7615_tx_tasklet(unsigned long data)
{
	struct mt7615_dev *dev = (struct mt7615_dev *)data;
	int i;

	for (i = MT7615_TXQ_MCU; i >= 0; i--)
		mt76_queue_tx_cleanup(dev, i, false);

	mt7615_irq_enable(dev, MT_INT_TX_DONE_ALL);
}

int mt7615_dma_init(struct mt7615_dev *dev)
{
	int ret;

	mt76_ct_dma_attach(&dev->mt76);

	tasklet_init(&dev->tx_tasklet, mt7615_tx_tasklet, (unsigned long)dev);

	mt76_wr(dev, MT_WPDMA_GLO_CFG,
		MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE |
		MT_WPDMA_GLO_CFG_FIFO_LITTLE_ENDIAN |
		MT_WPDMA_GLO_CFG_FIRST_TOKEN_ONLY |
		MT_WPDMA_GLO_CFG_OMIT_TX_INFO);

	mt76_rmw_field(dev, MT_WPDMA_GLO_CFG,
		       MT_WPDMA_GLO_CFG_TX_BT_SIZE_BIT0, 0x1);

	mt76_rmw_field(dev, MT_WPDMA_GLO_CFG,
		       MT_WPDMA_GLO_CFG_TX_BT_SIZE_BIT21, 0x1);

	mt76_rmw_field(dev, MT_WPDMA_GLO_CFG,
		       MT_WPDMA_GLO_CFG_DMA_BURST_SIZE, 0x3);

	mt76_rmw_field(dev, MT_WPDMA_GLO_CFG,
		       MT_WPDMA_GLO_CFG_MULTI_DMA_EN, 0x3);

	mt76_wr(dev, MT_WPDMA_GLO_CFG1, 0x1);
	mt76_wr(dev, MT_WPDMA_TX_PRE_CFG, 0xf0000);
	mt76_wr(dev, MT_WPDMA_RX_PRE_CFG, 0xf7f0000);
	mt76_wr(dev, MT_WPDMA_ABT_CFG, 0x4000026);
	mt76_wr(dev, MT_WPDMA_ABT_CFG1, 0x18811881);
	mt76_set(dev, 0x7158, BIT(16));
	mt76_clear(dev, 0x7000, BIT(23));
	mt76_wr(dev, MT_WPDMA_RST_IDX, ~0);

	ret = mt7615_init_tx_queue(dev, &dev->mt76.q_tx[MT7615_TXQ_MAIN],
				   MT7615_TX_RING_SIZE);
	if (ret)
		return ret;

	ret = mt7615_init_tx_queue(dev, &dev->mt76.q_tx[MT7615_TXQ_MCU],
				   MT7615_TX_MCU_RING_SIZE);
	if (ret)
		return ret;

	ret = mt7615_init_tx_queue(dev, &dev->mt76.q_tx[MT7615_TXQ_FWDL],
				   MT7615_TX_FWDL_RING_SIZE);
	if (ret)
		return ret;

	ret = mt7615_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MCU], 1,
				   MT7615_RX_MCU_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	ret = mt7615_init_rx_queue(dev, &dev->mt76.q_rx[MT_RXQ_MAIN], 0,
				   MT7615_RX_RING_SIZE, MT_RX_BUF_SIZE);
	if (ret)
		return ret;

	mt76_wr(dev, MT_DELAY_INT_CFG, 0);

	return mt76_init_queues(dev);
}

void mt7615_dma_cleanup(struct mt7615_dev *dev)
{
	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_RX_DMA_EN);
	mt76_set(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_SW_RESET);

	tasklet_kill(&dev->tx_tasklet);
	mt76_dma_cleanup(&dev->mt76);
}

static bool wait_for_wpdma(struct mt7615_dev *dev)
{
	return mt76_poll(dev, MT_WPDMA_GLO_CFG,
			 MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
			 MT_WPDMA_GLO_CFG_RX_DMA_BUSY,
			 0, 1000);
}

void mt7615_dma_start(struct mt7615_dev *dev)
{
	wait_for_wpdma(dev);

	mt76_set(dev, MT_WPDMA_GLO_CFG,
		 (MT_WPDMA_GLO_CFG_TX_DMA_EN |
		  MT_WPDMA_GLO_CFG_RX_DMA_EN));

	mt7615_irq_enable(dev, MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL);
}
