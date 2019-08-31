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
#include "raether.h"
#include "ra_mac.h"
#include "ra_ioctl.h"
#include "ra_switch.h"
#include "raether_hwlro.h"
#include "ra_ethtool.h"

void __iomem *ethdma_sysctl_base;
#if defined(CONFIG_RA_HW_NAT)  || defined(CONFIG_RA_HW_NAT_MODULE)
EXPORT_SYMBOL(ethdma_sysctl_base);
#endif
void __iomem *ethdma_frame_engine_base;
#if defined(CONFIG_RA_HW_NAT)  || defined(CONFIG_RA_HW_NAT_MODULE)
EXPORT_SYMBOL(ethdma_frame_engine_base);
#endif
struct net_device *dev_raether;
#if defined(CONFIG_RA_HW_NAT)  || defined(CONFIG_RA_HW_NAT_MODULE)
EXPORT_SYMBOL(dev_raether);
#endif

static int pending_recv;

/* LRO support */
unsigned int lan_ip;
struct lro_para_struct lro_para;
int lro_flush_needed;

static const char *const mtk_clks_source_name[] = {
	"ethif", "esw", "gp0", "gp1", "gp2",
	"sgmii_tx250m", "sgmii_rx250m", "sgmii_cdr_ref", "sgmii_cdr_fb",
	"trgpll", "sgmipll", "eth1pll", "eth2pll"
};

/* reset frame engine */
static void fe_reset(void)
{
	u32 val;

	val = sys_reg_read(RSTCTRL);
	val = val | RALINK_FE_RST;
	sys_reg_write(RSTCTRL, val);

	val = val & ~(RALINK_FE_RST);
	sys_reg_write(RSTCTRL, val);
}

/* Set the hardware MAC address. */
static int ei_set_mac_addr(struct net_device *dev, void *p)
{
	int ret = eth_mac_addr(dev, p);

	if (ret)
		return ret;

	if (netif_running(dev))
		return -EBUSY;

	set_mac_address(dev->dev_addr);

	return 0;
}

static int ei_set_mac2_addr(struct net_device *dev, void *p)
{
	int ret = eth_mac_addr(dev, p);

	if (ret)
		return ret;

	if (netif_running(dev))
		return -EBUSY;

	set_mac2_address(dev->dev_addr);

	return 0;
}

static int rt_get_skb_header(struct sk_buff *skb, void **iphdr, void **tcph,
			     u64 *hdr_flags, void *priv)
{
	struct iphdr *iph = NULL;
	int vhdr_len = 0;

	/* Make sure that this packet is Ethernet II, is not VLAN
	 * tagged, is IPv4, has a valid IP header, and is TCP.
	 */
	if (skb->protocol == 0x0081)
		vhdr_len = VLAN_HLEN;

	iph = (struct iphdr *)(skb->data + vhdr_len);
	if (iph->daddr != lro_para.lan_ip1)
		return -1;
	if (iph->protocol != IPPROTO_TCP)
		return -1;

	*iphdr = iph;
	*tcph = skb->data + (iph->ihl << 2) + vhdr_len;
	*hdr_flags = LRO_IPV4 | LRO_TCP;

	lro_flush_needed = 1;
	return 0;
}

static void ei_sw_lro_init(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	ei_local->lro_mgr.dev = dev;
	memset(&ei_local->lro_mgr.stats, 0, sizeof(ei_local->lro_mgr.stats));
	ei_local->lro_mgr.features = LRO_F_NAPI;
	ei_local->lro_mgr.ip_summed = CHECKSUM_UNNECESSARY;
	ei_local->lro_mgr.ip_summed_aggr = CHECKSUM_UNNECESSARY;
	ei_local->lro_mgr.max_desc = ARRAY_SIZE(ei_local->lro_arr);
	ei_local->lro_mgr.max_aggr = 64;
	ei_local->lro_mgr.frag_align_pad = 0;
	ei_local->lro_mgr.lro_arr = ei_local->lro_arr;
	ei_local->lro_mgr.get_skb_header = rt_get_skb_header;
	lro_flush_needed = 0;
}

static void ei_reset_statistics(struct END_DEVICE *ei_local)
{
	ei_local->stat.tx_packets = 0;
	ei_local->stat.tx_bytes = 0;
	ei_local->stat.tx_dropped = 0;
	ei_local->stat.tx_errors = 0;
	ei_local->stat.tx_aborted_errors = 0;
	ei_local->stat.tx_carrier_errors = 0;
	ei_local->stat.tx_fifo_errors = 0;
	ei_local->stat.tx_heartbeat_errors = 0;
	ei_local->stat.tx_window_errors = 0;

	ei_local->stat.rx_packets = 0;
	ei_local->stat.rx_bytes = 0;
	ei_local->stat.rx_dropped = 0;
	ei_local->stat.rx_errors = 0;
	ei_local->stat.rx_length_errors = 0;
	ei_local->stat.rx_over_errors = 0;
	ei_local->stat.rx_crc_errors = 0;
	ei_local->stat.rx_frame_errors = 0;
	ei_local->stat.rx_fifo_errors = 0;
	ei_local->stat.rx_missed_errors = 0;

	ei_local->stat.collisions = 0;
}

static inline void fe_rx_desc_init(struct PDMA_rxdesc *rx_ring,
				   dma_addr_t dma_addr)
{
	rx_ring->rxd_info1.PDP0 = dma_addr;
	rx_ring->rxd_info2.PLEN0 = MAX_RX_LENGTH;
	rx_ring->rxd_info2.LS0 = 0;
	rx_ring->rxd_info2.DDONE_bit = 0;
}

static int rt2880_eth_recv(struct net_device *dev,
			   struct napi_struct *napi, int budget)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);
	struct PSEUDO_ADAPTER *p_ad = netdev_priv(ei_local->pseudo_dev);
	struct sk_buff *rx_skb;
	unsigned int length = 0;
	int rx_processed = 0;
	struct PDMA_rxdesc *rx_ring, *rx_ring_next;
	unsigned int rx_dma_owner_idx, rx_next_idx;
	void *rx_data, *rx_data_next, *new_data;
	unsigned int skb_size;

#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
	rx_dma_owner_idx = (ei_local->rx_calc_idx[0] + 1) % NUM_RX_DESC;
#else
	rx_dma_owner_idx = (sys_reg_read(RAETH_RX_CALC_IDX0) + 1) % NUM_RX_DESC;
#endif
	rx_ring = &ei_local->rx_ring[0][rx_dma_owner_idx];
	rx_data = ei_local->netrx_skb_data[0][rx_dma_owner_idx];

	skb_size = SKB_DATA_ALIGN(MAX_RX_LENGTH + NET_IP_ALIGN + NET_SKB_PAD) +
	    SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	for (;;) {
		dma_addr_t dma_addr;

		if ((rx_processed++ > budget) ||
		    (rx_ring->rxd_info2.DDONE_bit == 0))
			break;

		rx_next_idx = (rx_dma_owner_idx + 1) % NUM_RX_DESC;
		rx_ring_next = &ei_local->rx_ring[0][rx_next_idx];
		rx_data_next = ei_local->netrx_skb_data[0][rx_next_idx];
		prefetch(rx_ring_next);

		/* We have to check the free memory size is big enough
		 * before pass the packet to cpu
		 */
		new_data = netdev_alloc_frag(skb_size);

		if (unlikely(!new_data)) {
			pr_err("skb not available...\n");
			goto skb_err;
		}

		dma_addr = dma_map_single(dev->dev.parent,
					  new_data + NET_SKB_PAD,
					  MAX_RX_LENGTH, DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(dev->dev.parent, dma_addr))) {
			pr_err("[%s]dma_map_single() failed...\n", __func__);
			skb_free_frag(new_data);
			goto skb_err;
		}

		rx_skb = build_skb(rx_data, skb_size);
		if (unlikely(!rx_skb)) {
			put_page(virt_to_head_page(rx_data));
			pr_err("build_skb failed\n");
			goto skb_err;
		}
		skb_reserve(rx_skb, NET_SKB_PAD + NET_IP_ALIGN);

		length = rx_ring->rxd_info2.PLEN0;
		dma_unmap_single(dev->dev.parent,
				 rx_ring->rxd_info1.PDP0,
				 length, DMA_FROM_DEVICE);

		prefetch(rx_skb->data);

		/* skb processing */
		skb_put(rx_skb, length);

		/* rx packet from GE2 */
		if (rx_ring->rxd_info4.SP == 2) {
			if (likely(ei_local->pseudo_dev)) {
				rx_skb->dev = ei_local->pseudo_dev;
				rx_skb->protocol =
				    eth_type_trans(rx_skb,
						   ei_local->pseudo_dev);
			} else {
				pr_err("pseudo_dev is still not initialize ");
				pr_err("but receive packet from GMAC2\n");
			}
		} else {
			rx_skb->dev = dev;
			rx_skb->protocol = eth_type_trans(rx_skb, dev);
		}

		/* rx checksum offload */
		if (rx_ring->rxd_info4.L4VLD)
			rx_skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			rx_skb->ip_summed = CHECKSUM_NONE;

#if defined(CONFIG_RA_HW_NAT)  || defined(CONFIG_RA_HW_NAT_MODULE)
		if (ra_sw_nat_hook_rx) {
			*(uint32_t *)(FOE_INFO_START_ADDR_HEAD(rx_skb)) =
			    *(uint32_t *)&rx_ring->rxd_info4;
			*(uint32_t *)(FOE_INFO_START_ADDR_TAIL(rx_skb) + 2) =
			    *(uint32_t *)&rx_ring->rxd_info4;
			FOE_ALG_HEAD(rx_skb) = 0;
			FOE_ALG_TAIL(rx_skb) = 0;
			FOE_MAGIC_TAG_HEAD(rx_skb) = FOE_MAGIC_GE;
			FOE_MAGIC_TAG_TAIL(rx_skb) = FOE_MAGIC_GE;
			FOE_TAG_PROTECT_HEAD(rx_skb) = TAG_PROTECT;
			FOE_TAG_PROTECT_TAIL(rx_skb) = TAG_PROTECT;
		}
#endif

/* ra_sw_nat_hook_rx return 1 --> continue
 * ra_sw_nat_hook_rx return 0 --> FWD & without netif_rx
 */
#if defined(CONFIG_RA_HW_NAT)  || defined(CONFIG_RA_HW_NAT_MODULE)
		if ((!ra_sw_nat_hook_rx) ||
		    (ra_sw_nat_hook_rx && ra_sw_nat_hook_rx(rx_skb))) {
#endif
			if (!(ei_local->features & FE_SW_LRO)) {
				if (ei_local->features & FE_INT_NAPI)
					/* napi_gro_receive(napi, rx_skb); */
					netif_receive_skb(rx_skb);
				else
					netif_rx(rx_skb);
			} else {
				if (likely(rx_skb->ip_summed ==
					   CHECKSUM_UNNECESSARY)) {
					lro_receive_skb(&ei_local->lro_mgr,
							rx_skb, NULL);
				} else {
					if (ei_local->features & FE_INT_NAPI)
						netif_receive_skb(rx_skb);
					else
						netif_rx(rx_skb);
				}
			}
#if defined(CONFIG_RA_HW_NAT)  || defined(CONFIG_RA_HW_NAT_MODULE)
		}
#endif

		if (rx_ring->rxd_info4.SP == 2) {
			p_ad->stat.rx_packets++;
			p_ad->stat.rx_bytes += length;
		} else {
			ei_local->stat.rx_packets++;
			ei_local->stat.rx_bytes += length;
		}

		/* init RX desc. */
		fe_rx_desc_init(rx_ring, dma_addr);
		ei_local->netrx_skb_data[0][rx_dma_owner_idx] = new_data;

		/* make sure that all changes to the dma ring are flushed before
		 * we continue
		 */
		wmb();

		sys_reg_write(RAETH_RX_CALC_IDX0, rx_dma_owner_idx);
#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
		ei_local->rx_calc_idx[0] = rx_dma_owner_idx;
#endif

		/* Update to Next packet point that was received.
		 */
#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
		rx_dma_owner_idx = rx_next_idx;
#else
		rx_dma_owner_idx =
		    (sys_reg_read(RAETH_RX_CALC_IDX0) + 1) % NUM_RX_DESC;
#endif

		/* use prefetched variable */
		rx_ring = rx_ring_next;
		rx_data = rx_data_next;
	}			/* for */

	if (lro_flush_needed) {
		ei_lro_flush_all(&ei_local->lro_mgr);
		lro_flush_needed = 0;
	}

	return rx_processed;

skb_err:
	/* rx packet from GE2 */
	if (rx_ring->rxd_info4.SP == 2)
		p_ad->stat.rx_dropped++;
	else
		ei_local->stat.rx_dropped++;

	/* Discard the rx packet */
	fe_rx_desc_init(rx_ring, rx_ring->rxd_info1.PDP0);

	/* make sure that all changes to the dma ring
	 * are flushed before we continue
	 */
	wmb();

	sys_reg_write(RAETH_RX_CALC_IDX0, rx_dma_owner_idx);
#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
	ei_local->rx_calc_idx[0] = rx_dma_owner_idx;
#endif

	return (budget + 1);
}

static int raeth_poll_full(struct napi_struct *napi, int budget)
{
	struct END_DEVICE *ei_local =
	    container_of(napi, struct END_DEVICE, napi);
	struct net_device *netdev = ei_local->netdev;
	unsigned long reg_int_val_rx, reg_int_val_tx;
	unsigned long reg_int_mask_rx, reg_int_mask_tx;
	unsigned long flags;
	int tx_done = 0, rx_done = 0;

	reg_int_val_tx = sys_reg_read(ei_local->fe_tx_int_status);
	reg_int_val_rx = sys_reg_read(ei_local->fe_rx_int_status);

	if (reg_int_val_tx & (TX_DLY_INT | TX_DONE_INT0)) {
		/* Clear TX interrupt status */
		sys_reg_write(ei_local->fe_tx_int_status, (TX_DLY_INT | TX_DONE_INT0));
		tx_done = ei_local->ei_xmit_housekeeping(netdev,
							 NUM_TX_MAX_PROCESS);
	}

	if (reg_int_val_rx & (RX_DLY_INT | RX_DONE_INT3 | RX_DONE_INT2 |
			      RX_DONE_INT1 | RX_DONE_INT0)) {
		/* Clear RX interrupt status */
		sys_reg_write(ei_local->fe_rx_int_status, (RX_DLY_INT |
							   RX_DONE_INT3 |
							   RX_DONE_INT2 |
							   RX_DONE_INT1 |
							   RX_DONE_INT0));
		rx_done = ei_local->ei_eth_recv(netdev, napi, budget);
	}

	if (rx_done >= budget)
		return budget;

	napi_complete(napi);

	spin_lock_irqsave(&ei_local->irq_lock, flags);

	/* Enable TX/RX interrupts */
	reg_int_mask_tx = sys_reg_read(ei_local->fe_tx_int_enable);
	sys_reg_write(ei_local->fe_tx_int_enable,
		      reg_int_mask_tx | TX_DONE_INT0);
	reg_int_mask_rx = sys_reg_read(ei_local->fe_rx_int_enable);
	sys_reg_write(ei_local->fe_rx_int_enable,
		      reg_int_mask_rx | RX_DONE_INT0 | RX_DONE_INT1 |
		      RX_DONE_INT2 | RX_DONE_INT3);

	spin_unlock_irqrestore(&ei_local->irq_lock, flags);

	return rx_done;
}

static int raeth_poll_rx(struct napi_struct *napi, int budget)
{
	struct END_DEVICE *ei_local =
	    container_of(napi, struct END_DEVICE, napi_rx);
	struct net_device *netdev = ei_local->netdev;
	unsigned long reg_int_val_rx;
	unsigned long reg_int_mask_rx;
	unsigned long flags;
	int rx_done = 0;

	reg_int_val_rx = sys_reg_read(ei_local->fe_rx_int_status);

	if (reg_int_val_rx & (RX_DLY_INT | RX_DONE_INT3 | RX_DONE_INT2 |
			      RX_DONE_INT1 | RX_DONE_INT0)) {
		/* Clear RX interrupt status */
		sys_reg_write(ei_local->fe_rx_int_status, (RX_DLY_INT |
							   RX_DONE_INT3 |
							   RX_DONE_INT2 |
							   RX_DONE_INT1 |
							   RX_DONE_INT0));
		rx_done = ei_local->ei_eth_recv(netdev, napi, budget);
	}

	if (rx_done >= budget)
		return budget;

	napi_complete(napi);

	spin_lock_irqsave(&ei_local->irq_lock, flags);

	/* Enable RX interrupt */
	reg_int_mask_rx = sys_reg_read(ei_local->fe_rx_int_enable);
	sys_reg_write(ei_local->fe_rx_int_enable,
		      (reg_int_mask_rx | RX_DONE_INT0 |
		       RX_DONE_INT1 | RX_DONE_INT2 | RX_DONE_INT3));

	spin_unlock_irqrestore(&ei_local->irq_lock, flags);

	return rx_done;
}

static int raeth_poll_tx(struct napi_struct *napi, int budget)
{
	struct END_DEVICE *ei_local =
	    container_of(napi, struct END_DEVICE, napi_tx);
	struct net_device *netdev = ei_local->netdev;
	unsigned long reg_int_val_tx;
	unsigned long reg_int_mask_tx;
	unsigned long flags;
	int tx_done = 0;

	reg_int_val_tx = sys_reg_read(ei_local->fe_tx_int_status);

	if (reg_int_val_tx & (TX_DLY_INT | TX_DONE_INT0)) {
		/* Clear TX interrupt status */
		sys_reg_write(ei_local->fe_tx_int_status, (TX_DLY_INT |
							   TX_DONE_INT0));
		tx_done = ei_local->ei_xmit_housekeeping(netdev,
							 NUM_TX_MAX_PROCESS);
	}

	napi_complete(napi);

	spin_lock_irqsave(&ei_local->irq_lock, flags);

	/* Enable TX interrupts */
	reg_int_mask_tx = sys_reg_read(ei_local->fe_tx_int_enable);
	sys_reg_write(ei_local->fe_tx_int_enable,
		      reg_int_mask_tx | TX_DONE_INT0);

	spin_unlock_irqrestore(&ei_local->irq_lock, flags);

	return 1;
}

static void ei_func_register(struct END_DEVICE *ei_local)
{
	/* TX handling */
	if (ei_local->features & FE_QDMA_TX) {
		ei_local->ei_start_xmit = ei_qdma_start_xmit;
		ei_local->ei_xmit_housekeeping = ei_qdma_xmit_housekeeping;
		ei_local->fe_tx_int_status = (void __iomem *)QFE_INT_STATUS;
		ei_local->fe_tx_int_enable = (void __iomem *)QFE_INT_ENABLE;
	} else {
		ei_local->ei_start_xmit = ei_pdma_start_xmit;
		ei_local->ei_xmit_housekeeping = ei_pdma_xmit_housekeeping;
		ei_local->fe_tx_int_status =
		    (void __iomem *)RAETH_FE_INT_STATUS;
		ei_local->fe_tx_int_enable =
		    (void __iomem *)RAETH_FE_INT_ENABLE;
	}

	/* RX handling */
	if (ei_local->features & FE_QDMA_RX) {
		ei_local->fe_rx_int_status = (void __iomem *)QFE_INT_STATUS;
		ei_local->fe_rx_int_enable = (void __iomem *)QFE_INT_ENABLE;
	} else {
		ei_local->fe_rx_int_status =
		    (void __iomem *)RAETH_FE_INT_STATUS;
		ei_local->fe_rx_int_enable =
		    (void __iomem *)RAETH_FE_INT_ENABLE;
	}

	/* HW LRO handling */
	if (ei_local->features & FE_HW_LRO)
		ei_local->ei_eth_recv = fe_hw_lro_recv;
	else
		ei_local->ei_eth_recv = rt2880_eth_recv;

	/* HW NAT handling */
#if defined(CONFIG_RA_HW_NAT)  || defined(CONFIG_RA_HW_NAT_MODULE)
	if (!(ei_local->features & FE_HW_NAT)) {
		ra_sw_nat_hook_rx = NULL;
		ra_sw_nat_hook_tx = NULL;
	}
#endif
}

static int __init ei_init(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	fe_reset();

	if (ei_local->features & FE_INT_NAPI) {
		/* we run 2 devices on the same DMA ring */
		/* so we need a dummy device for NAPI to work */
		init_dummy_netdev(&ei_local->dummy_dev);

		if (ei_local->features & FE_INT_NAPI_TX_RX) {
			netif_napi_add(&ei_local->dummy_dev, &ei_local->napi_rx,
				       raeth_poll_rx, MTK_NAPI_WEIGHT);
			netif_napi_add(&ei_local->dummy_dev, &ei_local->napi_tx,
				       raeth_poll_tx, MTK_NAPI_WEIGHT);

		} else if (ei_local->features & FE_INT_NAPI_RX_ONLY) {
			netif_napi_add(&ei_local->dummy_dev, &ei_local->napi_rx,
				       raeth_poll_rx, MTK_NAPI_WEIGHT);
		} else {
			netif_napi_add(&ei_local->dummy_dev, &ei_local->napi,
				       raeth_poll_full, MTK_NAPI_WEIGHT);
		}
	}

	spin_lock_init(&ei_local->page_lock);
	spin_lock_init(&ei_local->irq_lock);
	ether_setup(dev);

	if (ei_local->features & FE_SW_LRO)
		ei_sw_lro_init(dev);

	ei_func_register(ei_local);

	/* init  my IP */
	memcpy(ei_local->lan_ip4_addr, FE_DEFAULT_LAN_IP, IP4_ADDR_LEN);

	return 0;
}

static void ei_uninit(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	unregister_netdev(dev);
	free_netdev(dev);

	if (ei_local->features & FE_GE2_SUPPORT) {
		unregister_netdevice(ei_local->pseudo_dev);
		free_netdev(ei_local->pseudo_dev);
	}

	pr_info("Free ei_local and unregister netdev...\n");

	debug_proc_exit();
}

static void ei_mac_addr_setting(struct net_device *dev)
{
	/* If the mac address is invalid, use random mac address  */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		random_ether_addr(dev->dev_addr);
		pr_warn("generated random MAC address %pM\n", dev->dev_addr);
		dev->addr_assign_type = NET_ADDR_RANDOM;
	}

	ei_set_mac_addr(dev, dev->dev_addr);
}

static void ei_mac2_addr_setting(struct net_device *dev)
{
	/* If the mac address is invalid, use random mac address  */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		random_ether_addr(dev->dev_addr);
		pr_warn("generated random MAC address %pM\n", dev->dev_addr);
		dev->addr_assign_type = NET_ADDR_RANDOM;
	}

	ei_set_mac2_addr(dev, dev->dev_addr);
}

#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
static void fe_dma_rx_cal_idx_init(struct END_DEVICE *ei_local)
{
	if (unlikely(ei_local->features & FE_QDMA_RX)) {
		ei_local->rx_calc_idx[0] = sys_reg_read(QRX_CRX_IDX_0);
	} else {		/* PDMA RX */
		ei_local->rx_calc_idx[0] = sys_reg_read(RX_CALC_IDX0);
		if (ei_local->features & FE_HW_LRO) {
			ei_local->rx_calc_idx[1] = sys_reg_read(RX_CALC_IDX1);
			ei_local->rx_calc_idx[2] = sys_reg_read(RX_CALC_IDX2);
			ei_local->rx_calc_idx[3] = sys_reg_read(RX_CALC_IDX3);
		}
	}
}
#endif

static inline int ei_init_ptx_prx(struct net_device *dev)
{
	int err;
	struct END_DEVICE *ei_local = netdev_priv(dev);

	err = fe_pdma_wait_dma_idle();
	if (err)
		return err;

	err = fe_pdma_rx_dma_init(dev);
	if (err)
		return err;

	if (ei_local->features & FE_HW_LRO) {
		err = fe_hw_lro_init(dev);
		if (err)
			return err;
	}
#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
	fe_dma_rx_cal_idx_init(ei_local);
#endif

	err = fe_pdma_tx_dma_init(dev);
	if (err)
		return err;

	set_fe_pdma_glo_cfg();

	/* enable RXD prefetch of ADMA */
	SET_PDMA_LRO_RXD_PREFETCH_EN(ADMA_RXD_PREFETCH_EN |
				     ADMA_MULTI_RXD_PREFETCH_EN);

	return 0;
}

static inline int ei_init_qtx_prx(struct net_device *dev)
{
	int err;
	struct END_DEVICE *ei_local = netdev_priv(dev);

	err = fe_pdma_wait_dma_idle();
	if (err)
		return err;

	err = fe_qdma_wait_dma_idle();
	if (err)
		return err;

	err = fe_qdma_rx_dma_init(dev);
	if (err)
		return err;

	err = fe_pdma_rx_dma_init(dev);
	if (err)
		return err;

	if (ei_local->features & FE_HW_LRO) {
		err = fe_hw_lro_init(dev);
		if (err)
			return err;
	}
#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
	fe_dma_rx_cal_idx_init(ei_local);
#endif

	err = fe_qdma_tx_dma_init(dev);
	if (err)
		return err;

	set_fe_pdma_glo_cfg();
	set_fe_qdma_glo_cfg();

	/* enable RXD prefetch of ADMA */
	SET_PDMA_LRO_RXD_PREFETCH_EN(ADMA_RXD_PREFETCH_EN |
				     ADMA_MULTI_RXD_PREFETCH_EN);

	return 0;
}

static inline int ei_init_qtx_qrx(struct net_device *dev)
{
	int err;
	struct END_DEVICE *ei_local = netdev_priv(dev);

	err = fe_qdma_wait_dma_idle();
	if (err)
		return err;

	err = fe_qdma_rx_dma_init(dev);
	if (err)
		return err;

#ifdef CONFIG_RAETH_RW_PDMAPTR_FROM_VAR
	fe_dma_rx_cal_idx_init(ei_local);
#endif

	err = fe_qdma_tx_dma_init(dev);
	if (err)
		return err;

	set_fe_qdma_glo_cfg();

	return 0;
}

static int ei_init_dma(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	if ((ei_local->features & FE_QDMA_TX) &&
	    (ei_local->features & FE_QDMA_RX))
		return ei_init_qtx_qrx(dev);

	if (ei_local->features & FE_QDMA_TX)
		return ei_init_qtx_prx(dev);
	else
		return ei_init_ptx_prx(dev);
}

static void ei_deinit_dma(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	if (ei_local->features & FE_QDMA_TX)
		fe_qdma_tx_dma_deinit(dev);
	else
		fe_pdma_tx_dma_deinit(dev);

	if (ei_local->features & FE_QDMA_RX)
		fe_qdma_rx_dma_deinit(dev);
	else
		fe_pdma_rx_dma_deinit(dev);

	if (ei_local->features & FE_HW_LRO)
		fe_hw_lro_deinit(dev);

	pr_info("Free TX/RX Ring Memory!\n");
}

/* MT7623 PSE reset workaround to do PSE reset */
void fe_do_reset(void)
{
	u32 adma_rx_dbg0_r = 0;
	u32 dbg_rx_curr_state, rx_fifo_wcnt;
	u32 dbg_cdm_lro_rinf_afifo_rempty, dbg_cdm_eof_rdy_afifo_empty;
	u32 reg_tmp, loop_count;
	unsigned long flags;
	struct END_DEVICE *ei_local = netdev_priv(dev_raether);

	ei_local->fe_reset_times++;
	/* do CDM/PDMA reset */
	pr_crit("[%s] CDM/PDMA reset (%d times)!!!\n", __func__,
		ei_local->fe_reset_times);
	spin_lock_irqsave(&ei_local->page_lock, flags);
	reg_tmp = sys_reg_read(FE_GLO_MISC);
	reg_tmp |= 0x1;
	sys_reg_write(FE_GLO_MISC, reg_tmp);
	mdelay(10);
	reg_tmp = sys_reg_read(ADMA_LRO_CTRL_DW3);
	reg_tmp |= (0x1 << 14);
	sys_reg_write(ADMA_LRO_CTRL_DW3, reg_tmp);
	loop_count = 0;
	do {
		adma_rx_dbg0_r = sys_reg_read(ADMA_RX_DBG0);
		dbg_rx_curr_state = (adma_rx_dbg0_r >> 16) & 0x7f;
		rx_fifo_wcnt = (adma_rx_dbg0_r >> 8) & 0x3f;
		dbg_cdm_lro_rinf_afifo_rempty = (adma_rx_dbg0_r >> 7) & 0x1;
		dbg_cdm_eof_rdy_afifo_empty = (adma_rx_dbg0_r >> 6) & 0x1;
		loop_count++;
		if (loop_count >= 100) {
			pr_err("[%s] loop_count timeout!!!\n", __func__);
			break;
		}
		mdelay(10);
	} while (((dbg_rx_curr_state != 0x17) && (dbg_rx_curr_state != 0x00)) ||
		 (rx_fifo_wcnt != 0) ||
		 (!dbg_cdm_lro_rinf_afifo_rempty) ||
		 (!dbg_cdm_eof_rdy_afifo_empty));
	reg_tmp = sys_reg_read(ADMA_LRO_CTRL_DW3);
	reg_tmp &= 0xffffbfff;
	sys_reg_write(ADMA_LRO_CTRL_DW3, reg_tmp);
	reg_tmp = sys_reg_read(FE_GLO_MISC);
	reg_tmp &= 0xfffffffe;
	sys_reg_write(FE_GLO_MISC, reg_tmp);
	spin_unlock_irqrestore(&ei_local->page_lock, flags);
}

/* MT7623 PSE reset workaround to poll if PSE hang */
static int fe_reset_thread(void *data)
{
	u32 adma_rx_dbg0_r = 0;
	u32 dbg_rx_curr_state, rx_fifo_wcnt;
	u32 dbg_cdm_lro_rinf_afifo_rempty, dbg_cdm_eof_rdy_afifo_empty;

	pr_info("%s called\n", __func__);

	for (;;) {
		adma_rx_dbg0_r = sys_reg_read(ADMA_RX_DBG0);
		dbg_rx_curr_state = (adma_rx_dbg0_r >> 16) & 0x7f;
		rx_fifo_wcnt = (adma_rx_dbg0_r >> 8) & 0x3f;
		dbg_cdm_lro_rinf_afifo_rempty = (adma_rx_dbg0_r >> 7) & 0x1;
		dbg_cdm_eof_rdy_afifo_empty = (adma_rx_dbg0_r >> 6) & 0x1;

		/* check if PSE P0 hang */
		if (dbg_cdm_lro_rinf_afifo_rempty &&
		    dbg_cdm_eof_rdy_afifo_empty &&
		    (rx_fifo_wcnt & 0x20) &&
		    ((dbg_rx_curr_state == 0x17) ||
		     (dbg_rx_curr_state == 0x00))) {
			fe_do_reset();
		}

		msleep(FE_RESET_POLLING_MS);
		if (kthread_should_stop())
			break;
	}

	pr_info("%s leaved\n", __func__);
	return 0;
}

static irqreturn_t ei_interrupt_napi(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local = netdev_priv(dev);
	unsigned long flags;

	gsw_delay_setting();

	if (likely(napi_schedule_prep(&ei_local->napi))) {
		spin_lock_irqsave(&ei_local->irq_lock, flags);

		/* Disable TX interrupt */
		sys_reg_write(ei_local->fe_tx_int_enable, 0);
		/* Disable RX interrupt */
		sys_reg_write(ei_local->fe_rx_int_enable, 0);

		spin_unlock_irqrestore(&ei_local->irq_lock, flags);

		__napi_schedule(&ei_local->napi);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ei_interrupt_napi_sep(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local = netdev_priv(dev);
	unsigned int reg_int_mask;
	unsigned long flags;

	gsw_delay_setting();

	if (likely(napi_schedule_prep(&ei_local->napi_tx))) {
		spin_lock_irqsave(&ei_local->irq_lock, flags);

		/* Disable TX interrupt */
		reg_int_mask = sys_reg_read(ei_local->fe_tx_int_enable);
		sys_reg_write(ei_local->fe_tx_int_enable,
			      reg_int_mask & ~(TX_DLY_INT | TX_DONE_INT0));

		spin_unlock_irqrestore(&ei_local->irq_lock, flags);

		__napi_schedule(&ei_local->napi_tx);
	}

	if (likely(napi_schedule_prep(&ei_local->napi_rx))) {
		spin_lock_irqsave(&ei_local->irq_lock, flags);

		/* Disable RX interrupt */
		reg_int_mask = sys_reg_read(ei_local->fe_rx_int_enable);
		sys_reg_write(ei_local->fe_rx_int_enable,
			      reg_int_mask & ~(RX_DLY_INT |
					       RX_DONE_INT2 |
					       RX_DONE_INT3 |
					       RX_DONE_INT1 | RX_DONE_INT0));

		spin_unlock_irqrestore(&ei_local->irq_lock, flags);

		__napi_schedule(&ei_local->napi_rx);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ei_interrupt(int irq, void *dev_id)
{
	unsigned long reg_int_val = 0;
	unsigned long reg_int_val_p = 0;
	unsigned long reg_int_val_q = 0;
	unsigned long reg_int_mask = 0;
	unsigned int recv = 0;

	unsigned int transmit __maybe_unused = 0;
	unsigned long flags;

	struct net_device *dev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local = netdev_priv(dev);

	if (!dev) {
		pr_err("net_interrupt(): irq %x for unknown device.\n",
		       IRQ_ENET0);
		return IRQ_NONE;
	}

	spin_lock_irqsave(&ei_local->irq_lock, flags);
	reg_int_val_p = sys_reg_read(RAETH_FE_INT_STATUS);

	if (ei_local->features & FE_QDMA)
		reg_int_val_q = sys_reg_read(QFE_INT_STATUS);
	reg_int_val = reg_int_val_p | reg_int_val_q;

	if (ei_local->features & FE_DLY_INT) {
		if ((reg_int_val & RX_DLY_INT))
			recv = 1;

		if (reg_int_val & RAETH_TX_DLY_INT)
			transmit = 1;
	} else {
		if ((reg_int_val &
		     (RX_DONE_INT0 | RX_DONE_INT3 | RX_DONE_INT2 |
		      RX_DONE_INT1)))
			recv = 1;

		if (reg_int_val & RAETH_TX_DONE_INT0)
			transmit |= RAETH_TX_DONE_INT0;
	}

	if (ei_local->features & FE_QDMA)
		sys_reg_write(QFE_INT_STATUS, reg_int_val_q);

	ei_local->ei_xmit_housekeeping(dev, NUM_TX_MAX_PROCESS);

	/* QWERT */
	sys_reg_write(RAETH_FE_INT_STATUS, reg_int_val_p);

	if (((recv == 1) || (pending_recv == 1)) &&
	    (ei_local->tx_ring_full == 0)) {
		reg_int_mask = sys_reg_read(RAETH_FE_INT_ENABLE);
		if (ei_local->features & FE_DLY_INT)
			sys_reg_write(RAETH_FE_INT_ENABLE,
				      reg_int_mask & ~(RX_DLY_INT));
		else
			sys_reg_write(RAETH_FE_INT_ENABLE,
				      reg_int_mask & ~(RX_DONE_INT0 |
						       RX_DONE_INT1 |
						       RX_DONE_INT2 |
						       RX_DONE_INT3));

		if (ei_local->features & FE_QDMA) {
			reg_int_mask = sys_reg_read(QFE_INT_ENABLE);
			if (ei_local->features & FE_DLY_INT)
				sys_reg_write(QFE_INT_ENABLE,
					      reg_int_mask & ~(RX_DLY_INT));
			else
				sys_reg_write(QFE_INT_ENABLE,
					      reg_int_mask & ~(RX_DONE_INT0 |
							       RX_DONE_INT1 |
							       RX_DONE_INT2 |
							       RX_DONE_INT3));
		}

		pending_recv = 0;

		if (ei_local->features & FE_INT_WORKQ)
			schedule_work(&ei_local->rx_wq);
		else
			tasklet_hi_schedule(&ei_local->rx_tasklet);
	} else if (recv == 1 && ei_local->tx_ring_full == 1) {
		pending_recv = 1;
	} else if ((recv == 0) && (transmit == 0)) {
		gsw_delay_setting();
	}
	spin_unlock_irqrestore(&ei_local->irq_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t ei_rx_interrupt_napi(int irq, void *dev_id)
{
	struct net_device *netdev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local;
	unsigned int reg_int_val;
	unsigned long flags;

	if (unlikely(!netdev)) {
		pr_err("net_interrupt(): irq %x for unknown device.\n", irq);
		return IRQ_NONE;
	}
	ei_local = netdev_priv(netdev);

	reg_int_val = sys_reg_read(ei_local->fe_rx_int_status);
	if (likely(reg_int_val & (RX_DLY_INT | RX_DONE_INT3 | RX_DONE_INT2 |
				  RX_DONE_INT1 | RX_DONE_INT0))) {
		if (likely(napi_schedule_prep(&ei_local->napi))) {
			spin_lock_irqsave(&ei_local->irq_lock, flags);

			/* Disable RX interrupt */
			sys_reg_write(ei_local->fe_rx_int_enable, 0);
			/* Disable TX interrupt */
			sys_reg_write(ei_local->fe_tx_int_enable, 0);

			spin_unlock_irqrestore(&ei_local->irq_lock, flags);

			__napi_schedule(&ei_local->napi);
		}
	} else {
		spin_lock_irqsave(&ei_local->irq_lock, flags);

		/* Ack other interrupt status except TX irqs */
		reg_int_val &= ~(TX_DLY_INT | TX_DONE_INT0);
		sys_reg_write(ei_local->fe_rx_int_status, reg_int_val);

		spin_unlock_irqrestore(&ei_local->irq_lock, flags);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ei_rx_interrupt_napi_sep(int irq, void *dev_id)
{
	struct net_device *netdev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local;
	unsigned int reg_int_val, reg_int_mask;
	unsigned long flags;

	if (unlikely(!netdev)) {
		pr_err("net_interrupt(): irq %x for unknown device.\n", irq);
		return IRQ_NONE;
	}
	ei_local = netdev_priv(netdev);

	reg_int_val = sys_reg_read(ei_local->fe_rx_int_status);
	if (likely(reg_int_val & (RX_DLY_INT | RX_DONE_INT3 | RX_DONE_INT2 |
				  RX_DONE_INT1 | RX_DONE_INT0))) {
		if (likely(napi_schedule_prep(&ei_local->napi_rx))) {
			spin_lock_irqsave(&ei_local->irq_lock, flags);

			/* Disable RX interrupt */
			reg_int_mask = sys_reg_read(ei_local->fe_rx_int_enable);
			sys_reg_write(ei_local->fe_rx_int_enable,
				      reg_int_mask & ~(RX_DLY_INT |
						       RX_DONE_INT2 |
						       RX_DONE_INT3 |
						       RX_DONE_INT1 |
						       RX_DONE_INT0));

			spin_unlock_irqrestore(&ei_local->irq_lock, flags);

			__napi_schedule(&ei_local->napi_rx);
		}
	} else {
		spin_lock_irqsave(&ei_local->irq_lock, flags);

		/* Ack other interrupt status except TX irqs */
		reg_int_val &= ~(TX_DLY_INT | TX_DONE_INT0);
		sys_reg_write(ei_local->fe_rx_int_status, reg_int_val);

		spin_unlock_irqrestore(&ei_local->irq_lock, flags);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ei_rx_interrupt(int irq, void *dev_id)
{
	unsigned long reg_int_val;
	unsigned long reg_int_mask;
	unsigned int recv = 0;
	unsigned long flags;

	struct net_device *netdev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local;

	if (unlikely(!netdev)) {
		pr_err("net_interrupt(): irq %x for unknown device.\n", irq);
		return IRQ_NONE;
	}
	ei_local = netdev_priv(netdev);

	spin_lock_irqsave(&ei_local->irq_lock, flags);

	reg_int_val = sys_reg_read(ei_local->fe_rx_int_status);
	if (reg_int_val & (RX_DLY_INT | RX_DONE_INT3 | RX_DONE_INT2 |
			   RX_DONE_INT1 | RX_DONE_INT0))
		recv = 1;

	/* Clear RX interrupt status */
	sys_reg_write(ei_local->fe_rx_int_status, (RX_DLY_INT | RX_DONE_INT3 |
						   RX_DONE_INT2 | RX_DONE_INT1 |
						   RX_DONE_INT0));

	if (likely(((recv == 1) || (pending_recv == 1)) &&
		   (ei_local->tx_ring_full == 0))) {
		reg_int_mask = sys_reg_read(ei_local->fe_rx_int_enable);
		/* Disable RX interrupt */
		sys_reg_write(ei_local->fe_rx_int_enable,
			      reg_int_mask & ~(RX_DLY_INT |
					       RX_DONE_INT2 |
					       RX_DONE_INT3 |
					       RX_DONE_INT1 | RX_DONE_INT0));
		pending_recv = 0;

		if (likely(ei_local->features & FE_INT_TASKLET))
			tasklet_hi_schedule(&ei_local->rx_tasklet);
		else
			schedule_work(&ei_local->rx_wq);
	} else if (recv == 1 && ei_local->tx_ring_full == 1) {
		pending_recv = 1;
	} else if ((recv == 0)) {
		gsw_delay_setting();
	}

	spin_unlock_irqrestore(&ei_local->irq_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t ei_tx_interrupt_napi(int irq, void *dev_id)
{
	struct net_device *netdev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local;
	unsigned long flags;
	unsigned int reg_int_val;

	if (unlikely(!netdev)) {
		pr_err("net_interrupt(): irq %x for unknown device.\n", irq);
		return IRQ_NONE;
	}
	ei_local = netdev_priv(netdev);

	reg_int_val = sys_reg_read(ei_local->fe_tx_int_status);
	if (likely(reg_int_val & (TX_DLY_INT | TX_DONE_INT0))) {
		if (likely(napi_schedule_prep(&ei_local->napi))) {
			spin_lock_irqsave(&ei_local->irq_lock, flags);

			/* Disable TX interrupt */
			sys_reg_write(ei_local->fe_tx_int_enable, 0);
			/* Disable RX interrupt */
			sys_reg_write(ei_local->fe_rx_int_enable, 0);

			spin_unlock_irqrestore(&ei_local->irq_lock, flags);

			__napi_schedule(&ei_local->napi);
		}
	} else {
		spin_lock_irqsave(&ei_local->irq_lock, flags);

		/* Ack other interrupt status except RX irqs */
		reg_int_val &= ~(RX_DLY_INT | RX_DONE_INT2 | RX_DONE_INT3 |
				 RX_DONE_INT1 | RX_DONE_INT0);
		sys_reg_write(ei_local->fe_tx_int_status, reg_int_val);

		spin_unlock_irqrestore(&ei_local->irq_lock, flags);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ei_tx_interrupt_napi_sep(int irq, void *dev_id)
{
	struct net_device *netdev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local;
	unsigned long flags;
	unsigned int reg_int_val, reg_int_mask;

	if (unlikely(!netdev)) {
		pr_err("net_interrupt(): irq %x for unknown device.\n", irq);
		return IRQ_NONE;
	}
	ei_local = netdev_priv(netdev);

	reg_int_val = sys_reg_read(ei_local->fe_tx_int_status);
	if (likely(reg_int_val & (TX_DLY_INT | TX_DONE_INT0))) {
		if (likely(napi_schedule_prep(&ei_local->napi_tx))) {
			spin_lock_irqsave(&ei_local->irq_lock, flags);

			/* Disable TX interrupt */
			reg_int_mask = sys_reg_read(ei_local->fe_tx_int_enable);
			sys_reg_write(ei_local->fe_tx_int_enable,
				      reg_int_mask & ~(TX_DLY_INT |
						       TX_DONE_INT0));

			spin_unlock_irqrestore(&ei_local->irq_lock, flags);

			__napi_schedule(&ei_local->napi_tx);
		}
	} else {
		spin_lock_irqsave(&ei_local->irq_lock, flags);

		/* Ack other interrupt status except RX irqs */
		reg_int_val &= ~(RX_DLY_INT | RX_DONE_INT2 | RX_DONE_INT3 |
				 RX_DONE_INT1 | RX_DONE_INT0);
		sys_reg_write(ei_local->fe_tx_int_status, reg_int_val);

		spin_unlock_irqrestore(&ei_local->irq_lock, flags);
	}

	return IRQ_HANDLED;
}

static irqreturn_t ei_tx_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = (struct net_device *)dev_id;
	struct END_DEVICE *ei_local;
	unsigned long flags;
	unsigned long reg_int_val, reg_int_mask;

	if (unlikely(!netdev)) {
		pr_err("net_interrupt(): irq %x for unknown device.\n", irq);
		return IRQ_NONE;
	}
	ei_local = netdev_priv(netdev);

	spin_lock_irqsave(&ei_local->irq_lock, flags);

	reg_int_val = sys_reg_read(ei_local->fe_tx_int_status);

	if (likely(reg_int_val & (TX_DLY_INT | TX_DONE_INT0))) {
		/* Disable TX interrupt */
		reg_int_mask = sys_reg_read(ei_local->fe_tx_int_enable);
		sys_reg_write(ei_local->fe_tx_int_enable,
			      reg_int_mask & ~(TX_DLY_INT | TX_DONE_INT0));
		/* Clear TX interrupt status */
		sys_reg_write(ei_local->fe_tx_int_status, (TX_DLY_INT |
							   TX_DONE_INT0));
		ei_local->ei_xmit_housekeeping(netdev, NUM_TX_MAX_PROCESS);

		/* Enable TX interrupt */
		reg_int_mask = sys_reg_read(ei_local->fe_rx_int_enable);
		if (likely(ei_local->features & FE_DLY_INT))
			sys_reg_write(ei_local->fe_tx_int_enable,
				      reg_int_mask | TX_DLY_INT);
		else
			sys_reg_write(ei_local->fe_tx_int_enable,
				      reg_int_mask | TX_DONE_INT0);
	}

	spin_unlock_irqrestore(&ei_local->irq_lock, flags);

	return IRQ_HANDLED;
}

static inline void ei_receive(void)
{
	struct net_device *dev = dev_raether;
	struct END_DEVICE *ei_local = netdev_priv(dev);
	unsigned long reg_int_mask;
	int rx_processed;
	unsigned long flags;

	if (ei_local->tx_ring_full == 0) {
		rx_processed = ei_local->ei_eth_recv(dev, NULL,
						     NUM_RX_MAX_PROCESS);
		if (rx_processed > NUM_RX_MAX_PROCESS) {
			if (likely(ei_local->features & FE_INT_TASKLET))
				tasklet_hi_schedule(&ei_local->rx_tasklet);
			else
				schedule_work(&ei_local->rx_wq);
		} else {
			spin_lock_irqsave(&ei_local->irq_lock, flags);
			/* Enable RX interrupt */
			reg_int_mask = sys_reg_read(ei_local->fe_rx_int_enable);
			if (likely(ei_local->features & FE_DLY_INT))
				sys_reg_write(ei_local->fe_rx_int_enable,
					      reg_int_mask | RX_DLY_INT);
			else
				sys_reg_write(ei_local->fe_rx_int_enable,
					      (reg_int_mask | RX_DONE_INT0 |
					       RX_DONE_INT1 | RX_DONE_INT2 |
					       RX_DONE_INT3));
			spin_unlock_irqrestore(&ei_local->irq_lock, flags);
		}
	} else {
		if (likely(ei_local->features & FE_INT_TASKLET))
			tasklet_schedule(&ei_local->rx_tasklet);
		else
			schedule_work(&ei_local->rx_wq);
	}
}

static void ei_receive_tasklet(unsigned long unused)
{
	ei_receive();
}

static void ei_receive_workq(struct work_struct *work)
{
	ei_receive();
}

static int fe_int_enable(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);
	struct device_node *np = ei_local->switch_np;
	struct platform_device *pdev = of_find_device_by_node(np);
	int err0 = 0, err1 = 0, err2 = 0;
	struct mtk_gsw *gsw;
	unsigned long flags;

	if (ei_local->features & FE_INT_NAPI) {
		if (ei_local->features & FE_INT_NAPI_TX_RX)
			err0 =
			    request_irq(ei_local->irq0, ei_interrupt_napi_sep,
					IRQF_TRIGGER_LOW, dev->name, dev);
		else if (!(ei_local->features & FE_INT_NAPI_RX_ONLY))
			err0 =
			    request_irq(ei_local->irq0, ei_interrupt_napi,
					IRQF_TRIGGER_LOW, dev->name, dev);
	} else
		err0 =
		    request_irq(ei_local->irq0, ei_interrupt, IRQF_TRIGGER_LOW,
				dev->name, dev);

	if (ei_local->features & FE_IRQ_SEPARATE) {
		if (ei_local->features & FE_INT_NAPI) {
			if (ei_local->features & FE_INT_NAPI_TX_RX) {
				err1 =
				    request_irq(ei_local->irq1,
						ei_tx_interrupt_napi_sep,
						IRQF_TRIGGER_LOW,
						"eth_tx", dev);
				err2 =
				    request_irq(ei_local->irq2,
						ei_rx_interrupt_napi_sep,
						IRQF_TRIGGER_LOW,
						"eth_rx", dev);
			} else if (ei_local->features & FE_INT_NAPI_RX_ONLY) {
				pr_info("!!!!!!!FE_INT_NAPI_RX_ONLY\n");
				err1 =
				    request_irq(ei_local->irq1,
						ei_tx_interrupt,
						IRQF_TRIGGER_LOW,
						"eth_tx", dev);
				err2 =
				    request_irq(ei_local->irq2,
						ei_rx_interrupt_napi_sep,
						IRQF_TRIGGER_LOW,
						"eth_rx", dev);
			} else {
				err1 =
				    request_irq(ei_local->irq1,
						ei_tx_interrupt_napi,
						IRQF_TRIGGER_LOW,
						"eth_tx", dev);
				err2 =
				    request_irq(ei_local->irq2,
						ei_rx_interrupt_napi,
						IRQF_TRIGGER_LOW,
						"eth_rx", dev);
			}
		} else {
			err1 =
			    request_irq(ei_local->irq1, ei_tx_interrupt,
					IRQF_TRIGGER_LOW, "eth_tx", dev);
			err2 =
			    request_irq(ei_local->irq2, ei_rx_interrupt,
					IRQF_TRIGGER_LOW, "eth_rx", dev);
		}
	}
	if (err0 | err1 | err2)
		return (err0 | err1 | err2);

	if (ei_local->chip_name == MT7623_FE) {
		gsw = platform_get_drvdata(pdev);
		if (request_threaded_irq(gsw->irq, gsw_interrupt, NULL, 0,
					 "gsw", NULL))
			pr_err("fail to request irq\n");

		/* enable switch link change intr */
		mii_mgr_write(31, 0x7008, 0x1f);
	}

	if (ei_local->features & RAETH_ESW)
		request_irq(ei_local->esw_irq, esw_interrupt, IRQF_TRIGGER_LOW, "esw", dev);

	spin_lock_irqsave(&ei_local->irq_lock, flags);

	/* Enable PDMA interrupts */
	if ((ei_local->features & FE_DLY_INT) &&
	    !(ei_local->features & FE_INT_NAPI)) {
		sys_reg_write(RAETH_DLY_INT_CFG, DELAY_INT_INIT);
		sys_reg_write(RAETH_FE_INT_ENABLE, RAETH_FE_INT_DLY_INIT);
	} else {
		sys_reg_write(RAETH_FE_INT_ENABLE, RAETH_FE_INT_ALL);
	}

	/* Enable QDMA interrupts */
	if (ei_local->features & FE_QDMA) {
		if ((ei_local->features & FE_DLY_INT) &&
		    !(ei_local->features & FE_INT_NAPI)) {
			sys_reg_write(QDMA_DELAY_INT, DELAY_INT_INIT);
			sys_reg_write(QFE_INT_ENABLE, QFE_INT_DLY_INIT);
		} else {
			sys_reg_write(QFE_INT_ENABLE, QFE_INT_ALL);
		}
		if (ei_local->features & FE_INT_NAPI_RX_ONLY) {
			sys_reg_write(QDMA_DELAY_INT, DELAY_INT_INIT);
			sys_reg_write(QFE_INT_ENABLE, QFE_INT_DLY_INIT);
		}
	}

	/* IRQ separation settings */
	if (ei_local->features & FE_IRQ_SEPARATE) {
		if ((ei_local->features & FE_DLY_INT) &&
		    !(ei_local->features & FE_INT_NAPI)) {
			/* PDMA setting */
			sys_reg_write(RALINK_FRAME_ENGINE_BASE + RAPDMA_OFFSET +
				      0x250, TX_DLY_INT);
			sys_reg_write(RALINK_FRAME_ENGINE_BASE + RAPDMA_OFFSET +
				      0x254, RX_DLY_INT);
			/* QDMA setting */
			sys_reg_write(RALINK_FRAME_ENGINE_BASE + QDMA_RELATED +
				      0x220, RLS_DLY_INT);
			sys_reg_write(RALINK_FRAME_ENGINE_BASE + QDMA_RELATED +
				      0x224, RX_DLY_INT);
		} else {
			/* PDMA setting */
			sys_reg_write(RALINK_FRAME_ENGINE_BASE + RAPDMA_OFFSET +
				      0x250, TX_DONE_INT0);
			sys_reg_write(RALINK_FRAME_ENGINE_BASE + RAPDMA_OFFSET +
				      0x254,
				      RX_DONE_INT0 |
				      RX_DONE_INT1 |
				      RX_DONE_INT2 | RX_DONE_INT3);

			if (ei_local->features & FE_INT_NAPI_RX_ONLY) {
				/* QDMA setting */
				sys_reg_write(RALINK_FRAME_ENGINE_BASE + QDMA_RELATED +
					      0x220, RLS_DLY_INT);
				sys_reg_write(RALINK_FRAME_ENGINE_BASE + QDMA_RELATED +
					      0x224, RX_DLY_INT);
			} else {
				/* QDMA setting */
				sys_reg_write(QFE_INT_ENABLE, QFE_INT_ALL);
				sys_reg_write(RALINK_FRAME_ENGINE_BASE + QDMA_RELATED +
					      0x220, RLS_DONE_INT);
				sys_reg_write(RALINK_FRAME_ENGINE_BASE + QDMA_RELATED +
					      0x224, RX_DONE_INT0 | RX_DONE_INT1);
			}

		}
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0x20, 0x21021000);
	}

	if (ei_local->features & FE_INT_TASKLET) {
		tasklet_init(&ei_local->rx_tasklet, ei_receive_tasklet, 0);
	} else if (ei_local->features & FE_INT_WORKQ) {
		INIT_WORK(&ei_local->rx_wq, ei_receive_workq);
	} else {
		if (ei_local->features & FE_INT_NAPI_TX_RX) {
			napi_enable(&ei_local->napi_tx);
			napi_enable(&ei_local->napi_rx);
		} else if (ei_local->features & FE_INT_NAPI_RX_ONLY) {
			napi_enable(&ei_local->napi_rx);
		} else {
			napi_enable(&ei_local->napi);
		}
	}

	spin_unlock_irqrestore(&ei_local->irq_lock, flags);

	return 0;
}

static int fe_int_disable(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	free_irq(ei_local->irq0, dev);
	if (ei_local->features & FE_IRQ_SEPARATE) {
		free_irq(ei_local->irq1, dev);
		free_irq(ei_local->irq2, dev);
	}

	if (ei_local->features & RAETH_ESW)
		free_irq(ei_local->esw_irq, dev);

	cancel_work_sync(&ei_local->reset_task);

	if (ei_local->features & FE_INT_WORKQ)
		cancel_work_sync(&ei_local->rx_wq);
	else if (ei_local->features & FE_INT_TASKLET)
		tasklet_kill(&ei_local->rx_tasklet);

	if (ei_local->features & FE_INT_NAPI) {
		if (ei_local->features & FE_INT_NAPI_TX_RX) {
			napi_disable(&ei_local->napi_tx);
			napi_disable(&ei_local->napi_rx);
		} else if (ei_local->features & FE_INT_NAPI_RX_ONLY) {
			napi_disable(&ei_local->napi_rx);
		} else {
			napi_disable(&ei_local->napi);
		}
	}

	return 0;
}

int forward_config(struct net_device *dev)
{
	unsigned int reg_val, reg_csg;
	struct END_DEVICE *ei_local = netdev_priv(dev);
	unsigned int reg_val2 = 0;

	if (ei_local->features & FE_HW_VLAN_TX) {
		/*VLAN_IDX 0 = VLAN_ID 0
		 * .........
		 * VLAN_IDX 15 = VLAN ID 15
		 *
		 */
		/* frame engine will push VLAN tag
		 * regarding to VIDX feild in Tx desc.
		 */
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xa8, 0x00010000);
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xac, 0x00030002);
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xb0, 0x00050004);
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xb4, 0x00070006);
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xb8, 0x00090008);
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xbc, 0x000b000a);
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xc0, 0x000d000c);
		sys_reg_write(RALINK_FRAME_ENGINE_BASE + 0xc4, 0x000f000e);
	}

	reg_val = sys_reg_read(GDMA1_FWD_CFG);
	reg_csg = sys_reg_read(CDMA_CSG_CFG);

	if (ei_local->features & FE_GE2_SUPPORT)
		reg_val2 = sys_reg_read(GDMA2_FWD_CFG);

	/* set unicast/multicast/broadcast frame to cpu */
	reg_val &= ~0xFFFF;
	reg_val |= GDMA1_FWD_PORT;
	reg_csg &= ~0x7;

	if (ei_local->features & FE_HW_VLAN_TX)
		dev->features |= NETIF_F_HW_VLAN_CTAG_TX;

	if (ei_local->features & FE_HW_VLAN_RX)
		dev->features |= NETIF_F_HW_VLAN_CTAG_RX;

	if (ei_local->features & FE_CSUM_OFFLOAD) {
		/* enable ipv4 header checksum check */
		reg_val |= GDM1_ICS_EN;
		reg_csg |= ICS_GEN_EN;

		/* enable tcp checksum check */
		reg_val |= GDM1_TCS_EN;
		reg_csg |= TCS_GEN_EN;

		/* enable udp checksum check */
		reg_val |= GDM1_UCS_EN;
		reg_csg |= UCS_GEN_EN;

		if (ei_local->features & FE_GE2_SUPPORT) {
			reg_val2 &= ~0xFFFF;
			reg_val2 |= GDMA2_FWD_PORT;
			reg_val2 |= GDM1_ICS_EN;
			reg_val2 |= GDM1_TCS_EN;
			reg_val2 |= GDM1_UCS_EN;
		}

		if (ei_local->features & FE_HW_LRO)
			dev->features |= NETIF_F_HW_CSUM;
		else
			/* Can checksum TCP/UDP over IPv4 */
			dev->features |= NETIF_F_IP_CSUM;

		if (ei_local->features & FE_TSO) {
			dev->features |= NETIF_F_SG;
			dev->features |= NETIF_F_TSO;
		}

		if (ei_local->features & FE_TSO_V6) {
			dev->features |= NETIF_F_TSO6;
			/* Can checksum TCP/UDP over IPv6 */
			dev->features |= NETIF_F_IPV6_CSUM;
		}
	} else {		/* Checksum offload disabled */
		/* disable ipv4 header checksum check */
		reg_val &= ~GDM1_ICS_EN;
		reg_csg &= ~ICS_GEN_EN;

		/* disable tcp checksum check */
		reg_val &= ~GDM1_TCS_EN;
		reg_csg &= ~TCS_GEN_EN;

		/* disable udp checksum check */
		reg_val &= ~GDM1_UCS_EN;
		reg_csg &= ~UCS_GEN_EN;

		if (ei_local->features & FE_GE2_SUPPORT) {
			reg_val2 &= ~GDM1_ICS_EN;
			reg_val2 &= ~GDM1_TCS_EN;
			reg_val2 &= ~GDM1_UCS_EN;
		}

		/* disable checksum TCP/UDP over IPv4 */
		dev->features &= ~NETIF_F_IP_CSUM;
	}

	sys_reg_write(GDMA1_FWD_CFG, reg_val);
	sys_reg_write(CDMA_CSG_CFG, reg_csg);
	if (ei_local->features & FE_GE2_SUPPORT)
		sys_reg_write(GDMA2_FWD_CFG, reg_val2);

	dev->vlan_features = dev->features;

	/*FE_RST_GLO register definition -
	 *Bit 0: PSE Rest
	 *Reset PSE after re-programming PSE_FQ_CFG.
	 */
	reg_val = 0x1;
	sys_reg_write(FE_RST_GL, reg_val);
	sys_reg_write(FE_RST_GL, 0);	/* update for RSTCTL issue */

	reg_csg = sys_reg_read(CDMA_CSG_CFG);
	pr_info("CDMA_CSG_CFG = %0X\n", reg_csg);
	reg_val = sys_reg_read(GDMA1_FWD_CFG);
	pr_info("GDMA1_FWD_CFG = %0X\n", reg_val);

	if (ei_local->features & FE_GE2_SUPPORT) {
		reg_val = sys_reg_read(GDMA2_FWD_CFG);
		pr_info("GDMA2_FWD_CFG = %0X\n", reg_val);
	}

	return 1;
}

static void fe_gmac_reset(void)
{
	/*Reset GMAC */
	sys_reg_write(RALINK_SYSCTL_BASE + 0x34, 0x00800000);
	sys_reg_write(RALINK_SYSCTL_BASE + 0x34, 0x00000000);
}

void virtif_setup_statistics(struct PSEUDO_ADAPTER *p_ad)
{
	p_ad->stat.tx_packets = 0;
	p_ad->stat.tx_bytes = 0;
	p_ad->stat.tx_dropped = 0;
	p_ad->stat.tx_errors = 0;
	p_ad->stat.tx_aborted_errors = 0;
	p_ad->stat.tx_carrier_errors = 0;
	p_ad->stat.tx_fifo_errors = 0;
	p_ad->stat.tx_heartbeat_errors = 0;
	p_ad->stat.tx_window_errors = 0;

	p_ad->stat.rx_packets = 0;
	p_ad->stat.rx_bytes = 0;
	p_ad->stat.rx_dropped = 0;
	p_ad->stat.rx_errors = 0;
	p_ad->stat.rx_length_errors = 0;
	p_ad->stat.rx_over_errors = 0;
	p_ad->stat.rx_crc_errors = 0;
	p_ad->stat.rx_frame_errors = 0;
	p_ad->stat.rx_fifo_errors = 0;
	p_ad->stat.rx_missed_errors = 0;

	p_ad->stat.collisions = 0;
}

int virtualif_open(struct net_device *dev)
{
	u32 val;
	struct PSEUDO_ADAPTER *p_pseudo_ad = netdev_priv(dev);
	struct END_DEVICE *ei_local = netdev_priv(p_pseudo_ad->raeth_dev);

	pr_info("%s: ===> virtualif_open\n", dev->name);

	virtif_setup_statistics(p_pseudo_ad);

	if (ei_local->architecture & (WAN_AT_P0 | WAN_AT_P4)) {
		/* enable GE2 link change intr for MT7530 delay setting */
		val = sys_reg_read(FE_INT_ENABLE2);
		sys_reg_write(FE_INT_ENABLE2, val | (1 << 25));
	}

	netif_start_queue(p_pseudo_ad->pseudo_dev);

	return 0;
}

int virtualif_close(struct net_device *dev)
{
	struct PSEUDO_ADAPTER *p_pseudo_ad = netdev_priv(dev);

	pr_info("%s: ===> virtualif_close\n", dev->name);

	netif_stop_queue(p_pseudo_ad->pseudo_dev);

	return 0;
}

int virtualif_send_packets(struct sk_buff *p_skb, struct net_device *dev)
{
	struct PSEUDO_ADAPTER *p_pseudo_ad = netdev_priv(dev);
	struct END_DEVICE *ei_local;

	if (!(p_pseudo_ad->raeth_dev->flags & IFF_UP)) {
		dev_kfree_skb_any(p_skb);
		return 0;
	}
	/* p_skb->cb[40]=0x5a; */
	p_skb->dev = p_pseudo_ad->raeth_dev;
	ei_local = netdev_priv(p_pseudo_ad->raeth_dev);
	ei_local->ei_start_xmit(p_skb, p_pseudo_ad->raeth_dev, 2);
	return 0;
}

struct net_device_stats *virtualif_get_stats(struct net_device *dev)
{
	struct PSEUDO_ADAPTER *p_ad = netdev_priv(dev);

	return &p_ad->stat;
}

int virtualif_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	struct ra_mii_ioctl_data mii;
	unsigned long ret;

	switch (cmd) {
	case RAETH_MII_READ:
		ret = copy_from_user(&mii, ifr->ifr_data, sizeof(mii));
		mii_mgr_read(mii.phy_id, mii.reg_num, &mii.val_out);
		ret = copy_to_user(ifr->ifr_data, &mii, sizeof(mii));
		break;

	case RAETH_MII_WRITE:
		ret = copy_from_user(&mii, ifr->ifr_data, sizeof(mii));
		mii_mgr_write(mii.phy_id, mii.reg_num, mii.val_in);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ei_change_mtu(struct net_device *dev, int new_mtu)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	if (!ei_local) {
		pr_emerg
		    ("%s: %s passed a non-existent private pointer from net_dev!\n",
		     dev->name, __func__);
		return -ENXIO;
	}

	if ((new_mtu > 4096) || (new_mtu < 64))
		return -EINVAL;

	if (new_mtu > 1500)
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

static int ei_clock_enable(struct END_DEVICE *ei_local)
{
	unsigned long rate;
	int ret;

	pm_runtime_enable(ei_local->dev);
	pm_runtime_get_sync(ei_local->dev);

	clk_prepare_enable(ei_local->clks[MTK_CLK_ETH1PLL]);
	clk_prepare_enable(ei_local->clks[MTK_CLK_ETH2PLL]);
	clk_prepare_enable(ei_local->clks[MTK_CLK_ETHIF]);
	clk_prepare_enable(ei_local->clks[MTK_CLK_ESW]);
	clk_prepare_enable(ei_local->clks[MTK_CLK_GP1]);
	clk_prepare_enable(ei_local->clks[MTK_CLK_GP2]);

	if (ei_local->architecture & RAETH_ESW)
		clk_prepare_enable(ei_local->clks[MTK_CLK_GP0]);

	if (ei_local->architecture &
	    (GE1_TRGMII_FORCE_2000 | GE1_TRGMII_FORCE_2600)) {
		ret = clk_set_rate(ei_local->clks[MTK_CLK_TRGPLL], 500000000);
		if (ret)
			pr_err("Failed to set mt7530 trgmii pll: %d\n", ret);
		rate = clk_get_rate(ei_local->clks[MTK_CLK_TRGPLL]);
		pr_info("TRGMII_PLL rate = %ld\n", rate);
		clk_prepare_enable(ei_local->clks[MTK_CLK_TRGPLL]);
	}

	if (ei_local->architecture & RAETH_SGMII) {
		clk_prepare_enable(ei_local->clks[MTK_CLK_SGMIPLL]);
		clk_prepare_enable(ei_local->clks[MTK_CLK_SGMII_TX250M]);
		clk_prepare_enable(ei_local->clks[MTK_CLK_SGMII_RX250M]);
		clk_prepare_enable(ei_local->clks[MTK_CLK_SGMII_CDR_REF]);
		clk_prepare_enable(ei_local->clks[MTK_CLK_SGMII_CDR_FB]);
	}

	return 0;
}

static int ei_clock_disable(struct END_DEVICE *ei_local)
{
	if (ei_local->architecture & RAETH_ESW)
		clk_disable_unprepare(ei_local->clks[MTK_CLK_GP0]);

	if (ei_local->architecture &
	    (GE1_TRGMII_FORCE_2000 | GE1_TRGMII_FORCE_2600))
		clk_disable_unprepare(ei_local->clks[MTK_CLK_TRGPLL]);

	if (ei_local->architecture & RAETH_SGMII) {
		clk_disable_unprepare(ei_local->clks[MTK_CLK_SGMII_TX250M]);
		clk_disable_unprepare(ei_local->clks[MTK_CLK_SGMII_RX250M]);
		clk_disable_unprepare(ei_local->clks[MTK_CLK_SGMII_CDR_REF]);
		clk_disable_unprepare(ei_local->clks[MTK_CLK_SGMII_CDR_FB]);
		clk_disable_unprepare(ei_local->clks[MTK_CLK_SGMIPLL]);
	}

	clk_disable_unprepare(ei_local->clks[MTK_CLK_GP2]);
	clk_disable_unprepare(ei_local->clks[MTK_CLK_GP1]);
	clk_disable_unprepare(ei_local->clks[MTK_CLK_ESW]);
	clk_disable_unprepare(ei_local->clks[MTK_CLK_ETHIF]);
	clk_disable_unprepare(ei_local->clks[MTK_CLK_ETH2PLL]);
	clk_disable_unprepare(ei_local->clks[MTK_CLK_ETH1PLL]);

	pm_runtime_put_sync(ei_local->dev);
	pm_runtime_disable(ei_local->dev);

	return 0;
}

static struct ethtool_ops ra_ethtool_ops = {
	.get_settings = et_get_settings,
	.get_link = et_get_link,
};

static struct ethtool_ops ra_virt_ethtool_ops = {
	.get_settings = et_virt_get_settings,
	.get_link = et_virt_get_link,
};

static const struct net_device_ops virtualif_netdev_ops = {
	.ndo_open = virtualif_open,
	.ndo_stop = virtualif_close,
	.ndo_start_xmit = virtualif_send_packets,
	.ndo_get_stats = virtualif_get_stats,
	.ndo_set_mac_address = ei_set_mac2_addr,
	.ndo_change_mtu = ei_change_mtu,
	.ndo_do_ioctl = virtualif_ioctl,
	.ndo_validate_addr = eth_validate_addr,
};

void raeth_init_pseudo(struct END_DEVICE *p_ad, struct net_device *net_dev)
{
	int index;
	struct net_device *dev;
	struct PSEUDO_ADAPTER *p_pseudo_ad;
	struct END_DEVICE *ei_local = netdev_priv(net_dev);

	for (index = 0; index < MAX_PSEUDO_ENTRY; index++) {
		dev = alloc_etherdev_mqs(sizeof(struct PSEUDO_ADAPTER),
					 GMAC2_TXQ_NUM, 1);
		if (!dev) {
			pr_err("alloc_etherdev for PSEUDO_ADAPTER failed.\n");
			return;
		}
		strcpy(dev->name, DEV2_NAME);
		netif_set_real_num_tx_queues(dev, GMAC2_TXQ_NUM);
		netif_set_real_num_rx_queues(dev, 1);

		ei_mac2_addr_setting(dev);

		ether_setup(dev);
		p_pseudo_ad = netdev_priv(dev);

		p_pseudo_ad->pseudo_dev = dev;
		p_pseudo_ad->raeth_dev = net_dev;
		p_ad->pseudo_dev = dev;

		dev->netdev_ops = &virtualif_netdev_ops;

		if (ei_local->features & FE_HW_LRO)
			dev->features |= NETIF_F_HW_CSUM;
		else
			/* Can checksum TCP/UDP over IPv4 */
			dev->features |= NETIF_F_IP_CSUM;

		if (ei_local->features & FE_TSO) {
			dev->features |= NETIF_F_SG;
			dev->features |= NETIF_F_TSO;
		}

		if (ei_local->features & FE_TSO_V6) {
			dev->features |= NETIF_F_TSO6;
			/* Can checksum TCP/UDP over IPv6 */
			dev->features |= NETIF_F_IPV6_CSUM;
		}

		dev->vlan_features = dev->features;

		if (ei_local->features & FE_ETHTOOL) {
			dev->ethtool_ops = &ra_virt_ethtool_ops;
			ethtool_virt_init(dev);
		}

		/* Register this device */
		register_netdevice(dev);
	}
}

void fe_set_sw_lro_my_ip(char *lan_ip_addr)
{
	str_to_ip(&lan_ip, lan_ip_addr);
	lan_ip = htonl(lan_ip);
	lro_para.lan_ip1 = lan_ip;

	pr_info("[%s]lan_ip_addr = %s (lan_ip = 0x%x)\n",
		__func__, lan_ip_addr, lan_ip);
}

int ei_open(struct net_device *dev)
{
	int err;
	struct END_DEVICE *ei_local;

	ei_local = netdev_priv(dev);
	if (!ei_local) {
		pr_err("%s: ei_open passed a non-existent device!\n",
		       dev->name);
		return -ENXIO;
	}

	if (!try_module_get(THIS_MODULE)) {
		pr_err("%s: Cannot reserve module\n", __func__);
		return -1;
	}
	pr_info("Raeth %s (", RAETH_VERSION);
	if (ei_local->features & FE_INT_NAPI)
		pr_info("NAPI\n");
	else if (ei_local->features & FE_INT_TASKLET)
		pr_info("Tasklet");
	else if (ei_local->features & FE_INT_WORKQ)
		pr_info("Workqueue");
	pr_info(")\n");

	ei_reset_statistics(ei_local);

	err = ei_init_dma(dev);
	if (err)
		return err;

	fe_gmac_reset();

	/* initialize fe and switch register */
	fe_sw_preinit(ei_local);
	fe_sw_init();

	if (ei_local->features & FE_GE2_SUPPORT) {
		if (!ei_local->pseudo_dev)
			raeth_init_pseudo(ei_local, dev);

		if (!ei_local->pseudo_dev)
			pr_err("Open pseudo_dev failed.\n");
		else
			virtualif_open(ei_local->pseudo_dev);
	}

	if (ei_local->features & FE_SW_LRO)
		fe_set_sw_lro_my_ip(ei_local->lan_ip4_addr);

	forward_config(dev);

	if ((ei_local->chip_name == MT7623_FE) &&
	    (ei_local->features & FE_HW_LRO)) {
		ei_local->kreset_task =
		    kthread_create(fe_reset_thread, NULL, "FE_reset_kthread");
		if (IS_ERR(ei_local->kreset_task))
			return PTR_ERR(ei_local->kreset_task);
		wake_up_process(ei_local->kreset_task);
	}

	netif_start_queue(dev);

	fe_int_enable(dev);

	return 0;
}

int ei_close(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	if ((ei_local->chip_name == MT7623_FE) &&
	    (ei_local->features & FE_HW_LRO))
		kthread_stop(ei_local->kreset_task);

	netif_stop_queue(dev);
	ra2880stop(ei_local);

	fe_int_disable(dev);

	if (ei_local->features & FE_GE2_SUPPORT)
		virtualif_close(ei_local->pseudo_dev);

	ei_deinit_dma(dev);

	fe_sw_deinit(ei_local);

	fe_reset();
	module_put(THIS_MODULE);

	return 0;
}

static int ei_start_xmit_fake(struct sk_buff *skb, struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	return ei_local->ei_start_xmit(skb, dev, 1);
}

struct net_device_stats *ra_get_stats(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	return &ei_local->stat;
}

void dump_phy_reg(int port_no, int from, int to, int is_local, int page_no)
{
	u32 i = 0;
	u32 temp = 0;
	u32 r31 = 0;

	if (is_local == 0) {
		pr_info("\n\nGlobal Register Page %d\n", page_no);
		pr_info("===============");
		r31 |= 0 << 15;	/* global */
		r31 |= ((page_no & 0x7) << 12);	/* page no */
		mii_mgr_write(port_no, 31, r31);	/* select global page x */
		for (i = 16; i < 32; i++) {
			if (i % 8 == 0)
				pr_info("\n");
			mii_mgr_read(port_no, i, &temp);
			pr_info("%02d: %04X ", i, temp);
		}
	} else {
		pr_info("\n\nLocal Register Port %d Page %d\n", port_no,
			page_no);
		pr_info("===============");
		r31 |= 1 << 15;	/* local */
		r31 |= ((page_no & 0x7) << 12);	/* page no */
		mii_mgr_write(port_no, 31, r31);	/* select local page x */
		for (i = 16; i < 32; i++) {
			if (i % 8 == 0)
				pr_info("\n");
			mii_mgr_read(port_no, i, &temp);
			pr_info("%02d: %04X ", i, temp);
		}
	}
	pr_info("\n");
}

int ei_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct esw_reg reg;
	struct esw_rate ratelimit;
	struct ra_switch_ioctl_data ioctl_data;

	unsigned int offset = 0;
	unsigned int value = 0;
	u8 cnt = 0;
	u8 port_num = 0;
	int ret = 0;
	unsigned long result;
	struct END_DEVICE *ei_local = netdev_priv(dev);
	struct ra_mii_ioctl_data mii;

	if (ei_local->features & FE_QDMA) {
		ret = ei_qdma_ioctl(dev, ifr, cmd);
		if (ret <= 0)
			return ret;
	}

	spin_lock_irq(&ei_local->page_lock);

	switch (cmd) {
	case RAETH_MII_READ:
		result = copy_from_user(&mii, ifr->ifr_data, sizeof(mii));
		mii_mgr_read(mii.phy_id, mii.reg_num, &mii.val_out);
		result = copy_to_user(ifr->ifr_data, &mii, sizeof(mii));
		break;

	case RAETH_MII_WRITE:
		result = copy_from_user(&mii, ifr->ifr_data, sizeof(mii));
		mii_mgr_write(mii.phy_id, mii.reg_num, mii.val_in);
		break;
	case RAETH_MII_READ_CL45:
		result = copy_from_user(&mii, ifr->ifr_data, sizeof(mii));
		mii_mgr_read_cl45(mii.port_num, mii.dev_addr, mii.reg_addr,
				  &mii.val_out);
		result = copy_to_user(ifr->ifr_data, &mii, sizeof(mii));
		break;
	case RAETH_MII_WRITE_CL45:
		result = copy_from_user(&mii, ifr->ifr_data, sizeof(mii));
		mii_mgr_write_cl45(mii.port_num, mii.dev_addr, mii.reg_addr,
				   mii.val_in);
		break;
	case RAETH_ESW_REG_READ:
		result = copy_from_user(&reg, ifr->ifr_data, sizeof(reg));
		if (reg.off > REG_ESW_MAX) {
			ret = -EINVAL;
			break;
		}
		reg.val = sys_reg_read(RALINK_ETH_SW_BASE + reg.off);
		result = copy_to_user(ifr->ifr_data, &reg, sizeof(reg));
		break;
	case RAETH_ESW_REG_WRITE:
		result = copy_from_user(&reg, ifr->ifr_data, sizeof(reg));
		if (reg.off > REG_ESW_MAX) {
			ret = -EINVAL;
			break;
		}
		sys_reg_write(RALINK_ETH_SW_BASE + reg.off, reg.val);
		break;
	case RAETH_ESW_PHY_DUMP:
		result = copy_from_user(&reg, ifr->ifr_data, sizeof(reg));
		/* SPEC defined Register 0~15
		 * Global Register 16~31 for each page
		 * Local Register 16~31 for each page
		 */
		pr_info("SPEC defined Register");
		if (reg.val == 32) {	/* dump all phy register */
			int i = 0;

			for (i = 0; i < 5; i++) {
				pr_info("\n[Port %d]===============", i);
				for (offset = 0; offset < 16; offset++) {
					if (offset % 8 == 0)
						pr_info("\n");
					mii_mgr_read(i, offset, &value);
					pr_info("%02d: %04X ", offset, value);
				}
			}
		} else {
			pr_info("\n[Port %d]===============", reg.val);
			for (offset = 0; offset < 16; offset++) {
				if (offset % 8 == 0)
					pr_info("\n");
				mii_mgr_read(reg.val, offset, &value);
				pr_info("%02d: %04X ", offset, value);
			}
		}

		/* global register  page 0~4 */
		for (offset = 0; offset < 5; offset++) {
			if (reg.val == 32)	/* dump all phy register */
				dump_phy_reg(0, 16, 31, 0, offset);
			else
				dump_phy_reg(reg.val, 16, 31, 0, offset);
		}

		if (reg.val == 32) {	/* dump all phy register */
			/* local register port 0-port4 */
			for (offset = 0; offset < 5; offset++) {
				/* dump local page 0 */
				dump_phy_reg(offset, 16, 31, 1, 0);
				/* dump local page 1 */
				dump_phy_reg(offset, 16, 31, 1, 1);
				/* dump local page 2 */
				dump_phy_reg(offset, 16, 31, 1, 2);
				/* dump local page 3 */
				dump_phy_reg(offset, 16, 31, 1, 3);
			}
		} else {
			/* dump local page 0 */
			dump_phy_reg(reg.val, 16, 31, 1, 0);
			/* dump local page 1 */
			dump_phy_reg(reg.val, 16, 31, 1, 1);
			/* dump local page 2 */
			dump_phy_reg(reg.val, 16, 31, 1, 2);
			/* dump local page 3 */
			dump_phy_reg(reg.val, 16, 31, 1, 3);
		}
		break;

	case RAETH_ESW_INGRESS_RATE:
		result = copy_from_user(&ratelimit, ifr->ifr_data,
					sizeof(ratelimit));
		offset = 0x1800 + (0x100 * ratelimit.port);
		value = sys_reg_read(RALINK_ETH_SW_BASE + offset);

		value &= 0xffff0000;
		if (ratelimit.on_off == 1) {
			value |= (ratelimit.on_off << 15);
			if (ratelimit.bw < 100) {
				value |= (0x0 << 8);
				value |= ratelimit.bw;
			} else if (ratelimit.bw < 1000) {
				value |= (0x1 << 8);
				value |= ratelimit.bw / 10;
			} else if (ratelimit.bw < 10000) {
				value |= (0x2 << 8);
				value |= ratelimit.bw / 100;
			} else if (ratelimit.bw < 100000) {
				value |= (0x3 << 8);
				value |= ratelimit.bw / 1000;
			} else {
				value |= (0x4 << 8);
				value |= ratelimit.bw / 10000;
			}
		}
		pr_info("offset = 0x%4x value=0x%x\n\r", offset, value);
		mii_mgr_write(0x1f, offset, value);
		break;

	case RAETH_ESW_EGRESS_RATE:
		result = copy_from_user(&ratelimit, ifr->ifr_data,
					sizeof(ratelimit));
		offset = 0x1040 + (0x100 * ratelimit.port);
		value = sys_reg_read(RALINK_ETH_SW_BASE + offset);

		value &= 0xffff0000;
		if (ratelimit.on_off == 1) {
			value |= (ratelimit.on_off << 15);
			if (ratelimit.bw < 100) {
				value |= (0x0 << 8);
				value |= ratelimit.bw;
			} else if (ratelimit.bw < 1000) {
				value |= (0x1 << 8);
				value |= ratelimit.bw / 10;
			} else if (ratelimit.bw < 10000) {
				value |= (0x2 << 8);
				value |= ratelimit.bw / 100;
			} else if (ratelimit.bw < 100000) {
				value |= (0x3 << 8);
				value |= ratelimit.bw / 1000;
			} else {
				value |= (0x4 << 8);
				value |= ratelimit.bw / 10000;
			}
		}
		pr_info("offset = 0x%4x value=0x%x\n\r", offset, value);
		mii_mgr_write(0x1f, offset, value);
		break;

	case RAETH_SET_LAN_IP:
		result = copy_from_user(ei_local->lan_ip4_addr, ifr->ifr_data,
					IP4_ADDR_LEN);
		pr_info("RAETH_SET_LAN_IP: %s\n", ei_local->lan_ip4_addr);

		if (ei_local->features & FE_SW_LRO)
			fe_set_sw_lro_my_ip(ei_local->lan_ip4_addr);

		if (ei_local->features & FE_HW_LRO)
			fe_set_hw_lro_my_ip(ei_local->lan_ip4_addr);
		break;

	case RAETH_VBG_IEXT_CALIBRATION:
		cnt = 0;
		for (port_num = 0; port_num < 5; port_num++) {
			while ((fe_cal_vbg_flag == 0) && (cnt < 0x3)) {
				fe_cal_vbg(port_num, 1);
				cnt++;
				if (fe_cal_vbg_flag == 0)
					pr_info(" VBG wait! (%d)\n", cnt);
			}
		}
		break;

	case RAETH_TXG_R50_CALIBRATION:
		cnt = 0;
		for (port_num = 0; port_num < 5; port_num++) {
			while ((fe_cal_r50_flag == 0) && (cnt < 0x3)) {
				fe_cal_r50(port_num, 1);
				cnt++;
				if (fe_cal_r50_flag == 0)
					pr_info(" FE R50 wait! (%d)\n", cnt);
			}
		}
		break;

	case RAETH_TXG_OFFSET_CALIBRATION:
		for (port_num = 0; port_num < 5; port_num++) {
			cnt = 0;
			while ((fe_cal_tx_offset_flag == 0) && (cnt < 0x3)) {
				fe_cal_tx_offset(port_num, 100);
				cnt++;
				if (fe_cal_tx_offset_flag == 0)
					pr_info("FeTxOffsetAnaCal wait!(%d)\n",
						cnt);
			}
			cnt = 0;
			while ((fe_cal_tx_offset_flag_mdix == 0) && (cnt < 0x3)) {
				fe_cal_tx_offset_mdix(port_num, 100);
				cnt++;
				if (fe_cal_tx_offset_flag_mdix == 0)
					pr_info
					    ("FeTxOffsetAnaCal mdix wait!(%d)\n",
					     cnt);
			}
		}
		break;

	case RAETH_TXG_AMP_CALIBRATION:
		for (port_num = 0; port_num < 5; port_num++) {
			cnt = 0;
			while ((fe_cal_flag == 0) && (cnt < 0x3)) {
				fe_cal_tx_amp(port_num, 300);
				cnt++;
				if (fe_cal_flag == 0)
					pr_info("FETxAmpAnaCal wait!(%d)\n",
						cnt);
			}
			cnt = 0;
			while ((fe_cal_flag_mdix == 0) && (cnt < 0x3)) {
				fe_cal_tx_amp_mdix(port_num, 300);
				cnt++;
				if (fe_cal_flag_mdix == 0)
					pr_info
					    ("FETxAmpAnaCal mdix wait!(%d)\n",
					     cnt);
			}
		}
		break;

	case RAETH_SW_IOCTL:
		ret =
		    copy_from_user(&ioctl_data, ifr->ifr_data,
				   sizeof(ioctl_data));
		sw_ioctl(&ioctl_data);
		break;

	default:
		ret = -EOPNOTSUPP;
		break;
	}

	spin_unlock_irq(&ei_local->page_lock);
	return ret;
}

static const struct net_device_ops ei_netdev_ops = {
	.ndo_init = ei_init,
	.ndo_uninit = ei_uninit,
	.ndo_open = ei_open,
	.ndo_stop = ei_close,
	.ndo_start_xmit = ei_start_xmit_fake,
	.ndo_get_stats = ra_get_stats,
	.ndo_set_mac_address = ei_set_mac_addr,
	.ndo_change_mtu = ei_change_mtu,
	.ndo_do_ioctl = ei_ioctl,
	.ndo_validate_addr = eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = raeth_poll_full,
#endif
};

void raeth_setup_dev_fptable(struct net_device *dev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev);

	dev->netdev_ops = &ei_netdev_ops;

	if (ei_local->features & FE_ETHTOOL)
		dev->ethtool_ops = &ra_ethtool_ops;

#define TX_TIMEOUT (5 * HZ)
	dev->watchdog_timeo = TX_TIMEOUT;
}

void ei_ioc_setting(struct platform_device *pdev, struct END_DEVICE *ei_local)
{
	void __iomem *reg_virt;
	unsigned int reg_val;

	if (ei_local->features & FE_HW_IOCOHERENT) {
		pr_info("[Raether] HW IO coherent is enabled !\n");
		/* enable S4 coherence function */
		reg_virt = ioremap(0x10395000, 0x10);
		sys_reg_write(reg_virt, 0x00000003);

		/* Enable ETHSYS io coherence path */
		reg_virt = ioremap(0x1B000400, 0x10);
		reg_virt += 0x8;
		reg_val = sys_reg_read(reg_virt);

		if (ei_local->features & FE_QDMA_FQOS)
			reg_val |= IOC_ETH_PDMA;
		else
			reg_val |= IOC_ETH_PDMA | IOC_ETH_QDMA;

		sys_reg_write(reg_virt, reg_val);
		reg_virt -= 0x8;
		iounmap(reg_virt);

		arch_setup_dma_ops(&pdev->dev, 0, 0, NULL, TRUE);

		if (ei_local->features & FE_QDMA_FQOS)
			arch_setup_dma_ops(&ei_local->qdma_pdev->dev,
					   0, 0, NULL, FALSE);
		else
			arch_setup_dma_ops(&ei_local->qdma_pdev->dev,
					   0, 0, NULL, TRUE);
	} else {
		pr_info("[Raether] HW IO coherent is disabled !\n");
		arch_setup_dma_ops(&pdev->dev, 0, 0, NULL, FALSE);
		arch_setup_dma_ops(&ei_local->qdma_pdev->dev,
				   0, 0, NULL, FALSE);
	}
}

static void fe_chip_name_config(struct END_DEVICE *ei_local)
{
	unsigned int val_1, val_2, chip_id;
	unsigned int id[4];

	val_1 = sys_reg_read(ETHDMASYS_SYSCTL_BASE);
	val_2 = sys_reg_read(ETHDMASYS_SYSCTL_BASE + 4);

	id[3] = ((val_1 >> 16) & 0xff) - 0x30;
	id[2] = ((val_1 >> 24) & 0xff) - 0x30;
	id[1] = ((val_2 >> 0) & 0xff) - 0x30;
	id[0] = ((val_2 >> 8) & 0xff) - 0x30;

	chip_id = (id[3] * 1000) + (id[2] * 100) + (id[1] * 10) + id[0];

	ei_local->chip_name = chip_id;
}

static int rather_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct END_DEVICE *ei_local;
	struct net_device *netdev;
	struct device_node *node;
	const char *mac_addr;
	int ret;
	int i;

	netdev = alloc_etherdev_mqs(sizeof(struct END_DEVICE),
				    GMAC1_TXQ_NUM, 1);
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	dev_raether = netdev;
	ei_local = netdev_priv(netdev);
	ei_local->dev = &pdev->dev;
	ei_local->netdev = netdev;
	fe_features_config(ei_local);
	pr_info("[%s]ei_local->features = 0x%x\n", __func__,
		ei_local->features);
	fe_architecture_config(ei_local);
	pr_info("[%s]ei_local->architecture_config = %d\n", __func__,
		ei_local->architecture);

	if ((ei_local->features & FE_HW_IOCOHERENT) &&
	    (ei_local->features & FE_QDMA_FQOS)) {
		ei_local->qdma_pdev =
			platform_device_alloc("QDMA", PLATFORM_DEVID_AUTO);
		if (!ei_local->qdma_pdev) {
			dev_err(&pdev->dev,
				"QDMA platform device allocate fail!\n");
			ret = -ENOMEM;
			goto err_free_dev;
		}

		ei_local->qdma_pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
		ei_local->qdma_pdev->dev.dma_mask =
			&ei_local->qdma_pdev->dev.coherent_dma_mask;
	} else {
		ei_local->qdma_pdev = pdev;
	}

	/* iomap registers */
	node = of_parse_phandle(pdev->dev.of_node, "mediatek,ethsys", 0);
	ethdma_sysctl_base = of_iomap(node, 0);
	if (IS_ERR(ethdma_sysctl_base)) {
		dev_err(&pdev->dev, "no ethdma_sysctl_base found\n");
		return PTR_ERR(ethdma_sysctl_base);
	}
	pr_info("ethdma_sysctl_base = 0x%p\n", ethdma_sysctl_base);

	ethdma_frame_engine_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ethdma_frame_engine_base)) {
		dev_err(&pdev->dev, "no ethdma_frame_engine_base found\n");
		return PTR_ERR(ethdma_frame_engine_base);
	}
	pr_info("ethdma_frame_engine_base = 0x%p\n", ethdma_frame_engine_base);

	/* get clock ctrl */
	for (i = 0; i < ARRAY_SIZE(ei_local->clks); i++) {
		ei_local->clks[i] = devm_clk_get(&pdev->dev,
						 mtk_clks_source_name[i]);
		if (IS_ERR(ei_local->clks[i])) {
			if (PTR_ERR(ei_local->clks[i]) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			return -ENODEV;
		}
	}

	/* get gsw device node */
	ei_local->switch_np = of_parse_phandle(pdev->dev.of_node,
					       "mediatek,switch", 0);

	/* get MAC address */
	mac_addr = of_get_mac_address(pdev->dev.of_node);
	if (mac_addr)
		ether_addr_copy(netdev->dev_addr, mac_addr);

	/* get IRQs */
	ei_local->irq0 = platform_get_irq(pdev, 0);
	ei_local->irq1 = platform_get_irq(pdev, 1);
	ei_local->irq2 = platform_get_irq(pdev, 2);
	pr_info("ei_local->irq0 = %d\n", ei_local->irq0);
	pr_info("ei_local->irq1 = %d\n", ei_local->irq1);
	pr_info("ei_local->irq2 = %d\n", ei_local->irq2);
	if (ei_local->features & RAETH_ESW) {
		ei_local->esw_irq = platform_get_irq(pdev, 3);
		pr_info("ei_local->esw_irq = %d\n", ei_local->esw_irq);
	}

	ei_clock_enable(ei_local);

	fe_chip_name_config(ei_local);
	pr_info("[%s]ei_local->chip_name = %d\n", __func__,
		ei_local->chip_name);

	if (ei_local->chip_name == MT7622_FE)
		ei_ioc_setting(pdev, ei_local);

	raeth_setup_dev_fptable(netdev);
	ei_mac_addr_setting(netdev);

	strcpy(netdev->name, DEV_NAME);

	netif_set_real_num_tx_queues(netdev, GMAC1_TXQ_NUM);
	netif_set_real_num_rx_queues(netdev, 1);

	netdev->addr_len = 6;
	netdev->base_addr = (unsigned long)RALINK_FRAME_ENGINE_BASE;

	/* net_device structure Init */
	pr_info("Mediatek APSoC Ethernet Driver Initialization.");
	pr_info
	    ("%s  %d rx/%d tx descriptors allocated, mtu = %d!\n",
	     RAETH_VERSION, NUM_RX_DESC, NUM_TX_DESC, netdev->mtu);

	if (ei_local->features & FE_INT_NAPI)
		pr_info("NAPI enable, Tx Ring = %d, Rx Ring = %d\n",
			NUM_TX_DESC, NUM_RX_DESC);

	if (ei_local->features & FE_ETHTOOL)
		ethtool_init(netdev);

	ret = debug_proc_init();
	if (ret) {
		dev_err(&pdev->dev, "error set debug proc\n");
		goto err_free_dev;
	}

	/* Register net device for the driver */
	ret = register_netdev(netdev);
	if (ret) {
		dev_err(&pdev->dev, "error bringing up device\n");
		goto err_free_dev;
	}

	return 0;

err_free_dev:
	free_netdev(netdev);
	return ret;
}

static int raether_remove(struct platform_device *pdev)
{
	struct END_DEVICE *ei_local = netdev_priv(dev_raether);

	if (ei_local->features & FE_QDMA_FQOS)
		if (!ei_local->qdma_pdev)
			ei_local->qdma_pdev->dev.release
				(&ei_local->qdma_pdev->dev);

	ei_clock_disable(ei_local);

	return 0;
}

static const char raeth_string[] = "RAETH_DRV";

static const struct of_device_id raether_of_ids[] = {
	{.compatible = "mediatek,mt7623-eth"},
	{.compatible = "mediatek,mt7622-eth"},
	{},
};

static struct platform_driver raeth_driver = {
	.probe = rather_probe,
	.remove = raether_remove,
	.driver = {
		   .name = raeth_string,
		   .owner = THIS_MODULE,
		   .of_match_table = raether_of_ids,
		   },
};

module_platform_driver(raeth_driver);
MODULE_LICENSE("GPL");
