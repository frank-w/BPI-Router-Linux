// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Phylink and PCS support for MaxLinear MxL862xx switch family
 *
 * Copyright (C) 2024 MaxLinear Inc.
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/iopoll.h>
#include <linux/phylink.h>
#include <net/dsa.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-host.h"
#include "mxl862xx-phylink.h"

void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
			       struct phylink_config *config)
{
	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE | MAC_10 |
				   MAC_100 | MAC_1000 | MAC_2500FD;

	switch (port) {
	case 1 ... 8:
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;
	case 9:
	case 13:
		__set_bit(PHY_INTERFACE_MODE_SGMII, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_10GBASER, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_10GKR, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_USXGMII, config->supported_interfaces);
		config->mac_capabilities |= MAC_10000FD | MAC_5000FD;
		fallthrough;
	case 10 ... 12:
	case 14 ... 16:
		__set_bit(PHY_INTERFACE_MODE_QSGMII, config->supported_interfaces);
		break;
	default:
		break;
	}
}

static struct mxl862xx_pcs *pcs_to_mxl862xx_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mxl862xx_pcs, pcs);
}

static int mxl862xx_xpcs_port_id(int port)
{
	return port >= 13;
}

static int mxl862xx_xpcs_if_mode(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		return MXL862XX_XPCS_IF_SGMII;
	case PHY_INTERFACE_MODE_QSGMII:
		return MXL862XX_XPCS_IF_QSGMII;
	case PHY_INTERFACE_MODE_1000BASEX:
		return MXL862XX_XPCS_IF_1000BASEX;
	case PHY_INTERFACE_MODE_2500BASEX:
		return MXL862XX_XPCS_IF_2500BASEX;
	case PHY_INTERFACE_MODE_USXGMII:
		return MXL862XX_XPCS_IF_USXGMII;
	case PHY_INTERFACE_MODE_10GBASER:
		return MXL862XX_XPCS_IF_10GBASER;
	case PHY_INTERFACE_MODE_10GKR:
		return MXL862XX_XPCS_IF_10GKR;
	default:
		return -EINVAL;
	}
}

static int mxl862xx_xpcs_neg_mode(unsigned int neg_mode)
{
	if (!(neg_mode & PHYLINK_PCS_NEG_INBAND))
		return MXL862XX_XPCS_NEG_NONE;
	if (neg_mode & PHYLINK_PCS_NEG_ENABLED)
		return MXL862XX_XPCS_NEG_INBAND_AN_ON;
	return MXL862XX_XPCS_NEG_INBAND_AN_OFF;
}

static struct mxl862xx_xpcs_signal_detect
mxl862xx_xpcs_signal_detect(struct mxl862xx_priv *priv, int port_id)
{
	struct mxl862xx_xpcs_signal_detect sd = { .port_id = port_id };

	MXL862XX_API_READ(priv, MXL862XX_XPCS_SIGNAL_DETECT, sd);

	return sd;
}

static int mxl862xx_xpcs_poll_ready(struct mxl862xx_priv *priv, int port_id)
{
	struct mxl862xx_xpcs_signal_detect sd;
	int ret;

	ret = read_poll_timeout(mxl862xx_xpcs_signal_detect, sd,
				!sd.in_reset, 50000, 1000000,
				false, priv, port_id);
	if (ret)
		dev_warn(priv->ds->dev, "XPCS%d ready timeout\n", port_id);

	return ret;
}

static void mxl862xx_pcs_disable(struct phylink_pcs *pcs)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	int port = mpcs->port;
	struct mxl862xx_xpcs_pcs_power pwr = {};

	if (port != 9 && port != 13)
		return;

	pwr.port_id = mxl862xx_xpcs_port_id(port);

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_DISABLE, pwr);
	mpcs->enabled = false;
}

static void mxl862xx_pcs_pre_config(struct phylink_pcs *pcs,
				    phy_interface_t interface)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	int port = mpcs->port;
	struct mxl862xx_xpcs_pcs_power pwr = {};
	struct mxl862xx_xpcs_reset_cfg rst = {};
	int port_id, if_mode;

	if (port != 9 && port != 13)
		return;

	if_mode = mxl862xx_xpcs_if_mode(interface);
	if (if_mode < 0)
		return;

	port_id = mxl862xx_xpcs_port_id(port);

	/* Full reset only if PCS is already running (not after a clean disable,
	 * which already asserts hardware reset via XPCS_PCS_DISABLE).
	 */
	if (mpcs->enabled) {
		rst.port_id = port_id;
		rst.reset_type = MXL862XX_XPCS_RESET_HARD;
		MXL862XX_API_WRITE(priv, MXL862XX_XPCS_RESET, rst);
		mxl862xx_xpcs_poll_ready(priv, port_id);
	}

	pwr.port_id = port_id;
	pwr.interface = if_mode;
	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_ENABLE, pwr);
	mxl862xx_xpcs_poll_ready(priv, port_id);
	mpcs->enabled = true;
}

static int mxl862xx_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			       phy_interface_t interface,
			       const unsigned long *advertising,
			       bool permit_pause_to_mac)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	int port = mpcs->port;
	struct mxl862xx_xpcs_pcs_cfg cfg = {};
	int if_mode, ret;

	/* Sub-interfaces are set up implicitly by the main interface */
	if (port != 9 && port != 13)
		return 0;

	if_mode = mxl862xx_xpcs_if_mode(interface);
	if (if_mode < 0) {
		dev_err(priv->ds->dev, "unsupported interface: %s\n",
			phy_modes(interface));
		return if_mode;
	}

	mpcs->if_mode = if_mode;

	cfg.port_id = mxl862xx_xpcs_port_id(port);
	cfg.interface = if_mode;
	cfg.neg_mode = mxl862xx_xpcs_neg_mode(neg_mode);
	cfg.permit_pause = permit_pause_to_mac ? 1 : 0;

	if (neg_mode & PHYLINK_PCS_NEG_INBAND) {
		switch (interface) {
		case PHY_INTERFACE_MODE_1000BASEX:
		case PHY_INTERFACE_MODE_2500BASEX:
			cfg.advertising = cpu_to_le16(
				linkmode_adv_to_mii_adv_x(advertising,
					ETHTOOL_LINK_MODE_1000baseX_Full_BIT));
			break;
		case PHY_INTERFACE_MODE_SGMII:
		case PHY_INTERFACE_MODE_QSGMII:
			cfg.advertising = cpu_to_le16(ADVERTISE_SGMII);
			break;
		default:
			break;
		}
	}

	ret = MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_CONFIG, cfg);
	if (ret)
		return ret;

	/* result > 0 means AN restart is needed */
	return le16_to_cpu(cfg.result) > 0 ? 1 : 0;
}

static void mxl862xx_xpcs_decode_speed(u16 fw_speed,
					struct phylink_link_state *state)
{
	switch (fw_speed) {
	case MXL862XX_XPCS_SPEED_10:
		state->speed = SPEED_10;
		break;
	case MXL862XX_XPCS_SPEED_100:
		state->speed = SPEED_100;
		break;
	case MXL862XX_XPCS_SPEED_1000:
		state->speed = SPEED_1000;
		break;
	case MXL862XX_XPCS_SPEED_2500:
		state->speed = SPEED_2500;
		break;
	case MXL862XX_XPCS_SPEED_5000:
		state->speed = SPEED_5000;
		break;
	case MXL862XX_XPCS_SPEED_10000:
		state->speed = SPEED_10000;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}

	state->duplex = DUPLEX_FULL;
}

static void mxl862xx_pcs_get_state(struct phylink_pcs *pcs,
				   unsigned int neg_mode,
				   struct phylink_link_state *state)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_pcs_state st = {};
	int if_mode, ret;
	u16 fw_speed, lpa, bmsr;

	if_mode = mxl862xx_xpcs_if_mode(state->interface);
	if (if_mode < 0)
		return;

	st.port_id = mxl862xx_xpcs_port_id(port);
	st.interface = if_mode;

	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PCS_GET_STATE, st);
	if (ret)
		return;

	fw_speed = le16_to_cpu(st.speed);
	lpa = le16_to_cpu(st.lpa);

	state->link = st.link && !st.pcs_fault;
	if (!state->link)
		return;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		/* Synthesize BMSR from firmware state and use phylink's
		 * standard CL37/SGMII decoders for LPA, pause, and speed.
		 */
		bmsr = BMSR_LSTATUS;
		if (st.an_complete)
			bmsr |= BMSR_ANEGCOMPLETE;
		phylink_mii_c22_pcs_decode_state(state, neg_mode, bmsr, lpa);

		/* Override speed/duplex with firmware's resolved values
		 * for downshift detection.
		 */
		mxl862xx_xpcs_decode_speed(fw_speed, state);
		state->duplex = st.duplex ? DUPLEX_FULL : DUPLEX_HALF;
		break;

	case PHY_INTERFACE_MODE_USXGMII:
		state->an_complete = st.an_complete;
		phylink_decode_usxgmii_word(state, lpa);

		/* Override with firmware's resolved values */
		mxl862xx_xpcs_decode_speed(fw_speed, state);
		state->duplex = st.duplex ? DUPLEX_FULL : DUPLEX_HALF;
		break;

	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_10GKR:
		mxl862xx_xpcs_decode_speed(fw_speed, state);
		break;

	default:
		state->link = false;
		break;
	}
}

static void mxl862xx_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	int port = mpcs->port;
	struct mxl862xx_xpcs_an_restart an = {};

	if (port != 9 && port != 13)
		return;

	an.port_id = mxl862xx_xpcs_port_id(port);
	an.interface = mpcs->if_mode;

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_AN_RESTART, an);
}

static void mxl862xx_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
				 phy_interface_t interface, int speed,
				 int duplex)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_force_speed fs = {};

	/* Only SGMII needs explicit speed forcing */
	if (interface != PHY_INTERFACE_MODE_SGMII)
		return;

	if (port != 9 && port != 13)
		return;

	fs.port_id = mxl862xx_xpcs_port_id(port);
	fs.duplex = (duplex == DUPLEX_FULL) ? MXL862XX_XPCS_DUPLEX_FULL :
					      MXL862XX_XPCS_DUPLEX_HALF;
	fs.speed = cpu_to_le16(speed);

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_FORCE_SPEED, fs);
}

static unsigned int mxl862xx_pcs_inband_caps(struct phylink_pcs *pcs,
					     phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_10GBASER:
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE |
		       LINK_INBAND_BYPASS;
	case PHY_INTERFACE_MODE_10GKR:
		return LINK_INBAND_ENABLE | LINK_INBAND_BYPASS;
	default:
		return 0;
	}
}

static const struct phylink_pcs_ops mxl862xx_pcs_ops = {
	.pcs_disable = mxl862xx_pcs_disable,
	.pcs_pre_config = mxl862xx_pcs_pre_config,
	.pcs_config = mxl862xx_pcs_config,
	.pcs_get_state = mxl862xx_pcs_get_state,
	.pcs_an_restart = mxl862xx_pcs_an_restart,
	.pcs_link_up = mxl862xx_pcs_link_up,
	.pcs_inband_caps = mxl862xx_pcs_inband_caps,
};

void mxl862xx_setup_pcs(struct mxl862xx_priv *priv, struct mxl862xx_pcs *pcs,
			int port)
{
	pcs->priv = priv;
	pcs->port = port;

	pcs->pcs.ops = &mxl862xx_pcs_ops;
	pcs->pcs.poll = true;

	/* Sub-ports only support QSGMII (quad mode with dedicated
	 * PHY_INTERFACE_MODE). Single-lane USXGMII is supported on main
	 * ports only; quad USXGMII is not yet supported as Linux lacks the
	 * infrastructure to signal TDM mode before AN.
	 */
	__set_bit(PHY_INTERFACE_MODE_QSGMII, pcs->pcs.supported_interfaces);
	if (port != 9 && port != 13)
		return;

	__set_bit(PHY_INTERFACE_MODE_SGMII, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10GKR, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, pcs->pcs.supported_interfaces);
}

static struct phylink_pcs *
mxl862xx_phylink_mac_select_pcs(struct phylink_config *config,
				phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct mxl862xx_priv *priv = dp->ds->priv;
	int port = dp->index;

	if (!MXL862XX_FW_VER_MIN(priv, 1, 0, 80))
		return NULL;

	switch (port) {
	case 9 ... 16:
		return &priv->serdes_ports[port - 9].pcs;
	default:
		return NULL;
	}
}

static void mxl862xx_phylink_mac_config(struct phylink_config *config,
					unsigned int mode,
					const struct phylink_link_state *state)
{
}

static void mxl862xx_phylink_mac_link_down(struct phylink_config *config,
					   unsigned int mode,
					   phy_interface_t interface)
{
}

static void mxl862xx_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phydev,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
}

const struct phylink_mac_ops mxl862xx_phylink_mac_ops = {
	.mac_config = mxl862xx_phylink_mac_config,
	.mac_link_down = mxl862xx_phylink_mac_link_down,
	.mac_link_up = mxl862xx_phylink_mac_link_up,
	.mac_select_pcs = mxl862xx_phylink_mac_select_pcs,
};
