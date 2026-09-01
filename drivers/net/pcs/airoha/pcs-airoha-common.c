// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 AIROHA Inc
 * Author: Christian Marangi <ansuelsmth@gmail.com>
 */

#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pcs/pcs-provider.h>
#include <linux/phy/phy.h>
#include <linux/phylink.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/rtnetlink.h>

#include "pcs-airoha.h"

static void airoha_pcs_setup_scu_eth(struct airoha_pcs_priv *priv,
				     phy_interface_t interface)
{
	u32 xsi_sel;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		xsi_sel = AIROHA_SCU_ETH_XSI_HSGMII;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10GBASER:
	default:
		xsi_sel = AIROHA_SCU_ETH_XSI_USXGMII;
	}

	regmap_update_bits(priv->scu, AIROHA_SCU_SSR3,
			   AIROHA_SCU_ETH_XSI_SEL,
			   xsi_sel);
}

static void airoha_pcs_setup_scu_pon(struct airoha_pcs_priv *priv,
				     phy_interface_t interface)
{
	u32 xsi_sel, wan_sel;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		wan_sel = AIROHA_SCU_WAN_SEL_SGMII;
		xsi_sel = AIROHA_SCU_PON_XSI_HSGMII;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		wan_sel = AIROHA_SCU_WAN_SEL_HSGMII;
		xsi_sel = AIROHA_SCU_PON_XSI_HSGMII;
		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10GBASER:
	default:
		wan_sel = AIROHA_SCU_WAN_SEL_USXGMII;
		xsi_sel = AIROHA_SCU_PON_XSI_USXGMII;
	}

	regmap_update_bits(priv->scu, AIROHA_SCU_SSTR,
			   AIROHA_SCU_PON_XSI_SEL,
			   xsi_sel);

	regmap_update_bits(priv->scu, AIROHA_SCU_WAN_CONF,
			   AIROHA_SCU_WAN_SEL,
			   wan_sel);
}

static void airoha_pcs_setup_scu_pcie(struct airoha_pcs_priv *priv,
				      int index, phy_interface_t interface)
{
	u32 xsi_sel;

	if (index == 0) {
		switch (interface) {
		case PHY_INTERFACE_MODE_SGMII:
		case PHY_INTERFACE_MODE_1000BASEX:
		case PHY_INTERFACE_MODE_2500BASEX:
			xsi_sel = AIROHA_SCU_PCIE_XSI0_HSGMII;
			break;
		case PHY_INTERFACE_MODE_USXGMII:
		case PHY_INTERFACE_MODE_10GBASER:
		default:
			xsi_sel = AIROHA_SCU_PCIE_XSI0_USXGMII;
		}

		regmap_update_bits(priv->scu, AIROHA_SCU_SSTR,
				   AIROHA_SCU_PCIE_XSI0_SEL,
				   xsi_sel);
	} else {
		switch (interface) {
		case PHY_INTERFACE_MODE_SGMII:
		case PHY_INTERFACE_MODE_1000BASEX:
		case PHY_INTERFACE_MODE_2500BASEX:
			xsi_sel = AIROHA_SCU_PCIE_XSI1_HSGMII;
			break;
		case PHY_INTERFACE_MODE_USXGMII:
		case PHY_INTERFACE_MODE_10GBASER:
		default:
			xsi_sel = AIROHA_SCU_PCIE_XSI1_USXGMII;
		}

		regmap_update_bits(priv->scu, AIROHA_SCU_SSTR,
				   AIROHA_SCU_PCIE_XSI1_SEL,
				   xsi_sel);
	}
}

static int airoha_pcs_setup_scu(struct airoha_pcs_priv *priv,
				int index, phy_interface_t interface)
{
	const struct airoha_pcs_match_data *data = priv->data;
	int ret;

	switch (data->port_type) {
	case AIROHA_PCS_ETH:
		airoha_pcs_setup_scu_eth(priv, interface);
		break;
	case AIROHA_PCS_PON:
		airoha_pcs_setup_scu_pon(priv, interface);
		break;
	case AIROHA_PCS_PCIE:
		airoha_pcs_setup_scu_pcie(priv, index, interface);
		break;
	case AIROHA_PCS_USB:
		break;
	}

	ret = reset_control_bulk_assert(ARRAY_SIZE(priv->rsts),
					priv->rsts);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(ARRAY_SIZE(priv->rsts),
					  priv->rsts);
	if (ret)
		return ret;

	return 0;
}

static void airoha_pcs_init_usxgmii(struct airoha_pcs_priv *priv, int index)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];

	regmap_set_bits(maps->multi_sgmii, AIROHA_PCS_MULTI_SGMII_MSG_RX_CTRL_0,
			AIROHA_PCS_HSGMII_XFI_SEL);

	/* Disable Hibernation */
	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_CTROL_1,
			  AIROHA_PCS_USXGMII_SPEED_SEL_H);

	/* FIXME: wait Airoha */
	/* Avoid PCS sending garbage to MAC in some HW revision (E0) */
	regmap_write(maps->usxgmii_pcs, AIROHA_PCS_USGMII_VENDOR_DEFINE_116, 0);
}

static void airoha_pcs_init_hsgmii(struct airoha_pcs_priv *priv, int index)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];

	regmap_clear_bits(maps->multi_sgmii, AIROHA_PCS_MULTI_SGMII_MSG_RX_CTRL_0,
			  AIROHA_PCS_HSGMII_XFI_SEL);

	regmap_update_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_1,
			   AIROHA_PCS_TBI_10B_MODE,
			   priv->phy ? 0 : AIROHA_PCS_TBI_10B_MODE);
}

static void airoha_pcs_init_sgmii(struct airoha_pcs_priv *priv, int index)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];

	regmap_clear_bits(maps->multi_sgmii, AIROHA_PCS_MULTI_SGMII_MSG_RX_CTRL_0,
			  AIROHA_PCS_HSGMII_XFI_SEL);

	regmap_set_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_1,
			AIROHA_PCS_TBI_10B_MODE);

	regmap_update_bits(maps->hsgmii_rate_adp, AIROHA_PCS_HSGMII_RATE_ADAPT_CTRL_6,
			   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_DOUT_L,
			   FIELD_PREP(AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_DOUT_L, 0x07070707));

	regmap_update_bits(maps->hsgmii_rate_adp, AIROHA_PCS_HSGMII_RATE_ADAPT_CTRL_8,
			   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_DOUT_C,
			   FIELD_PREP(AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_DOUT_C, 0xff));
}

static void airoha_pcs_init(struct airoha_pcs_priv *priv,
			    int index, phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		airoha_pcs_init_sgmii(priv, index);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		airoha_pcs_init_hsgmii(priv, index);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10GBASER:
		airoha_pcs_init_usxgmii(priv, index);
		break;
	default:
		return;
	}
}

static void airoha_pcs_interrupt_init_sgmii(struct airoha_pcs_priv *priv,
					    int index)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];

	/* Disable every interrupt */
	regmap_clear_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_HSGMII_MODE_INTERRUPT,
			  AIROHA_PCS_HSGMII_MODE2_REMOVE_FAULT_OCCUR_INT |
			  AIROHA_PCS_HSGMII_MODE2_AN_CL37_TIMERDONE_INT |
			  AIROHA_PCS_HSGMII_MODE2_AN_MIS_INT |
			  AIROHA_PCS_HSGMII_MODE2_RX_SYN_DONE_INT |
			  AIROHA_PCS_HSGMII_MODE2_AN_DONE_INT);

	/* Clear interrupt */
	regmap_set_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_HSGMII_MODE_INTERRUPT,
			AIROHA_PCS_HSGMII_MODE2_REMOVE_FAULT_OCCUR_INT_CLEAR |
			AIROHA_PCS_HSGMII_MODE2_AN_CL37_TIMERDONE_INT_CLEAR |
			AIROHA_PCS_HSGMII_MODE2_AN_MIS_INT_CLEAR |
			AIROHA_PCS_HSGMII_MODE2_RX_SYN_DONE_INT_CLEAR |
			AIROHA_PCS_HSGMII_MODE2_AN_DONE_INT_CLEAR);

	regmap_clear_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_HSGMII_MODE_INTERRUPT,
			  AIROHA_PCS_HSGMII_MODE2_REMOVE_FAULT_OCCUR_INT_CLEAR |
			  AIROHA_PCS_HSGMII_MODE2_AN_CL37_TIMERDONE_INT_CLEAR |
			  AIROHA_PCS_HSGMII_MODE2_AN_MIS_INT_CLEAR |
			  AIROHA_PCS_HSGMII_MODE2_RX_SYN_DONE_INT_CLEAR |
			  AIROHA_PCS_HSGMII_MODE2_AN_DONE_INT_CLEAR);
}

static void airoha_pcs_interrupt_init_usxgmii(struct airoha_pcs_priv *priv,
					      int index)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];

	/* Disable every Interrupt */
	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_CTRL_0,
			  AIROHA_PCS_USXGMII_T_TYPE_T_INT_EN |
			  AIROHA_PCS_USXGMII_T_TYPE_D_INT_EN |
			  AIROHA_PCS_USXGMII_T_TYPE_C_INT_EN |
			  AIROHA_PCS_USXGMII_T_TYPE_S_INT_EN);

	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_CTRL_1,
			  AIROHA_PCS_USXGMII_R_TYPE_C_INT_EN |
			  AIROHA_PCS_USXGMII_R_TYPE_S_INT_EN |
			  AIROHA_PCS_USXGMII_TXPCS_FSM_ENC_ERR_INT_EN |
			  AIROHA_PCS_USXGMII_T_TYPE_E_INT_EN);

	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_CTRL_2,
			  AIROHA_PCS_USXGMII_RPCS_FSM_DEC_ERR_INT_EN |
			  AIROHA_PCS_USXGMII_R_TYPE_E_INT_EN |
			  AIROHA_PCS_USXGMII_R_TYPE_T_INT_EN |
			  AIROHA_PCS_USXGMII_R_TYPE_D_INT_EN);

	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_CTRL_3,
			  AIROHA_PCS_USXGMII_FAIL_SYNC_XOR_ST_INT_EN |
			  AIROHA_PCS_USXGMII_RX_BLOCK_LOCK_ST_INT_EN |
			  AIROHA_PCS_USXGMII_LINK_UP_ST_INT_EN |
			  AIROHA_PCS_USXGMII_HI_BER_ST_INT_EN);

	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_CTRL_4,
			  AIROHA_PCS_USXGMII_LINK_DOWN_ST_INT_EN);

	/* Clear any pending interrupt */
	regmap_set_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_INT_STA_2,
			AIROHA_PCS_USXGMII_RPCS_FSM_DEC_ERR_INT |
			AIROHA_PCS_USXGMII_R_TYPE_E_INT |
			AIROHA_PCS_USXGMII_R_TYPE_T_INT |
			AIROHA_PCS_USXGMII_R_TYPE_D_INT);

	regmap_set_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_INT_STA_3,
			AIROHA_PCS_USXGMII_FAIL_SYNC_XOR_ST_INT |
			AIROHA_PCS_USXGMII_RX_BLOCK_LOCK_ST_INT |
			AIROHA_PCS_USXGMII_LINK_UP_ST_INT |
			AIROHA_PCS_USXGMII_HI_BER_ST_INT);

	regmap_set_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_INT_STA_4,
			AIROHA_PCS_USXGMII_LINK_DOWN_ST_INT);

	/* Interrupt saddly seems to be not weel supported for Link Down.
	 * PCS Poll is a must to correctly read and react on Cable Deatch
	 * as only cable attach interrupt are fired and Link Down interrupt
	 * are fired only in special case like AN restart.
	 */
}

static void airoha_pcs_interrupt_init(struct airoha_pcs_priv *priv,
				      int index, phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return airoha_pcs_interrupt_init_sgmii(priv, index);
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10GBASER:
		return airoha_pcs_interrupt_init_usxgmii(priv, index);
	default:
		return;
	}
}

static void airoha_pcs_get_state_sgmii(struct airoha_pcs_priv *priv,
				       unsigned int neg_mode, int index,
				       struct phylink_link_state *state)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];
	u32 bmsr = 0, lpa = 0;

	regmap_read(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_1,
		    &bmsr);
	regmap_read(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_5,
		    &lpa);

	bmsr = (AIROHA_PCS_HSGMII_AN_SGMII_AN_COMPLETE |
		AIROHA_PCS_HSGMII_AN_SGMII_REMOTE_FAULT |
		AIROHA_PCS_HSGMII_AN_SGMII_AN_ABILITY |
		AIROHA_PCS_HSGMII_AN_SGMII_LINK_STATUS) & bmsr;
	lpa = AIROHA_PCS_HSGMII_AN_SGMII_PARTNER_ABILITY & lpa;

	phylink_mii_c22_pcs_decode_state(state, neg_mode, bmsr, lpa);
}

static void airoha_pcs_get_state_hsgmii(struct airoha_pcs_priv *priv, int index,
					struct phylink_link_state *state)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];
	u32 bmsr = 0;

	regmap_read(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_1,
		    &bmsr);

	bmsr = (AIROHA_PCS_HSGMII_AN_SGMII_AN_COMPLETE |
		AIROHA_PCS_HSGMII_AN_SGMII_REMOTE_FAULT |
		AIROHA_PCS_HSGMII_AN_SGMII_AN_ABILITY |
		AIROHA_PCS_HSGMII_AN_SGMII_LINK_STATUS) & bmsr;

	state->link = !!(bmsr & BMSR_LSTATUS);
	state->an_complete = !!(bmsr & BMSR_ANEGCOMPLETE);
	state->speed = SPEED_2500;
	state->duplex = DUPLEX_FULL;
}

static void airoha_pcs_get_state_usxgmii(struct airoha_pcs_priv *priv, int index,
					 struct phylink_link_state *state)
{
	const struct airoha_pcs_match_data *data = priv->data;
	struct airoha_pcs_maps *maps = &priv->maps[index];
	u32 an_done = 0, lpa = 0;

	/* Trigger HW workaround if needed. If an error is reported,
	 * consider link down and test again later.
	 */
	if (data->rxlock_workaround && data->rxlock_workaround(priv, index)) {
		state->link = false;
		return;
	}

	/* Toggle AN Status */
	regmap_set_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_CONTROL_6,
			AIROHA_PCS_USXGMII_TOG_PCS_AUTONEG_STS);
	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_CONTROL_6,
			  AIROHA_PCS_USXGMII_TOG_PCS_AUTONEG_STS);

	regmap_read(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_STATS_0, &lpa);
	regmap_read(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_STATS_2, &an_done);

	state->link = !!(lpa & MDIO_USXGMII_LINK);
	state->an_complete = !!(an_done & AIROHA_PCS_USXGMII_PCS_AN_COMPLETE);

	phylink_decode_usxgmii_word(state, lpa);
}

static void airoha_pcs_get_state_10gbaser(struct airoha_pcs_priv *priv, int index,
					  struct phylink_link_state *state)
{
	struct airoha_pcs_maps *maps = &priv->maps[index];
	u32 status = 0, curr_mode = 0;

	/* Toggle AN Status */
	regmap_set_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_CONTROL_6,
			AIROHA_PCS_USXGMII_TOG_PCS_AUTONEG_STS);
	regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_CONTROL_6,
			  AIROHA_PCS_USXGMII_TOG_PCS_AUTONEG_STS);

	regmap_read(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_BASE_R_10GB_T_PCS_STUS_1,
		    &status);
	regmap_read(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_STATS_0, &curr_mode);

	state->link = !!(status & AIROHA_PCS_USXGMII_RX_LINK_STUS);

	switch (curr_mode & AIROHA_PCS_USXGMII_CUR_USXGMII_MODE) {
	case AIROHA_PCS_USXGMII_CUR_USXGMII_MODE_10G:
		state->speed = SPEED_10000;
		break;
	case AIROHA_PCS_USXGMII_CUR_USXGMII_MODE_5G:
		state->speed = SPEED_5000;
		break;
	case AIROHA_PCS_USXGMII_CUR_USXGMII_MODE_2_5G:
		state->speed = SPEED_2500;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		return;
	}

	state->duplex = DUPLEX_FULL;
}

static void airoha_pcs_get_state(struct phylink_pcs *pcs,
				 unsigned int neg_mode,
				 struct phylink_link_state *state)
{
	struct airoha_pcs_port *port = to_airoha_pcs_port(pcs);
	struct airoha_pcs_priv *priv = port->priv;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		airoha_pcs_get_state_sgmii(priv, neg_mode, port->index, state);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		airoha_pcs_get_state_hsgmii(priv, port->index, state);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		airoha_pcs_get_state_usxgmii(priv, port->index, state);
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		airoha_pcs_get_state_10gbaser(priv, port->index, state);
		break;
	default:
		return;
	}
}

static int airoha_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			     phy_interface_t interface,
			     const unsigned long *advertising,
			     bool permit_pause_to_mac)
{
	struct airoha_pcs_port *port = to_airoha_pcs_port(pcs);
	struct airoha_pcs_priv *priv = port->priv;
	const struct airoha_pcs_match_data *data;
	struct airoha_pcs_maps *maps;
	int index = port->index;
	u32 rate_adapt;
	int ret;

	maps = &priv->maps[port->index];
	port->interface = interface;
	data = priv->data;

	/* Apply Analog and Digital configuration for PCS */
	if (data->bringup) {
		ret = data->bringup(priv, index, interface);
		if (ret)
			return ret;
	}

	/* Set final configuration for various modes */
	airoha_pcs_init(priv, index, interface);

	/* Configure Interrupt for various modes */
	airoha_pcs_interrupt_init(priv, index, interface);

	rate_adapt = AIROHA_PCS_HSGMII_RATE_ADAPT_RX_EN |
		     AIROHA_PCS_HSGMII_RATE_ADAPT_TX_EN;

	if (interface == PHY_INTERFACE_MODE_SGMII)
		rate_adapt |= AIROHA_PCS_HSGMII_RATE_ADAPT_RX_BYPASS |
			      AIROHA_PCS_HSGMII_RATE_ADAPT_TX_BYPASS;

	/* AN Auto Settings (Rate Adaptation) */
	regmap_update_bits(maps->hsgmii_rate_adp, AIROHA_PCS_HSGMII_RATE_ADAPT_CTRL_0,
			   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_BYPASS |
			   AIROHA_PCS_HSGMII_RATE_ADAPT_TX_BYPASS |
			   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_EN |
			   AIROHA_PCS_HSGMII_RATE_ADAPT_TX_EN, rate_adapt);

	if (interface == PHY_INTERFACE_MODE_USXGMII ||
	    interface == PHY_INTERFACE_MODE_10GBASER) {
		if (interface == PHY_INTERFACE_MODE_USXGMII) {
			if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
				regmap_set_bits(maps->usxgmii_pcs,
						AIROHA_PCS_USXGMII_PCS_AN_CONTROL_0,
						AIROHA_PCS_USXGMII_AN_ENABLE);
			else
				regmap_clear_bits(maps->usxgmii_pcs,
						  AIROHA_PCS_USXGMII_PCS_AN_CONTROL_0,
						  AIROHA_PCS_USXGMII_AN_ENABLE);

			regmap_clear_bits(maps->usxgmii_pcs,
					  AIROHA_PCS_USXGMII_PCS_AN_CONTROL_7,
					  AIROHA_PCS_USXGMII_RATE_UPDATE_MODE);
		} else {
			regmap_clear_bits(maps->usxgmii_pcs,
					  AIROHA_PCS_USXGMII_PCS_AN_CONTROL_0,
					  AIROHA_PCS_USXGMII_AN_ENABLE);

			regmap_set_bits(maps->usxgmii_pcs,
					AIROHA_PCS_USXGMII_PCS_AN_CONTROL_7,
					AIROHA_PCS_USXGMII_RATE_UPDATE_MODE);
		}
	}

	/* Clear any force bit that my be set by bootloader */
	if (interface == PHY_INTERFACE_MODE_SGMII ||
	    interface == PHY_INTERFACE_MODE_1000BASEX ||
	    interface == PHY_INTERFACE_MODE_2500BASEX) {
		regmap_clear_bits(maps->multi_sgmii, AIROHA_PCS_MULTI_SGMII_SGMII_STS_CTRL_0,
				  AIROHA_PCS_LINK_MODE_P0 |
				  AIROHA_PCS_FORCE_SPD_MODE_P0 |
				  AIROHA_PCS_FORCE_LINKDOWN_P0 |
				  AIROHA_PCS_FORCE_LINKUP_P0);
	}

	/* Toggle Rate Adaption for SGMII/HSGMII mode */
	if (interface == PHY_INTERFACE_MODE_SGMII ||
	    interface == PHY_INTERFACE_MODE_1000BASEX ||
	    interface == PHY_INTERFACE_MODE_2500BASEX) {
		if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
			regmap_clear_bits(maps->hsgmii_rate_adp,
					  AIROHA_PCS_HSGMII_RATE_ADP_P0_CTRL_0,
					  AIROHA_PCS_HSGMII_P0_DIS_MII_MODE);
		else
			regmap_set_bits(maps->hsgmii_rate_adp,
					AIROHA_PCS_HSGMII_RATE_ADP_P0_CTRL_0,
					AIROHA_PCS_HSGMII_P0_DIS_MII_MODE);
	}

	/* Setup AN Link Timer */
	if (interface == PHY_INTERFACE_MODE_SGMII ||
	    interface == PHY_INTERFACE_MODE_1000BASEX) {
		u32 an_timer;

		an_timer = phylink_get_link_timer_ns(interface);

		/* Value needs to be shifted by 4, seems value is internally * 16 */
		regmap_update_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_11,
				   AIROHA_PCS_HSGMII_AN_SGMII_LINK_TIMER,
				   FIELD_PREP(AIROHA_PCS_HSGMII_AN_SGMII_LINK_TIMER,
					      an_timer >> 4));

		regmap_update_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_3,
				   AIROHA_PCS_HSGMII_PCS_LINK_STSTIME,
				   FIELD_PREP(AIROHA_PCS_HSGMII_PCS_LINK_STSTIME,
					      an_timer >> 4));
	}

	/* Setup SGMII AN and advertisement in DEV_ABILITY */
	if (interface == PHY_INTERFACE_MODE_SGMII) {
		if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
			int advertise = phylink_mii_c22_pcs_encode_advertisement(interface,
										 advertising);
			if (advertise < 0)
				return advertise;

			regmap_update_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_4,
					   AIROHA_PCS_HSGMII_AN_SGMII_DEV_ABILITY,
					   FIELD_PREP(AIROHA_PCS_HSGMII_AN_SGMII_DEV_ABILITY,
						      advertise));

			regmap_set_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_0,
					AIROHA_PCS_HSGMII_AN_SGMII_RA_ENABLE);
		} else {
			regmap_clear_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_0,
					  AIROHA_PCS_HSGMII_AN_SGMII_RA_ENABLE);
		}
	}

	if (interface == PHY_INTERFACE_MODE_2500BASEX) {
		regmap_clear_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_0,
				  AIROHA_PCS_HSGMII_AN_SGMII_RA_ENABLE);

		regmap_set_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_6,
				AIROHA_PCS_HSGMII_PCS_TX_ENABLE);
	}

	if (interface == PHY_INTERFACE_MODE_SGMII ||
	    interface == PHY_INTERFACE_MODE_1000BASEX) {
		u32 if_mode = AIROHA_PCS_HSGMII_AN_SIDEBAND_EN;

		/* Toggle SGMII or 1000base-x mode */
		if (interface == PHY_INTERFACE_MODE_SGMII)
			if_mode |= AIROHA_PCS_HSGMII_AN_SGMII_EN;

		if (neg_mode & PHYLINK_PCS_NEG_INBAND)
			regmap_set_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_13,
					AIROHA_PCS_HSGMII_AN_SGMII_REMOTE_FAULT_DIS);
		else
			regmap_clear_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_13,
					  AIROHA_PCS_HSGMII_AN_SGMII_REMOTE_FAULT_DIS);

		if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
			/* Clear force speed bits and MAC mode */
			regmap_clear_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_6,
					  AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_10 |
					  AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_100 |
					  AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_1000 |
					  AIROHA_PCS_HSGMII_PCS_MAC_MODE |
					  AIROHA_PCS_HSGMII_PCS_FORCE_RATEADAPT_VAL |
					  AIROHA_PCS_HSGMII_PCS_FORCE_RATEADAPT);
		} else {
			/* Enable compatibility with MAC PCS Layer */
			if_mode |= AIROHA_PCS_HSGMII_AN_SGMII_COMPAT_EN;

			/* AN off force rate adaption, speed is set later in Link Up */
			regmap_set_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_6,
					AIROHA_PCS_HSGMII_PCS_MAC_MODE |
					AIROHA_PCS_HSGMII_PCS_FORCE_RATEADAPT);
		}

		regmap_update_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_13,
				   AIROHA_PCS_HSGMII_AN_SGMII_IF_MODE_5_0, if_mode);

		regmap_set_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_6,
				AIROHA_PCS_HSGMII_PCS_TX_ENABLE |
				AIROHA_PCS_HSGMII_PCS_MODE2_EN);
	}

	if (interface == PHY_INTERFACE_MODE_1000BASEX &&
	    neg_mode != PHYLINK_PCS_NEG_INBAND_ENABLED) {
		regmap_set_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_1,
				AIROHA_PCS_SGMII_SEND_AN_ERR_EN);

		regmap_set_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_FORCE_CL37,
				AIROHA_PCS_HSGMII_AN_FORCE_AN_DONE);
	}

	if (interface == PHY_INTERFACE_MODE_2500BASEX) {
		regmap_set_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_0,
				AIROHA_PCS_HSGMII_AN_SGMII_RESET_PHY);
	}

	/* Configure Flow Control on XFI */
	regmap_update_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_GIB_CFG,
			   AIROHA_PCS_XFI_TX_FC_EN | AIROHA_PCS_XFI_RX_FC_EN,
			   permit_pause_to_mac ?
				AIROHA_PCS_XFI_TX_FC_EN | AIROHA_PCS_XFI_RX_FC_EN :
				0);

	return 0;
}

static void airoha_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct airoha_pcs_port *port = to_airoha_pcs_port(pcs);
	struct airoha_pcs_priv *priv = port->priv;
	struct airoha_pcs_maps *maps;

	maps = &priv->maps[port->index];

	switch (port->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		regmap_set_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_0,
				AIROHA_PCS_HSGMII_AN_SGMII_AN_RESTART);
		udelay(3);
		regmap_clear_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_0,
				  AIROHA_PCS_HSGMII_AN_SGMII_AN_RESTART);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		regmap_set_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_CONTROL_0,
				AIROHA_PCS_USXGMII_AN_RESTART);
		udelay(3);
		regmap_clear_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_CONTROL_0,
				  AIROHA_PCS_USXGMII_AN_RESTART);
		break;
	default:
		return;
	}
}

static void airoha_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
			       phy_interface_t interface, int speed, int duplex)
{
	struct airoha_pcs_port *port = to_airoha_pcs_port(pcs);
	struct airoha_pcs_priv *priv = port->priv;
	const struct airoha_pcs_match_data *data;
	struct airoha_pcs_maps *maps;

	maps = &priv->maps[port->index];
	data = priv->data;

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) {
		if (interface == PHY_INTERFACE_MODE_SGMII) {
			regmap_update_bits(maps->hsgmii_rate_adp,
					   AIROHA_PCS_HSGMII_RATE_ADAPT_CTRL_1,
					   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_WR_THR |
					   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_RD_THR,
					   FIELD_PREP(AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_WR_THR, 0x0) |
					   FIELD_PREP(AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_RD_THR, 0x0));
			udelay(1);
			regmap_update_bits(maps->hsgmii_rate_adp,
					   AIROHA_PCS_HSGMII_RATE_ADAPT_CTRL_1,
					   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_WR_THR |
					   AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_RD_THR,
					   FIELD_PREP(AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_WR_THR, 0xf) |
					   FIELD_PREP(AIROHA_PCS_HSGMII_RATE_ADAPT_RX_AFIFO_RD_THR, 0x5));
		}
	} else {
		if (interface == PHY_INTERFACE_MODE_USXGMII ||
		    interface == PHY_INTERFACE_MODE_10GBASER) {
			u32 mode;
			u32 rate_adapt;

			switch (speed) {
			case SPEED_10000:
				rate_adapt = AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE_10000;
				mode = AIROHA_PCS_USXGMII_MODE_10000;
				break;
			case SPEED_5000:
				rate_adapt = AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE_5000;
				mode = AIROHA_PCS_USXGMII_MODE_5000;
				break;
			case SPEED_2500:
				rate_adapt = AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE_2500;
				mode = AIROHA_PCS_USXGMII_MODE_2500;
				break;
			case SPEED_1000:
				rate_adapt = AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE_1000;
				mode = AIROHA_PCS_USXGMII_MODE_1000;
				break;
			case SPEED_100:
				rate_adapt = AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE_100;
				mode = AIROHA_PCS_USXGMII_MODE_100;
				break;
			default:
				/* Not supported */
				return;
			}

			/* Force USXGMII to selected speed */
			regmap_update_bits(maps->usxgmii_pcs, AIROHA_PCS_USXGMII_PCS_AN_CONTROL_7,
					   AIROHA_PCS_USXGMII_MODE, mode);

			if (interface == PHY_INTERFACE_MODE_10GBASER)
				regmap_update_bits(maps->hsgmii_rate_adp, AIROHA_PCS_HSGMII_RATE_ADAPT_CTRL_11,
						   AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE_EN |
						   AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE,
						   AIROHA_PCS_HSGMII_RATE_ADPT_FORCE_RATE_ADAPT_MODE_EN |
						   rate_adapt);
		}

		if (interface == PHY_INTERFACE_MODE_SGMII ||
		    interface == PHY_INTERFACE_MODE_1000BASEX) {
			u32 force_speed;
			u32 rate_adapt;

			switch (speed) {
			case SPEED_1000:
				force_speed = AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_1000;
				rate_adapt = AIROHA_PCS_HSGMII_PCS_FORCE_RATEADAPT_VAL_1000;
				break;
			case SPEED_100:
				force_speed = AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_100;
				rate_adapt = AIROHA_PCS_HSGMII_PCS_FORCE_RATEADAPT_VAL_100;
				break;
			case SPEED_10:
				force_speed = AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_10;
				rate_adapt = AIROHA_PCS_HSGMII_PCS_FORCE_RATEADAPT_VAL_10;
				break;
			default:
				/* Not supported */
				return;
			}

			regmap_update_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_CTROL_6,
					   AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_10 |
					   AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_100 |
					   AIROHA_PCS_HSGMII_PCS_SGMII_SPD_FORCE_1000 |
					   AIROHA_PCS_HSGMII_PCS_FORCE_RATEADAPT_VAL,
					   force_speed | rate_adapt);
		}

		if (interface == PHY_INTERFACE_MODE_SGMII ||
		    interface == PHY_INTERFACE_MODE_2500BASEX) {
			u32 ck_gen_mode;
			u32 speed_reg;
			u32 if_mode;

			switch (speed) {
			case SPEED_2500:
				speed_reg = AIROHA_PCS_LINK_MODE_P0_2_5G;
				break;
			case SPEED_1000:
				speed_reg = AIROHA_PCS_LINK_MODE_P0_1G;
				if_mode = AIROHA_PCS_HSGMII_AN_SPEED_FORCE_MODE_1000;
				ck_gen_mode = AIROHA_PCS_HSGMII_PCS_FORCE_CUR_SGMII_MODE_1000;
				break;
			case SPEED_100:
				speed_reg = AIROHA_PCS_LINK_MODE_P0_100M;
				if_mode = AIROHA_PCS_HSGMII_AN_SPEED_FORCE_MODE_100;
				ck_gen_mode = AIROHA_PCS_HSGMII_PCS_FORCE_CUR_SGMII_MODE_100;
				break;
			case SPEED_10:
				speed_reg = AIROHA_PCS_LINK_MODE_P0_10M;
				if_mode = AIROHA_PCS_HSGMII_AN_SPEED_FORCE_MODE_10;
				ck_gen_mode = AIROHA_PCS_HSGMII_PCS_FORCE_CUR_SGMII_MODE_10;
				break;
			}

			if (interface == PHY_INTERFACE_MODE_SGMII) {
				regmap_update_bits(maps->hsgmii_an, AIROHA_PCS_HSGMII_AN_SGMII_REG_AN_13,
						   AIROHA_PCS_HSGMII_AN_SPEED_FORCE_MODE,
						   if_mode);

				regmap_update_bits(maps->hsgmii_pcs, AIROHA_PCS_HSGMII_PCS_AN_SGMII_MODE_FORCE,
						   AIROHA_PCS_HSGMII_PCS_FORCE_CUR_SGMII_MODE |
						   AIROHA_PCS_HSGMII_PCS_FORCE_CUR_SGMII_MODE_SEL,
						   ck_gen_mode |
						   AIROHA_PCS_HSGMII_PCS_FORCE_CUR_SGMII_MODE_SEL);
			}

			regmap_update_bits(maps->multi_sgmii, AIROHA_PCS_MULTI_SGMII_SGMII_STS_CTRL_0,
					   AIROHA_PCS_LINK_MODE_P0 |
					   AIROHA_PCS_FORCE_SPD_MODE_P0,
					   speed_reg |
					   AIROHA_PCS_FORCE_SPD_MODE_P0);
		}
	}

	if (data->link_up)
		data->link_up(priv, port->index);

	/* BPI BMI enable */
	regmap_clear_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_GIB_CFG,
			  AIROHA_PCS_XFI_RXMPI_STOP |
			  AIROHA_PCS_XFI_RXMBI_STOP |
			  AIROHA_PCS_XFI_TXMPI_STOP |
			  AIROHA_PCS_XFI_TXMBI_STOP);
}

static void airoha_pcs_link_down(struct phylink_pcs *pcs)
{
	struct airoha_pcs_port *port = to_airoha_pcs_port(pcs);
	struct airoha_pcs_priv *priv = port->priv;
	struct airoha_pcs_maps *maps;

	maps = &priv->maps[port->index];

	/* MPI MBI disable */
	regmap_set_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_GIB_CFG,
			AIROHA_PCS_XFI_RXMPI_STOP |
			AIROHA_PCS_XFI_RXMBI_STOP |
			AIROHA_PCS_XFI_TXMPI_STOP |
			AIROHA_PCS_XFI_TXMBI_STOP);
}

static void airoha_pcs_pre_config(struct phylink_pcs *pcs,
				  phy_interface_t interface)
{
	struct airoha_pcs_port *port = to_airoha_pcs_port(pcs);
	struct airoha_pcs_priv *priv = port->priv;
	struct airoha_pcs_maps *maps;

	maps = &priv->maps[port->index];

	/* Select HSGMII or USXGMII in SCU regs */
	airoha_pcs_setup_scu(priv, port->index, interface);

	/* MPI MBI disable */
	regmap_set_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_GIB_CFG,
			AIROHA_PCS_XFI_RXMPI_STOP |
			AIROHA_PCS_XFI_RXMBI_STOP |
			AIROHA_PCS_XFI_TXMPI_STOP |
			AIROHA_PCS_XFI_TXMBI_STOP);

	/* Write 1 to trigger reset and clear */
	regmap_clear_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_LOGIC_RST,
			  AIROHA_PCS_XFI_MAC_LOGIC_RST);
	regmap_set_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_LOGIC_RST,
			AIROHA_PCS_XFI_MAC_LOGIC_RST);

	usleep_range(1000, 2000);

	/* Clear XFI MAC counter */
	regmap_set_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_CNT_CLR,
			AIROHA_PCS_XFI_GLB_CNT_CLR);
}

static int airoha_pcs_post_config(struct phylink_pcs *pcs,
				  phy_interface_t interface)
{
	struct airoha_pcs_port *port = to_airoha_pcs_port(pcs);
	struct airoha_pcs_priv *priv = port->priv;
	struct airoha_pcs_maps *maps;

	maps = &priv->maps[port->index];

	/* Frag disable */
	regmap_update_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_GIB_CFG,
			   AIROHA_PCS_XFI_RX_FRAG_LEN,
			   FIELD_PREP(AIROHA_PCS_XFI_RX_FRAG_LEN, 31));
	regmap_update_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_GIB_CFG,
			   AIROHA_PCS_XFI_TX_FRAG_LEN,
			   FIELD_PREP(AIROHA_PCS_XFI_TX_FRAG_LEN, 31));

	/* IPG NUM */
	regmap_update_bits(maps->pcs_mac, AIROHA_PCS_XFI_MAC_XFI_GIB_CFG,
			   AIROHA_PCS_XFI_IPG_NUM,
			   FIELD_PREP(AIROHA_PCS_XFI_IPG_NUM, 10));

	return 0;
}

static unsigned int airoha_pcs_inband_caps(struct phylink_pcs *pcs,
					   phy_interface_t interface)
{
	return LINK_INBAND_ENABLE | LINK_INBAND_DISABLE;
}

static const struct phylink_pcs_ops airoha_pcs_ops = {
	.pcs_inband_caps = airoha_pcs_inband_caps,
	.pcs_pre_config = airoha_pcs_pre_config,
	.pcs_post_config = airoha_pcs_post_config,
	.pcs_get_state = airoha_pcs_get_state,
	.pcs_config = airoha_pcs_config,
	.pcs_an_restart = airoha_pcs_an_restart,
	.pcs_link_up = airoha_pcs_link_up,
	.pcs_link_down = airoha_pcs_link_down,
};

static int airoha_pcs_init_named_regmap(struct platform_device *pdev,
					const char *name, struct regmap **regmap)
{
	struct regmap_config config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
	};
	void __iomem *base;

	base = devm_platform_ioremap_resource_byname(pdev, name);
	if (IS_ERR(base))
		return PTR_ERR(base);

	config.name = name;
	*regmap = devm_regmap_init_mmio(&pdev->dev, base, &config);

	return PTR_ERR_OR_ZERO(*regmap);
}

static int airoha_pcs_alloc_maps(struct platform_device *pdev,
				 struct airoha_pcs_priv *priv)
{
	struct airoha_pcs_maps *maps = &priv->maps[0];
	int ret;

	ret = airoha_pcs_init_named_regmap(pdev, "pcs_mac", &maps->pcs_mac);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_an", &maps->hsgmii_an);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_pcs", &maps->hsgmii_pcs);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_rate_adp", &maps->hsgmii_rate_adp);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "multi_sgmii", &maps->multi_sgmii);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "usxgmii", &maps->usxgmii_pcs);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "pcs_pma", &priv->pcs_pma[0]);
	if (ret)
		return ret;

	return airoha_pcs_init_named_regmap(pdev, "pcs_ana", &priv->pcs_ana);
}

static int airoha_pcs_usb_alloc_maps(struct platform_device *pdev,
				     struct airoha_pcs_priv *priv)
{
	struct airoha_pcs_maps *maps = &priv->maps[0];
	int ret;

	ret = airoha_pcs_init_named_regmap(pdev, "pcs_mac", &maps->pcs_mac);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_an", &maps->hsgmii_an);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_pcs", &maps->hsgmii_pcs);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_rate_adp", &maps->hsgmii_rate_adp);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "multi_sgmii", &maps->multi_sgmii);
	if (ret)
		return ret;

	return airoha_pcs_init_named_regmap(pdev, "pcs_ana", &priv->pcs_ana);
}

static int airoha_pcs_pcie_alloc_maps(struct platform_device *pdev,
				      struct airoha_pcs_priv *priv)
{
	struct airoha_pcs_maps *maps = priv->maps;
	int ret;

	ret = airoha_pcs_init_named_regmap(pdev, "pcs_mac0", &maps[0].pcs_mac);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_an0", &maps[0].hsgmii_an);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_pcs0", &maps[0].hsgmii_pcs);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_rate_adp0", &maps[0].hsgmii_rate_adp);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "multi_sgmii0", &maps[0].multi_sgmii);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "usxgmii0", &maps[0].usxgmii_pcs);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "pcs_mac1", &maps[1].pcs_mac);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_an1", &maps[1].hsgmii_an);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_pcs1", &maps[1].hsgmii_pcs);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "hsgmii_rate_adp1", &maps[1].hsgmii_rate_adp);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "multi_sgmii1", &maps[1].multi_sgmii);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "usxgmii1", &maps[1].usxgmii_pcs);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "pcs_pma0", &priv->pcs_pma[0]);
	if (ret)
		return ret;

	ret = airoha_pcs_init_named_regmap(pdev, "pcs_pma1", &priv->pcs_pma[1]);
	if (ret)
		return ret;

	return airoha_pcs_init_named_regmap(pdev, "pcs_ana", &priv->pcs_ana);
}

static struct phylink_pcs *airoha_pcs_xlate(struct fwnode_reference_args *pcsspec,
					    void *data)
{
	struct airoha_pcs_priv *priv = data;
	struct device *dev = priv->dev;
	u64 index = 0;

	switch (priv->data->port_type) {
	case AIROHA_PCS_ETH:
	case AIROHA_PCS_PON:
	case AIROHA_PCS_USB:
		if (pcsspec->nargs) {
			dev_err(dev, "invalid number of cells in 'pcs-handle' property\n");
			return ERR_PTR(-EINVAL);
		}

		break;
	case AIROHA_PCS_PCIE:
		if (pcsspec->nargs != 1) {
			dev_err(dev, "invalid number of cells in 'pcs-handle' property\n");
			return ERR_PTR(-EINVAL);
		}

		break;
	}

	if (pcsspec->nargs)
		index = pcsspec->args[0];

	if (index >= priv->data->num_port) {
		dev_err(dev, "invalid index cell in 'pcs-handle' property\n");
		return ERR_PTR(-EINVAL);
	}

	return &priv->ports[index].pcs;
}

static int airoha_pcs_probe(struct platform_device *pdev)
{
	const struct airoha_pcs_match_data *data;
	struct fwnode_pcs_provider *pcs_provider;
	struct device *dev = &pdev->dev;
	struct airoha_pcs_priv *priv;
	int index, ret;

	data = of_device_get_match_data(dev);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ports = devm_kcalloc(dev, data->num_port,
				   sizeof(*priv->ports), GFP_KERNEL);
	if (!priv->ports)
		return -ENOMEM;

	priv->dev = dev;
	priv->data = data;

	if (data->port_type == AIROHA_PCS_USB) {
		struct phy *phy;

		phy = devm_phy_get(dev, NULL);
		if (IS_ERR(phy))
			return dev_err_probe(dev, PTR_ERR(phy), "failed to get phy\n");

		priv->phy = phy;
	}

	switch (data->port_type) {
	case AIROHA_PCS_ETH:
	case AIROHA_PCS_PON:
		ret = airoha_pcs_alloc_maps(pdev, priv);
		if (ret)
			return ret;

		break;
	case AIROHA_PCS_PCIE:
		ret = airoha_pcs_pcie_alloc_maps(pdev, priv);
		if (ret)
			return ret;

		break;
	case AIROHA_PCS_USB:
		ret = airoha_pcs_usb_alloc_maps(pdev, priv);
		if (ret)
			return ret;

		break;
	}

	if (data->alloc_regmap_fields) {
		ret = data->alloc_regmap_fields(priv);
		if (ret)
			return ret;
	}

	/* SCU is used to toggle XFI or HSGMII in global SoC registers */
	if (!priv->phy) {
		priv->scu = syscon_regmap_lookup_by_phandle(dev->of_node, "airoha,scu");
		if (IS_ERR(priv->scu))
			return PTR_ERR(priv->scu);
	}

	priv->rsts[0].id = "mac";
	priv->rsts[1].id = "phy";
	ret = devm_reset_control_bulk_get_optional_exclusive(dev, ARRAY_SIZE(priv->rsts),
							     priv->rsts);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get bulk reset lines\n");

	/* For Ethernet PCS, read the AN7581 SoC revision to check if
	 * manual rx calibration is needed. This is only limited to
	 * any SoC revision before E2.
	 */
	if (device_is_compatible(dev, "airoha,an7581-pcs-eth")) {
		u32 val;

		ret = regmap_read(priv->scu, AIROHA_SCU_PDIDR, &val);
		if (ret)
			return ret;

		if (FIELD_GET(AIROHA_SCU_PRODUCT_ID, val) < 0x2)
			priv->manual_rx_calib = true;
	}

	for (index = 0; index < data->num_port; index++) {
		struct airoha_pcs_port *port = &priv->ports[index];

		port->priv = priv;
		port->index = index;
		port->pcs.poll = true;
		port->pcs.ops = &airoha_pcs_ops;

		switch (data->port_type) {
		case AIROHA_PCS_ETH:
		case AIROHA_PCS_PON:
		case AIROHA_PCS_PCIE:
			__set_bit(PHY_INTERFACE_MODE_10GBASER,
				  port->pcs.supported_interfaces);
			__set_bit(PHY_INTERFACE_MODE_USXGMII,
				  port->pcs.supported_interfaces);
			fallthrough;
		case AIROHA_PCS_USB:
			__set_bit(PHY_INTERFACE_MODE_SGMII,
				  port->pcs.supported_interfaces);
			__set_bit(PHY_INTERFACE_MODE_1000BASEX,
				  port->pcs.supported_interfaces);
			__set_bit(PHY_INTERFACE_MODE_2500BASEX,
				  port->pcs.supported_interfaces);
			break;
		}
	}

	platform_set_drvdata(pdev, priv);

	pcs_provider = devm_fwnode_pcs_add_provider(dev, dev_fwnode(dev),
						    airoha_pcs_xlate, priv);
	if (IS_ERR(pcs_provider))
		return PTR_ERR(pcs_provider);

	return 0;
}

static const struct airoha_pcs_match_data an7581_pcs_eth = {
	.num_port = 1,
	.port_type = AIROHA_PCS_ETH,
	.alloc_regmap_fields = an7581_pcs_alloc_regmap_fields,
	.bringup = an7581_pcs_bringup,
	.link_up = an7581_pcs_phya_link_up,
	.rxlock_workaround = an7581_pcs_rxlock_workaround,
};

static const struct airoha_pcs_match_data an7581_pcs_pon = {
	.num_port = 1,
	.port_type = AIROHA_PCS_PON,
	.alloc_regmap_fields = an7581_pcs_alloc_regmap_fields,
	.bringup = an7581_pcs_bringup,
	.link_up = an7581_pcs_phya_link_up,
};

static const struct airoha_pcs_match_data an7581_pcs_pcie = {
	.num_port = 2,
	.port_type = AIROHA_PCS_PCIE,
	.alloc_regmap_fields = an7581_pcs_pcie_alloc_regmap_fields,
	.bringup = an7581_pcs_bringup,
	.link_up = an7581_pcs_phya_link_up,
};

static const struct airoha_pcs_match_data an7581_pcs_usb = {
	.num_port = 1,
	.port_type = AIROHA_PCS_USB,
	.bringup = an7581_pcs_usb_bringup,
};

static const struct of_device_id airoha_pcs_of_table[] = {
	{ .compatible = "airoha,an7581-pcs-eth", .data = &an7581_pcs_eth },
	{ .compatible = "airoha,an7581-pcs-pon", .data = &an7581_pcs_pon },
	{ .compatible = "airoha,an7581-pcs-pcie", .data = &an7581_pcs_pcie },
	{ .compatible = "airoha,an7581-pcs-usb", .data = &an7581_pcs_usb },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, airoha_pcs_of_table);

static struct platform_driver airoha_pcs_driver = {
	.driver = {
		.name	 = "airoha-pcs",
		.of_match_table = airoha_pcs_of_table,
	},
	.probe = airoha_pcs_probe,
};
module_platform_driver(airoha_pcs_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Airoha PCS driver");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
