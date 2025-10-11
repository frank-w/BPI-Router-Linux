// SPDX-License-Identifier: GPL-2.0
/*
 * Aeonsemi AS21XXxX PHY Driver
 *
 */

//#include "./as21xx_bbu_api/as21xx_debugfs.h"

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

struct as21xxx_priv {
	bool parity_status;
	/* Protect concurrent IPC access */
	struct mutex ipc_lock;
	struct an_mdi_cfg mdi_cfg;
	struct dentry *debugfs_root;
};

int aeon_mdio_read(struct phy_device *phydev, unsigned int dev_addr,
		   unsigned int phy_reg);
void aeon_mdio_write(struct phy_device *phydev, unsigned int dev_addr,
		     unsigned int phy_reg, unsigned int phy_data);
