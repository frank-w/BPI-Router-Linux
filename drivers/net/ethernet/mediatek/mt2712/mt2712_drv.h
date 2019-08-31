/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __MT2712_DRV_H__
#define __MT2712_DRV_H__

extern unsigned long dwc_eth_qos_platform_base_addr;

static int open(struct net_device *);
static int close(struct net_device *);
static void set_rx_mode(struct net_device *);
static void tx_interrupt(struct net_device *, struct prv_data *, unsigned int q_inx);
static struct net_device_stats *get_stats(struct net_device *);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void poll_controller(struct net_device *);
#endif
static int handle_prv_ioctl(struct prv_data *pdata, struct ifr_data_struct *req);
static int ioctl(struct net_device *, struct ifreq *, int);
irqreturn_t ISR_SW_ETH(int, void *);
static int clean_rx_irq(struct prv_data *pdata, int quota, unsigned int q_inx);
static void receive_skb(struct prv_data *pdata, struct net_device *dev, struct sk_buff *skb, unsigned int q_inx);
static void configure_rx_fun_ptr(struct prv_data *pdata);
static int alloc_rx_buf(struct prv_data *pdata, struct rx_buffer *buffer, gfp_t gfp);
static void default_common_confs(struct prv_data *pdata);
static void default_tx_confs(struct prv_data *pdata);
static void default_tx_confs_single_q(struct prv_data *pdata, unsigned int q_inx);
static void default_rx_confs(struct prv_data *pdata);
static void default_rx_confs_single_q(struct prv_data *pdata, unsigned int q_inx);
int poll(struct prv_data *pdata, int budget, int q_inx);
static void mmc_setup(struct prv_data *pdata);
inline unsigned int mtk_eth_reg_read(unsigned long addr);
#ifdef QUEUE_SELECT_ALGO
u16	select_queue(struct net_device *dev, struct sk_buff *skb);
#endif
int send_frame(struct prv_data *pdata, int num);

#endif

