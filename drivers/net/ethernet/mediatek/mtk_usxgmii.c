// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Henry Yen <henry.yen@mediatek.com>
 *         Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include "mtk_eth_soc.h"

static struct mtk_usxgmii_pcs *pcs_to_mtk_usxgmii_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mtk_usxgmii_pcs, pcs);
}

static int mtk_xfi_pextp_init(struct mtk_eth *eth)
{
	struct device *dev = eth->dev;
	struct device_node *r = dev->of_node;
	struct device_node *np;
	int i;

	for (i = 0; i < MTK_MAX_DEVS; i++) {
		np = of_parse_phandle(r, "mediatek,xfi-pextp", i);
		if (!np)
			break;

		eth->regmap_pextp[i] = syscon_node_to_regmap(np);
		if (IS_ERR(eth->regmap_pextp[i]))
			return PTR_ERR(eth->regmap_pextp[i]);
	}

	return 0;
}

static int mtk_xfi_pll_init(struct mtk_eth *eth)
{
	struct device_node *r = eth->dev->of_node;
	struct device_node *np;

	np = of_parse_phandle(r, "mediatek,xfi-pll", 0);
	if (!np)
		return -1;

	eth->usxgmii_pll = syscon_node_to_regmap(np);
	if (IS_ERR(eth->usxgmii_pll))
		return PTR_ERR(eth->usxgmii_pll);

	return 0;
}

static int mtk_toprgu_init(struct mtk_eth *eth)
{
	struct device_node *r = eth->dev->of_node;
	struct device_node *np;

	np = of_parse_phandle(r, "mediatek,toprgu", 0);
	if (!np)
		return -1;

	eth->toprgu = syscon_node_to_regmap(np);
	if (IS_ERR(eth->toprgu))
		return PTR_ERR(eth->toprgu);

	return 0;
}

static int mtk_xfi_pll_enable(struct mtk_eth *eth)
{
	u32 val = 0;

	if (!eth->usxgmii_pll)
		return -EINVAL;

	/* Add software workaround for USXGMII PLL TCL issue */
	regmap_write(eth->usxgmii_pll, XFI_PLL_ANA_GLB8, RG_XFI_PLL_ANA_SWWA);

	regmap_read(eth->usxgmii_pll, XFI_PLL_DIG_GLB8, &val);
	val |= RG_XFI_PLL_EN;
	regmap_write(eth->usxgmii_pll, XFI_PLL_DIG_GLB8, val);

	return 0;
}

static void mtk_usxgmii_setup_phya(struct regmap *pextp, phy_interface_t interface, int id)
{
	bool is_10g = (interface == PHY_INTERFACE_MODE_10GBASER ||
		       interface == PHY_INTERFACE_MODE_USXGMII);
	bool is_2p5g = (interface == PHY_INTERFACE_MODE_2500BASEX);
	bool is_5g = (interface == PHY_INTERFACE_MODE_5GBASER);

	/* Setup operation mode */
	if (is_10g)
		regmap_write(pextp, 0x9024, 0x00C9071C);
	else
		regmap_write(pextp, 0x9024, 0x00D9071C);

	if (is_5g)
		regmap_write(pextp, 0x2020, 0xAAA5A5AA);
	else
		regmap_write(pextp, 0x2020, 0xAA8585AA);

	if (is_2p5g || is_5g || is_10g) {
		regmap_write(pextp, 0x2030, 0x0C020707);
		regmap_write(pextp, 0x2034, 0x0E050F0F);
		regmap_write(pextp, 0x2040, 0x00140032);
	} else {
		regmap_write(pextp, 0x2030, 0x0C020207);
		regmap_write(pextp, 0x2034, 0x0E05050F);
		regmap_write(pextp, 0x2040, 0x00200032);
	}

	if (is_2p5g || is_10g)
		regmap_write(pextp, 0x50F0, 0x00C014AA);
	else if (is_5g)
		regmap_write(pextp, 0x50F0, 0x00C018AA);
	else
		regmap_write(pextp, 0x50F0, 0x00C014BA);

	if (is_5g) {
		regmap_write(pextp, 0x50E0, 0x3777812B);
		regmap_write(pextp, 0x506C, 0x005C9CFF);
		regmap_write(pextp, 0x5070, 0x9DFAFAFA);
		regmap_write(pextp, 0x5074, 0x273F3F3F);
		regmap_write(pextp, 0x5078, 0xA8883868);
		regmap_write(pextp, 0x507C, 0x14661466);
	} else {
		regmap_write(pextp, 0x50E0, 0x3777C12B);
		regmap_write(pextp, 0x506C, 0x005F9CFF);
		regmap_write(pextp, 0x5070, 0x9D9DFAFA);
		regmap_write(pextp, 0x5074, 0x27273F3F);
		regmap_write(pextp, 0x5078, 0xA7883C68);
		regmap_write(pextp, 0x507C, 0x11661166);
	}

	if (is_2p5g || is_10g) {
		regmap_write(pextp, 0x5080, 0x0E000AAF);
		regmap_write(pextp, 0x5084, 0x08080D0D);
		regmap_write(pextp, 0x5088, 0x02030909);
	} else if (is_5g) {
		regmap_write(pextp, 0x5080, 0x0E001ABF);
		regmap_write(pextp, 0x5084, 0x080B0D0D);
		regmap_write(pextp, 0x5088, 0x02050909);
	} else {
		regmap_write(pextp, 0x5080, 0x0E000EAF);
		regmap_write(pextp, 0x5084, 0x08080E0D);
		regmap_write(pextp, 0x5088, 0x02030B09);
	}

	if (is_5g) {
		regmap_write(pextp, 0x50E4, 0x0C000000);
		regmap_write(pextp, 0x50E8, 0x04000000);
	} else {
		regmap_write(pextp, 0x50E4, 0x0C0C0000);
		regmap_write(pextp, 0x50E8, 0x04040000);
	}

	if (is_2p5g || mtk_interface_mode_is_xgmii(interface))
		regmap_write(pextp, 0x50EC, 0x0F0F0C06);
	else
		regmap_write(pextp, 0x50EC, 0x0F0F0606);

	if (is_5g) {
		regmap_write(pextp, 0x50A8, 0x50808C8C);
		regmap_write(pextp, 0x6004, 0x18000000);
	} else {
		regmap_write(pextp, 0x50A8, 0x506E8C8C);
		regmap_write(pextp, 0x6004, 0x18190000);
	}

	if (is_10g)
		regmap_write(pextp, 0x00F8, 0x01423342);
	else if (is_5g)
		regmap_write(pextp, 0x00F8, 0x00A132A1);
	else if (is_2p5g)
		regmap_write(pextp, 0x00F8, 0x009C329C);
	else
		regmap_write(pextp, 0x00F8, 0x00FA32FA);

	/* Force SGDT_OUT off and select PCS */
	if (mtk_interface_mode_is_xgmii(interface))
		regmap_write(pextp, 0x00F4, 0x80201F20);
	else
		regmap_write(pextp, 0x00F4, 0x80201F21);

	/* Force GLB_CKDET_OUT */
	regmap_write(pextp, 0x0030, 0x00050C00);

	/* Force AEQ on */
	regmap_write(pextp, 0x0070, 0x02002800);
	ndelay(1020);

	/* Setup DA default value */
	regmap_write(pextp, 0x30B0, 0x00000020);
	regmap_write(pextp, 0x3028, 0x00008A01);
	regmap_write(pextp, 0x302C, 0x0000A884);
	regmap_write(pextp, 0x3024, 0x00083002);
	if (mtk_interface_mode_is_xgmii(interface)) {
		regmap_write(pextp, 0x3010, 0x00022220);
		regmap_write(pextp, 0x5064, 0x0F020A01);
		regmap_write(pextp, 0x50B4, 0x06100600);
		if (interface == PHY_INTERFACE_MODE_USXGMII)
			regmap_write(pextp, 0x3048, 0x40704000);
		else
			regmap_write(pextp, 0x3048, 0x47684100);
	} else {
		regmap_write(pextp, 0x3010, 0x00011110);
		regmap_write(pextp, 0x3048, 0x40704000);
	}

	if (!mtk_interface_mode_is_xgmii(interface) && !is_2p5g)
		regmap_write(pextp, 0x3064, 0x0000C000);

	if (interface == PHY_INTERFACE_MODE_USXGMII) {
		regmap_write(pextp, 0x3050, 0xA8000000);
		regmap_write(pextp, 0x3054, 0x000000AA);
	} else if (mtk_interface_mode_is_xgmii(interface)) {
		regmap_write(pextp, 0x3050, 0x00000000);
		regmap_write(pextp, 0x3054, 0x00000000);
	} else {
		regmap_write(pextp, 0x3050, 0xA8000000);
		regmap_write(pextp, 0x3054, 0x000000AA);
	}

	if (mtk_interface_mode_is_xgmii(interface))
		regmap_write(pextp, 0x306C, 0x00000F00);
	else if (is_2p5g)
		regmap_write(pextp, 0x306C, 0x22000F00);
	else
		regmap_write(pextp, 0x306C, 0x20200F00);

	if (interface == PHY_INTERFACE_MODE_10GBASER && id == 0)
		regmap_write(pextp, 0xA008, 0x0007B400);

	if (mtk_interface_mode_is_xgmii(interface))
		regmap_write(pextp, 0xA060, 0x00040000);
	else
		regmap_write(pextp, 0xA060, 0x00050000);

	if (is_10g)
		regmap_write(pextp, 0x90D0, 0x00000001);
	else if (is_5g)
		regmap_write(pextp, 0x90D0, 0x00000003);
	else if (is_2p5g)
		regmap_write(pextp, 0x90D0, 0x00000005);
	else
		regmap_write(pextp, 0x90D0, 0x00000007);

	/* Release reset */
	regmap_write(pextp, 0x0070, 0x0200E800);
	usleep_range(150, 500);

	/* Switch to P0 */
	regmap_write(pextp, 0x0070, 0x0200C111);
	ndelay(1020);
	regmap_write(pextp, 0x0070, 0x0200C101);
	usleep_range(15, 50);

	if (mtk_interface_mode_is_xgmii(interface)) {
		/* Switch to Gen3 */
		regmap_write(pextp, 0x0070, 0x0202C111);
	} else {
		/* Switch to Gen2 */
		regmap_write(pextp, 0x0070, 0x0201C111);
	}
	ndelay(1020);
	if (mtk_interface_mode_is_xgmii(interface))
		regmap_write(pextp, 0x0070, 0x0202C101);
	else
		regmap_write(pextp, 0x0070, 0x0201C101);
	usleep_range(100, 500);
	regmap_write(pextp, 0x30B0, 0x00000030);
	if (mtk_interface_mode_is_xgmii(interface))
		regmap_write(pextp, 0x00F4, 0x80201F00);
	else
		regmap_write(pextp, 0x00F4, 0x80201F01);

	regmap_write(pextp, 0x3040, 0x30000000);
	usleep_range(400, 1000);
}

static void mtk_usxgmii_reset(struct mtk_eth *eth, int id)
{
	u32 toggle, val;

	if (id >= MTK_MAX_DEVS || !eth->toprgu)
		return;

	switch (id) {
	case 0:
		toggle = SWSYSRST_XFI_PEXPT0_GRST | SWSYSRST_XFI0_GRST |
			 SWSYSRST_SGMII0_GRST;
		break;
	case 1:
		toggle = SWSYSRST_XFI_PEXPT1_GRST | SWSYSRST_XFI1_GRST |
			 SWSYSRST_SGMII1_GRST;
		break;
	default:
		return;
	}

	/* Enable software reset */
	regmap_set_bits(eth->toprgu, TOPRGU_SWSYSRST_EN, toggle);

	/* Assert USXGMII reset */
	regmap_set_bits(eth->toprgu, TOPRGU_SWSYSRST,
			FIELD_PREP(SWSYSRST_UNLOCK_KEY, 0x88) | toggle);

	usleep_range(100, 500);

	/* De-assert USXGMII reset */
	regmap_read(eth->toprgu, TOPRGU_SWSYSRST, &val);
	val |= FIELD_PREP(SWSYSRST_UNLOCK_KEY, 0x88);
	val &= ~toggle;
	regmap_write(eth->toprgu, TOPRGU_SWSYSRST, val);

	/* Disable software reset */
	regmap_clear_bits(eth->toprgu, TOPRGU_SWSYSRST_EN, toggle);

	mdelay(10);
}

/* As the USXGMII PHYA is shared with the 1000Base-X/2500Base-X/Cisco SGMII unit
 * the psc-mtk-lynxi instance needs to be wrapped, so that calls to .pcs_config
 * also trigger an initial reset and subsequent configuration of the PHYA.
 */
struct mtk_sgmii_wrapper_pcs {
	struct mtk_eth		*eth;
	struct phylink_pcs	*wrapped_pcs;
	u8			id;
	struct phylink_pcs	pcs;
};

static int mtk_sgmii_wrapped_pcs_config(struct phylink_pcs *pcs,
					unsigned int neg_mode,
					phy_interface_t interface,
					const unsigned long *advertising,
					bool permit_pause_to_mac)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);
	bool full_reconf;
	int ret;

	full_reconf = interface != wp->eth->usxgmii_pcs[wp->id]->interface;
	if (full_reconf) {
		mtk_xfi_pll_enable(wp->eth);
		mtk_usxgmii_reset(wp->eth, wp->id);
	}

	ret = wp->wrapped_pcs->ops->pcs_config(wp->wrapped_pcs, neg_mode, interface,
					       advertising, permit_pause_to_mac);

	if (full_reconf)
		mtk_usxgmii_setup_phya(wp->eth->regmap_pextp[wp->id], interface, wp->id);

	wp->eth->usxgmii_pcs[wp->id]->interface = interface;

	return ret;
}

static void mtk_sgmii_wrapped_pcs_get_state(struct phylink_pcs *pcs,
					    struct phylink_link_state *state)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	return wp->wrapped_pcs->ops->pcs_get_state(wp->wrapped_pcs, state);
}

static void mtk_sgmii_wrapped_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	wp->wrapped_pcs->ops->pcs_an_restart(wp->wrapped_pcs);
}

static void mtk_sgmii_wrapped_pcs_link_up(struct phylink_pcs *pcs,
					  unsigned int neg_mode,
					  phy_interface_t interface, int speed,
					  int duplex)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	wp->wrapped_pcs->ops->pcs_link_up(wp->wrapped_pcs, neg_mode, interface, speed, duplex);
}

static void mtk_sgmii_wrapped_pcs_disable(struct phylink_pcs *pcs)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	wp->wrapped_pcs->ops->pcs_disable(wp->wrapped_pcs);

	wp->eth->usxgmii_pcs[wp->id]->interface = PHY_INTERFACE_MODE_NA;
}

static const struct phylink_pcs_ops mtk_sgmii_wrapped_pcs_ops = {
	.pcs_get_state = mtk_sgmii_wrapped_pcs_get_state,
	.pcs_config = mtk_sgmii_wrapped_pcs_config,
	.pcs_an_restart = mtk_sgmii_wrapped_pcs_an_restart,
	.pcs_link_up = mtk_sgmii_wrapped_pcs_link_up,
	.pcs_disable = mtk_sgmii_wrapped_pcs_disable,
};

static int mtk_sgmii_wrapper_init(struct mtk_eth *eth)
{
	struct mtk_sgmii_wrapper_pcs *wp;
	int i;

	for (i = 0; i < MTK_MAX_DEVS; i++) {
		if (!eth->sgmii_pcs[i])
			continue;

		if (!eth->usxgmii_pcs[i])
			continue;

		/* Make sure all PCS ops are supported by wrapped PCS */
		if (!eth->sgmii_pcs[i]->ops->pcs_get_state ||
		    !eth->sgmii_pcs[i]->ops->pcs_config ||
		    !eth->sgmii_pcs[i]->ops->pcs_an_restart ||
		    !eth->sgmii_pcs[i]->ops->pcs_link_up ||
		    !eth->sgmii_pcs[i]->ops->pcs_disable)
			return -EOPNOTSUPP;

		wp = devm_kzalloc(eth->dev, sizeof(*wp), GFP_KERNEL);
		if (!wp)
			return -ENOMEM;

		wp->wrapped_pcs = eth->sgmii_pcs[i];
		wp->id = i;
		wp->pcs.neg_mode = true;
		wp->pcs.poll = true;
		wp->pcs.ops = &mtk_sgmii_wrapped_pcs_ops;
		wp->eth = eth;

		eth->usxgmii_pcs[i]->wrapped_sgmii_pcs = &wp->pcs;
	}

	return 0;
}

struct phylink_pcs *mtk_sgmii_wrapper_select_pcs(struct mtk_eth *eth, int mac_id)
{
	u32 xgmii_id = mtk_mac2xgmii_id(eth, mac_id);

	if (!eth->usxgmii_pcs[xgmii_id])
		return NULL;

	return eth->usxgmii_pcs[xgmii_id]->wrapped_sgmii_pcs;
}

static int mtk_usxgmii_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
				  phy_interface_t interface,
				  const unsigned long *advertising,
				  bool permit_pause_to_mac)
{
	struct mtk_usxgmii_pcs *mpcs = pcs_to_mtk_usxgmii_pcs(pcs);
	struct mtk_eth *eth = mpcs->eth;
	struct regmap *pextp = eth->regmap_pextp[mpcs->id];
	unsigned int an_ctrl = 0, link_timer = 0, xfi_mode = 0, adapt_mode = 0;
	bool mode_changed = false;

	if (!pextp)
		return -ENODEV;

	if (interface == PHY_INTERFACE_MODE_USXGMII) {
		an_ctrl = FIELD_PREP(USXGMII_AN_SYNC_CNT, 0x1FF) |
			  (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED) ?
			  USXGMII_AN_ENABLE : 0;
		link_timer = FIELD_PREP(USXGMII_LINK_TIMER_IDLE_DETECT, 0x7B) |
			     FIELD_PREP(USXGMII_LINK_TIMER_COMP_ACK_DETECT, 0x7B) |
			     FIELD_PREP(USXGMII_LINK_TIMER_AN_RESTART, 0x7B);
		xfi_mode = FIELD_PREP(USXGMII_XFI_RX_MODE, USXGMII_XFI_RX_MODE_10G) |
			   FIELD_PREP(USXGMII_XFI_TX_MODE, USXGMII_XFI_TX_MODE_10G);
	} else if (interface == PHY_INTERFACE_MODE_10GBASER) {
		an_ctrl = FIELD_PREP(USXGMII_AN_SYNC_CNT, 0x1FF);
		link_timer = FIELD_PREP(USXGMII_LINK_TIMER_IDLE_DETECT, 0x7B) |
			     FIELD_PREP(USXGMII_LINK_TIMER_COMP_ACK_DETECT, 0x7B) |
			     FIELD_PREP(USXGMII_LINK_TIMER_AN_RESTART, 0x7B);
		xfi_mode = FIELD_PREP(USXGMII_XFI_RX_MODE, USXGMII_XFI_RX_MODE_10G) |
			   FIELD_PREP(USXGMII_XFI_TX_MODE, USXGMII_XFI_TX_MODE_10G);
		adapt_mode = USXGMII_RATE_UPDATE_MODE;
	} else if (interface == PHY_INTERFACE_MODE_5GBASER) {
		an_ctrl = FIELD_PREP(USXGMII_AN_SYNC_CNT, 0xFF);
		link_timer = FIELD_PREP(USXGMII_LINK_TIMER_IDLE_DETECT, 0x3D) |
			     FIELD_PREP(USXGMII_LINK_TIMER_COMP_ACK_DETECT, 0x3D) |
			     FIELD_PREP(USXGMII_LINK_TIMER_AN_RESTART, 0x3D);
		xfi_mode = FIELD_PREP(USXGMII_XFI_RX_MODE, USXGMII_XFI_RX_MODE_5G) |
			   FIELD_PREP(USXGMII_XFI_TX_MODE, USXGMII_XFI_TX_MODE_5G);
		adapt_mode = USXGMII_RATE_UPDATE_MODE;
	} else {
		return -EINVAL;
	}

	adapt_mode |= FIELD_PREP(USXGMII_RATE_ADAPT_MODE, USXGMII_RATE_ADAPT_MODE_X1);

	if (mpcs->interface != interface) {
		mpcs->interface = interface;
		mode_changed = true;
	}

	mtk_xfi_pll_enable(eth);
	mtk_usxgmii_reset(eth, mpcs->id);

	/* Setup USXGMII AN ctrl */
	regmap_update_bits(mpcs->regmap, RG_PCS_AN_CTRL0,
			   USXGMII_AN_SYNC_CNT | USXGMII_AN_ENABLE,
			   an_ctrl);

	regmap_update_bits(mpcs->regmap, RG_PCS_AN_CTRL2,
			   USXGMII_LINK_TIMER_IDLE_DETECT |
			   USXGMII_LINK_TIMER_COMP_ACK_DETECT |
			   USXGMII_LINK_TIMER_AN_RESTART,
			   link_timer);

	mpcs->neg_mode = neg_mode;

	/* Gated MAC CK */
	regmap_update_bits(mpcs->regmap, RG_PHY_TOP_SPEED_CTRL1,
			   USXGMII_MAC_CK_GATED, USXGMII_MAC_CK_GATED);

	/* Enable interface force mode */
	regmap_update_bits(mpcs->regmap, RG_PHY_TOP_SPEED_CTRL1,
			   USXGMII_IF_FORCE_EN, USXGMII_IF_FORCE_EN);

	/* Setup USXGMII adapt mode */
	regmap_update_bits(mpcs->regmap, RG_PHY_TOP_SPEED_CTRL1,
			   USXGMII_RATE_UPDATE_MODE | USXGMII_RATE_ADAPT_MODE,
			   adapt_mode);

	/* Setup USXGMII speed */
	regmap_update_bits(mpcs->regmap, RG_PHY_TOP_SPEED_CTRL1,
			   USXGMII_XFI_RX_MODE | USXGMII_XFI_TX_MODE,
			   xfi_mode);

	usleep_range(1, 10);

	/* Un-gated MAC CK */
	regmap_update_bits(mpcs->regmap, RG_PHY_TOP_SPEED_CTRL1,
			   USXGMII_MAC_CK_GATED, 0);

	usleep_range(1, 10);

	/* Disable interface force mode for the AN mode */
	if (an_ctrl & USXGMII_AN_ENABLE)
		regmap_update_bits(mpcs->regmap, RG_PHY_TOP_SPEED_CTRL1,
				   USXGMII_IF_FORCE_EN, 0);

	/* Setup USXGMIISYS with the determined property */
	mtk_usxgmii_setup_phya(pextp, interface, mpcs->id);

	return mode_changed;
}

static void mtk_usxgmii_pcs_get_state(struct phylink_pcs *pcs,
				      struct phylink_link_state *state)
{
	struct mtk_usxgmii_pcs *mpcs = pcs_to_mtk_usxgmii_pcs(pcs);
	struct mtk_eth *eth = mpcs->eth;
	struct mtk_mac *mac = eth->mac[mtk_xgmii2mac_id(eth, mpcs->id)];
	u32 val = 0;

	regmap_read(mpcs->regmap, RG_PCS_AN_CTRL0, &val);
	if (FIELD_GET(USXGMII_AN_ENABLE, val)) {
		/* Refresh LPA by inverting LPA_LATCH */
		regmap_read(mpcs->regmap, RG_PCS_AN_STS0, &val);
		regmap_update_bits(mpcs->regmap, RG_PCS_AN_STS0,
				   USXGMII_LPA_LATCH,
				   !(val & USXGMII_LPA_LATCH));

		regmap_read(mpcs->regmap, RG_PCS_AN_STS0, &val);

		phylink_decode_usxgmii_word(state, FIELD_GET(USXGMII_PCS_AN_WORD,
							     val));

		state->interface = mpcs->interface;
	} else {
		val = mtk_r32(mac->hw, MTK_XGMAC_STS(mac->id));

		if (mac->id == MTK_GMAC2_ID)
			val >>= 16;

		switch (FIELD_GET(MTK_USXGMII_PCS_MODE, val)) {
		case 0:
			state->speed = SPEED_10000;
			break;
		case 1:
			state->speed = SPEED_5000;
			break;
		case 2:
			state->speed = SPEED_2500;
			break;
		case 3:
			state->speed = SPEED_1000;
			break;
		}

		state->interface = mpcs->interface;
		state->link = FIELD_GET(MTK_USXGMII_PCS_LINK, val);
		state->duplex = DUPLEX_FULL;
	}

	/* Continuously repeat re-configuration sequence until link comes up */
	if (state->link == 0)
		mtk_usxgmii_pcs_config(pcs, mpcs->neg_mode,
				       state->interface, NULL, false);
}

static void mtk_usxgmii_pcs_restart_an(struct phylink_pcs *pcs)
{
	struct mtk_usxgmii_pcs *mpcs = pcs_to_mtk_usxgmii_pcs(pcs);
	unsigned int val = 0;

	if (!mpcs->regmap)
		return;

	regmap_read(mpcs->regmap, RG_PCS_AN_CTRL0, &val);
	val |= USXGMII_AN_RESTART;
	regmap_write(mpcs->regmap, RG_PCS_AN_CTRL0, val);
}

static void mtk_usxgmii_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
				    phy_interface_t interface,
				    int speed, int duplex)
{
	/* Reconfiguring USXGMII to ensure the quality of the RX signal
	 * after the line side link up.
	 */
	mtk_usxgmii_pcs_config(pcs, neg_mode,
			       interface, NULL, false);
}

static const struct phylink_pcs_ops mtk_usxgmii_pcs_ops = {
	.pcs_config = mtk_usxgmii_pcs_config,
	.pcs_get_state = mtk_usxgmii_pcs_get_state,
	.pcs_an_restart = mtk_usxgmii_pcs_restart_an,
	.pcs_link_up = mtk_usxgmii_pcs_link_up,
};

int mtk_usxgmii_init(struct mtk_eth *eth)
{
	struct device_node *r = eth->dev->of_node;
	struct device *dev = eth->dev;
	struct device_node *np;
	int i, ret;

	for (i = 0; i < MTK_MAX_DEVS; i++) {
		np = of_parse_phandle(r, "mediatek,usxgmiisys", i);
		if (!np)
			break;

		eth->usxgmii_pcs[i] = devm_kzalloc(dev, sizeof(*eth->usxgmii_pcs), GFP_KERNEL);
		if (!eth->usxgmii_pcs[i])
			return -ENOMEM;

		eth->usxgmii_pcs[i]->id = i;
		eth->usxgmii_pcs[i]->eth = eth;
		eth->usxgmii_pcs[i]->regmap = syscon_node_to_regmap(np);
		if (IS_ERR(eth->usxgmii_pcs[i]->regmap))
			return PTR_ERR(eth->usxgmii_pcs[i]->regmap);

		eth->usxgmii_pcs[i]->pcs.ops = &mtk_usxgmii_pcs_ops;
		eth->usxgmii_pcs[i]->pcs.poll = true;
		eth->usxgmii_pcs[i]->pcs.neg_mode = true;
		eth->usxgmii_pcs[i]->interface = PHY_INTERFACE_MODE_NA;
		eth->usxgmii_pcs[i]->neg_mode = -1;

		of_node_put(np);
	}

	ret = mtk_xfi_pextp_init(eth);
	if (ret)
		return ret;

	ret = mtk_xfi_pll_init(eth);
	if (ret)
		return ret;

	ret = mtk_toprgu_init(eth);
	if (ret)
		return ret;

	return mtk_sgmii_wrapper_init(eth);
}

struct phylink_pcs *mtk_usxgmii_select_pcs(struct mtk_eth *eth, int mac_id)
{
	u32 xgmii_id = mtk_mac2xgmii_id(eth, mac_id);

	if (!eth->usxgmii_pcs[xgmii_id]->regmap)
		return NULL;

	return &eth->usxgmii_pcs[xgmii_id]->pcs;
}
