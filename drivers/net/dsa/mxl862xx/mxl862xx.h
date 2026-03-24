/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_H
#define __MXL862XX_H

#include <asm/byteorder.h>
#include <linux/mdio.h>
#include <linux/workqueue.h>
#include <net/dsa.h>

#define MXL862XX_MAX_PORTS		17
#define MXL862XX_DEFAULT_BRIDGE		0
#define MXL862XX_MAX_BRIDGES		48
#define MXL862XX_MAX_BRIDGE_PORTS	128
#define MXL862XX_TOTAL_EVLAN_ENTRIES	1024
#define MXL862XX_TOTAL_VF_ENTRIES	1024
#define MXL862XX_MAX_LAG_IDS		16

/* Trunk hash field bitmask (matches PCE_TRUNK_CONF layout) */
#define MXL862XX_TRUNK_HASH_SA		BIT(0)
#define MXL862XX_TRUNK_HASH_DA		BIT(1)
#define MXL862XX_TRUNK_HASH_SIP	BIT(2)
#define MXL862XX_TRUNK_HASH_DIP	BIT(3)
#define MXL862XX_TRUNK_HASH_SPORT	BIT(4)
#define MXL862XX_TRUNK_HASH_DPORT	BIT(5)

/* P-mapper LAG entries occupy indices 9..72 (64 entries) */
#define MXL862XX_PMAPPER_LAG_FIRST	9
#define MXL862XX_PMAPPER_LAG_COUNT	64

/* Number of __le16 words in a firmware portmap (128-bit bitmap). */
#define MXL862XX_FW_PORTMAP_WORDS	(MXL862XX_MAX_BRIDGE_PORTS / 16)

struct mxl862xx_priv;

/**
 * mxl862xx_fw_portmap_from_bitmap - convert a kernel bitmap to a firmware
 *                                   portmap (__le16[8])
 * @dst: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 * @src: kernel bitmap of at least MXL862XX_MAX_BRIDGE_PORTS bits
 */
static inline void
mxl862xx_fw_portmap_from_bitmap(__le16 *dst, const unsigned long *src)
{
	int i;

	for (i = 0; i < MXL862XX_FW_PORTMAP_WORDS; i++)
		dst[i] = cpu_to_le16(bitmap_read(src, i * 16, 16));
}

/**
 * mxl862xx_fw_portmap_to_bitmap - convert a firmware portmap (__le16[8]) to
 *                                 a kernel bitmap
 * @dst: kernel bitmap of at least MXL862XX_MAX_BRIDGE_PORTS bits
 * @src: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 */
static inline void
mxl862xx_fw_portmap_to_bitmap(unsigned long *dst, const __le16 *src)
{
	int i;

	bitmap_zero(dst, MXL862XX_MAX_BRIDGE_PORTS);
	for (i = 0; i < MXL862XX_FW_PORTMAP_WORDS; i++)
		bitmap_write(dst, le16_to_cpu(src[i]), i * 16, 16);
}

/**
 * mxl862xx_fw_portmap_set_bit - set a single port bit in a firmware portmap
 * @map: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 * @port: port index (0..MXL862XX_MAX_BRIDGE_PORTS-1)
 */
static inline void mxl862xx_fw_portmap_set_bit(__le16 *map, int port)
{
	map[port / 16] |= cpu_to_le16(BIT(port % 16));
}

/**
 * mxl862xx_fw_portmap_clear_bit - clear a single port bit in a firmware portmap
 * @map: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 * @port: port index (0..MXL862XX_MAX_BRIDGE_PORTS-1)
 */
static inline void mxl862xx_fw_portmap_clear_bit(__le16 *map, int port)
{
	map[port / 16] &= ~cpu_to_le16(BIT(port % 16));
}

/**
 * mxl862xx_fw_portmap_is_empty - check whether a firmware portmap has no
 *                                bits set
 * @map: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 *
 * Return: true if every word in @map is zero.
 */
static inline bool mxl862xx_fw_portmap_is_empty(const __le16 *map)
{
	int i;

	for (i = 0; i < MXL862XX_FW_PORTMAP_WORDS; i++)
		if (map[i])
			return false;
	return true;
}

/**
 * struct mxl862xx_vf_vid - Per-VID entry within a VLAN Filter block
 * @list:     Linked into &mxl862xx_vf_block.vids
 * @vid:      VLAN ID
 * @index:    Entry index within the VLAN Filter HW block
 * @untagged: Strip tag on egress for this VID (drives EVLAN tag-stripping)
 */
struct mxl862xx_vf_vid {
	struct list_head list;
	u16 vid;
	u16 index;
	bool untagged;
};

/**
 * struct mxl862xx_vf_block - Per-port VLAN Filter block
 * @allocated:    Whether the HW block has been allocated via VLANFILTER_ALLOC
 * @block_id:     HW VLAN Filter block ID from VLANFILTER_ALLOC
 * @block_size:   Total entries allocated in this block
 * @active_count: Number of ALLOW entries at indices [0, active_count).
 *                The bridge port config sends max(active_count, 1) as
 *                block_size to narrow the HW scan window.
 *                discard_unmatched_tagged handles frames outside this range.
 * @vids:         List of &mxl862xx_vf_vid entries programmed in this block
 */
struct mxl862xx_vf_block {
	bool allocated;
	u16 block_id;
	u16 block_size;
	u16 active_count;
	struct list_head vids;
};

/**
 * struct mxl862xx_evlan_block - Per-port per-direction extended VLAN block
 * @allocated:  Whether the HW block has been allocated via EXTENDEDVLAN_ALLOC.
 *              Guards alloc/free idempotency—the block_id is only valid
 *              while allocated is true.
 * @in_use:     Whether the EVLAN engine should be enabled for this block
 *              on the bridge port (sent as the enable flag in
 *              set_bridge_port). Can be false while allocated is still
 *              true -- e.g. when all egress VIDs are removed (idx == 0 in
 *              evlan_program_egress) the block stays allocated for
 *              potential reuse, but the engine is disabled so an empty
 *              rule set does not discard all traffic.
 * @block_id:   HW block ID from EXTENDEDVLAN_ALLOC
 * @block_size: Total entries allocated
 * @n_active:   Number of HW entries currently written.  The bridge port
 *              config sends this as the egress scan window, so entries
 *              beyond n_active are never scanned.  Always equals
 *              block_size for ingress blocks (fixed catchall rules).
 */
struct mxl862xx_evlan_block {
	bool allocated;
	bool in_use;
	u16 block_id;
	u16 block_size;
	u16 n_active;
};

/**
 * struct mxl862xx_port_stats - 64-bit accumulated hardware port statistics
 *
 * The firmware RMON counters are 32-bit free-running (64-bit for byte
 * counters).  This structure holds 64-bit accumulators alongside the
 * previous raw snapshot so that deltas can be computed across polls,
 * handling 32-bit wrap correctly via unsigned subtraction.
 */
struct mxl862xx_port_stats {
	/* 64-bit accumulators */
	u64 rx_packets;
	u64 tx_packets;
	u64 rx_bytes;
	u64 tx_bytes;
	u64 rx_errors;
	u64 tx_errors;
	u64 rx_dropped;
	u64 tx_dropped;
	u64 multicast;
	u64 collisions;
	u64 rx_length_errors;
	u64 rx_crc_errors;
	u64 rx_frame_errors;
	/* Previous raw RMON values for delta computation */
	u32 prev_rx_good_pkts;
	u32 prev_tx_good_pkts;
	u64 prev_rx_good_bytes;
	u64 prev_tx_good_bytes;
	u32 prev_rx_fcserror_pkts;
	u32 prev_rx_under_size_error_pkts;
	u32 prev_rx_oversize_error_pkts;
	u32 prev_rx_align_error_pkts;
	u32 prev_tx_dropped_pkts;
	u32 prev_rx_dropped_pkts;
	u32 prev_rx_evlan_discard_pkts;
	u32 prev_mtu_exceed_discard_pkts;
	u32 prev_tx_acm_dropped_pkts;
	u32 prev_rx_multicast_pkts;
	u32 prev_tx_coll_count;
};

/**
 * struct mxl862xx_port - per-port state tracked by the driver
 * @priv:                back-pointer to switch private data; needed by
 *                       deferred work handlers to access ds and priv
 * @fid:                 firmware FID for the permanent single-port bridge; kept
 *                       alive for the lifetime of the port so traffic is never
 *                       forwarded while the port is unbridged
 * @portmap:             bitmap of switch port indices that share the current
 *                       bridge with this port
 * @flood_block:         bitmask of firmware meter indices that are currently
 *                       rate-limiting flood traffic on this port (zero-rate
 *                       meters used to block flooding)
 * @learning:            true when address learning is enabled on this port
 * @setup_done:          set at end of port_setup, cleared at start of
 *                       port_teardown; guards deferred work against
 *                       acting on torn-down state
 * @pvid:                port VLAN ID (native VLAN) assigned to untagged traffic
 * @vlan_filtering:      true when VLAN filtering is enabled on this port
 * @vf:                  per-port VLAN Filter block state
 * @ingress_evlan:       ingress extended VLAN block state
 * @egress_evlan:        egress extended VLAN block state
 * @bridge_port_cpu:     virtual bridge port ID for tag_8021q CPU-side CTP
 * @host_flood_block:    bitmask of firmware meter indices used to block
 *                       host flooding on the virtual bridge port (tag_8021q)
 * @host_flood_uc:       desired host unicast flood state (true = flood);
 *                       updated atomically by port_set_host_flood, consumed
 *                       by the deferred host_flood_work
 * @host_flood_mc:       desired host multicast flood state (true = flood)
 * @host_flood_work:     deferred work for applying host flood changes;
 *                       port_set_host_flood runs in atomic context (under
 *                       netif_addr_lock) so firmware calls must be deferred.
 *                       The worker acquires rtnl_lock() to serialize with
 *                       DSA callbacks and checks @setup_done to avoid
 *                       acting on torn-down ports.
 * @stats:               64-bit accumulated hardware statistics; updated
 *                       periodically by the stats polling work
 * @stats_lock:          protects accumulator reads in .get_stats64 against
 *                       concurrent updates from the polling work
 * @tag_8021q_vid:       currently assigned tag_8021q management VID
 * @lag:                 non-NULL when port is member of a LAG group;
 *                       points to the DSA LAG structure
 * @lag_tx_enabled:      true when this port is active for TX in its LAG
 * @lag_hash_bits:       hash field bitmask (MXL862XX_TRUNK_HASH_*) requested
 *                       when this port joined its LAG; used to recompute the
 *                       global trunk_hash when a LAG is destroyed
 */
struct mxl862xx_port {
	struct mxl862xx_priv *priv;
	u16 fid;
	DECLARE_BITMAP(portmap, MXL862XX_MAX_BRIDGE_PORTS);
	unsigned long flood_block;
	bool learning;
	bool setup_done;
	/* VLAN state */
	u16 pvid;
	bool vlan_filtering;
	struct mxl862xx_vf_block vf;
	struct mxl862xx_evlan_block ingress_evlan;
	struct mxl862xx_evlan_block egress_evlan;
	/* tag_8021q state */
	u16 bridge_port_cpu;
	unsigned long host_flood_block;
	bool host_flood_uc;
	bool host_flood_mc;
	struct work_struct host_flood_work;
	u16 tag_8021q_vid;
	struct mxl862xx_evlan_block cpu_egress_evlan;
	/* LAG state */
	struct dsa_lag *lag;
	bool lag_tx_enabled;
	u8 lag_hash_bits;
	/* Hardware stats accumulation */
	struct mxl862xx_port_stats stats;
	spinlock_t stats_lock;
};

/**
 * struct mxl862xx_pcs - link SerDes interfaces to bridge ports
 * @pcs:  &struct phylink_pcs instance
 * @priv: pointer to &struct mxl862xx_priv
 * @port: bridge port index
 */
struct mxl862xx_pcs {
	struct phylink_pcs pcs;
	struct mxl862xx_priv *priv;
	int port;
};

/**
 * union mxl862xx_fw_version - firmware version for comparison and display
 * @major: firmware major version
 * @minor: firmware minor version
 * @revision: firmware revision number
 * @raw: combined u32 for direct >= comparison (major most significant)
 *
 * The struct layout places major in the most-significant byte of the
 * u32 on both big- and little-endian machines, so raw values compare
 * with the natural major > minor > revision ordering.
 */
union mxl862xx_fw_version {
	struct {
#if defined(__BIG_ENDIAN)
		u8 major;
		u8 minor;
		u16 revision;
#elif defined(__LITTLE_ENDIAN)
		u16 revision;
		u8 minor;
		u8 major;
#endif
	};
	u32 raw;
};

#define MXL862XX_FW_VER(maj, min, rev) \
	((union mxl862xx_fw_version){ .major = (maj), .minor = (min), \
				      .revision = (rev) }).raw
#define MXL862XX_FW_VER_MIN(priv, maj, min, rev) \
	((priv)->fw_version.raw >= MXL862XX_FW_VER(maj, min, rev))

/**
 * struct mxl862xx_priv - driver private data for an MxL862xx switch
 * @ds:                 pointer to the DSA switch instance
 * @mdiodev:            MDIO device used to communicate with the switch firmware
 * @crc_err_work:       deferred work for taking down all ports on CRC errors
 * @crc_err:            set atomically before CRC-triggerd takedown,
 *                      cleared after
 * @tag_proto:          active DSA tag protocol (native or 8021q)
 * @drop_meter:         index of the single shared zero-rate firmware meter
 *                      used to unconditionally drop traffic (used to block
 *                      flooding)
 * @fw_version:         cached firmware version, populated at probe and
 *                      compared with MXL862XX_FW_VER_MIN()
 * @serdes_ports:       SerDes interfaces incl. sub-interfaces in case of
 *                      10G_QXGMII
 * @ports:              per-port state, indexed by switch port number
 * @evlan_ingress_size: per-port ingress Extended VLAN block size
 * @evlan_egress_size:  per-port egress Extended VLAN block size
 * @cpu_evlan_ingress_size: CPU port ingress EVLAN block size (tag_8021q)
 * @bridges:            maps DSA bridge number to firmware bridge ID;
 *                      zero means no firmware bridge allocated for that
 *                      DSA bridge number.  Indexed by dsa_bridge.num
 *                      (0 .. ds->max_num_bridges).
 * @vf_block_size:      per-port VLAN Filter block size
 * @lag_bridge_ports:   maps DSA LAG ID to firmware bridge port ID;
 *                      zero means no bridge port allocated for that LAG.
 *                      Indexed by lag->id (entry 0 is unused).
 *                      The bridge port is stable for the LAG's lifetime
 *                      so FDB/MDB entries never need migration on
 *                      membership changes.
 * @trunk_hash:         current global hash field bitmask (6 bits,
 *                      MXL862XX_TRUNK_HASH_*); union of all active LAGs'
 *                      hash requirements
 * @stats_work:         periodic work item that polls RMON hardware counters
 *                      and accumulates them into 64-bit per-port stats
 */
struct mxl862xx_priv {
	struct dsa_switch *ds;
	struct mdio_device *mdiodev;
	struct work_struct crc_err_work;
	unsigned long crc_err;
	enum dsa_tag_protocol tag_proto;
	u16 drop_meter;
	union mxl862xx_fw_version fw_version;
	struct mxl862xx_pcs serdes_ports[8];
	struct mxl862xx_port ports[MXL862XX_MAX_PORTS];
	u16 bridges[MXL862XX_MAX_BRIDGES + 1];
	u16 evlan_ingress_size;
	u16 evlan_egress_size;
	u16 cpu_evlan_ingress_size;
	u16 vf_block_size;
	u16 lag_bridge_ports[MXL862XX_MAX_LAG_IDS + 1];
	u8 trunk_hash;
	struct delayed_work stats_work;
};

#endif /* __MXL862XX_H */
