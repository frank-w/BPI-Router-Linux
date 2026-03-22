// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DSA 802.1Q-based tag driver for MaxLinear MxL862xx switches
 *
 * Uses the DSA tag_8021q framework to encode port information in
 * 802.1Q VLAN tags instead of the native 8-byte MxL862xx special tag.
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/dsa/8021q.h>

#include "tag.h"
#include "tag_8021q.h"

#define MXL862_8021Q_NAME "mxl862xx-8021q"

static struct sk_buff *mxl862_8021q_xmit(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct dsa_port *dp = dsa_user_to_port(netdev);
	u16 tx_vid = dsa_tag_8021q_standalone_vid(dp);
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 pcp = netdev_txq_to_tc(netdev, queue_mapping);

	return dsa_8021q_xmit(skb, netdev, ETH_P_8021Q,
			      (pcp << VLAN_PRIO_SHIFT) | tx_vid);
}

static struct sk_buff *mxl862_8021q_rcv(struct sk_buff *skb,
					struct net_device *netdev)
{
	int src_port = -1, switch_id = -1;

	dsa_8021q_rcv(skb, &src_port, &switch_id, NULL, NULL);

	skb->dev = dsa_conduit_find_user(netdev, switch_id, src_port);
	if (!skb->dev)
		return NULL;

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static const struct dsa_device_ops mxl862_8021q_netdev_ops = {
	.name			= MXL862_8021Q_NAME,
	.proto			= DSA_TAG_PROTO_MXL862_8021Q,
	.xmit			= mxl862_8021q_xmit,
	.rcv			= mxl862_8021q_rcv,
	.needed_headroom	= VLAN_HLEN,
	.promisc_on_conduit	= true,
};

MODULE_DESCRIPTION("DSA tag driver for MaxLinear MxL862xx switches, using VLAN");
MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_MXL862_8021Q, MXL862_8021Q_NAME);

module_dsa_tag_driver(mxl862_8021q_netdev_ops);
