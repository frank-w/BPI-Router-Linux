/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_API_H
#define __MXL862XX_API_H

#include <linux/bits.h>
#include <linux/if_ether.h>

/**
 * struct mdio_relay_data - relayed access to the switch internal MDIO bus
 * @data: data to be read or written
 * @phy: PHY index
 * @mmd: MMD device
 * @reg: register index
 */
struct mdio_relay_data {
	__le16 data;
	u8 phy;
	u8 mmd;
	__le16 reg;
} __packed;

/**
 * struct mxl862xx_register_mod - Register access parameter to directly
 *                                modify internal registers
 * @addr: Register address offset for modification
 * @data: Value to write to the register address
 * @mask: Mask of bits to be modified (1 to modify, 0 to ignore)
 *
 * Used for direct register modification operations.
 */
struct mxl862xx_register_mod {
	__le16 addr;
	__le16 data;
	__le16 mask;
} __packed;

/**
 * enum mxl862xx_mac_table_filter - Source/Destination MAC address filtering
 *
 * @MXL862XX_MAC_FILTER_NONE: no filter
 * @MXL862XX_MAC_FILTER_SRC: source address filter
 * @MXL862XX_MAC_FILTER_DEST: destination address filter
 * @MXL862XX_MAC_FILTER_BOTH: both source and destination filter
 */
enum mxl862xx_mac_table_filter {
	MXL862XX_MAC_FILTER_NONE = 0,
	MXL862XX_MAC_FILTER_SRC = BIT(0),
	MXL862XX_MAC_FILTER_DEST = BIT(1),
	MXL862XX_MAC_FILTER_BOTH = BIT(0) | BIT(1),
};

#define MXL862XX_TCI_VLAN_ID		GENMASK(11, 0)
#define MXL862XX_TCI_VLAN_CFI_DEI	BIT(12)
#define MXL862XX_TCI_VLAN_PRI		GENMASK(15, 13)

/* Set in port_id to use port_map[] as a portmap bitmap instead of a single
 * port ID. When clear, port_id selects one port; when set, the firmware
 * ignores the lower bits of port_id and writes port_map[] directly into
 * the PCE bridge port map.
 */
#define MXL862XX_PORTMAP_FLAG		BIT(31)

/**
 * struct mxl862xx_mac_table_add - MAC Table Entry to be added
 * @fid: Filtering Identifier (FID) (not supported by all switches)
 * @port_id: Ethernet Port number
 * @port_map: Bridge Port Map
 * @sub_if_id: Sub-Interface Identifier Destination
 * @age_timer: Aging Time in seconds
 * @vlan_id: STAG VLAN Id
 * @static_entry: Static Entry (value will be aged out if not set to static)
 * @traffic_class: Egress queue traffic class
 * @mac: MAC Address to add to the table
 * @filter_flag: See &enum mxl862xx_mac_table_filter
 * @igmp_controlled: Packet is marked as IGMP controlled if destination MAC
 *                   address matches MAC in this entry
 * @associated_mac: Associated Mac address
 * @tci: TCI for B-Step
 *	Bit [0:11] - VLAN ID
 *	Bit [12] - VLAN CFI/DEI
 *	Bit [13:15] - VLAN PRI
 */
struct mxl862xx_mac_table_add {
	__le16 fid;
	__le32 port_id;
	__le16 port_map[8];
	__le16 sub_if_id;
	__le32 age_timer;
	__le16 vlan_id;
	u8 static_entry;
	u8 traffic_class;
	u8 mac[ETH_ALEN];
	u8 filter_flag;
	u8 igmp_controlled;
	u8 associated_mac[ETH_ALEN];
	__le16 tci;
} __packed;

/**
 * struct mxl862xx_mac_table_remove - MAC Table Entry to be removed
 * @fid: Filtering Identifier (FID)
 * @mac: MAC Address to be removed from the table.
 * @filter_flag: See &enum mxl862xx_mac_table_filter
 * @tci: TCI for B-Step
 *	Bit [0:11] - VLAN ID
 *	Bit [12] - VLAN CFI/DEI
 *	Bit [13:15] - VLAN PRI
 */
struct mxl862xx_mac_table_remove {
	__le16 fid;
	u8 mac[ETH_ALEN];
	u8 filter_flag;
	__le16 tci;
} __packed;

/**
 * struct mxl862xx_mac_table_read - MAC Table Entry to be read
 * @initial: Restart the get operation from the beginning of the table
 * @last: Indicates that the read operation returned last entry
 * @fid: Get the MAC table entry belonging to the given Filtering Identifier
 * @port_id: The Bridge Port ID
 * @port_map: Bridge Port Map
 * @age_timer: Aging Time
 * @vlan_id: STAG VLAN Id
 * @static_entry: Indicates if this is a Static Entry
 * @sub_if_id: Sub-Interface Identifier Destination
 * @mac: MAC Address. Filled out by the switch API implementation.
 * @filter_flag: See &enum mxl862xx_mac_table_filter
 * @igmp_controlled: Packet is marked as IGMP controlled if destination MAC
 *                   address matches the MAC in this entry
 * @entry_changed: Indicate if the Entry has Changed
 * @associated_mac: Associated MAC address
 * @hit_status: MAC Table Hit Status Update
 * @tci: TCI for B-Step
 *	Bit [0:11] - VLAN ID
 *	Bit [12] - VLAN CFI/DEI
 *	Bit [13:15] - VLAN PRI
 * @first_bridge_port_id: The port this MAC address has first been learned.
 *                        This is used for loop detection.
 */
struct mxl862xx_mac_table_read {
	u8 initial;
	u8 last;
	__le16 fid;
	__le32 port_id;
	__le16 port_map[8];
	__le32 age_timer;
	__le16 vlan_id;
	u8 static_entry;
	__le16 sub_if_id;
	u8 mac[ETH_ALEN];
	u8 filter_flag;
	u8 igmp_controlled;
	u8 entry_changed;
	u8 associated_mac[ETH_ALEN];
	u8 hit_status;
	__le16 tci;
	__le16 first_bridge_port_id;
} __packed;

/**
 * struct mxl862xx_mac_table_query - MAC Table Entry key-based lookup
 * @mac: MAC Address to search for (input)
 * @fid: Filtering Identifier (input)
 * @found: Set by firmware: 1 if entry was found, 0 if not
 * @port_id: Bridge Port ID (output; MSB set if portmap mode)
 * @port_map: Bridge Port Map (output; valid for static entries)
 * @sub_if_id: Sub-Interface Identifier Destination
 * @age_timer: Aging Time
 * @vlan_id: STAG VLAN Id
 * @static_entry: Indicates if this is a Static Entry
 * @filter_flag: See &enum mxl862xx_mac_table_filter (input+output)
 * @igmp_controlled: IGMP controlled flag
 * @entry_changed: Entry changed flag
 * @associated_mac: Associated MAC address
 * @hit_status: MAC Table Hit Status Update
 * @tci: TCI (VLAN ID + CFI/DEI + PRI) (input)
 * @first_bridge_port_id: First learned bridge port
 */
struct mxl862xx_mac_table_query {
	u8 mac[ETH_ALEN];
	__le16 fid;
	u8 found;
	__le32 port_id;
	__le16 port_map[8];
	__le16 sub_if_id;
	__le32 age_timer;
	__le16 vlan_id;
	u8 static_entry;
	u8 filter_flag;
	u8 igmp_controlled;
	u8 entry_changed;
	u8 associated_mac[ETH_ALEN];
	u8 hit_status;
	__le16 tci;
	__le16 first_bridge_port_id;
} __packed;

/**
 * enum mxl862xx_mac_clear_type - MAC table clear type
 * @MXL862XX_MAC_CLEAR_PHY_PORT: clear dynamic entries based on port_id
 * @MXL862XX_MAC_CLEAR_DYNAMIC: clear all dynamic entries
 */
enum mxl862xx_mac_clear_type {
	MXL862XX_MAC_CLEAR_PHY_PORT = 0,
	MXL862XX_MAC_CLEAR_DYNAMIC,
};

/**
 * struct mxl862xx_mac_table_clear - MAC table clear
 * @type: see &enum mxl862xx_mac_clear_type
 * @port_id: physical port id
 */
struct mxl862xx_mac_table_clear {
	u8 type;
	u8 port_id;
} __packed;

/**
 * enum mxl862xx_age_timer - Aging Timer Value.
 * @MXL862XX_AGETIMER_1_SEC: 1 second aging time
 * @MXL862XX_AGETIMER_10_SEC: 10 seconds aging time
 * @MXL862XX_AGETIMER_300_SEC: 300 seconds aging time
 * @MXL862XX_AGETIMER_1_HOUR: 1 hour aging time
 * @MXL862XX_AGETIMER_1_DAY: 24 hours aging time
 * @MXL862XX_AGETIMER_CUSTOM: Custom aging time in seconds
 */
enum mxl862xx_age_timer {
	MXL862XX_AGETIMER_1_SEC = 1,
	MXL862XX_AGETIMER_10_SEC,
	MXL862XX_AGETIMER_300_SEC,
	MXL862XX_AGETIMER_1_HOUR,
	MXL862XX_AGETIMER_1_DAY,
	MXL862XX_AGETIMER_CUSTOM,
};

/**
 * struct mxl862xx_bridge_alloc - Bridge Allocation
 * @bridge_id: If the bridge allocation is successful, a valid ID will be
 *             returned in this field. Otherwise, INVALID_HANDLE is
 *             returned. For bridge free, this field should contain a
 *             valid ID returned by the bridge allocation. ID 0 is not
 *             used for historic reasons.
 *
 * Used by MXL862XX_BRIDGE_ALLOC and MXL862XX_BRIDGE_FREE.
 */
struct mxl862xx_bridge_alloc {
	__le16 bridge_id;
};

/**
 * enum mxl862xx_bridge_config_mask - Bridge configuration mask
 * @MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNING_LIMIT:
 *     Mask for mac_learning_limit_enable and mac_learning_limit.
 * @MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNED_COUNT:
 *     Mask for mac_learning_count
 * @MXL862XX_BRIDGE_CONFIG_MASK_MAC_DISCARD_COUNT:
 *     Mask for learning_discard_event
 * @MXL862XX_BRIDGE_CONFIG_MASK_SUB_METER:
 *     Mask for sub_metering_enable and traffic_sub_meter_id
 * @MXL862XX_BRIDGE_CONFIG_MASK_FORWARDING_MODE:
 *     Mask for forward_broadcast, forward_unknown_multicast_ip,
 *     forward_unknown_multicast_non_ip and forward_unknown_unicast.
 * @MXL862XX_BRIDGE_CONFIG_MASK_ALL: Enable all
 * @MXL862XX_BRIDGE_CONFIG_MASK_FORCE: Bypass any check for debug purpose
 */
enum mxl862xx_bridge_config_mask {
	MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNING_LIMIT = BIT(0),
	MXL862XX_BRIDGE_CONFIG_MASK_MAC_LEARNED_COUNT = BIT(1),
	MXL862XX_BRIDGE_CONFIG_MASK_MAC_DISCARD_COUNT = BIT(2),
	MXL862XX_BRIDGE_CONFIG_MASK_SUB_METER = BIT(3),
	MXL862XX_BRIDGE_CONFIG_MASK_FORWARDING_MODE = BIT(4),
	MXL862XX_BRIDGE_CONFIG_MASK_ALL = 0x7FFFFFFF,
	MXL862XX_BRIDGE_CONFIG_MASK_FORCE = BIT(31)
};

/**
 * enum mxl862xx_bridge_port_egress_meter - Meters for egress traffic type
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST:
 *     Index of broadcast traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_MULTICAST:
 *     Index of known multicast traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP:
 *     Index of unknown multicast IP traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP:
 *     Index of unknown multicast non-IP traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC:
 *     Index of unknown unicast traffic meter
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_OTHERS:
 *     Index of traffic meter for other types
 * @MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX: Number of index
 */
enum mxl862xx_bridge_port_egress_meter {
	MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST = 0,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_MULTICAST,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_OTHERS,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX,
};

/**
 * struct mxl862xx_qos_meter_cfg - Rate meter configuration
 * @enable: Enable/disable meter
 * @meter_id: Meter ID (assigned by firmware on alloc)
 * @meter_name: Meter name string
 * @meter_type: Meter algorithm type (srTCM = 0, trTCM = 1)
 * @cbs: Committed Burst Size (in bytes)
 * @res1: Reserved
 * @ebs: Excess Burst Size (in bytes)
 * @res2: Reserved
 * @rate: Committed Information Rate (in kbit/s)
 * @pi_rate: Peak Information Rate (in kbit/s)
 * @colour_blind_mode: Colour-blind mode enable
 * @pkt_mode: Packet mode enable
 * @local_overhd: Local overhead accounting enable
 * @local_overhd_val: Local overhead accounting value
 */
struct mxl862xx_qos_meter_cfg {
	u8 enable;
	__le16 meter_id;
	char meter_name[32];
	__le32 meter_type;
	__le32 cbs;
	__le32 res1;
	__le32 ebs;
	__le32 res2;
	__le32 rate;
	__le32 pi_rate;
	u8 colour_blind_mode;
	u8 pkt_mode;
	u8 local_overhd;
	__le16 local_overhd_val;
} __packed;

/**
 * enum mxl862xx_bridge_forward_mode - Bridge forwarding type of packet
 * @MXL862XX_BRIDGE_FORWARD_FLOOD: Packet is flooded to port members of
 *                                 ingress bridge port
 * @MXL862XX_BRIDGE_FORWARD_DISCARD: Packet is discarded
 */
enum mxl862xx_bridge_forward_mode {
	MXL862XX_BRIDGE_FORWARD_FLOOD = 0,
	MXL862XX_BRIDGE_FORWARD_DISCARD,
};

/**
 * struct mxl862xx_bridge_config - Bridge Configuration
 * @bridge_id: Bridge ID (FID)
 * @mask: See &enum mxl862xx_bridge_config_mask
 * @mac_learning_limit_enable: Enable MAC learning limitation
 * @mac_learning_limit: Max number of MAC addresses that can be learned in
 *                      this bridge (all bridge ports)
 * @mac_learning_count: Number of MAC addresses learned from this bridge
 * @learning_discard_event: Number of learning discard events due to
 *                          hardware resource not available
 * @sub_metering_enable: Traffic metering on type of traffic (such as
 *                       broadcast, multicast, unknown unicast, etc) applies
 * @traffic_sub_meter_id: Meter for bridge process with specific type (such
 *                        as broadcast, multicast, unknown unicast, etc)
 * @forward_broadcast: Forwarding mode of broadcast traffic. See
 *                     &enum mxl862xx_bridge_forward_mode
 * @forward_unknown_multicast_ip: Forwarding mode of unknown multicast IP
 *                                traffic.
 *                                See &enum mxl862xx_bridge_forward_mode
 * @forward_unknown_multicast_non_ip: Forwarding mode of unknown multicast
 *                                    non-IP traffic.
 *                                    See &enum mxl862xx_bridge_forward_mode
 * @forward_unknown_unicast: Forwarding mode of unknown unicast traffic. See
 *                           &enum mxl862xx_bridge_forward_mode
 */
struct mxl862xx_bridge_config {
	__le16 bridge_id;
	__le32 mask; /* enum mxl862xx_bridge_config_mask */
	u8 mac_learning_limit_enable;
	__le16 mac_learning_limit;
	__le16 mac_learning_count;
	__le32 learning_discard_event;
	u8 sub_metering_enable[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	__le16 traffic_sub_meter_id[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	__le32 forward_broadcast; /* enum mxl862xx_bridge_forward_mode */
	__le32 forward_unknown_multicast_ip; /* enum mxl862xx_bridge_forward_mode */
	__le32 forward_unknown_multicast_non_ip; /* enum mxl862xx_bridge_forward_mode */
	__le32 forward_unknown_unicast; /* enum mxl862xx_bridge_forward_mode */
} __packed;

/**
 * struct mxl862xx_bridge_port_alloc - Bridge Port Allocation
 * @bridge_port_id: If the bridge port allocation is successful, a valid ID
 *                  will be returned in this field. Otherwise, INVALID_HANDLE
 *                  is returned. For bridge port free, this field should
 *                  contain a valid ID returned by the bridge port allocation.
 *
 * Used by MXL862XX_BRIDGE_PORT_ALLOC and MXL862XX_BRIDGE_PORT_FREE.
 */
struct mxl862xx_bridge_port_alloc {
	__le16 bridge_port_id;
};

/**
 * enum mxl862xx_bridge_port_config_mask - Bridge Port configuration mask
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID:
 *     Mask for bridge_id
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN:
 *     Mask for ingress_extended_vlan_enable,
 *     ingress_extended_vlan_block_id and ingress_extended_vlan_block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN:
 *     Mask for egress_extended_vlan_enable, egress_extended_vlan_block_id
 *     and egress_extended_vlan_block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_MARKING:
 *     Mask for ingress_marking_mode
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_REMARKING:
 *     Mask for egress_remarking_mode
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_METER:
 *     Mask for ingress_metering_enable and ingress_traffic_meter_id
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER:
 *     Mask for egress_sub_metering_enable and egress_traffic_sub_meter_id
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_CTP_MAPPING:
 *     Mask for dest_logical_port_id, pmapper_enable, dest_sub_if_id_group,
 *     pmapper_mapping_mode, pmapper_id_valid and pmapper
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP:
 *     Mask for bridge_port_map
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_IP_LOOKUP:
 *     Mask for mc_dest_ip_lookup_disable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_IP_LOOKUP:
 *     Mask for mc_src_ip_lookup_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_MAC_LOOKUP:
 *     Mask for dest_mac_lookup_disable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING:
 *     Mask for src_mac_learning_disable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_SPOOFING:
 *     Mask for mac_spoofing_detect_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_PORT_LOCK:
 *     Mask for port_lock_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNING_LIMIT:
 *     Mask for mac_learning_limit_enable and mac_learning_limit
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNED_COUNT:
 *     Mask for mac_learning_count
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN_FILTER:
 *     Mask for ingress_vlan_filter_enable, ingress_vlan_filter_block_id
 *     and ingress_vlan_filter_block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER1:
 *     Mask for bypass_egress_vlan_filter1, egress_vlan_filter1enable,
 *     egress_vlan_filter1block_id and egress_vlan_filter1block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER2:
 *     Mask for egress_vlan_filter2enable, egress_vlan_filter2block_id and
 *     egress_vlan_filter2block_size
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING:
 *     Mask for vlan_tag_selection, vlan_src_mac_priority_enable,
 *     vlan_src_mac_dei_enable, vlan_src_mac_vid_enable,
 *     vlan_dst_mac_priority_enable, vlan_dst_mac_dei_enable and
 *     vlan_dst_mac_vid_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MULTICAST_LOOKUP:
 *     Mask for vlan_multicast_priority_enable,
 *     vlan_multicast_dei_enable and vlan_multicast_vid_enable
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_LOOP_VIOLATION_COUNTER:
 *     Mask for loop_violation_count
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_ALL: Enable all
 * @MXL862XX_BRIDGE_PORT_CONFIG_MASK_FORCE: Bypass any check for debug purpose
 */
enum mxl862xx_bridge_port_config_mask {
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID = BIT(0),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN = BIT(1),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN = BIT(2),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_MARKING = BIT(3),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_REMARKING = BIT(4),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_METER = BIT(5),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER = BIT(6),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_CTP_MAPPING = BIT(7),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP = BIT(8),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_IP_LOOKUP = BIT(9),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_IP_LOOKUP = BIT(10),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_DEST_MAC_LOOKUP = BIT(11),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING = BIT(12),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_SPOOFING = BIT(13),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_PORT_LOCK = BIT(14),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNING_LIMIT = BIT(15),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_MAC_LEARNED_COUNT = BIT(16),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN_FILTER = BIT(17),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER1 = BIT(18),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER2 = BIT(19),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING = BIT(20),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MULTICAST_LOOKUP = BIT(21),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_LOOP_VIOLATION_COUNTER = BIT(22),
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_ALL = 0x7FFFFFFF,
	MXL862XX_BRIDGE_PORT_CONFIG_MASK_FORCE = BIT(31)
};

/**
 * enum mxl862xx_color_marking_mode - Color Marking Mode
 * @MXL862XX_MARKING_ALL_GREEN: mark packets (except critical) to green
 * @MXL862XX_MARKING_INTERNAL_MARKING: do not change color and priority
 * @MXL862XX_MARKING_DEI: DEI mark mode
 * @MXL862XX_MARKING_PCP_8P0D: PCP 8P0D mark mode
 * @MXL862XX_MARKING_PCP_7P1D: PCP 7P1D mark mode
 * @MXL862XX_MARKING_PCP_6P2D: PCP 6P2D mark mode
 * @MXL862XX_MARKING_PCP_5P3D: PCP 5P3D mark mode
 * @MXL862XX_MARKING_DSCP_AF: DSCP AF class
 */
enum mxl862xx_color_marking_mode {
	MXL862XX_MARKING_ALL_GREEN = 0,
	MXL862XX_MARKING_INTERNAL_MARKING,
	MXL862XX_MARKING_DEI,
	MXL862XX_MARKING_PCP_8P0D,
	MXL862XX_MARKING_PCP_7P1D,
	MXL862XX_MARKING_PCP_6P2D,
	MXL862XX_MARKING_PCP_5P3D,
	MXL862XX_MARKING_DSCP_AF,
};

/**
 * enum mxl862xx_color_remarking_mode - Color Remarking Mode
 * @MXL862XX_REMARKING_NONE: values from last process stage
 * @MXL862XX_REMARKING_DEI: DEI mark mode
 * @MXL862XX_REMARKING_PCP_8P0D: PCP 8P0D mark mode
 * @MXL862XX_REMARKING_PCP_7P1D: PCP 7P1D mark mode
 * @MXL862XX_REMARKING_PCP_6P2D: PCP 6P2D mark mode
 * @MXL862XX_REMARKING_PCP_5P3D: PCP 5P3D mark mode
 * @MXL862XX_REMARKING_DSCP_AF: DSCP AF class
 */
enum mxl862xx_color_remarking_mode {
	MXL862XX_REMARKING_NONE = 0,
	MXL862XX_REMARKING_DEI = 2,
	MXL862XX_REMARKING_PCP_8P0D,
	MXL862XX_REMARKING_PCP_7P1D,
	MXL862XX_REMARKING_PCP_6P2D,
	MXL862XX_REMARKING_PCP_5P3D,
	MXL862XX_REMARKING_DSCP_AF,
};

/**
 * enum mxl862xx_pmapper_mapping_mode - P-mapper Mapping Mode
 * @MXL862XX_PMAPPER_MAPPING_PCP: Use PCP for VLAN tagged packets to derive
 *                                sub interface ID group
 * @MXL862XX_PMAPPER_MAPPING_LAG: Use LAG Index for Pmapper access
 *                                regardless of IP and VLAN packet
 * @MXL862XX_PMAPPER_MAPPING_DSCP: Use DSCP for VLAN tagged IP packets to
 *                                 derive sub interface ID group
 */
enum mxl862xx_pmapper_mapping_mode {
	MXL862XX_PMAPPER_MAPPING_PCP = 0,
	MXL862XX_PMAPPER_MAPPING_LAG,
	MXL862XX_PMAPPER_MAPPING_DSCP,
};

/**
 * struct mxl862xx_pmapper - P-mapper Configuration
 * @pmapper_id: Index of P-mapper (0-31)
 * @dest_sub_if_id_group: Sub interface ID group. Entry 0 is for non-IP and
 *                        non-VLAN tagged packets.
 *                        Entries 1-8 are PCP mapping entries for VLAN tagged
 *                        packets.
 *                        Entries 9-72 are DSCP or LAG mapping entries.
 *
 * Used by CTP port config and bridge port config. In case of LAG, it is
 * user's responsibility to provide the mapped entries in given P-mapper
 * table. In other modes the entries are auto mapped from input packet.
 */
struct mxl862xx_pmapper {
	__le16 pmapper_id;
	u8 dest_sub_if_id_group[73];
} __packed;

/**
 * struct mxl862xx_trunking_cfg - LAG hash algorithm configuration
 * @ip_src:   Include source IP address in trunk hash (1 = include)
 * @ip_dst:   Include destination IP address in trunk hash
 * @mac_src:  Include source MAC address in trunk hash
 * @mac_dst:  Include destination MAC address in trunk hash
 * @src_port: Include TCP/UDP source port in trunk hash
 * @dst_port: Include TCP/UDP destination port in trunk hash
 *
 * The firmware inverts the boolean sense when writing the hardware
 * register (PCE_TRUNK_CONF): bit=0 means include, bit=1 means exclude.
 * This struct uses the logical sense (1 = include).
 */
struct mxl862xx_trunking_cfg {
	u8 ip_src;
	u8 ip_dst;
	u8 mac_src;
	u8 mac_dst;
	u8 src_port;
	u8 dst_port;
} __packed;

/**
 * struct mxl862xx_bridge_port_config - Bridge Port Configuration
 * @bridge_port_id: Bridge Port ID allocated by bridge port allocation
 * @mask: See &enum mxl862xx_bridge_port_config_mask
 * @bridge_id: Bridge ID (FID) to which this bridge port is associated
 * @ingress_extended_vlan_enable: Enable extended VLAN processing for
 *                                ingress traffic
 * @ingress_extended_vlan_block_id: Extended VLAN block allocated for
 *                                  ingress traffic
 * @ingress_extended_vlan_block_size: Extended VLAN block size for ingress
 *                                    traffic
 * @egress_extended_vlan_enable: Enable extended VLAN processing for egress
 *                               traffic
 * @egress_extended_vlan_block_id: Extended VLAN block allocated for egress
 *                                 traffic
 * @egress_extended_vlan_block_size: Extended VLAN block size for egress
 *                                   traffic
 * @ingress_marking_mode: Ingress color marking mode. See
 *                        &enum mxl862xx_color_marking_mode
 * @egress_remarking_mode: Color remarking for egress traffic. See
 *                         &enum mxl862xx_color_remarking_mode
 * @ingress_metering_enable: Traffic metering on ingress traffic applies
 * @ingress_traffic_meter_id: Meter for ingress Bridge Port process
 * @egress_sub_metering_enable: Traffic metering on various types of egress
 *                              traffic
 * @egress_traffic_sub_meter_id: Meter for egress Bridge Port process with
 *                               specific type
 * @dest_logical_port_id: Destination logical port
 * @pmapper_enable: Enable P-mapper
 * @dest_sub_if_id_group: Destination sub interface ID group when
 *                        pmapper_enable is false
 * @pmapper_mapping_mode: P-mapper mapping mode. See
 *                        &enum mxl862xx_pmapper_mapping_mode
 * @pmapper_id_valid: When true, P-mapper is re-used; when false,
 *                    allocation is handled by API
 * @pmapper: P-mapper configuration used when pmapper_enable is true
 * @bridge_port_map: Port map defining broadcast domain. Each bit
 *                   represents one bridge port. Bridge port ID is
 *                   index * 16 + bit offset.
 * @mc_dest_ip_lookup_disable: Disable multicast IP destination table
 *                             lookup
 * @mc_src_ip_lookup_enable: Enable multicast IP source table lookup
 * @dest_mac_lookup_disable: Disable destination MAC lookup; packet treated
 *                           as unknown
 * @src_mac_learning_disable: Disable source MAC address learning
 * @mac_spoofing_detect_enable: Enable MAC spoofing detection
 * @port_lock_enable: Enable port locking
 * @mac_learning_limit_enable: Enable MAC learning limitation
 * @mac_learning_limit: Maximum number of MAC addresses that can be learned
 *                      from this bridge port
 * @loop_violation_count: Number of loop violation events from this bridge
 *                        port
 * @mac_learning_count: Number of MAC addresses learned from this bridge
 *                      port
 * @ingress_vlan_filter_enable: Enable ingress VLAN filter
 * @ingress_vlan_filter_block_id: VLAN filter block of ingress traffic
 * @ingress_vlan_filter_block_size: VLAN filter block size for ingress
 *                                  traffic
 * @bypass_egress_vlan_filter1: For ingress traffic, bypass VLAN filter 1
 *                              at egress bridge port processing
 * @egress_vlan_filter1enable: Enable egress VLAN filter 1
 * @egress_vlan_filter1block_id: VLAN filter block 1 of egress traffic
 * @egress_vlan_filter1block_size: VLAN filter block 1 size
 * @egress_vlan_filter2enable: Enable egress VLAN filter 2
 * @egress_vlan_filter2block_id: VLAN filter block 2 of egress traffic
 * @egress_vlan_filter2block_size: VLAN filter block 2 size
 * @vlan_tag_selection: VLAN tag selection for MAC address/multicast
 *                      learning, lookup and filtering.
 *                      0 - Intermediate outer VLAN tag is used.
 *                      1 - Original outer VLAN tag is used.
 * @vlan_src_mac_priority_enable: Enable VLAN Priority field for source MAC
 *                                learning and filtering
 * @vlan_src_mac_dei_enable: Enable VLAN DEI/CFI field for source MAC
 *                           learning and filtering
 * @vlan_src_mac_vid_enable: Enable VLAN ID field for source MAC learning
 *                           and filtering
 * @vlan_dst_mac_priority_enable: Enable VLAN Priority field for destination
 *                                MAC lookup and filtering
 * @vlan_dst_mac_dei_enable: Enable VLAN CFI/DEI field for destination MAC
 *                           lookup and filtering
 * @vlan_dst_mac_vid_enable: Enable VLAN ID field for destination MAC lookup
 *                           and filtering
 * @vlan_multicast_priority_enable: Enable VLAN Priority field for IP
 *                                  multicast lookup
 * @vlan_multicast_dei_enable: Enable VLAN CFI/DEI field for IP multicast
 *                             lookup
 * @vlan_multicast_vid_enable: Enable VLAN ID field for IP multicast lookup
 */
struct mxl862xx_bridge_port_config {
	__le16 bridge_port_id;
	__le32 mask; /* enum mxl862xx_bridge_port_config_mask */
	__le16 bridge_id;
	u8 ingress_extended_vlan_enable;
	__le16 ingress_extended_vlan_block_id;
	__le16 ingress_extended_vlan_block_size;
	u8 egress_extended_vlan_enable;
	__le16 egress_extended_vlan_block_id;
	__le16 egress_extended_vlan_block_size;
	__le32 ingress_marking_mode; /* enum mxl862xx_color_marking_mode */
	__le32 egress_remarking_mode; /* enum mxl862xx_color_remarking_mode */
	u8 ingress_metering_enable;
	__le16 ingress_traffic_meter_id;
	u8 egress_sub_metering_enable[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	__le16 egress_traffic_sub_meter_id[MXL862XX_BRIDGE_PORT_EGRESS_METER_MAX];
	u8 dest_logical_port_id;
	u8 pmapper_enable;
	__le16 dest_sub_if_id_group;
	__le32 pmapper_mapping_mode; /* enum mxl862xx_pmapper_mapping_mode */
	u8 pmapper_id_valid;
	struct mxl862xx_pmapper pmapper;
	__le16 bridge_port_map[8];
	u8 mc_dest_ip_lookup_disable;
	u8 mc_src_ip_lookup_enable;
	u8 dest_mac_lookup_disable;
	u8 src_mac_learning_disable;
	u8 mac_spoofing_detect_enable;
	u8 port_lock_enable;
	u8 mac_learning_limit_enable;
	__le16 mac_learning_limit;
	__le16 loop_violation_count;
	__le16 mac_learning_count;
	u8 ingress_vlan_filter_enable;
	__le16 ingress_vlan_filter_block_id;
	__le16 ingress_vlan_filter_block_size;
	u8 bypass_egress_vlan_filter1;
	u8 egress_vlan_filter1enable;
	__le16 egress_vlan_filter1block_id;
	__le16 egress_vlan_filter1block_size;
	u8 egress_vlan_filter2enable;
	__le16 egress_vlan_filter2block_id;
	__le16 egress_vlan_filter2block_size;
	u8 vlan_tag_selection;
	u8 vlan_src_mac_priority_enable;
	u8 vlan_src_mac_dei_enable;
	u8 vlan_src_mac_vid_enable;
	u8 vlan_dst_mac_priority_enable;
	u8 vlan_dst_mac_dei_enable;
	u8 vlan_dst_mac_vid_enable;
	u8 vlan_multicast_priority_enable;
	u8 vlan_multicast_dei_enable;
	u8 vlan_multicast_vid_enable;
} __packed;

/**
 * struct mxl862xx_monitor_port_cfg - Monitor port configuration
 * @port_id: Destination port for mirrored traffic (zero-based)
 * @sub_if_id: Monitoring sub-interface ID
 * @monitor_port: Reserved
 */
struct mxl862xx_monitor_port_cfg {
	u8 port_id;
	__le16 sub_if_id;
	u8 monitor_port;
} __packed;

/**
 * struct mxl862xx_cfg -  Global Switch configuration Attributes
 * @mac_table_age_timer: See &enum mxl862xx_age_timer
 * @age_timer: Custom MAC table aging timer in seconds
 * @max_packet_len: Maximum Ethernet packet length
 * @learning_limit_action: Automatic MAC address table learning limitation
 *                         consecutive action
 * @mac_locking_action: Accept or discard MAC port locking violation
 *                      packets
 * @mac_spoofing_action: Accept or discard MAC spoofing and port MAC locking
 *                       violation packets
 * @pause_mac_mode_src: Pause frame MAC source address mode
 * @pause_mac_src: Pause frame MAC source address
 */
struct mxl862xx_cfg {
	__le32 mac_table_age_timer; /* enum mxl862xx_age_timer */
	__le32 age_timer;
	__le16 max_packet_len;
	u8 learning_limit_action;
	u8 mac_locking_action;
	u8 mac_spoofing_action;
	u8 pause_mac_mode_src;
	u8 pause_mac_src[ETH_ALEN];
} __packed;

/**
 * enum mxl862xx_extended_vlan_filter_type - Extended VLAN filter tag type
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NORMAL: Normal tagged
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_FILTER: No filter (wildcard)
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT: Default entry
 * @MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_TAG: Untagged
 */
enum mxl862xx_extended_vlan_filter_type {
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NORMAL = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_FILTER = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT = 2,
	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_TAG = 3,
};

/**
 * enum mxl862xx_extended_vlan_filter_tpid - Extended VLAN filter TPID
 * @MXL862XX_EXTENDEDVLAN_FILTER_TPID_NO_FILTER: No TPID filter
 * @MXL862XX_EXTENDEDVLAN_FILTER_TPID_8021Q: 802.1Q TPID
 * @MXL862XX_EXTENDEDVLAN_FILTER_TPID_VTETYPE: VLAN type extension
 */
enum mxl862xx_extended_vlan_filter_tpid {
	MXL862XX_EXTENDEDVLAN_FILTER_TPID_NO_FILTER = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_TPID_8021Q = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_TPID_VTETYPE = 2,
};

/**
 * enum mxl862xx_extended_vlan_filter_dei - Extended VLAN filter DEI
 * @MXL862XX_EXTENDEDVLAN_FILTER_DEI_NO_FILTER: No DEI filter
 * @MXL862XX_EXTENDEDVLAN_FILTER_DEI_0: DEI = 0
 * @MXL862XX_EXTENDEDVLAN_FILTER_DEI_1: DEI = 1
 */
enum mxl862xx_extended_vlan_filter_dei {
	MXL862XX_EXTENDEDVLAN_FILTER_DEI_NO_FILTER = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_DEI_0 = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_DEI_1 = 2,
};

/**
 * enum mxl862xx_extended_vlan_treatment_remove_tag - Tag removal action
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG: Do not remove tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG: Remove one tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_2_TAG: Remove two tags
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM: Discard frame
 */
enum mxl862xx_extended_vlan_treatment_remove_tag {
	MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_2_TAG = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM = 3,
};

/**
 * enum mxl862xx_extended_vlan_treatment_priority - Treatment priority mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_PRIORITY_VAL: Use explicit value
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_PRIORITY: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_PRIORITY: Copy from outer tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DSCP: Derive from DSCP
 */
enum mxl862xx_extended_vlan_treatment_priority {
	MXL862XX_EXTENDEDVLAN_TREATMENT_PRIORITY_VAL = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_PRIORITY = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_PRIORITY = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DSCP = 3,
};

/**
 * enum mxl862xx_extended_vlan_treatment_vid - Treatment VID mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL: Use explicit VID value
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_VID: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_VID: Copy from outer tag
 */
enum mxl862xx_extended_vlan_treatment_vid {
	MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_VID = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_VID = 2,
};

/**
 * enum mxl862xx_extended_vlan_treatment_tpid - Treatment TPID mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_TPID: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_TPID: Copy from outer tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_VTETYPE: Use VLAN type extension
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q: Use 802.1Q TPID
 */
enum mxl862xx_extended_vlan_treatment_tpid {
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_TPID = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_TPID = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_VTETYPE = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q = 3,
};

/**
 * enum mxl862xx_extended_vlan_treatment_dei - Treatment DEI mode
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_DEI: Copy from inner tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_DEI: Copy from outer tag
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_0: Set DEI to 0
 * @MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_1: Set DEI to 1
 */
enum mxl862xx_extended_vlan_treatment_dei {
	MXL862XX_EXTENDEDVLAN_TREATMENT_INNER_DEI = 0,
	MXL862XX_EXTENDEDVLAN_TREATMENT_OUTER_DEI = 1,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_0 = 2,
	MXL862XX_EXTENDEDVLAN_TREATMENT_DEI_1 = 3,
};

/**
 * enum mxl862xx_extended_vlan_4_tpid_mode - 4-TPID mode selector
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_1: VLAN TPID type 1
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_2: VLAN TPID type 2
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_3: VLAN TPID type 3
 * @MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_4: VLAN TPID type 4
 */
enum mxl862xx_extended_vlan_4_tpid_mode {
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_1 = 0,
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_2 = 1,
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_3 = 2,
	MXL862XX_EXTENDEDVLAN_TPID_VTETYPE_4 = 3,
};

/**
 * enum mxl862xx_extended_vlan_filter_ethertype - Filter EtherType match
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_NO_FILTER: No filter
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPOE: IPoE
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_PPPOE: PPPoE
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_ARP: ARP
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPV6IPOE: IPv6 IPoE
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_EAPOL: EAPOL
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV4: DHCPv4
 * @MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV6: DHCPv6
 */
enum mxl862xx_extended_vlan_filter_ethertype {
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_NO_FILTER = 0,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPOE = 1,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_PPPOE = 2,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_ARP = 3,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_IPV6IPOE = 4,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_EAPOL = 5,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV4 = 6,
	MXL862XX_EXTENDEDVLAN_FILTER_ETHERTYPE_DHCPV6 = 7,
};

/**
 * struct mxl862xx_extendedvlan_filter_vlan - Per-tag filter in Extended VLAN
 * @type: Tag presence/type match (see &enum mxl862xx_extended_vlan_filter_type)
 * @priority_enable: Enable PCP value matching
 * @priority_val: PCP value to match
 * @vid_enable: Enable VID matching
 * @vid_val: VID value to match
 * @tpid: TPID match mode (see &enum mxl862xx_extended_vlan_filter_tpid)
 * @dei: DEI match mode (see &enum mxl862xx_extended_vlan_filter_dei)
 */
struct mxl862xx_extendedvlan_filter_vlan {
	__le32 type;
	u8 priority_enable;
	__le32 priority_val;
	u8 vid_enable;
	__le32 vid_val;
	__le32 tpid;
	__le32 dei;
} __packed;

/**
 * struct mxl862xx_extendedvlan_filter - Extended VLAN filter configuration
 * @original_packet_filter_mode: If true, filter on original (pre-treatment)
 *                               packet
 * @filter_4_tpid_mode: 4-TPID mode (see &enum mxl862xx_extended_vlan_4_tpid_mode)
 * @outer_vlan: Outer VLAN tag filter
 * @inner_vlan: Inner VLAN tag filter
 * @ether_type: EtherType filter (see
 *              &enum mxl862xx_extended_vlan_filter_ethertype)
 */
struct mxl862xx_extendedvlan_filter {
	u8 original_packet_filter_mode;
	__le32 filter_4_tpid_mode;
	struct mxl862xx_extendedvlan_filter_vlan outer_vlan;
	struct mxl862xx_extendedvlan_filter_vlan inner_vlan;
	__le32 ether_type;
} __packed;

/**
 * struct mxl862xx_extendedvlan_treatment_vlan - Per-tag treatment in
 *                                               Extended VLAN
 * @priority_mode: Priority assignment mode
 *                 (see &enum mxl862xx_extended_vlan_treatment_priority)
 * @priority_val: Priority value (when mode is VAL)
 * @vid_mode: VID assignment mode
 *            (see &enum mxl862xx_extended_vlan_treatment_vid)
 * @vid_val: VID value (when mode is VAL)
 * @tpid: TPID assignment mode
 *        (see &enum mxl862xx_extended_vlan_treatment_tpid)
 * @dei: DEI assignment mode
 *       (see &enum mxl862xx_extended_vlan_treatment_dei)
 */
struct mxl862xx_extendedvlan_treatment_vlan {
	__le32 priority_mode;
	__le32 priority_val;
	__le32 vid_mode;
	__le32 vid_val;
	__le32 tpid;
	__le32 dei;
} __packed;

/**
 * struct mxl862xx_extendedvlan_treatment - Extended VLAN treatment
 * @remove_tag: Tag removal action
 *              (see &enum mxl862xx_extended_vlan_treatment_remove_tag)
 * @treatment_4_tpid_mode: 4-TPID treatment mode
 * @add_outer_vlan: Add outer VLAN tag
 * @outer_vlan: Outer VLAN tag treatment parameters
 * @add_inner_vlan: Add inner VLAN tag
 * @inner_vlan: Inner VLAN tag treatment parameters
 * @reassign_bridge_port: Reassign to different bridge port
 * @new_bridge_port_id: New bridge port ID
 * @new_dscp_enable: Enable new DSCP assignment
 * @new_dscp: New DSCP value
 * @new_traffic_class_enable: Enable new traffic class assignment
 * @new_traffic_class: New traffic class value
 * @new_meter_enable: Enable new metering
 * @s_new_traffic_meter_id: New traffic meter ID
 * @dscp2pcp_map: DSCP to PCP mapping table (64 entries)
 * @loopback_enable: Enable loopback
 * @da_sa_swap_enable: Enable DA/SA swap
 * @mirror_enable: Enable mirroring
 */
struct mxl862xx_extendedvlan_treatment {
	__le32 remove_tag;
	__le32 treatment_4_tpid_mode;
	u8 add_outer_vlan;
	struct mxl862xx_extendedvlan_treatment_vlan outer_vlan;
	u8 add_inner_vlan;
	struct mxl862xx_extendedvlan_treatment_vlan inner_vlan;
	u8 reassign_bridge_port;
	__le16 new_bridge_port_id;
	u8 new_dscp_enable;
	__le16 new_dscp;
	u8 new_traffic_class_enable;
	u8 new_traffic_class;
	u8 new_meter_enable;
	__le16 s_new_traffic_meter_id;
	u8 dscp2pcp_map[64];
	u8 loopback_enable;
	u8 da_sa_swap_enable;
	u8 mirror_enable;
} __packed;

/**
 * struct mxl862xx_extendedvlan_alloc - Extended VLAN block allocation
 * @number_of_entries: Number of entries to allocate (input) / allocated
 *                     (output)
 * @extended_vlan_block_id: Block ID assigned by firmware (output on alloc,
 *                          input on free)
 *
 * Used with %MXL862XX_EXTENDEDVLAN_ALLOC and %MXL862XX_EXTENDEDVLAN_FREE.
 */
struct mxl862xx_extendedvlan_alloc {
	__le16 number_of_entries;
	__le16 extended_vlan_block_id;
} __packed;

/**
 * struct mxl862xx_extendedvlan_config - Extended VLAN entry configuration
 * @extended_vlan_block_id: Block ID from allocation
 * @entry_index: Entry index within the block
 * @filter: Filter (match) configuration
 * @treatment: Treatment (action) configuration
 *
 * Used with %MXL862XX_EXTENDEDVLAN_SET and %MXL862XX_EXTENDEDVLAN_GET.
 */
struct mxl862xx_extendedvlan_config {
	__le16 extended_vlan_block_id;
	__le16 entry_index;
	struct mxl862xx_extendedvlan_filter filter;
	struct mxl862xx_extendedvlan_treatment treatment;
} __packed;

/**
 * enum mxl862xx_vlan_filter_tci_mask - VLAN Filter TCI mask
 * @MXL862XX_VLAN_FILTER_TCI_MASK_VID: TCI mask for VLAN ID
 * @MXL862XX_VLAN_FILTER_TCI_MASK_PCP: TCI mask for VLAN PCP
 * @MXL862XX_VLAN_FILTER_TCI_MASK_TCI: TCI mask for VLAN TCI
 */
enum mxl862xx_vlan_filter_tci_mask {
	MXL862XX_VLAN_FILTER_TCI_MASK_VID = 0,
	MXL862XX_VLAN_FILTER_TCI_MASK_PCP = 1,
	MXL862XX_VLAN_FILTER_TCI_MASK_TCI = 2,
};

/**
 * struct mxl862xx_vlanfilter_alloc - VLAN Filter block allocation
 * @number_of_entries: Number of entries to allocate (input) / allocated
 *                     (output)
 * @vlan_filter_block_id: Block ID assigned by firmware (output on alloc,
 *                        input on free)
 * @discard_untagged: Discard untagged packets
 * @discard_unmatched_tagged: Discard tagged packets that do not match any
 *                            entry in the block
 * @use_default_port_vid: Use default port VLAN ID for filtering
 *
 * Used with %MXL862XX_VLANFILTER_ALLOC and %MXL862XX_VLANFILTER_FREE.
 */
struct mxl862xx_vlanfilter_alloc {
	__le16 number_of_entries;
	__le16 vlan_filter_block_id;
	u8 discard_untagged;
	u8 discard_unmatched_tagged;
	u8 use_default_port_vid;
} __packed;

/**
 * struct mxl862xx_vlanfilter_config - VLAN Filter entry configuration
 * @vlan_filter_block_id: Block ID from allocation
 * @entry_index: Entry index within the block
 * @vlan_filter_mask: TCI field(s) to match (see
 *                    &enum mxl862xx_vlan_filter_tci_mask)
 * @val: TCI value(s) to match (VID, PCP, or full TCI depending on mask)
 * @discard_matched: When true, discard frames matching this entry;
 *                   when false, allow them
 *
 * Used with %MXL862XX_VLANFILTER_SET and %MXL862XX_VLANFILTER_GET.
 */
struct mxl862xx_vlanfilter_config {
	__le16 vlan_filter_block_id;
	__le16 entry_index;
	__le32 vlan_filter_mask; /* enum mxl862xx_vlan_filter_tci_mask */
	__le32 val;
	u8 discard_matched;
} __packed;

/**
 * enum mxl862xx_ss_sp_tag_mask - Special tag valid field indicator bits
 * @MXL862XX_SS_SP_TAG_MASK_RX: valid RX special tag mode
 * @MXL862XX_SS_SP_TAG_MASK_TX: valid TX special tag mode
 * @MXL862XX_SS_SP_TAG_MASK_RX_PEN: valid RX special tag info over preamble
 * @MXL862XX_SS_SP_TAG_MASK_TX_PEN: valid TX special tag info over preamble
 */
enum mxl862xx_ss_sp_tag_mask {
	MXL862XX_SS_SP_TAG_MASK_RX = BIT(0),
	MXL862XX_SS_SP_TAG_MASK_TX = BIT(1),
	MXL862XX_SS_SP_TAG_MASK_RX_PEN = BIT(2),
	MXL862XX_SS_SP_TAG_MASK_TX_PEN = BIT(3),
};

/**
 * enum mxl862xx_ss_sp_tag_rx - RX special tag mode
 * @MXL862XX_SS_SP_TAG_RX_NO_TAG_NO_INSERT: packet does NOT have special
 *                                          tag and special tag is NOT inserted
 * @MXL862XX_SS_SP_TAG_RX_NO_TAG_INSERT: packet does NOT have special tag
 *                                       and special tag is inserted
 * @MXL862XX_SS_SP_TAG_RX_TAG_NO_INSERT: packet has special tag and special
 *                                       tag is NOT inserted
 */
enum mxl862xx_ss_sp_tag_rx {
	MXL862XX_SS_SP_TAG_RX_NO_TAG_NO_INSERT = 0,
	MXL862XX_SS_SP_TAG_RX_NO_TAG_INSERT = 1,
	MXL862XX_SS_SP_TAG_RX_TAG_NO_INSERT = 2,
};

/**
 * enum mxl862xx_ss_sp_tag_tx - TX special tag mode
 * @MXL862XX_SS_SP_TAG_TX_NO_TAG_NO_REMOVE: packet does NOT have special
 *                                          tag and special tag is NOT removed
 * @MXL862XX_SS_SP_TAG_TX_TAG_REPLACE: packet has special tag and special
 *                                     tag is replaced
 * @MXL862XX_SS_SP_TAG_TX_TAG_NO_REMOVE: packet has special tag and special
 *                                       tag is NOT removed
 * @MXL862XX_SS_SP_TAG_TX_TAG_REMOVE: packet has special tag and special
 *                                    tag is removed
 */
enum mxl862xx_ss_sp_tag_tx {
	MXL862XX_SS_SP_TAG_TX_NO_TAG_NO_REMOVE = 0,
	MXL862XX_SS_SP_TAG_TX_TAG_REPLACE = 1,
	MXL862XX_SS_SP_TAG_TX_TAG_NO_REMOVE = 2,
	MXL862XX_SS_SP_TAG_TX_TAG_REMOVE = 3,
};

/**
 * enum mxl862xx_ss_sp_tag_rx_pen - RX special tag info over preamble
 * @MXL862XX_SS_SP_TAG_RX_PEN_ALL_0: special tag info inserted from byte 2
 *                                   to 7 are all 0
 * @MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_IS_16: special tag byte 5 is 16, other
 *                                          bytes from 2 to 7 are 0
 * @MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_FROM_PREAMBLE: special tag byte 5 is
 *                                                  from preamble field, others
 *                                                  are 0
 * @MXL862XX_SS_SP_TAG_RX_PEN_BYTE_2_TO_7_FROM_PREAMBLE: special tag byte 2
 *                                                       to 7 are from preamble
 *                                                       field
 */
enum mxl862xx_ss_sp_tag_rx_pen {
	MXL862XX_SS_SP_TAG_RX_PEN_ALL_0 = 0,
	MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_IS_16 = 1,
	MXL862XX_SS_SP_TAG_RX_PEN_BYTE_5_FROM_PREAMBLE = 2,
	MXL862XX_SS_SP_TAG_RX_PEN_BYTE_2_TO_7_FROM_PREAMBLE = 3,
};

/**
 * struct mxl862xx_ss_sp_tag - Special tag port settings
 * @pid: port ID (1~16)
 * @mask: See &enum mxl862xx_ss_sp_tag_mask
 * @rx: See &enum mxl862xx_ss_sp_tag_rx
 * @tx: See &enum mxl862xx_ss_sp_tag_tx
 * @rx_pen: See &enum mxl862xx_ss_sp_tag_rx_pen
 * @tx_pen: TX special tag info over preamble
 *	0 - disabled
 *	1 - enabled
 */
struct mxl862xx_ss_sp_tag {
	u8 pid;
	u8 mask; /* enum mxl862xx_ss_sp_tag_mask */
	u8 rx; /* enum mxl862xx_ss_sp_tag_rx */
	u8 tx; /* enum mxl862xx_ss_sp_tag_tx */
	u8 rx_pen; /* enum mxl862xx_ss_sp_tag_rx_pen */
	u8 tx_pen; /* boolean */
} __packed;

/**
 * enum mxl862xx_logical_port_mode - Logical port mode
 * @MXL862XX_LOGICAL_PORT_8BIT_WLAN: WLAN with 8-bit station ID
 * @MXL862XX_LOGICAL_PORT_9BIT_WLAN: WLAN with 9-bit station ID
 * @MXL862XX_LOGICAL_PORT_ETHERNET: Ethernet port
 * @MXL862XX_LOGICAL_PORT_OTHER: Others
 */
enum mxl862xx_logical_port_mode {
	MXL862XX_LOGICAL_PORT_8BIT_WLAN = 0,
	MXL862XX_LOGICAL_PORT_9BIT_WLAN,
	MXL862XX_LOGICAL_PORT_ETHERNET,
	MXL862XX_LOGICAL_PORT_OTHER = 0xFF,
};

/**
 * struct mxl862xx_ctp_port_assignment - CTP Port Assignment/association
 *                                       with logical port
 * @logical_port_id: Logical Port Id. The valid range is hardware dependent
 * @first_ctp_port_id: First CTP (Connectivity Termination Port) ID mapped
 *                     to above logical port ID
 * @number_of_ctp_port: Total number of CTP Ports mapped above logical port
 *                      ID
 * @mode: Logical port mode to define sub interface ID format. See
 *        &enum mxl862xx_logical_port_mode
 * @bridge_port_id: Bridge Port ID (not FID). For allocation, each CTP
 *                  allocated is mapped to the Bridge Port given by this field.
 *                  The Bridge Port will be configured to use first CTP as
 *                  egress CTP.
 */
struct mxl862xx_ctp_port_assignment {
	u8 logical_port_id;
	__le16 first_ctp_port_id;
	__le16 number_of_ctp_port;
	__le32 mode; /* enum mxl862xx_logical_port_mode */
	__le16 bridge_port_id;
} __packed;

/**
 * enum mxl862xx_ctp_port_config_mask - CTP Port Configuration Mask
 * @MXL862XX_CTP_PORT_CONFIG_MASK_BRIDGE_PORT_ID:
 *     Mask for bridge_port_id.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_FORCE_TRAFFIC_CLASS:
 *     Mask for forced_traffic_class and default_traffic_class.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_VLAN:
 *     Mask for ingress_extended_vlan_enable and
 *     ingress_extended_vlan_block_id.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_VLAN_IGMP:
 *     Mask for ingress_extended_vlan_igmp_enable and
 *     ingress_extended_vlan_block_id_igmp.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_VLAN:
 *     Mask for egress_extended_vlan_enable and
 *     egress_extended_vlan_block_id.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_VLAN_IGMP:
 *     Mask for egress_extended_vlan_igmp_enable and
 *     egress_extended_vlan_block_id_igmp.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_NTO1_VLAN:
 *     Mask for ingress_nto1vlan_enable.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_NTO1_VLAN:
 *     Mask for egress_nto1vlan_enable.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_MARKING:
 *     Mask for ingress_marking_mode.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_MARKING:
 *     Mask for egress_marking_mode.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_MARKING_OVERRIDE:
 *     Mask for egress_marking_override_enable and
 *     egress_marking_mode_override.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_REMARKING:
 *     Mask for egress_remarking_mode.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_METER:
 *     Mask for ingress_metering_enable and ingress_traffic_meter_id.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_METER:
 *     Mask for egress_metering_enable and egress_traffic_meter_id.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_BRIDGING_BYPASS:
 *     Mask for bridging_bypass, dest_logical_port_id, pmapper_enable,
 *     dest_sub_if_id_group, pmapper_mapping_mode, pmapper_id_valid and
 *     pmapper_dest_sub_if_id_group.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_FLOW_ENTRY:
 *     Mask for first_flow_entry_index and number_of_flow_entries.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_LOOPBACK_AND_MIRROR:
 *     Mask for ingress_loopback_enable, ingress_da_sa_swap_enable,
 *     egress_loopback_enable, egress_da_sa_swap_enable,
 *     ingress_mirror_enable and egress_mirror_enable.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_ALL: Enable all fields.
 * @MXL862XX_CTP_PORT_CONFIG_MASK_FORCE: Bypass any check for debug purpose.
 */
enum mxl862xx_ctp_port_config_mask {
	MXL862XX_CTP_PORT_CONFIG_MASK_BRIDGE_PORT_ID = BIT(0),
	MXL862XX_CTP_PORT_CONFIG_MASK_FORCE_TRAFFIC_CLASS = BIT(1),
	MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_VLAN = BIT(2),
	MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_VLAN_IGMP = BIT(3),
	MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_VLAN = BIT(4),
	MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_VLAN_IGMP = BIT(5),
	MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_NTO1_VLAN = BIT(6),
	MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_NTO1_VLAN = BIT(7),
	MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_MARKING = BIT(8),
	MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_MARKING = BIT(9),
	MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_MARKING_OVERRIDE = BIT(10),
	MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_REMARKING = BIT(11),
	MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_METER = BIT(12),
	MXL862XX_CTP_PORT_CONFIG_MASK_EGRESS_METER = BIT(13),
	MXL862XX_CTP_PORT_CONFIG_MASK_BRIDGING_BYPASS = BIT(14),
	MXL862XX_CTP_PORT_CONFIG_MASK_FLOW_ENTRY = BIT(15),
	MXL862XX_CTP_PORT_CONFIG_MASK_LOOPBACK_AND_MIRROR = BIT(16),
	MXL862XX_CTP_PORT_CONFIG_MASK_ALL = 0x7FFFFFFF,
	MXL862XX_CTP_PORT_CONFIG_MASK_FORCE = BIT(31),
};

/**
 * struct mxl862xx_ctp_port_config - CTP Port Configuration
 * @logical_port_id: Logical Port Id. The valid range is hardware dependent.
 *                   Ignored when mask has
 *                   %MXL862XX_CTP_PORT_CONFIG_MASK_FORCE.
 * @n_sub_if_id_group: Sub interface ID group. The valid range is
 *                     hardware/protocol dependent. When mask has
 *                     %MXL862XX_CTP_PORT_CONFIG_MASK_FORCE, this is the
 *                     absolute CTP index in hardware (debug only).
 * @mask: See &enum mxl862xx_ctp_port_config_mask
 * @bridge_port_id: Ingress Bridge Port ID to which this CTP port is
 *                  associated for ingress traffic
 * @forced_traffic_class: Default traffic class cannot be overridden by other
 *                        rules (except traffic flow table and special tag)
 * @default_traffic_class: Default traffic class for all ingress traffic from
 *                         this CTP port
 * @ingress_extended_vlan_enable: Enable extended VLAN processing for ingress
 *                                non-IGMP traffic
 * @ingress_extended_vlan_block_id: Extended VLAN block allocated for ingress
 *                                  non-IGMP traffic. Valid when
 *                                  ingress_extended_vlan_enable is set.
 * @ingress_extended_vlan_block_size: Extended VLAN block size for ingress
 *                                    non-IGMP traffic. If 0, the block size of
 *                                    ingress_extended_vlan_block_id is used.
 * @ingress_extended_vlan_igmp_enable: Enable extended VLAN processing for
 *                                     ingress IGMP traffic
 * @ingress_extended_vlan_block_id_igmp: Extended VLAN block allocated for
 *                                       ingress IGMP traffic. Valid when
 *                                       ingress_extended_vlan_igmp_enable is
 *                                       set.
 * @ingress_extended_vlan_block_size_igmp: Extended VLAN block size for ingress
 *                                         IGMP traffic. If 0, the block size of
 *                                         ingress_extended_vlan_block_id_igmp
 *                                         is used.
 * @egress_extended_vlan_enable: Enable extended VLAN processing for egress
 *                               non-IGMP traffic
 * @egress_extended_vlan_block_id: Extended VLAN block allocated for egress
 *                                 non-IGMP traffic. Valid when
 *                                 egress_extended_vlan_enable is set.
 * @egress_extended_vlan_block_size: Extended VLAN block size for egress
 *                                   non-IGMP traffic. If 0, the block size of
 *                                   egress_extended_vlan_block_id is used.
 * @egress_extended_vlan_igmp_enable: Enable extended VLAN processing for
 *                                    egress IGMP traffic
 * @egress_extended_vlan_block_id_igmp: Extended VLAN block allocated for
 *                                      egress IGMP traffic. Valid when
 *                                      egress_extended_vlan_igmp_enable is set.
 * @egress_extended_vlan_block_size_igmp: Extended VLAN block size for egress
 *                                        IGMP traffic. If 0, the block size of
 *                                        egress_extended_vlan_block_id_igmp is
 *                                        used.
 * @ingress_nto1vlan_enable: If enabled and ingress packet is VLAN tagged,
 *                           outer VLAN ID is used for nSubIfId field in MAC
 *                           table; otherwise 0 is used
 * @egress_nto1vlan_enable: If enabled and egress packet is known unicast,
 *                          outer VLAN ID is from nSubIfId field in MAC table
 * @ingress_marking_mode: Ingress color marking mode for ingress traffic
 * @egress_marking_mode: Egress color marking mode for ingress traffic at
 *                       egress priority queue color marking stage
 * @egress_marking_override_enable: Override color marking mode from last stage
 * @egress_marking_mode_override: Egress color marking mode for egress traffic.
 *                                Valid only when
 *                                egress_marking_override_enable is set.
 * @egress_remarking_mode: Color remarking for egress traffic
 * @ingress_metering_enable: Traffic metering on ingress traffic applies
 * @ingress_traffic_meter_id: Meter for ingress CTP process
 * @egress_metering_enable: Traffic metering on egress traffic applies
 * @egress_traffic_meter_id: Meter for egress CTP process
 * @bridging_bypass: Ingress traffic bypasses bridging/multicast processing.
 *                   Traffic flow table is not bypassed.
 * @dest_logical_port_id: Destination logical port when bridging_bypass is set
 * @pmapper_enable: When bridging_bypass is set, selects whether to use
 *                  dest_sub_if_id_group or P-mapper for sub interface
 * @dest_sub_if_id_group: Destination sub interface ID group when
 *                        bridging_bypass is set and pmapper_enable is false
 * @pmapper_mapping_mode: When bridging_bypass and pmapper_enable are set,
 *                        selects DSCP or PCP to derive sub interface ID
 * @pmapper_id_valid: When set, P-mapper is re-used and no new allocation or
 *                    value change occurs. When false, allocation is handled
 *                    by the API.
 * @pmapper_id: P-mapper ID. Valid when pmapper_id_valid is set.
 * @pmapper_dest_sub_if_id_group: P-mapper destination sub interface ID group
 *                                entries (73 bytes, firmware layout)
 * @first_flow_entry_index: First traffic flow table entry associated to this
 *                          CTP port. Should be a multiple of 4.
 * @number_of_flow_entries: Number of traffic flow table entries associated to
 *                          this CTP port. Should be a multiple of 4.
 * @ingress_loopback_enable: Ingress traffic is redirected to ingress logical
 *                           port of this CTP with source sub interface ID as
 *                           destination. Bypasses processing except flow table.
 * @ingress_da_sa_swap_enable: Destination/Source MAC address of ingress traffic
 *                             is swapped before transmitted
 * @egress_loopback_enable: Egress traffic to this CTP port is redirected to
 *                          ingress logical port with same sub interface ID
 * @egress_da_sa_swap_enable: Destination/Source MAC address of egress traffic
 *                            is swapped before transmitted
 * @ingress_mirror_enable: If enabled, ingress traffic is mirrored to the
 *                         monitoring port. Mutually exclusive with
 *                         ingress_loopback_enable.
 * @egress_mirror_enable: If enabled, egress traffic is mirrored to the
 *                        monitoring port. Mutually exclusive with
 *                        egress_loopback_enable.
 */
struct mxl862xx_ctp_port_config {
	u8 logical_port_id;
	__le16 n_sub_if_id_group;
	__le32 mask;
	__le16 bridge_port_id;
	u8 forced_traffic_class;
	u8 default_traffic_class;
	u8 ingress_extended_vlan_enable;
	__le16 ingress_extended_vlan_block_id;
	__le16 ingress_extended_vlan_block_size;
	u8 ingress_extended_vlan_igmp_enable;
	__le16 ingress_extended_vlan_block_id_igmp;
	__le16 ingress_extended_vlan_block_size_igmp;
	u8 egress_extended_vlan_enable;
	__le16 egress_extended_vlan_block_id;
	__le16 egress_extended_vlan_block_size;
	u8 egress_extended_vlan_igmp_enable;
	__le16 egress_extended_vlan_block_id_igmp;
	__le16 egress_extended_vlan_block_size_igmp;
	u8 ingress_nto1vlan_enable;
	u8 egress_nto1vlan_enable;
	__le32 ingress_marking_mode;
	__le32 egress_marking_mode;
	u8 egress_marking_override_enable;
	__le32 egress_marking_mode_override;
	__le32 egress_remarking_mode;
	u8 ingress_metering_enable;
	__le16 ingress_traffic_meter_id;
	u8 egress_metering_enable;
	__le16 egress_traffic_meter_id;
	u8 bridging_bypass;
	u8 dest_logical_port_id;
	u8 pmapper_enable;
	__le16 dest_sub_if_id_group;
	__le32 pmapper_mapping_mode;
	u8 pmapper_id_valid;
	__le16 pmapper_id;
	u8 pmapper_dest_sub_if_id_group[73];
	__le16 first_flow_entry_index;
	__le16 number_of_flow_entries;
	u8 ingress_loopback_enable;
	u8 ingress_da_sa_swap_enable;
	u8 egress_loopback_enable;
	u8 egress_da_sa_swap_enable;
	u8 ingress_mirror_enable;
	u8 egress_mirror_enable;
} __packed;

/* PCE (Packet Classification Engine) rule structures.
 *
 * Binary layout must exactly match the firmware's GSW_PCE_rule_t
 * (gsw_flow.h, packed little-endian). The firmware deserializes
 * the structure directly from the MDIO data buffer produced by
 * mxl862xx_api_wrap().
 */

/**
 * union mxl862xx_ip - IPv4 or IPv6 address
 * @ipv4: IPv4 address in little-endian
 * @ipv6: IPv6 address as array of little-endian 16-bit words
 */
union mxl862xx_ip {
	__le32 ipv4;
	__le16 ipv6[8];
} __packed;

/**
 * struct mxl862xx_pce_pattern - PCE rule match pattern
 *
 * Every field must remain in the order shown; the firmware
 * interprets the buffer positionally.
 *
 * @index: PCE rule index (0..511)
 * @dst_ip_mask: Destination IP nibble mask (outer)
 * @inner_dst_ip_mask: Inner destination IP nibble mask
 * @src_ip_mask: Source IP nibble mask (outer)
 * @inner_src_ip_mask: Inner source IP nibble mask
 * @sub_if_id: Incoming sub-interface ID value
 * @pkt_lng: Packet length in bytes
 * @pkt_lng_range: Packet length range upper bound
 * @mac_dst_mask: Destination MAC nibble mask
 * @mac_src_mask: Source MAC nibble mask
 * @app_data_msb: MSB application field (first 2 bytes after IP header,
 *                typically TCP/UDP source port)
 * @app_mask_range_msb: MSB application nibble mask, or range span when
 *                      @app_mask_range_msb_select is set: the rule then
 *                      matches values from @app_data_msb to
 *                      @app_data_msb + @app_mask_range_msb, inclusive
 * @ether_type: EtherType value to match
 * @ether_type_mask: EtherType nibble mask
 * @session_id: PPPoE session ID
 * @ppp_protocol: PPP protocol value
 * @ppp_protocol_mask: PPP protocol bit mask
 * @vid: CTAG VLAN ID (inner VLAN)
 * @slan_vid: STAG VLAN ID (outer VLAN)
 * @flex_field4_value: Flexible field 4 match value
 * @flex_field4_mask_range: Flexible field 4 mask or range
 * @flex_field3_value: Flexible field 3 match value
 * @flex_field3_mask_range: Flexible field 3 mask or range
 * @flex_field1_value: Flexible field 1 match value
 * @flex_field1_mask_range: Flexible field 1 mask or range
 * @outer_vid_range: Outer VLAN ID range
 * @payload1: Payload-1 value (16-bit)
 * @payload1_mask: Payload-1 bit mask
 * @payload2: Payload-2 value (16-bit)
 * @payload2_mask: Payload-2 bit mask
 * @parser_flag_lsb: Parser flag LSW value (bits 15:0)
 * @parser_flag_lsb_mask: Parser flag LSW mask (1 = masked out)
 * @parser_flag_msb: Parser flag MSW value (bits 31:16)
 * @parser_flag_msb_mask: Parser flag MSW mask (1 = masked out)
 * @parser_flag1_lsb: Parser flag1 LSW value (bits 47:32)
 * @parser_flag1_lsb_mask: Parser flag1 LSW mask
 * @parser_flag1_msb: Parser flag1 MSW value (bits 63:48)
 * @parser_flag1_msb_mask: Parser flag1 MSW mask
 * @app_data_lsb: LSB application field (next 2 bytes after @app_data_msb,
 *                typically TCP/UDP destination port)
 * @app_mask_range_lsb: LSB application nibble mask, or range span when
 *                      @app_mask_range_lsb_select is set: the rule then
 *                      matches values from @app_data_lsb to
 *                      @app_data_lsb + @app_mask_range_lsb, inclusive
 * @insertion_flag: CPU-inserted packet flag
 * @vid_range: CTAG VLAN ID range (used as mask when @vid_range_select is 0)
 * @flex_field2_mask_range: Flexible field 2 mask or range
 * @flex_field2_value: Flexible field 2 match value
 * @port_id: Ingress port ID for classification
 * @dscp: Outer DSCP value
 * @inner_dscp: Inner DSCP value
 * @pcp: CTAG VLAN PCP (bits 2:0) and DEI (bit 3)
 * @stag_pcp_dei: STAG VLAN PCP (bits 2:0) and DEI (bit 3)
 * @mac_dst: Destination MAC address
 * @mac_src: Source MAC address
 * @protocol: Outer IP protocol value
 * @protocol_mask: Outer IP protocol nibble mask
 * @inner_protocol: Inner IP protocol value
 * @inner_protocol_mask: Inner IP protocol bit mask
 * @flex_field4_parser_index: Flexible field 4 parser output index (0..127)
 * @flex_field3_parser_index: Flexible field 3 parser output index (0..127)
 * @flex_field1_parser_index: Flexible field 1 parser output index (0..127)
 * @flex_field2_parser_index: Flexible field 2 parser output index (0..127)
 * @enable: Rule is enabled (used) or disabled (unused)
 * @port_id_enable: Enable ingress port ID matching
 * @port_id_exclude: Exclude (negate) port ID match
 * @sub_if_id_type: Sub-interface ID field mode selector
 * @sub_if_id_enable: Enable sub-interface ID matching
 * @sub_if_id_exclude: Exclude sub-interface ID match
 * @dscp_enable: Enable outer DSCP matching
 * @dscp_exclude: Exclude outer DSCP match
 * @inner_dscp_enable: Enable inner DSCP matching
 * @inner_dscp_exclude: Exclude inner DSCP match
 * @pcp_enable: Enable CTAG PCP/DEI matching
 * @ctag_pcp_dei_exclude: Exclude CTAG PCP/DEI match
 * @stag_pcp_dei_enable: Enable STAG PCP/DEI matching
 * @stag_pcp_dei_exclude: Exclude STAG PCP/DEI match
 * @pkt_lng_enable: Enable packet length matching
 * @pkt_lng_exclude: Exclude packet length match
 * @mac_dst_enable: Enable destination MAC matching
 * @dst_mac_exclude: Exclude destination MAC match
 * @mac_src_enable: Enable source MAC matching
 * @src_mac_exclude: Exclude source MAC match
 * @app_data_msb_enable: Enable MSB application field matching
 * @app_mask_range_msb_select: MSB application mask/range selection
 *                             (0 = nibble mask, 1 = range)
 * @app_msb_exclude: Exclude MSB application match
 * @app_data_lsb_enable: Enable LSB application field matching
 * @app_mask_range_lsb_select: LSB application mask/range selection
 *                             (0 = nibble mask, 1 = range)
 * @app_lsb_exclude: Exclude LSB application match
 * @dst_ip_select: Outer destination IP selection
 *                 (0 = disabled, 1 = IPv4, 2 = IPv6)
 * @dst_ip: Outer destination IP address
 * @dst_ip_exclude: Exclude outer destination IP match
 * @inner_dst_ip_select: Inner destination IP selection
 * @inner_dst_ip: Inner destination IP address
 * @inner_dst_ip_exclude: Exclude inner destination IP match
 * @src_ip_select: Outer source IP selection
 *                 (0 = disabled, 1 = IPv4, 2 = IPv6)
 * @src_ip: Outer source IP address
 * @src_ip_exclude: Exclude outer source IP match
 * @inner_src_ip_select: Inner source IP selection
 * @inner_src_ip: Inner source IP address
 * @inner_src_ip_exclude: Exclude inner source IP match
 * @ether_type_enable: Enable EtherType matching
 * @ether_type_exclude: Exclude EtherType match
 * @protocol_enable: Enable outer IP protocol matching
 * @protocol_exclude: Exclude outer IP protocol match
 * @inner_protocol_enable: Enable inner IP protocol matching
 * @inner_protocol_exclude: Exclude inner IP protocol match
 * @session_id_enable: Enable PPPoE session ID matching
 * @session_id_exclude: Exclude PPPoE session ID match
 * @ppp_protocol_enable: Enable PPP protocol matching
 * @ppp_protocol_exclude: Exclude PPP protocol match
 * @vid_used: Enable CTAG VLAN ID matching
 * @vid_range_select: CVLAN mask/range selection (0 = mask, 1 = range)
 * @vid_exclude: Exclude CTAG VLAN ID match
 * @vid_original: Use original VLAN ID as key even if modified earlier
 * @slan_vid_used: Enable STAG VLAN ID matching
 * @slan_vid_exclude: Exclude STAG VLAN ID match
 * @svid_range_select: SVLAN mask/range selection (0 = mask, 1 = range)
 * @outer_vid_original: Use original outer VLAN ID as key even if modified
 * @payload1_src_enable: Enable payload-1 matching
 * @payload1_mask_range_select: Payload-1 mask/range selection
 *                              (0 = bit mask, 1 = range)
 * @payload1_exclude: Exclude payload-1 match
 * @payload2_src_enable: Enable payload-2 matching
 * @payload2_mask_range_select: Payload-2 mask/range selection
 *                              (0 = bit mask, 1 = range)
 * @payload2_exclude: Exclude payload-2 match
 * @parser_flag_lsb_enable: Enable parser flag LSW matching
 * @parser_flag_lsb_exclude: Exclude parser flag LSW match
 * @parser_flag_msb_enable: Enable parser flag MSW matching
 * @parser_flag_msb_exclude: Exclude parser flag MSW match
 * @parser_flag1_lsb_enable: Enable parser flag1 LSW matching
 * @parser_flag1_lsb_exclude: Exclude parser flag1 LSW match
 * @parser_flag1_msb_enable: Enable parser flag1 MSW matching
 * @parser_flag1_msb_exclude: Exclude parser flag1 MSW match
 * @insertion_flag_enable: Enable insertion flag matching
 * @flex_field4_enable: Enable flexible field 4 matching
 * @flex_field4_exclude_enable: Exclude flexible field 4 match
 * @flex_field4_range_enable: Flexible field 4 range mode
 *                            (0 = mask, 1 = range)
 * @flex_field3_enable: Enable flexible field 3 matching
 * @flex_field3_exclude_enable: Exclude flexible field 3 match
 * @flex_field3_range_enable: Flexible field 3 range mode
 * @flex_field2_enable: Enable flexible field 2 matching
 * @flex_field2_exclude_enable: Exclude flexible field 2 match
 * @flex_field2_range_enable: Flexible field 2 range mode
 * @flex_field1_enable: Enable flexible field 1 matching
 * @flex_field1_exclude_enable: Exclude flexible field 1 match
 * @flex_field1_range_enable: Flexible field 1 range mode
 */
struct mxl862xx_pce_pattern {
	__le16 index;
	__le32 dst_ip_mask;
	__le32 inner_dst_ip_mask;
	__le32 src_ip_mask;
	__le32 inner_src_ip_mask;
	__le16 sub_if_id;
	__le16 pkt_lng;
	__le16 pkt_lng_range;
	__le16 mac_dst_mask;
	__le16 mac_src_mask;
	__le16 app_data_msb;
	__le16 app_mask_range_msb;
	__le16 ether_type;
	__le16 ether_type_mask;
	__le16 session_id;
	__le16 ppp_protocol;
	__le16 ppp_protocol_mask;
	__le16 vid;
	__le16 slan_vid;
	__le16 flex_field4_value;
	__le16 flex_field4_mask_range;
	__le16 flex_field3_value;
	__le16 flex_field3_mask_range;
	__le16 flex_field1_value;
	__le16 flex_field1_mask_range;
	__le16 outer_vid_range;
	__le16 payload1;
	__le16 payload1_mask;
	__le16 payload2;
	__le16 payload2_mask;
	__le16 parser_flag_lsb;
	__le16 parser_flag_lsb_mask;
	__le16 parser_flag_msb;
	__le16 parser_flag_msb_mask;
	__le16 parser_flag1_lsb;
	__le16 parser_flag1_lsb_mask;
	__le16 parser_flag1_msb;
	__le16 parser_flag1_msb_mask;
	__le16 app_data_lsb;
	__le16 app_mask_range_lsb;
	__le16 insertion_flag;
	__le16 vid_range;
	__le16 flex_field2_mask_range;
	__le16 flex_field2_value;
	u8 port_id;
	u8 dscp;
	u8 inner_dscp;
	u8 pcp;
	u8 stag_pcp_dei;
	u8 mac_dst[ETH_ALEN];
	u8 mac_src[ETH_ALEN];
	u8 protocol;
	u8 protocol_mask;
	u8 inner_protocol;
	u8 inner_protocol_mask;
	u8 flex_field4_parser_index;
	u8 flex_field3_parser_index;
	u8 flex_field1_parser_index;
	u8 flex_field2_parser_index;
	u8 enable;
	u8 port_id_enable;
	u8 port_id_exclude;
	__le32 sub_if_id_type;
	u8 sub_if_id_enable;
	u8 sub_if_id_exclude;
	u8 dscp_enable;
	u8 dscp_exclude;
	u8 inner_dscp_enable;
	u8 inner_dscp_exclude;
	u8 pcp_enable;
	u8 ctag_pcp_dei_exclude;
	u8 stag_pcp_dei_enable;
	u8 stag_pcp_dei_exclude;
	u8 pkt_lng_enable;
	u8 pkt_lng_exclude;
	u8 mac_dst_enable;
	u8 dst_mac_exclude;
	u8 mac_src_enable;
	u8 src_mac_exclude;
	u8 app_data_msb_enable;
	u8 app_mask_range_msb_select;
	u8 app_msb_exclude;
	u8 app_data_lsb_enable;
	u8 app_mask_range_lsb_select;
	u8 app_lsb_exclude;
	__le32 dst_ip_select;
	union mxl862xx_ip dst_ip;
	u8 dst_ip_exclude;
	__le32 inner_dst_ip_select;
	union mxl862xx_ip inner_dst_ip;
	u8 inner_dst_ip_exclude;
	__le32 src_ip_select;
	union mxl862xx_ip src_ip;
	u8 src_ip_exclude;
	__le32 inner_src_ip_select;
	union mxl862xx_ip inner_src_ip;
	u8 inner_src_ip_exclude;
	u8 ether_type_enable;
	u8 ether_type_exclude;
	u8 protocol_enable;
	u8 protocol_exclude;
	u8 inner_protocol_enable;
	u8 inner_protocol_exclude;
	u8 session_id_enable;
	u8 session_id_exclude;
	u8 ppp_protocol_enable;
	u8 ppp_protocol_exclude;
	u8 vid_used;
	u8 vid_range_select;
	u8 vid_exclude;
	u8 vid_original;
	u8 slan_vid_used;
	u8 slan_vid_exclude;
	u8 svid_range_select;
	u8 outer_vid_original;
	u8 payload1_src_enable;
	u8 payload1_mask_range_select;
	u8 payload1_exclude;
	u8 payload2_src_enable;
	u8 payload2_mask_range_select;
	u8 payload2_exclude;
	u8 parser_flag_lsb_enable;
	u8 parser_flag_lsb_exclude;
	u8 parser_flag_msb_enable;
	u8 parser_flag_msb_exclude;
	u8 parser_flag1_lsb_enable;
	u8 parser_flag1_lsb_exclude;
	u8 parser_flag1_msb_enable;
	u8 parser_flag1_msb_exclude;
	u8 insertion_flag_enable;
	u8 flex_field4_enable;
	u8 flex_field4_exclude_enable;
	u8 flex_field4_range_enable;
	u8 flex_field3_enable;
	u8 flex_field3_exclude_enable;
	u8 flex_field3_range_enable;
	u8 flex_field2_enable;
	u8 flex_field2_exclude_enable;
	u8 flex_field2_range_enable;
	u8 flex_field1_enable;
	u8 flex_field1_exclude_enable;
	u8 flex_field1_range_enable;
} __packed;

static_assert(sizeof(struct mxl862xx_pce_pattern) == 279);

/**
 * struct mxl862xx_pce_action_pbb - Provider Backbone Bridging (Mac-in-Mac)
 *                                  action configuration
 *
 * @tunnel_id_known_traffic: Tunnel template index for I-Header known traffic
 * @tunnel_id_unknown_traffic: Tunnel template index for I-Header unknown
 *                             traffic
 * @process_id_known_traffic: Tunnel template index for B-TAG known traffic
 * @process_id_unknown_traffic: Tunnel template index for B-TAG unknown
 *                              traffic
 * @iheader_op_mode: I-Header operation mode (0 = no change, 1 = insert,
 *                   2 = remove, 3 = replace)
 * @btag_op_mode: B-TAG operation mode (0 = no change, 1 = insert,
 *               2 = remove, 3 = replace)
 * @mac_table_macinmac_select: MAC table Mac-in-Mac selection
 *                             (0 = outer MAC, 1 = inner MAC)
 * @iheader_action_enable: Enable Mac-in-Mac I-Header action
 * @tunnel_id_known_traffic_enable: Enable tunnel ID for known traffic
 * @tunnel_id_unknown_traffic_enable: Enable tunnel ID for unknown traffic
 * @b_dst_mac_from_mac_table_enable: Use B-DA from MAC table instead of
 *                                   tunnel template (I-Header insertion
 *                                   mode only)
 * @replace_b_src_mac_enable: Replace B-SA from tunnel template
 * @replace_b_dst_mac_enable: Replace B-DA from tunnel template
 * @replace_i_tag_res_enable: Replace I-Tag Res from tunnel template
 * @replace_i_tag_uac_enable: Replace I-Tag UAC from tunnel template
 * @replace_i_tag_dei_enable: Replace I-Tag DEI from tunnel template
 * @replace_i_tag_pcp_enable: Replace I-Tag PCP from tunnel template
 * @replace_i_tag_sid_enable: Replace I-Tag SID from tunnel template
 * @replace_i_tag_tpid_enable: Replace I-Tag TPID from tunnel template
 * @btag_action_enable: Enable B-TAG action
 * @process_id_known_traffic_enable: Enable process ID for B-TAG known
 *                                   traffic
 * @process_id_unknown_traffic_enable: Enable process ID for B-TAG unknown
 *                                     traffic
 * @replace_b_tag_dei_enable: Replace B-Tag DEI from tunnel template
 * @replace_b_tag_pcp_enable: Replace B-Tag PCP from tunnel template
 * @replace_b_tag_vid_enable: Replace B-Tag VID from tunnel template
 * @replace_b_tag_tpid_enable: Replace B-Tag TPID from tunnel template
 * @mac_table_macinmac_action_enable: Enable MAC table Mac-in-Mac action
 */
struct mxl862xx_pce_action_pbb {
	u8 tunnel_id_known_traffic;
	u8 tunnel_id_unknown_traffic;
	u8 process_id_known_traffic;
	u8 process_id_unknown_traffic;
	__le32 iheader_op_mode;
	__le32 btag_op_mode;
	__le32 mac_table_macinmac_select;
	u8 iheader_action_enable;
	u8 tunnel_id_known_traffic_enable;
	u8 tunnel_id_unknown_traffic_enable;
	u8 b_dst_mac_from_mac_table_enable;
	u8 replace_b_src_mac_enable;
	u8 replace_b_dst_mac_enable;
	u8 replace_i_tag_res_enable;
	u8 replace_i_tag_uac_enable;
	u8 replace_i_tag_dei_enable;
	u8 replace_i_tag_pcp_enable;
	u8 replace_i_tag_sid_enable;
	u8 replace_i_tag_tpid_enable;
	u8 btag_action_enable;
	u8 process_id_known_traffic_enable;
	u8 process_id_unknown_traffic_enable;
	u8 replace_b_tag_dei_enable;
	u8 replace_b_tag_pcp_enable;
	u8 replace_b_tag_vid_enable;
	u8 replace_b_tag_tpid_enable;
	u8 mac_table_macinmac_action_enable;
} __packed;

static_assert(sizeof(struct mxl862xx_pce_action_pbb) == 36);

/**
 * struct mxl862xx_pce_action_dest_subif - Destination sub-interface ID
 *                                         action configuration
 *
 * @dest_subifid_action_enable: Destination sub-interface ID group field
 *                              action enable
 * @dest_subifid_assignment_enable: Destination sub-interface ID group field
 *                                  assignment enable
 * @dest_subifgrp_field: Destination sub-interface ID group field value,
 *                       or LAG index when trunking action is enabled
 */
struct mxl862xx_pce_action_dest_subif {
	u8 dest_subifid_action_enable;
	u8 dest_subifid_assignment_enable;
	__le16 dest_subifgrp_field;
} __packed;

static_assert(sizeof(struct mxl862xx_pce_action_dest_subif) == 4);

/**
 * enum mxl862xx_pce_action_portmap - Forwarding group action selector
 *
 * Selects how the packet is forwarded. Mutually exclusive with
 * the flow_id_action (bFlowID_Action in vendor API).
 *
 * @MXL862XX_PCE_ACTION_PORTMAP_DISABLE: Forwarding action is disabled
 * @MXL862XX_PCE_ACTION_PORTMAP_REGULAR: Use default port-map from
 *     forwarding classification
 * @MXL862XX_PCE_ACTION_PORTMAP_DISCARD: Discard the packet
 * @MXL862XX_PCE_ACTION_PORTMAP_CPU: Forward to the CPU port
 *     (as configured by GSW_CPU_PortCfgSet, typically the on-die
 *     microcontroller -- not the DSA CPU port)
 * @MXL862XX_PCE_ACTION_PORTMAP_ALTERNATIVE: Forward to an explicit
 *     portmap given by forward_port_map[]
 */
enum mxl862xx_pce_action_portmap {
	MXL862XX_PCE_ACTION_PORTMAP_DISABLE		= 0,
	MXL862XX_PCE_ACTION_PORTMAP_REGULAR		= 1,
	MXL862XX_PCE_ACTION_PORTMAP_DISCARD		= 2,
	MXL862XX_PCE_ACTION_PORTMAP_CPU			= 3,
	MXL862XX_PCE_ACTION_PORTMAP_ALTERNATIVE		= 4,
};

/**
 * enum mxl862xx_pce_action_cross_state - Cross state action selector
 *
 * Controls whether the packet ignores STP port-state filtering.
 *
 * @MXL862XX_PCE_ACTION_CROSS_STATE_DISABLE: Cross state action is disabled
 * @MXL862XX_PCE_ACTION_CROSS_STATE_REGULAR: Enabled; packet is treated
 *     as non-cross-state (does not ignore port-state filtering)
 * @MXL862XX_PCE_ACTION_CROSS_STATE_CROSS: Enabled; packet ignores
 *     port-state filtering rules (e.g. passes through BLOCKING state)
 */
enum mxl862xx_pce_action_cross_state {
	MXL862XX_PCE_ACTION_CROSS_STATE_DISABLE		= 0,
	MXL862XX_PCE_ACTION_CROSS_STATE_REGULAR		= 1,
	MXL862XX_PCE_ACTION_CROSS_STATE_CROSS		= 2,
};

/**
 * enum mxl862xx_pce_action_learning - MAC learning action selector
 *
 * Controls source address learning for packets matching the rule.
 *
 * @MXL862XX_PCE_ACTION_LEARNING_DISABLE: Learning action is disabled
 * @MXL862XX_PCE_ACTION_LEARNING_REGULAR: Enabled; learning follows the
 *     regular port and bridge configuration
 * @MXL862XX_PCE_ACTION_LEARNING_FORCE_NOT: Enabled; the source address
 *     of matching packets is never learned
 * @MXL862XX_PCE_ACTION_LEARNING_FORCE: Enabled; the source address of
 *     matching packets is always learned
 */
enum mxl862xx_pce_action_learning {
	MXL862XX_PCE_ACTION_LEARNING_DISABLE		= 0,
	MXL862XX_PCE_ACTION_LEARNING_REGULAR		= 1,
	MXL862XX_PCE_ACTION_LEARNING_FORCE_NOT		= 2,
	MXL862XX_PCE_ACTION_LEARNING_FORCE		= 3,
};

/**
 * struct mxl862xx_pce_action - PCE rule action configuration
 *
 * Defines the actions applied to packets matching a PCE rule pattern.
 *
 * @time_comp: Signed time compensation value for OAM delay measurement
 * @extended_vlan_block_id: Extended VLAN block allocated for this flow
 *                          entry (valid when @extended_vlan_enable is set)
 * @ins_ext_point: Insertion/extraction point
 * @ptp_seq_id: PTP sequence ID for PTP application
 * @pkt_update_offset: Byte offset (2..255) for counter/timestamp
 *                     insertion (used when @no_pkt_update and
 *                     @append_to_pkt are both false)
 * @oam_flow_id: Traffic flow counter ID for OAM loss measurement
 * @record_id: Record ID used by extraction and/or OAM process
 * @forward_port_map: Target portmap for forwarded packets. Each bit
 *                    represents one bridge port. Used when
 *                    @port_map_action is %MXL862XX_PCE_ACTION_PORTMAP_ALTERNATIVE.
 * @rmon_id: RMON counter ID (index starts from zero)
 * @svlan_id: Alternative STAG VLAN ID
 * @flow_id: Flow ID
 * @rout_ext_id: Routing extension ID value (8-bit range)
 * @traffic_class_alternate: Alternative traffic class (used when
 *                           @traffic_class_action selects alternate)
 * @meter_id: Meter ID
 * @vlan_id: Alternative CTAG VLAN ID
 * @fid: Alternative Filtering Identifier (FID)
 * @traffic_class_action: Traffic class action selector
 *                        (0 = disable, 1 = regular CoS, 2 = alternative)
 * @snooping_type_action: IGMP snooping control selector
 * @learning_action: MAC learning action selector.
 *     See &enum mxl862xx_pce_action_learning
 * @irq_action: Interrupt action selector
 *              (0 = disable, 1 = regular, 2 = generate interrupt)
 * @cross_state_action: Cross state action selector.
 *     See &enum mxl862xx_pce_action_cross_state
 * @crit_frame_action: Critical frame action selector
 *                     (0 = disable, 1 = regular, 2 = critical)
 * @color_frame_action: Color frame action selector (replaces
 *                      @crit_frame_action in GSWIP-3.1)
 * @timestamp_action: Timestamp action selector
 *                    (0 = disable, 1 = regular, 2 = store timestamps)
 * @port_map_action: Forwarding portmap action selector.
 *     See &enum mxl862xx_pce_action_portmap
 * @meter_action: Meter action selector
 *                (0 = disable, 1 = regular, 2 = meter 1,
 *                3 = meter 1 and 2)
 * @vlan_action: CTAG VLAN action selector
 *              (0 = disable, 1 = regular, 2 = alternative)
 * @svlan_action: STAG VLAN action selector
 *               (0 = disable, 1 = regular, 2 = alternative)
 * @vlan_cross_action: Cross-VLAN action selector
 *                     (0 = disable, 1 = regular, 2 = cross-VLAN)
 * @process_path_action: MPE processing path assignment
 *                       (0 = unused, 1 = path-1, 2 = path-2, 3 = both)
 * @port_filter_type_action: Port filter action type (0..6)
 * @pbb_action: Provider Backbone Bridging (Mac-in-Mac) action.
 *     See &struct mxl862xx_pce_action_pbb
 * @dest_subif_action: Destination sub-interface ID action.
 *     See &struct mxl862xx_pce_action_dest_subif
 * @remark_action: Enable remarking action
 * @remark_pcp: Enable CTAG VLAN PCP remarking
 * @remark_stag_pcp: Enable STAG VLAN PCP remarking
 * @remark_stag_dei: Enable STAG VLAN DEI remarking
 * @remark_dscp: Enable DSCP remarking
 * @remark_class: Enable class remarking
 * @rmon_action: Enable RMON counter action
 * @fid_enable: Enable alternative FID
 * @extended_vlan_enable: Enable extended VLAN operation for matching
 *                        traffic
 * @cvlan_ignore_control: CVLAN ignore control
 * @port_bitmap_mux_control: Port bitmap mux control
 * @port_trunk_action: Enable trunking action
 * @port_link_selection: Port link selection control
 * @flow_id_action: Enable flow ID action (mutually exclusive with
 *                  @port_map_action)
 * @rout_ext_id_action: Enable routing extension ID action
 * @rt_dst_port_mask_cmp_action: Routing destination port mask comparison
 * @rt_src_port_mask_cmp_action: Routing source port mask comparison
 * @rt_dst_ip_mask_cmp_action: Routing destination IP mask comparison
 * @rt_src_ip_mask_cmp_action: Routing source IP mask comparison
 * @rt_inner_ip_as_key_action: Use inner IP in tunneled header as
 *                             routing key
 * @rt_accel_ena_action: Routing acceleration enable
 * @rt_ctrl_ena_action: Routing control enable (selects routing
 *                      accelerate action)
 * @extract_enable: Enable packet extraction at point defined by
 *                  @record_id
 * @oam_enable: Enable OAM processing for matching packets
 * @pce_bypass_path: Update packet in PCE bypass path (after QoS queue)
 * @tx_flow_cnt: Use TX flow counter (otherwise RX flow counter)
 * @time_format: Timestamp format (0 = digital 10B, 1 = binary 10B,
 *               2 = digital 8B, 3 = binary 8B)
 * @no_pkt_update: Do not update packet
 * @append_to_pkt: Append counter/timestamp to end of packet (when
 *                 @no_pkt_update is false)
 * @pbb_action_enable: Enable PBB action. See &struct mxl862xx_pce_action_pbb
 * @dest_subif_action_enable: Enable destination sub-interface ID action.
 *     See &struct mxl862xx_pce_action_dest_subif
 */
struct mxl862xx_pce_action {
	__le64 time_comp;
	__le16 extended_vlan_block_id;
	u8 ins_ext_point;
	u8 ptp_seq_id;
	__le16 pkt_update_offset;
	__le16 oam_flow_id;
	__le16 record_id;
	__le16 forward_port_map[8];
	__le16 rmon_id;
	__le16 svlan_id;
	__le16 flow_id;
	__le16 rout_ext_id;
	u8 traffic_class_alternate;
	u8 meter_id;
	u8 vlan_id;
	u8 fid;
	__le32 traffic_class_action;
	__le32 snooping_type_action;
	__le32 learning_action;
	__le32 irq_action;
	__le32 cross_state_action;
	__le32 crit_frame_action;
	__le32 color_frame_action;
	__le32 timestamp_action;
	__le32 port_map_action;
	__le32 meter_action;
	__le32 vlan_action;
	__le32 svlan_action;
	__le32 vlan_cross_action;
	__le32 process_path_action;
	__le32 port_filter_type_action;
	struct mxl862xx_pce_action_pbb pbb_action;
	struct mxl862xx_pce_action_dest_subif dest_subif_action;
	u8 remark_action;
	u8 remark_pcp;
	u8 remark_stag_pcp;
	u8 remark_stag_dei;
	u8 remark_dscp;
	u8 remark_class;
	u8 rmon_action;
	u8 fid_enable;
	u8 extended_vlan_enable;
	u8 cvlan_ignore_control;
	u8 port_bitmap_mux_control;
	u8 port_trunk_action;
	u8 port_link_selection;
	u8 flow_id_action;
	u8 rout_ext_id_action;
	u8 rt_dst_port_mask_cmp_action;
	u8 rt_src_port_mask_cmp_action;
	u8 rt_dst_ip_mask_cmp_action;
	u8 rt_src_ip_mask_cmp_action;
	u8 rt_inner_ip_as_key_action;
	u8 rt_accel_ena_action;
	u8 rt_ctrl_ena_action;
	u8 extract_enable;
	u8 oam_enable;
	u8 pce_bypass_path;
	u8 tx_flow_cnt;
	__le32 time_format;
	u8 no_pkt_update;
	u8 append_to_pkt;
	u8 pbb_action_enable;
	u8 dest_subif_action_enable;
} __packed;

static_assert(sizeof(struct mxl862xx_pce_action) == 180);

/**
 * enum mxl862xx_pce_rule_region - PCE rule table region selector
 *
 * Selects which region of the traffic flow table the rule belongs to.
 *
 * @MXL862XX_PCE_RULE_COMMON: Common region shared by all CTPs
 *     (global rules, indices 0..63)
 * @MXL862XX_PCE_RULE_CTP: Per-CTP region. The rule index is relative
 *     to the CTP block identified by logicalportid; the firmware
 *     translates it to an absolute hardware index.
 * @MXL862XX_PCE_RULE_DEBUG: Debug region with direct HW index mapping
 */
enum mxl862xx_pce_rule_region {
	MXL862XX_PCE_RULE_COMMON	= 0,
	MXL862XX_PCE_RULE_CTP		= 1,
	MXL862XX_PCE_RULE_DEBUG		= 2,
};

/**
 * struct mxl862xx_pce_rule - PCE rule configuration
 * @logicalportid: Logical Port Id
 * @subifidgroup: Sub-interface ID group
 * @region: PCE rule region (common or per-CTP)
 * @pattern: Match pattern (destination MAC, EtherType, etc.)
 * @action: Forwarding action (portmap, cross-state, etc.)
 *
 * This structure is passed to the firmware via the MDIO data
 * buffer using the %MXL862XX_TFLOW_PCERULELOGICWRITE command.
 * The binary layout must exactly match the firmware's
 * GSW_PCE_rule_t (466 bytes, packed, little-endian scalars).
 */
struct mxl862xx_pce_rule {
	u8 logicalportid;
	__le16 subifidgroup;
	__le32 region;
	struct mxl862xx_pce_pattern pattern;
	struct mxl862xx_pce_action action;
} __packed;

static_assert(sizeof(struct mxl862xx_pce_rule) == 466);

/**
 * struct mxl862xx_pce_rule_alloc - PCE rule block allocation
 * @num_of_rules: Number of rules to allocate (input) / allocated (output).
 *   The firmware rounds up to a multiple of four consecutive entries.
 * @blockid: Starting rule index of the allocated block (output on alloc,
 *   input on free).
 *
 * Used with %MXL862XX_TFLOW_PCERULEALLOC and %MXL862XX_TFLOW_PCERULEFREE.
 * Maps to the firmware's ``GSW_PCE_rule_alloc_t``.
 */
struct mxl862xx_pce_rule_alloc {
	__le16 num_of_rules;
	__le16 blockid;
} __packed;

static_assert(sizeof(struct mxl862xx_pce_rule_alloc) == 4);

/**
 * enum mxl862xx_stp_port_state - Spanning Tree Protocol port states
 * @MXL862XX_STP_PORT_STATE_FORWARD: Forwarding state
 * @MXL862XX_STP_PORT_STATE_DISABLE: Disabled/Discarding state
 * @MXL862XX_STP_PORT_STATE_LEARNING: Learning state
 * @MXL862XX_STP_PORT_STATE_BLOCKING: Blocking/Listening
 */
enum mxl862xx_stp_port_state {
	MXL862XX_STP_PORT_STATE_FORWARD = 0,
	MXL862XX_STP_PORT_STATE_DISABLE,
	MXL862XX_STP_PORT_STATE_LEARNING,
	MXL862XX_STP_PORT_STATE_BLOCKING,
};

/**
 * struct mxl862xx_stp_port_cfg - Configures the Spanning Tree Protocol state
 * @port_id: Port number
 * @fid: Filtering Identifier (FID)
 * @port_state: See &enum mxl862xx_stp_port_state
 */
struct mxl862xx_stp_port_cfg {
	__le16 port_id;
	__le16 fid;
	__le32 port_state; /* enum mxl862xx_stp_port_state */
} __packed;

/**
 * struct mxl862xx_sys_fw_image_version - Firmware version information
 * @iv_major: firmware major version
 * @iv_minor: firmware minor version
 * @iv_revision: firmware revision
 * @iv_build_num: firmware build number
 */
struct mxl862xx_sys_fw_image_version {
	u8 iv_major;
	u8 iv_minor;
	__le16 iv_revision;
	__le32 iv_build_num;
} __packed;

/**
 * struct mxl862xx_sys_reg_rw - System register read/write
 * @addr: 32-bit register address
 * @val: register value
 */
struct mxl862xx_sys_reg_rw {
	__le32 addr;
	__le32 val;
} __packed;

/**
 * enum mxl862xx_port_type - Port Type
 * @MXL862XX_LOGICAL_PORT: Logical Port
 * @MXL862XX_PHYSICAL_PORT: Physical Port
 * @MXL862XX_CTP_PORT: Connectivity Termination Port (CTP)
 * @MXL862XX_BRIDGE_PORT: Bridge Port
 */
enum mxl862xx_port_type {
	MXL862XX_LOGICAL_PORT = 0,
	MXL862XX_PHYSICAL_PORT,
	MXL862XX_CTP_PORT,
	MXL862XX_BRIDGE_PORT,
};

/**
 * enum mxl862xx_rmon_port_type - RMON counter table type
 * @MXL862XX_RMON_CTP_PORT_RX: CTP RX counters
 * @MXL862XX_RMON_CTP_PORT_TX: CTP TX counters
 * @MXL862XX_RMON_BRIDGE_PORT_RX: Bridge port RX counters
 * @MXL862XX_RMON_BRIDGE_PORT_TX: Bridge port TX counters
 * @MXL862XX_RMON_CTP_PORT_PCE_BYPASS: CTP PCE bypass counters
 * @MXL862XX_RMON_TFLOW_RX: TFLOW RX counters
 * @MXL862XX_RMON_TFLOW_TX: TFLOW TX counters
 * @MXL862XX_RMON_QMAP: QMAP counters
 * @MXL862XX_RMON_METER: Meter counters
 * @MXL862XX_RMON_PMAC: PMAC counters
 */
enum mxl862xx_rmon_port_type {
	MXL862XX_RMON_CTP_PORT_RX = 0,
	MXL862XX_RMON_CTP_PORT_TX,
	MXL862XX_RMON_BRIDGE_PORT_RX,
	MXL862XX_RMON_BRIDGE_PORT_TX,
	MXL862XX_RMON_CTP_PORT_PCE_BYPASS,
	MXL862XX_RMON_TFLOW_RX,
	MXL862XX_RMON_TFLOW_TX,
	MXL862XX_RMON_QMAP = 0x0e,
	MXL862XX_RMON_METER = 0x19,
	MXL862XX_RMON_PMAC = 0x1c,
};

/**
 * struct mxl862xx_rmon_port_cnt - RMON counters for a port
 * @port_type: Port type for counter retrieval (see &enum mxl862xx_port_type)
 * @port_id: Ethernet port number (zero-based)
 * @sub_if_id_group: Sub-interface ID group
 * @pce_bypass: Separate CTP Tx counters when PCE is bypassed
 * @rx_extended_vlan_discard_pkts: Discarded at extended VLAN operation
 * @mtu_exceed_discard_pkts: Discarded due to MTU exceeded
 * @tx_under_size_good_pkts: Tx undersize (<64) packet count
 * @tx_oversize_good_pkts: Tx oversize (>1518) packet count
 * @rx_good_pkts: Received good packet count
 * @rx_unicast_pkts: Received unicast packet count
 * @rx_broadcast_pkts: Received broadcast packet count
 * @rx_multicast_pkts: Received multicast packet count
 * @rx_fcserror_pkts: Received FCS error packet count
 * @rx_under_size_good_pkts: Received undersize good packet count
 * @rx_oversize_good_pkts: Received oversize good packet count
 * @rx_under_size_error_pkts: Received undersize error packet count
 * @rx_good_pause_pkts: Received good pause packet count
 * @rx_oversize_error_pkts: Received oversize error packet count
 * @rx_align_error_pkts: Received alignment error packet count
 * @rx_filtered_pkts: Filtered packet count
 * @rx64byte_pkts: Received 64-byte packet count
 * @rx127byte_pkts: Received 65-127 byte packet count
 * @rx255byte_pkts: Received 128-255 byte packet count
 * @rx511byte_pkts: Received 256-511 byte packet count
 * @rx1023byte_pkts: Received 512-1023 byte packet count
 * @rx_max_byte_pkts: Received 1024-max byte packet count
 * @tx_good_pkts: Transmitted good packet count
 * @tx_unicast_pkts: Transmitted unicast packet count
 * @tx_broadcast_pkts: Transmitted broadcast packet count
 * @tx_multicast_pkts: Transmitted multicast packet count
 * @tx_single_coll_count: Transmit single collision count
 * @tx_mult_coll_count: Transmit multiple collision count
 * @tx_late_coll_count: Transmit late collision count
 * @tx_excess_coll_count: Transmit excessive collision count
 * @tx_coll_count: Transmit collision count
 * @tx_pause_count: Transmit pause packet count
 * @tx64byte_pkts: Transmitted 64-byte packet count
 * @tx127byte_pkts: Transmitted 65-127 byte packet count
 * @tx255byte_pkts: Transmitted 128-255 byte packet count
 * @tx511byte_pkts: Transmitted 256-511 byte packet count
 * @tx1023byte_pkts: Transmitted 512-1023 byte packet count
 * @tx_max_byte_pkts: Transmitted 1024-max byte packet count
 * @tx_dropped_pkts: Transmit dropped packet count
 * @tx_acm_dropped_pkts: Transmit ACM dropped packet count
 * @rx_dropped_pkts: Received dropped packet count
 * @rx_good_bytes: Received good byte count (64-bit)
 * @rx_bad_bytes: Received bad byte count (64-bit)
 * @tx_good_bytes: Transmitted good byte count (64-bit)
 */
struct mxl862xx_rmon_port_cnt {
	__le32 port_type; /* enum mxl862xx_port_type */
	__le16 port_id;
	__le16 sub_if_id_group;
	u8 pce_bypass;
	__le32 rx_extended_vlan_discard_pkts;
	__le32 mtu_exceed_discard_pkts;
	__le32 tx_under_size_good_pkts;
	__le32 tx_oversize_good_pkts;
	__le32 rx_good_pkts;
	__le32 rx_unicast_pkts;
	__le32 rx_broadcast_pkts;
	__le32 rx_multicast_pkts;
	__le32 rx_fcserror_pkts;
	__le32 rx_under_size_good_pkts;
	__le32 rx_oversize_good_pkts;
	__le32 rx_under_size_error_pkts;
	__le32 rx_good_pause_pkts;
	__le32 rx_oversize_error_pkts;
	__le32 rx_align_error_pkts;
	__le32 rx_filtered_pkts;
	__le32 rx64byte_pkts;
	__le32 rx127byte_pkts;
	__le32 rx255byte_pkts;
	__le32 rx511byte_pkts;
	__le32 rx1023byte_pkts;
	__le32 rx_max_byte_pkts;
	__le32 tx_good_pkts;
	__le32 tx_unicast_pkts;
	__le32 tx_broadcast_pkts;
	__le32 tx_multicast_pkts;
	__le32 tx_single_coll_count;
	__le32 tx_mult_coll_count;
	__le32 tx_late_coll_count;
	__le32 tx_excess_coll_count;
	__le32 tx_coll_count;
	__le32 tx_pause_count;
	__le32 tx64byte_pkts;
	__le32 tx127byte_pkts;
	__le32 tx255byte_pkts;
	__le32 tx511byte_pkts;
	__le32 tx1023byte_pkts;
	__le32 tx_max_byte_pkts;
	__le32 tx_dropped_pkts;
	__le32 tx_acm_dropped_pkts;
	__le32 rx_dropped_pkts;
	__le64 rx_good_bytes;
	__le64 rx_bad_bytes;
	__le64 tx_good_bytes;
} __packed;

/* XPCS interface mode, MXL862XX_XPCS_*_INTERFACE field values */
#define MXL862XX_XPCS_IF_SGMII		0
#define MXL862XX_XPCS_IF_1000BASEX	1
#define MXL862XX_XPCS_IF_2500BASEX	2
#define MXL862XX_XPCS_IF_USXGMII	3	/* single or quad */
#define MXL862XX_XPCS_IF_10GBASER	4
#define MXL862XX_XPCS_IF_10GKR		5	/* 10GBASE-KR */
#define MXL862XX_XPCS_IF_5GBASER	6
#define MXL862XX_XPCS_IF_QSGMII		7

/* PCS negotiation mode, MXL862XX_XPCS_CFG_NEG_MODE field values */
#define MXL862XX_XPCS_NEG_NONE		0	/* no inband negotiation */
#define MXL862XX_XPCS_NEG_INBAND_AN_OFF	1	/* inband, AN disabled */
#define MXL862XX_XPCS_NEG_INBAND_AN_ON	2	/* inband, AN enabled */

/*
 * PCS protocol role, MXL862XX_XPCS_CFG_ROLE field value. Selects the role
 * the XPCS plays in protocols with an asymmetric AN code word (Cisco SGMII
 * / QSGMII / USXGMII), driving VR_MII_AN_CTRL.TX_CONFIG: MAC means the
 * local end receives the partner's AN word, PHY means it sources one.
 * Ignored for symmetric protocols (1000BASE-X, 2500BASE-X, 10GBASE-R/KR).
 */
#define MXL862XX_XPCS_ROLE_MAC		0	/* local end is MAC side */
#define MXL862XX_XPCS_ROLE_PHY		1	/* local end is PHY side */

/* USXGMII lane mode, MXL862XX_XPCS_*_USX_LANE_MODE field values */
#define MXL862XX_XPCS_USX_SINGLE	0	/* single USXGMII lane */
#define MXL862XX_XPCS_USX_QUAD		1	/* quad USXGMII, 4 ports/lane */

/**
 * union mxl862xx_xpcs_an_word - XPCS AN code word, tagged by interface mode
 * @cl37: 16-bit base-page word exchanged over the CL37 hardware AN path
 *        (SR_MII_AN_ADV on write, SR_MII_LP_BABL on read). Carries the
 *        802.3 CL37 base page for 1000BASE-X/2500BASE-X and the Cisco
 *        SGMII config word for SGMII/QSGMII.
 * @usx: USXGMII 16-bit AN code word, MDIO_USXGMII_* layout
 * @cl73: CL73 48-bit base page (10GBASE-KR), three 16-bit registers per
 *        802.3 Annex 28C
 * @cl73.adv1: CL73 SR_AN_ADV1 / SR_AN_LP_ABL1
 * @cl73.adv2: CL73 SR_AN_ADV2 / SR_AN_LP_ABL2
 * @cl73.adv3: CL73 SR_AN_ADV3 / SR_AN_LP_ABL3
 *
 * The host picks the right member based on the interface field of the
 * surrounding struct (and, for the asymmetric protocols, on the role).
 */
union mxl862xx_xpcs_an_word {
	__le16 cl37;
	__le16 usx;
	struct {
		__le16 adv1;
		__le16 adv2;
		__le16 adv3;
	} cl73;
} __packed;

/* PCS duplex mode, MXL862XX_XPCS_*_DUPLEX field values */
#define MXL862XX_XPCS_DUPLEX_HALF	0
#define MXL862XX_XPCS_DUPLEX_FULL	1

/**
 * enum mxl862xx_xpcs_loopback_mode - XPCS loopback mode
 * @MXL862XX_XPCS_LB_DISABLE: disable all loopback
 * @MXL862XX_XPCS_LB_PCS_SERIAL: PCS TX-to-RX serial loopback
 * @MXL862XX_XPCS_LB_PCS_PARALLEL: PCS RX-to-TX parallel loopback
 * @MXL862XX_XPCS_LB_PMA_SERIAL: PMA TX-to-RX serial loopback
 * @MXL862XX_XPCS_LB_PMA_PARALLEL: PMA RX-to-TX parallel loopback
 */
enum mxl862xx_xpcs_loopback_mode {
	MXL862XX_XPCS_LB_DISABLE = 0,
	MXL862XX_XPCS_LB_PCS_SERIAL = 1,
	MXL862XX_XPCS_LB_PCS_PARALLEL = 2,
	MXL862XX_XPCS_LB_PMA_SERIAL = 3,
	MXL862XX_XPCS_LB_PMA_PARALLEL = 4,
};

/* Fields of mxl862xx_xpcs_pcs_cfg.mode */
#define MXL862XX_XPCS_CFG_PORT_ID	GENMASK(1, 0)
#define MXL862XX_XPCS_CFG_INTERFACE	GENMASK(7, 2)
#define MXL862XX_XPCS_CFG_NEG_MODE	GENMASK(9, 8)
#define MXL862XX_XPCS_CFG_PERMIT_PAUSE	BIT(10)
#define MXL862XX_XPCS_CFG_USX_LANE_MODE	GENMASK(12, 11)
#define MXL862XX_XPCS_CFG_ROLE		BIT(13)
#define MXL862XX_XPCS_CFG_USX_SUBPORT	GENMASK(15, 14)

/**
 * struct mxl862xx_xpcs_pcs_cfg - PCS configuration parameters
 * @mode: Packed interface and negotiation parameters, see
 *        MXL862XX_XPCS_CFG_*. port_id is the XPCS port index (0-3);
 *        interface is the PCS interface mode (MXL862XX_XPCS_IF_*);
 *        neg_mode is the negotiation mode (MXL862XX_XPCS_NEG_*);
 *        permit_pause allows pause to MAC; usx_lane_mode is the USXGMII
 *        lane mode (MXL862XX_XPCS_USX_*); role is the protocol role
 *        (MXL862XX_XPCS_ROLE_*); usx_subport is the sub-port (0-3) within
 *        the XPCS -- despite the name it also identifies the QSGMII
 *        sub-port -- used by the firmware to set MAC pause per sub-port
 *        and ignored for the XPCS-wide bringup, which is idempotent across
 *        slots.
 * @advertising: AN code word the local end transmits. The active union
 *               member is selected by the interface field (and, for the
 *               asymmetric protocols, by role). Ignored when the local end
 *               does not transmit an AN word (role=MAC for SGMII/QSGMII/
 *               USXGMII, 10GBASE-R, 5GBASE-R) or when neg_mode is not
 *               INBAND_AN_ON. Pass all-zero to keep the firmware default
 *               advertisement.
 * @result: Firmware result. >0 means the host must follow with an AN
 *          restart, 0 means no host follow-up is needed, <0 is an errno.
 */
struct mxl862xx_xpcs_pcs_cfg {
	__le16 mode;
	union mxl862xx_xpcs_an_word advertising;
	__le16 result;
} __packed;

/* Fields of mxl862xx_xpcs_pcs_state.mode */
#define MXL862XX_XPCS_ST_PORT_ID	GENMASK(1, 0)
#define MXL862XX_XPCS_ST_INTERFACE	GENMASK(7, 2)
#define MXL862XX_XPCS_ST_USX_LANE_MODE	GENMASK(9, 8)
#define MXL862XX_XPCS_ST_USX_SUBPORT	GENMASK(11, 10)
#define MXL862XX_XPCS_ST_LINK		BIT(12)
#define MXL862XX_XPCS_ST_AN_COMPLETE	BIT(13)
#define MXL862XX_XPCS_ST_DUPLEX		BIT(14)
#define MXL862XX_XPCS_ST_PCS_FAULT	BIT(15)
#define MXL862XX_XPCS_ST_PAUSE		GENMASK(17, 16)
#define MXL862XX_XPCS_ST_LP_EEE_CAP	BIT(18)
#define MXL862XX_XPCS_ST_LP_EEE_CS_CAP	BIT(19)

/**
 * struct mxl862xx_xpcs_pcs_state - PCS link state
 * @mode: Packed input parameters and firmware status, see
 *        MXL862XX_XPCS_ST_*. The host writes port_id (XPCS port index 0-3),
 *        interface (MXL862XX_XPCS_IF_*), usx_lane_mode
 *        (MXL862XX_XPCS_USX_*) and usx_subport (0-3); the firmware fills in
 *        link, an_complete, duplex (MXL862XX_XPCS_DUPLEX_*), pcs_fault,
 *        pause (bit 0 symmetric, bit 1 asymmetric), lp_eee_cap and
 *        lp_eee_cs_cap.
 * @speed: Resolved speed in Mbit/s (output)
 * @lpa: Link partner ability word (output). Same union as
 *       &union mxl862xx_xpcs_an_word; the host picks the member based on
 *       the interface field.
 */
struct mxl862xx_xpcs_pcs_state {
	__le32 mode;
	__le16 speed; /* Mbit/s */
	union mxl862xx_xpcs_an_word lpa;
} __packed;

/**
 * struct mxl862xx_xpcs_pcs_disable - PCS disable parameters
 * @port_id: XPCS port index
 * @__pad: padding
 * @result: Firmware result. 0 on success, <0 on error.
 *
 * Asserts IDDQ + PHY + XPCS resets to power down the SERDES when the
 * port is admin-down or no module is plugged in. The next PCS config
 * implicitly powers it back up and reprograms the desired interface.
 */
struct mxl862xx_xpcs_pcs_disable {
	u8 port_id;
	u8 __pad;
	__le16 result;
} __packed;

/* Fields of mxl862xx_xpcs_an_restart.mode */
#define MXL862XX_XPCS_ANR_PORT_ID	GENMASK(1, 0)
#define MXL862XX_XPCS_ANR_INTERFACE	GENMASK(7, 2)
#define MXL862XX_XPCS_ANR_USX_LANE_MODE	GENMASK(9, 8)
#define MXL862XX_XPCS_ANR_USX_SUBPORT	GENMASK(11, 10)

/**
 * struct mxl862xx_xpcs_an_restart - AN restart parameters
 * @mode: Packed input parameters, see MXL862XX_XPCS_ANR_*. port_id is the
 *        XPCS port index (0-3); interface is the PCS interface mode
 *        (MXL862XX_XPCS_IF_*); usx_lane_mode is the USX lane mode
 *        (MXL862XX_XPCS_USX_*); usx_subport (0-3) selects the lane whose
 *        AN is restarted for QSGMII and QUSXGMII and is ignored by
 *        single-lane modes.
 * @result: Firmware result. 0 on success, <0 on error.
 *
 * Restarts auto-negotiation on a single sub-port of the XPCS. The
 * SERDES must already be configured.
 */
struct mxl862xx_xpcs_an_restart {
	__le16 mode;
	__le16 result;
} __packed;

/* Fields of mxl862xx_xpcs_pcs_link_up.mode */
#define MXL862XX_XPCS_LU_PORT_ID	GENMASK(1, 0)
#define MXL862XX_XPCS_LU_INTERFACE	GENMASK(7, 2)
#define MXL862XX_XPCS_LU_DUPLEX		BIT(8)
#define MXL862XX_XPCS_LU_USX_LANE_MODE	GENMASK(10, 9)
#define MXL862XX_XPCS_LU_USX_SUBPORT	GENMASK(12, 11)

/**
 * struct mxl862xx_xpcs_pcs_link_up - PCS link-up parameters
 * @mode: Packed input parameters, see MXL862XX_XPCS_LU_*. port_id is the
 *        XPCS port index (0-3); interface is the PCS interface mode
 *        (MXL862XX_XPCS_IF_*); duplex is the duplex mode
 *        (MXL862XX_XPCS_DUPLEX_*); usx_lane_mode is the USX lane mode
 *        (USXGMII only, ignored otherwise, MXL862XX_XPCS_USX_*);
 *        usx_subport (0-3) selects the sub-port for QUSXGMII and QSGMII
 *        (despite the name) and is ignored otherwise.
 * @speed: Resolved speed in Mbit/s
 * @result: Firmware result. 0 on success, <0 is errno.
 *
 * Called once per link-up event after the host has resolved the
 * line-side speed/duplex (from the PHY's read_status, from a preceding
 * PCS get-state, or from a fixed-link description).
 */
struct mxl862xx_xpcs_pcs_link_up {
	__le16 mode;
	__le16 speed; /* Mbit/s */
	__le16 result;
} __packed;

#endif /* __MXL862XX_API_H */
