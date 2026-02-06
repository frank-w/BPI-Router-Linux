/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Aeonsemi AS21XXxX PHY Driver
 *
 */

#include "./as21xx_bbu_api/as21xx_debugfs.h"

// SDS Eye Scan Parameters
#define EYE_GRPS	31
#define EYE_COLS_GRP	4
#define EYE_YRES	254
#define EYE_NSAMP	256
#define EYE_XRES	(EYE_GRPS * EYE_COLS_GRP)
#define EYE_STRIDE	(EYE_COLS_GRP * EYE_YRES)
#define EYE_TOTAL_BYTES	(EYE_XRES * EYE_YRES)
#define EYE_PART_0	0
#define EYE_PART_1	1
#define EYE_PART_0_GRPS	(EYE_GRPS / 2)
#define EYE_PART_1_GRPS	(EYE_GRPS - EYE_PART_0_GRPS)

struct downshift_cfg {
	uint8_t enable;
	uint8_t retry_limit;
};

struct an_mdi_cfg {
	uint8_t top_spd;
	uint8_t eee_spd;
	uint8_t fr_spd;
	uint8_t thp_byp;
	uint8_t port_type;
	uint8_t ms_en;
	uint8_t ms_config;
	uint8_t nstd_pbo;
	struct downshift_cfg smt_spd;
	uint8_t trd_ovrd;
	uint8_t trd_swap;
	uint8_t cfr;
};

typedef struct {
	unsigned char tm_done : 1;
	unsigned char tm_alarm : 1;
	unsigned char wol_sts : 1;
	unsigned char link_sts : 1;
	unsigned char reserved2 : 1;
	unsigned char reserved3 : 1;
	unsigned char reserved4 : 1;
	unsigned char reserved5 : 1;
} irq_stats_t;

struct as21xxx_priv {
	bool parity_status;
	/* Protect concurrent IPC access */
	struct mutex ipc_lock;
	struct an_mdi_cfg mdi_cfg;
	struct dentry *debugfs_root;
	unsigned short raw_eye_data[EYE_TOTAL_BYTES];
};

int aeon_cl45_read(struct phy_device *phydev, int dev_addr,
		   unsigned int phy_reg);
void aeon_cl45_write(struct phy_device *phydev, int dev_addr,
		     unsigned int phy_reg, unsigned short phy_data);
