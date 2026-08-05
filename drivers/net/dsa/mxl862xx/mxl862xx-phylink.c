// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Phylink and PCS support for MaxLinear MxL862xx switch family
 *
 * Copyright (C) 2024 MaxLinear Inc.
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/bitfield.h>
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

/* Legacy SFP-based PCS implementation for firmware < 1.0.80 */
static int mxl862xx_legacy_pcs_config(struct phylink_pcs *pcs,
				      unsigned int neg_mode,
				      phy_interface_t interface,
				      const unsigned long *advertising,
				      bool permit_pause_to_mac)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_sys_sfp_cfg ser_intf = {
		.option = 0,
		.mode = 1,
	};

	if (port != 9 && port != 13)
		return 0;

	if (port == 9)
		ser_intf.port_id = 0;
	else
		ser_intf.port_id = 1;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		ser_intf.speed = 8;
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
		ser_intf.speed = (neg_mode & PHYLINK_PCS_NEG_INBAND) ? 1 : 7;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		ser_intf.speed = 4;
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		ser_intf.speed = 2;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		ser_intf.speed = 3;
		break;
	default:
		dev_err(priv->ds->dev, "unsupported interface: %s\n",
			phy_modes(interface));
		return -EINVAL;
	}

	return MXL862XX_API_WRITE(priv, SYS_MISC_SFP_SET, ser_intf);
}

static void mxl862xx_legacy_pcs_get_state(struct phylink_pcs *pcs,
					  unsigned int neg_mode,
					  struct phylink_link_state *state)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_port_link_cfg port_link_cfg = {
		.port_id = port,
	};
	struct mxl862xx_port_cfg port_cfg = {
		.port_id = port,
	};
	int ret;

	/* phylink presets state->link = 1 before calling pcs_get_state();
	 * make sure a failed firmware read reports link down instead of a
	 * spurious link up with SPEED_UNKNOWN.
	 */
	state->link = false;

	ret = MXL862XX_API_READ(priv, MXL862XX_COMMON_PORTLINKCFGGET,
				port_link_cfg);
	if (ret)
		return;

	ret = MXL862XX_API_READ(priv, MXL862XX_COMMON_PORTCFGGET, port_cfg);
	if (ret)
		return;

	state->link = (port_link_cfg.link == MXL862XX_PORT_LINK_UP);
	state->an_complete = state->link;

	switch (port_link_cfg.speed) {
	case MXL862XX_PORT_SPEED_10:
		state->speed = SPEED_10;
		break;
	case MXL862XX_PORT_SPEED_100:
		state->speed = SPEED_100;
		break;
	case MXL862XX_PORT_SPEED_1000:
		state->speed = SPEED_1000;
		break;
	case MXL862XX_PORT_SPEED_2500:
		state->speed = SPEED_2500;
		break;
	case MXL862XX_PORT_SPEED_5000:
		state->speed = SPEED_5000;
		break;
	case MXL862XX_PORT_SPEED_10000:
		state->speed = SPEED_10000;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}

	switch (port_link_cfg.duplex) {
	case MXL862XX_DUPLEX_HALF:
		state->duplex = DUPLEX_HALF;
		break;
	case MXL862XX_DUPLEX_FULL:
		state->duplex = DUPLEX_FULL;
		break;
	default:
		state->duplex = DUPLEX_UNKNOWN;
		break;
	}

	state->pause &= ~(MLO_PAUSE_RX | MLO_PAUSE_TX);
	switch (port_cfg.flow_ctrl) {
	case MXL862XX_FLOW_RXTX:
		state->pause |= MLO_PAUSE_TXRX_MASK;
		break;
	case MXL862XX_FLOW_TX:
		state->pause |= MLO_PAUSE_TX;
		break;
	case MXL862XX_FLOW_RX:
		state->pause |= MLO_PAUSE_RX;
		break;
	case MXL862XX_FLOW_OFF:
	default:
		break;
	}
}

static unsigned int
mxl862xx_legacy_pcs_inband_caps(struct phylink_pcs *pcs,
				phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_USXGMII:
		return LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_1000BASEX:
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_2500BASEX:
		return LINK_INBAND_DISABLE;
	default:
		return 0;
	}
}

static const struct phylink_pcs_ops mxl862xx_legacy_pcs_ops = {
	.pcs_get_state = mxl862xx_legacy_pcs_get_state,
	.pcs_config = mxl862xx_legacy_pcs_config,
	.pcs_inband_caps = mxl862xx_legacy_pcs_inband_caps,
};

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

	/* phylink presets state->link = 1 before calling pcs_get_state();
	 * make sure a failed firmware read reports link down instead of a
	 * spurious link up with SPEED_UNKNOWN.
	 */
	state->link = false;

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

/* Ops for the reshaped XPCS API (MXL862XX_CAP_XPCS_V2, firmware >=
 * 1.0.84): PCS_CONFIG/PCS_GET_STATE/AN_RESTART take packed mode words,
 * PCS_ENABLE and AN_DISABLE no longer exist (the bringup is idempotent
 * and implicit in PCS_CONFIG, so there is no pre_config either) and
 * 0x1a07 is PCS_LINK_UP.  PCS_DISABLE is unchanged, so the v1
 * pcs_disable is reused.  This tree drives each XPCS as a single lane
 * with sub-port 0; the quad USXGMII fields stay zero.
 */

static int mxl862xx_xpcs_errno(int result)
{
	switch (result) {
	case -5:	/* Zephyr -EIO */
		return -EIO;
	case -134:	/* Zephyr -ENOTSUP */
		return -EOPNOTSUPP;
	default:	/* Zephyr -EINVAL and anything unexpected */
		return -EINVAL;
	}
}

static int mxl862xx_pcs_v2_config(struct phylink_pcs *pcs,
				  unsigned int neg_mode,
				  phy_interface_t interface,
				  const unsigned long *advertising,
				  bool permit_pause_to_mac)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	struct mxl862xx_xpcs_pcs_cfg_v2 cfg = {};
	int port = mpcs->port;
	int if_mode, ret, adv;

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

	cfg.mode = cpu_to_le16(FIELD_PREP(MXL862XX_XPCS_CFG_PORT_ID,
					  mxl862xx_xpcs_port_id(port)) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_INTERFACE,
					  if_mode) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_NEG_MODE,
					  mxl862xx_xpcs_neg_mode(neg_mode)) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_USX_LANE_MODE,
					  MXL862XX_XPCS_USX_SINGLE) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_ROLE,
					  MXL862XX_XPCS_ROLE_MAC) |
			       FIELD_PREP(MXL862XX_XPCS_CFG_PERMIT_PAUSE,
					  permit_pause_to_mac));

	if (neg_mode & PHYLINK_PCS_NEG_INBAND) {
		adv = phylink_mii_c22_pcs_encode_advertisement(interface,
							       advertising);
		if (adv >= 0)
			cfg.advertising.cl37 = cpu_to_le16(adv);
	}

	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PCS_CONFIG, cfg);
	if (ret)
		return ret;

	ret = (s16)le16_to_cpu(cfg.result);
	if (ret < 0)
		return mxl862xx_xpcs_errno(ret);

	/* result > 0 means AN restart is needed */
	return ret > 0 ? 1 : 0;
}

/* These interface modes carry no AN code word, so there is no
 * negotiated pause to decode and phylink would otherwise resolve pause
 * off.  Report the flow control the switch port is configured with,
 * which is what the legacy register path reports for every mode.
 */
static void mxl862xx_pcs_read_flow_ctrl(struct mxl862xx_priv *priv, int port,
					struct phylink_link_state *state)
{
	struct mxl862xx_port_cfg port_cfg = {
		.port_id = port,
	};

	if (MXL862XX_API_READ(priv, MXL862XX_COMMON_PORTCFGGET, port_cfg))
		return;

	state->pause &= ~(MLO_PAUSE_RX | MLO_PAUSE_TX);
	switch (port_cfg.flow_ctrl) {
	case MXL862XX_FLOW_RXTX:
		state->pause |= MLO_PAUSE_TXRX_MASK;
		break;
	case MXL862XX_FLOW_TX:
		state->pause |= MLO_PAUSE_TX;
		break;
	case MXL862XX_FLOW_RX:
		state->pause |= MLO_PAUSE_RX;
		break;
	case MXL862XX_FLOW_OFF:
	default:
		break;
	}
}

static void mxl862xx_pcs_v2_get_state(struct phylink_pcs *pcs,
				      unsigned int neg_mode,
				      struct phylink_link_state *state)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	struct mxl862xx_xpcs_pcs_state_v2 st = {};
	int port = mpcs->port;
	int if_mode, ret;
	u32 mode;
	u16 bmsr;

	/* phylink presets state->link = 1 before calling pcs_get_state();
	 * make sure a failed firmware read reports link down instead of a
	 * spurious link up with SPEED_UNKNOWN.
	 */
	state->link = false;

	if_mode = mxl862xx_xpcs_if_mode(state->interface);
	if (if_mode < 0)
		return;

	st.mode = cpu_to_le32(FIELD_PREP(MXL862XX_XPCS_ST_PORT_ID,
					 mxl862xx_xpcs_port_id(port)) |
			      FIELD_PREP(MXL862XX_XPCS_ST_INTERFACE,
					 if_mode) |
			      FIELD_PREP(MXL862XX_XPCS_ST_USX_LANE_MODE,
					 MXL862XX_XPCS_USX_SINGLE));

	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PCS_GET_STATE, st);
	if (ret)
		return;

	mode = le32_to_cpu(st.mode);
	state->link = FIELD_GET(MXL862XX_XPCS_ST_LINK, mode) &&
		      !FIELD_GET(MXL862XX_XPCS_ST_PCS_FAULT, mode);
	state->an_complete = FIELD_GET(MXL862XX_XPCS_ST_AN_COMPLETE, mode);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		bmsr = (state->link ? BMSR_LSTATUS : 0) |
		       (state->an_complete ? BMSR_ANEGCOMPLETE : 0);
		phylink_mii_c22_pcs_decode_state(state, neg_mode, bmsr,
						 le16_to_cpu(st.lpa.cl37));
		break;

	case PHY_INTERFACE_MODE_USXGMII:
		if (state->link)
			phylink_decode_usxgmii_word(state,
						    le16_to_cpu(st.lpa.usx));
		break;

	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_10GKR:
		if (state->link) {
			state->speed = SPEED_10000;
			state->duplex = DUPLEX_FULL;
			mxl862xx_pcs_read_flow_ctrl(priv, port, state);
		}
		break;

	default:
		state->link = false;
		break;
	}
}

static void mxl862xx_pcs_v2_an_restart(struct phylink_pcs *pcs)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	struct mxl862xx_xpcs_an_restart_v2 an = {};
	int port = mpcs->port;

	if (port != 9 && port != 13)
		return;

	an.mode = cpu_to_le16(FIELD_PREP(MXL862XX_XPCS_ANR_PORT_ID,
					 mxl862xx_xpcs_port_id(port)) |
			      FIELD_PREP(MXL862XX_XPCS_ANR_INTERFACE,
					 mpcs->if_mode) |
			      FIELD_PREP(MXL862XX_XPCS_ANR_USX_LANE_MODE,
					 MXL862XX_XPCS_USX_SINGLE));

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_AN_RESTART, an);
}

static void mxl862xx_pcs_v2_link_up(struct phylink_pcs *pcs,
				    unsigned int neg_mode,
				    phy_interface_t interface, int speed,
				    int duplex)
{
	struct mxl862xx_pcs *mpcs = pcs_to_mxl862xx_pcs(pcs);
	struct mxl862xx_priv *priv = mpcs->priv;
	struct mxl862xx_xpcs_pcs_link_up lu = {};
	int port = mpcs->port;
	int if_mode, dup;

	if (port != 9 && port != 13)
		return;

	/* With inband AN the XPCS resolves speed and duplex from the
	 * partner's AN word itself; skip the firmware round-trip.
	 */
	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		return;

	if_mode = mxl862xx_xpcs_if_mode(interface);
	if (if_mode < 0)
		return;

	dup = (duplex == DUPLEX_FULL) ? MXL862XX_XPCS_DUPLEX_FULL :
					MXL862XX_XPCS_DUPLEX_HALF;

	lu.mode = cpu_to_le16(FIELD_PREP(MXL862XX_XPCS_LU_PORT_ID,
					 mxl862xx_xpcs_port_id(port)) |
			      FIELD_PREP(MXL862XX_XPCS_LU_INTERFACE,
					 if_mode) |
			      FIELD_PREP(MXL862XX_XPCS_LU_USX_LANE_MODE,
					 MXL862XX_XPCS_USX_SINGLE) |
			      FIELD_PREP(MXL862XX_XPCS_LU_DUPLEX, dup));
	lu.speed = cpu_to_le16(speed);

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_LINK_UP, lu);
}

static unsigned int mxl862xx_pcs_v2_inband_caps(struct phylink_pcs *pcs,
						phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10GKR:
		return LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_10GBASER:
		return LINK_INBAND_DISABLE;
	default:
		return 0;
	}
}

static const struct phylink_pcs_ops mxl862xx_pcs_v2_ops = {
	.pcs_disable = mxl862xx_pcs_disable,
	.pcs_config = mxl862xx_pcs_v2_config,
	.pcs_get_state = mxl862xx_pcs_v2_get_state,
	.pcs_an_restart = mxl862xx_pcs_v2_an_restart,
	.pcs_link_up = mxl862xx_pcs_v2_link_up,
	.pcs_inband_caps = mxl862xx_pcs_v2_inband_caps,
};

void mxl862xx_setup_pcs(struct mxl862xx_priv *priv, struct mxl862xx_pcs *pcs,
			int port)
{
	pcs->priv = priv;
	pcs->port = port;

	/* Keep the CPU port on the legacy path.  Its link is fixed, so
	 * phylink resolves it from the fixed-link configuration and never
	 * calls pcs_get_state() for it, and the firmware refuses the
	 * negotiation commands for an instance configured into a
	 * fixed-rate mode, so the XPCS API adds nothing there.  It also
	 * costs: PCS_CONFIG is not covered by that refusal and replaces
	 * the configuration the legacy path installs, after which the
	 * port keeps reporting link at 10G and forwards nothing, which
	 * takes the switch off the network it is managed over.
	 */
	if (dsa_is_cpu_port(priv->ds, port))
		pcs->pcs.ops = &mxl862xx_legacy_pcs_ops;
	else if (mxl862xx_fw_has(priv, MXL862XX_CAP_XPCS_V2))
		pcs->pcs.ops = &mxl862xx_pcs_v2_ops;
	else if (mxl862xx_fw_has(priv, MXL862XX_CAP_XPCS_API))
		pcs->pcs.ops = &mxl862xx_pcs_ops;
	else
		pcs->pcs.ops = &mxl862xx_legacy_pcs_ops;
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

/* --- SerDes ethtool statistics --- */

static const char mxl862xx_serdes_stats[][ETH_GSTRING_LEN] = {
	"serdes_tx_main",
	"serdes_tx_pre",
	"serdes_tx_post",
	"serdes_tx_iboost",
	"serdes_tx_vboost",
	"serdes_tx_vboost_en",
	"serdes_rx_att",
	"serdes_rx_vga1",
	"serdes_rx_vga2",
	"serdes_rx_ctle_boost",
	"serdes_rx_ctle_pole",
	"serdes_rx_dfe_tap1",
	"serdes_rx_dfe_bypass",
	"serdes_rx_adapt_mode",
	"serdes_rx_adapt_sel",
	"serdes_rx_signal",
	"serdes_pma_link",
	"serdes_link_fault",
	"serdes_in_reset",
};

static bool mxl862xx_port_has_serdes_stats(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;

	return port >= 9 && port <= 16 &&
	       mxl862xx_fw_has(priv, MXL862XX_CAP_SERDES_STATS);
}

int mxl862xx_serdes_stats_count(struct dsa_switch *ds, int port)
{
	if (mxl862xx_port_has_serdes_stats(ds, port))
		return ARRAY_SIZE(mxl862xx_serdes_stats);

	return 0;
}

void mxl862xx_serdes_get_strings(struct dsa_switch *ds, int port, u8 *data)
{
	int i;

	if (!mxl862xx_port_has_serdes_stats(ds, port))
		return;

	for (i = 0; i < ARRAY_SIZE(mxl862xx_serdes_stats); i++)
		ethtool_puts(&data, mxl862xx_serdes_stats[i]);
}

void mxl862xx_serdes_get_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct mxl862xx_xpcs_eq_get eq = {
		.port_id = mxl862xx_xpcs_port_id(port),
	};
	struct mxl862xx_xpcs_signal_detect sig = {};

	if (!mxl862xx_port_has_serdes_stats(ds, port))
		return;

	sig.port_id = mxl862xx_xpcs_port_id(port);

	if (!MXL862XX_API_READ(ds->priv, MXL862XX_XPCS_EQ_GET, eq)) {
		*data++ = eq.tx.main.value;
		*data++ = eq.tx.pre.value;
		*data++ = eq.tx.post.value;
		*data++ = eq.tx.iboost_lvl.value;
		*data++ = eq.tx.vboost_lvl.value;
		*data++ = eq.tx.vboost_en.value;
		*data++ = eq.rx.att_lvl.value;
		*data++ = eq.rx.vga1_gain.value;
		*data++ = eq.rx.vga2_gain.value;
		*data++ = eq.rx.ctle_boost.value;
		*data++ = eq.rx.ctle_pole.value;
		*data++ = eq.rx.dfe_tap1.value;
		*data++ = eq.rx.dfe_bypass.value;
		*data++ = eq.rx.adapt_mode.value;
		*data++ = eq.rx.adapt_sel.value;
	} else {
		data += 15;
	}

	if (!MXL862XX_API_READ(ds->priv, MXL862XX_XPCS_SIGNAL_DETECT, sig)) {
		*data++ = sig.rx_signal;
		*data++ = sig.pma_link;
		*data++ = sig.link_fault;
		*data++ = sig.in_reset;
	} else {
		data += 4;
	}
}

void mxl862xx_serdes_self_test(struct dsa_switch *ds, int port,
			      struct ethtool_test *etest, u64 *data)
{
	struct mxl862xx_xpcs_prbs_cfg prbs = {};
	struct mxl862xx_xpcs_bert_cfg bert = {};
	struct mxl862xx_priv *priv = ds->priv;
	int xpcs_id = mxl862xx_xpcs_port_id(port);
	int i = 0;
	int ret;

	if (!mxl862xx_port_has_serdes_stats(ds, port))
		return;

	/* Test 1: PCS PRBS31 */
	prbs.port_id = xpcs_id;
	prbs.tx_en = 1;
	prbs.rx_en = 1;
	ret = MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PRBS_CFG, prbs);
	if (ret) {
		data[i++] = 1;
		goto skip_prbs;
	}

	msleep(100);

	memset(&prbs, 0, sizeof(prbs));
	prbs.port_id = xpcs_id;
	prbs.read_err = 1;
	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PRBS_CFG, prbs);

	/* Disable PRBS */
	memset(&prbs, 0, sizeof(prbs));
	prbs.port_id = xpcs_id;
	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PRBS_CFG, prbs);

	if (ret) {
		data[i++] = 1;
	} else {
		data[i] = le16_to_cpu(prbs.rx_err_cnt) ? 1 : 0;
		if (data[i])
			etest->flags |= ETH_TEST_FL_FAILED;
		i++;
	}

skip_prbs:
	/* Test 2: SerDes BERT PRBS31 -- clear error counter first */
	bert.port_id = xpcs_id;
	bert.clear_err = 1;
	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_BERT_CFG, bert);

	/* Enable BERT with PRBS31 pattern */
	memset(&bert, 0, sizeof(bert));
	bert.port_id = xpcs_id;
	bert.tx_en = 1;
	bert.rx_en = 1;
	bert.pattern = 6; /* PRBS31 */
	ret = MXL862XX_API_WRITE(priv, MXL862XX_XPCS_BERT_CFG, bert);
	if (ret) {
		data[i++] = 1;
		return;
	}

	msleep(100);

	/* Read error count */
	memset(&bert, 0, sizeof(bert));
	bert.port_id = xpcs_id;
	bert.read_err = 1;
	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_BERT_CFG, bert);

	/* Disable BERT */
	memset(&bert, 0, sizeof(bert));
	bert.port_id = xpcs_id;
	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_BERT_CFG, bert);

	if (ret) {
		data[i++] = 1;
	} else {
		data[i] = le16_to_cpu(bert.rx_err_cnt) ? 1 : 0;
		if (data[i])
			etest->flags |= ETH_TEST_FL_FAILED;
		i++;
	}
}
