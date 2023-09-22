// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Henry Yen <henry.yen@mediatek.com>
 *         Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/mdio.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/pcs/pcs-mtk-lynxi.h>
#include <linux/pcs/pcs-mtk-usxgmii.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

/* USXGMII subsystem config registers */
/* Register to control speed */
#define RG_PHY_TOP_SPEED_CTRL1			0x80C
#define USXGMII_RATE_UPDATE_MODE		BIT(31)
#define USXGMII_MAC_CK_GATED			BIT(29)
#define USXGMII_IF_FORCE_EN			BIT(28)
#define USXGMII_RATE_ADAPT_MODE			GENMASK(10, 8)
#define USXGMII_RATE_ADAPT_MODE_X1		0
#define USXGMII_RATE_ADAPT_MODE_X2		1
#define USXGMII_RATE_ADAPT_MODE_X4		2
#define USXGMII_RATE_ADAPT_MODE_X10		3
#define USXGMII_RATE_ADAPT_MODE_X100		4
#define USXGMII_RATE_ADAPT_MODE_X5		5
#define USXGMII_RATE_ADAPT_MODE_X50		6
#define USXGMII_XFI_RX_MODE			GENMASK(6, 4)
#define USXGMII_XFI_RX_MODE_10G			0
#define USXGMII_XFI_RX_MODE_5G			1
#define USXGMII_XFI_TX_MODE			GENMASK(2, 0)
#define USXGMII_XFI_TX_MODE_10G			0
#define USXGMII_XFI_TX_MODE_5G			1

/* Register to control PCS AN */
#define RG_PCS_AN_CTRL0				0x810
#define USXGMII_AN_RESTART			BIT(31)
#define USXGMII_AN_SYNC_CNT			GENMASK(30, 11)
#define USXGMII_AN_ENABLE			BIT(0)

#define RG_PCS_AN_CTRL2				0x818
#define USXGMII_LINK_TIMER_IDLE_DETECT		GENMASK(29, 20)
#define USXGMII_LINK_TIMER_COMP_ACK_DETECT	GENMASK(19, 10)
#define USXGMII_LINK_TIMER_AN_RESTART		GENMASK(9, 0)

/* Register to read PCS AN status */
#define RG_PCS_AN_STS0				0x81c
#define USXGMII_PCS_AN_WORD			GENMASK(15, 0)
#define USXGMII_LPA_LATCH			BIT(31)

/* Register to read PCS link status */
#define RG_PCS_RX_STATUS0			0x904
#define RG_PCS_RX_STATUS_UPDATE			BIT(16)
#define RG_PCS_RX_LINK_STATUS			BIT(2)

/* Register to control USXGMII XFI PLL digital */
#define XFI_PLL_DIG_GLB8			0x08
#define RG_XFI_PLL_EN				BIT(31)

/* Register to control USXGMII XFI PLL analog */
#define XFI_PLL_ANA_GLB8			0x108
#define RG_XFI_PLL_ANA_SWWA			0x02283248

#define MTK_NETSYS_V3_AMA_RGC3			0x128

/* struct mtk_usxgmii_pcs - This structure holds each usxgmii PCS
 * @pcs:		Phylink PCS structure
 * @dev:		Pointer to device structure
 * @base:		IO memory to access PCS hardware
 * @clk:		Pointer to USXGMII clk
 * @pextp:		The PHYA instance attached to the PCS
 * @reset:		Pointer to USXGMII reset control
 * @regmap_pll:		The register map pointing at the range used to control
 *			the USXGMII SerDes PLL
 * @interface:		Currently selected interface mode
 * @neg_mode:		Currently used phylink neg_mode
 */
struct mtk_usxgmii_pcs {
	struct phylink_pcs		pcs;
	struct phylink_pcs		*sgmii_pcs;
	struct device			*dev;
	void __iomem			*base;
	struct clk			*clk;
	struct phy			*pextp;
	struct reset_control		*reset;
	struct regmap			*regmap_pll;
	phy_interface_t			interface;
	unsigned int			neg_mode;
};

/* struct mtk_sgmii_wrapper_pcs - Structure holding wrapped SGMII PCS
 * @usxgmii_pcs		Pointer to owning mtk_usxgmii_pcs structure
 * @pcs			Phylink PCS structure
 * @clks:		Pointers to 3 SGMII clks
 * @reset:		Pointer to SGMII reset control
 * @interface:		Currently selected interface mode
 * @neg_mode:		Currently used phylink neg_mode
 */
struct mtk_sgmii_wrapper_pcs {
	struct mtk_usxgmii_pcs		*usxgmii_pcs;
	struct phylink_pcs		*sgmii_pcs;
	struct phylink_pcs		pcs;
	struct clk			*clks[3];
	struct reset_control		*reset;
	phy_interface_t			interface;
	unsigned int			neg_mode;
	bool				permit_pause_to_mac;
};

static u32 mtk_r32(struct mtk_usxgmii_pcs *mpcs, unsigned int reg)
{
	return ioread32(mpcs->base + reg);
}

static void mtk_m32(struct mtk_usxgmii_pcs *mpcs, unsigned int reg, u32 mask, u32 set)
{
	u32 val;

	val = ioread32(mpcs->base + reg);
	val &= ~mask;
	val |= set;
	iowrite32(val, mpcs->base + reg);
}

static struct mtk_usxgmii_pcs *pcs_to_mtk_usxgmii_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mtk_usxgmii_pcs, pcs);
}

static int mtk_xfi_pextp_init(struct mtk_usxgmii_pcs *mpcs)
{
	struct device *dev = mpcs->dev;

	mpcs->pextp = devm_phy_get(dev, NULL);
	if (IS_ERR(mpcs->pextp)) {
		printk(KERN_INFO "PHYA got error\n");
		return PTR_ERR(mpcs->pextp);
	}

	if (!mpcs->pextp) {
		printk(KERN_INFO "PHYA not found\n");
		return -ENODEV;
	}

	return 0;
}

static int mtk_xfi_pll_init(struct mtk_usxgmii_pcs *mpcs)
{
	struct device_node *r = mpcs->dev->of_node;
	struct device_node *np;

	np = of_parse_phandle(r, "mediatek,xfi-pll", 0);
	if (!np)
		return -1;

	mpcs->regmap_pll = syscon_node_to_regmap(np);
	of_node_put(np);
	if (IS_ERR(mpcs->regmap_pll))
		return PTR_ERR(mpcs->regmap_pll);

	return 0;
}

static int mtk_xfi_pll_enable(struct mtk_usxgmii_pcs *mpcs)
{
	u32 val = 0;

	if (!mpcs->regmap_pll)
		return -EINVAL;

	/* Add software workaround for USXGMII PLL TCL issue */
	regmap_write(mpcs->regmap_pll, XFI_PLL_ANA_GLB8, RG_XFI_PLL_ANA_SWWA);

	regmap_read(mpcs->regmap_pll, XFI_PLL_DIG_GLB8, &val);
	val |= RG_XFI_PLL_EN;
	regmap_write(mpcs->regmap_pll, XFI_PLL_DIG_GLB8, val);

	return 0;
}

static void mtk_usxgmii_reset(struct mtk_usxgmii_pcs *mpcs)
{
	if (!mpcs->pextp)
		return;

	phy_reset(mpcs->pextp);

	reset_control_assert(mpcs->reset);
	usleep_range(100, 500);
	reset_control_deassert(mpcs->reset);

	mdelay(10);
}

static void mtk_sgmii_reset(struct mtk_sgmii_wrapper_pcs *wp)
{
	if (!wp->usxgmii_pcs->pextp)
		return;

	phy_reset(wp->usxgmii_pcs->pextp);

	reset_control_assert(wp->reset);
	usleep_range(100, 500);
	reset_control_deassert(wp->reset);

	mdelay(10);
}

static int mtk_sgmii_wrapped_pcs_config(struct phylink_pcs *pcs,
					unsigned int neg_mode,
					phy_interface_t interface,
					const unsigned long *advertising,
					bool permit_pause_to_mac)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);
	bool full_reconf;
	int ret;

	phy_power_on(wp->usxgmii_pcs->pextp);

	full_reconf = (interface != wp->interface);
	if (full_reconf) {
		mtk_xfi_pll_enable(wp->usxgmii_pcs);
		mtk_sgmii_reset(wp);
	}

	ret = wp->sgmii_pcs->ops->pcs_config(wp->sgmii_pcs, neg_mode, interface, advertising,
					     permit_pause_to_mac);

	if (full_reconf)
		phy_set_mode_ext(wp->usxgmii_pcs->pextp, PHY_MODE_ETHERNET, interface);

	wp->interface = interface;
	wp->neg_mode = neg_mode;
	wp->permit_pause_to_mac = permit_pause_to_mac;

	return ret;
}

static void mtk_sgmii_wrapped_pcs_get_state(struct phylink_pcs *pcs,
					    struct phylink_link_state *state)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	wp->sgmii_pcs->ops->pcs_get_state(wp->sgmii_pcs, state);

	/* Continuously repeat re-configuration sequence until link comes up */
	if (!state->link)
		mtk_sgmii_wrapped_pcs_config(pcs, wp->neg_mode, state->interface, NULL, wp->permit_pause_to_mac);
}

static void mtk_sgmii_wrapped_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	wp->sgmii_pcs->ops->pcs_an_restart(wp->sgmii_pcs);
}

static void mtk_sgmii_wrapped_pcs_link_up(struct phylink_pcs *pcs,
					  unsigned int neg_mode,
					  phy_interface_t interface, int speed,
					  int duplex)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	wp->sgmii_pcs->ops->pcs_link_up(wp->sgmii_pcs, neg_mode, interface, speed, duplex);
}

static void mtk_sgmii_wrapped_pcs_disable(struct phylink_pcs *pcs)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	wp->sgmii_pcs->ops->pcs_disable(wp->sgmii_pcs);
	wp->interface = PHY_INTERFACE_MODE_NA;
	wp->neg_mode = -1;

//	phy_power_off(wp->usxgmii_pcs->pextp);
}

static int mtk_sgmii_wrapped_pcs_enable(struct phylink_pcs *pcs)
{
	struct mtk_sgmii_wrapper_pcs *wp = container_of(pcs, struct mtk_sgmii_wrapper_pcs, pcs);

	if (wp->sgmii_pcs->ops->pcs_enable)
		wp->sgmii_pcs->ops->pcs_enable(wp->sgmii_pcs);

	wp->interface = PHY_INTERFACE_MODE_NA;
	wp->neg_mode = -1;

	phy_power_on(wp->usxgmii_pcs->pextp);

	return 0;
}

static const struct phylink_pcs_ops mtk_sgmii_wrapped_pcs_ops = {
	.pcs_get_state = mtk_sgmii_wrapped_pcs_get_state,
	.pcs_config = mtk_sgmii_wrapped_pcs_config,
	.pcs_an_restart = mtk_sgmii_wrapped_pcs_an_restart,
	.pcs_link_up = mtk_sgmii_wrapped_pcs_link_up,
	.pcs_disable = mtk_sgmii_wrapped_pcs_disable,
	.pcs_enable = mtk_sgmii_wrapped_pcs_enable,
};

static int mtk_sgmii_wrapper_init(struct mtk_usxgmii_pcs *mpcs)
{
	struct device_node *r = mpcs->dev->of_node, *np;
	struct mtk_sgmii_wrapper_pcs *wp;
	struct phylink_pcs *sgmii_pcs;
	struct reset_control *rstc;
	struct regmap *regmap;
	struct clk *sgmii_sel, *sgmii_rx, *sgmii_tx;

	np = of_parse_phandle(r, "mediatek,sgmiisys", 0);
	if (!np)
		return -ENODEV;

	regmap = syscon_node_to_regmap(np);
	rstc = of_reset_control_get_shared(r, "sgmii");
	of_node_put(np);

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	if (IS_ERR(rstc))
		return PTR_ERR(rstc);

	sgmii_sel = devm_clk_get_enabled(mpcs->dev, "sgmii_sel");
	if (IS_ERR(sgmii_sel))
		return PTR_ERR(sgmii_sel);

	sgmii_rx = devm_clk_get_enabled(mpcs->dev, "sgmii_rx");
	if (IS_ERR(sgmii_rx))
		return PTR_ERR(sgmii_rx);

	sgmii_tx = devm_clk_get_enabled(mpcs->dev, "sgmii_tx");
	if (IS_ERR(sgmii_tx))
		return PTR_ERR(sgmii_tx);

	sgmii_pcs = mtk_pcs_lynxi_create(mpcs->dev, regmap, MTK_NETSYS_V3_AMA_RGC3, 0);

	if (IS_ERR(sgmii_pcs))
		return PTR_ERR(sgmii_pcs);

	if (!sgmii_pcs)
		return -ENODEV;

	/* Make sure all PCS ops are supported by wrapped PCS */
	if (!sgmii_pcs->ops->pcs_get_state ||
	    !sgmii_pcs->ops->pcs_config ||
	    !sgmii_pcs->ops->pcs_an_restart ||
	    !sgmii_pcs->ops->pcs_link_up ||
	    !sgmii_pcs->ops->pcs_disable)
		return -EOPNOTSUPP;

	wp = devm_kzalloc(mpcs->dev, sizeof(*wp), GFP_KERNEL);
	if (!wp)
		return -ENOMEM;

	wp->pcs.neg_mode = sgmii_pcs->neg_mode;
	wp->pcs.ops = &mtk_sgmii_wrapped_pcs_ops;
	wp->pcs.poll = true;
	wp->sgmii_pcs = sgmii_pcs;
	wp->usxgmii_pcs = mpcs;
	wp->clks[0] = sgmii_sel;
	wp->clks[1] = sgmii_rx;
	wp->clks[2] = sgmii_tx;
	wp->reset = rstc;
	wp->interface = PHY_INTERFACE_MODE_NA;
	wp->neg_mode = -1;

	if (IS_ERR(wp->reset))
		return PTR_ERR(wp->reset);

	reset_control_deassert(wp->reset);

	mpcs->sgmii_pcs = &wp->pcs;

	return 0;
}

static int mtk_usxgmii_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
				  phy_interface_t interface,
				  const unsigned long *advertising,
				  bool permit_pause_to_mac)
{
	struct mtk_usxgmii_pcs *mpcs = pcs_to_mtk_usxgmii_pcs(pcs);
	unsigned int an_ctrl = 0, link_timer = 0, xfi_mode = 0, adapt_mode = 0;
	bool mode_changed = false;

	if (interface == PHY_INTERFACE_MODE_USXGMII) {
		an_ctrl = FIELD_PREP(USXGMII_AN_SYNC_CNT, 0x1FF) | USXGMII_AN_ENABLE;
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

	mtk_xfi_pll_enable(mpcs);
	phy_power_on(mpcs->pextp);
	mtk_usxgmii_reset(mpcs);

	/* Setup USXGMII AN ctrl */
	mtk_m32(mpcs, RG_PCS_AN_CTRL0,
		USXGMII_AN_SYNC_CNT | USXGMII_AN_ENABLE,
		an_ctrl);

	mtk_m32(mpcs, RG_PCS_AN_CTRL2,
		USXGMII_LINK_TIMER_IDLE_DETECT |
		USXGMII_LINK_TIMER_COMP_ACK_DETECT |
		USXGMII_LINK_TIMER_AN_RESTART,
		link_timer);

	mpcs->neg_mode = neg_mode;

	/* Gated MAC CK */
	mtk_m32(mpcs, RG_PHY_TOP_SPEED_CTRL1,
		USXGMII_MAC_CK_GATED, USXGMII_MAC_CK_GATED);

	/* Enable interface force mode */
	mtk_m32(mpcs, RG_PHY_TOP_SPEED_CTRL1,
		USXGMII_IF_FORCE_EN, USXGMII_IF_FORCE_EN);

	/* Setup USXGMII adapt mode */
	mtk_m32(mpcs, RG_PHY_TOP_SPEED_CTRL1,
		USXGMII_RATE_UPDATE_MODE | USXGMII_RATE_ADAPT_MODE,
		adapt_mode);

	/* Setup USXGMII speed */
	mtk_m32(mpcs, RG_PHY_TOP_SPEED_CTRL1,
		USXGMII_XFI_RX_MODE | USXGMII_XFI_TX_MODE,
		xfi_mode);

	usleep_range(1, 10);

	/* Un-gated MAC CK */
	mtk_m32(mpcs, RG_PHY_TOP_SPEED_CTRL1,
			   USXGMII_MAC_CK_GATED, 0);

	usleep_range(1, 10);

	/* Disable interface force mode for the AN mode */
	if (an_ctrl & USXGMII_AN_ENABLE)
		mtk_m32(mpcs, RG_PHY_TOP_SPEED_CTRL1,
				   USXGMII_IF_FORCE_EN, 0);

	/* Setup PHY for interface mode */
	phy_set_mode_ext(mpcs->pextp, PHY_MODE_ETHERNET, interface);

	return mode_changed;
}

static void mtk_usxgmii_pcs_get_state(struct phylink_pcs *pcs,
				      struct phylink_link_state *state)
{
	struct mtk_usxgmii_pcs *mpcs = pcs_to_mtk_usxgmii_pcs(pcs);
	u32 val;

	val = mtk_r32(mpcs, RG_PCS_AN_CTRL0);
	if (FIELD_GET(USXGMII_AN_ENABLE, val)) {
		/* Refresh LPA by inverting LPA_LATCH */
		val = mtk_r32(mpcs, RG_PCS_AN_STS0);
		mtk_m32(mpcs, RG_PCS_AN_STS0,
				   USXGMII_LPA_LATCH,
				   !(val & USXGMII_LPA_LATCH));

		val = mtk_r32(mpcs, RG_PCS_AN_STS0);

		phylink_decode_usxgmii_word(state, FIELD_GET(USXGMII_PCS_AN_WORD,
							     val));
	} else {
		/* Refresh USXGMII link status by toggling RG_PCS_AN_STATUS_UPDATE */
		mtk_m32(mpcs, RG_PCS_RX_STATUS0, RG_PCS_RX_STATUS_UPDATE, RG_PCS_RX_STATUS_UPDATE);
		ndelay(1020);
		mtk_m32(mpcs, RG_PCS_RX_STATUS0, RG_PCS_RX_STATUS_UPDATE, 0);
		ndelay(1020);

		/* Read USXGMII link status */
		val = mtk_r32(mpcs, RG_PCS_RX_STATUS0);
		state->link = FIELD_GET(RG_PCS_RX_LINK_STATUS, val);
		/* ToDo: read speed and pause status */
		state->duplex = DUPLEX_FULL;
	}

	/* Continuously repeat re-configuration sequence until link comes up */
	if (!state->link)
		mtk_usxgmii_pcs_config(pcs, mpcs->neg_mode,
				       state->interface, NULL, false);
}

static void mtk_usxgmii_pcs_restart_an(struct phylink_pcs *pcs)
{
	struct mtk_usxgmii_pcs *mpcs = pcs_to_mtk_usxgmii_pcs(pcs);

	mtk_m32(mpcs, RG_PCS_AN_CTRL0, USXGMII_AN_RESTART, USXGMII_AN_RESTART);
}

static void mtk_usxgmii_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
				    phy_interface_t interface,
				    int speed, int duplex)
{
	/* Reconfiguring USXGMII to ensure the quality of the RX signal
	 * after the line side link up.
	 */
	mtk_usxgmii_pcs_config(pcs, neg_mode, interface, NULL, false);
}

static void mtk_usxgmii_pcs_disable(struct phylink_pcs *pcs)
{
	struct mtk_usxgmii_pcs *mpcs = pcs_to_mtk_usxgmii_pcs(pcs);

//	phy_power_off(mpcs->pextp); // causes havoc... should disable clocks here as well
	mpcs->interface = PHY_INTERFACE_MODE_NA;
	mpcs->neg_mode = -1;
}

static const struct phylink_pcs_ops mtk_usxgmii_pcs_ops = {
	.pcs_config = mtk_usxgmii_pcs_config,
	.pcs_get_state = mtk_usxgmii_pcs_get_state,
	.pcs_an_restart = mtk_usxgmii_pcs_restart_an,
	.pcs_link_up = mtk_usxgmii_pcs_link_up,
	.pcs_disable = mtk_usxgmii_pcs_disable,
};

static int mtk_usxgmii_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_usxgmii_pcs *mpcs;
	int ret;

	mpcs = devm_kzalloc(dev, sizeof(*mpcs), GFP_KERNEL);
	if (!mpcs)
		return -ENOMEM;

	mpcs->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mpcs->base))
		return PTR_ERR(mpcs->base);

	mpcs->dev = dev;
	mpcs->pcs.ops = &mtk_usxgmii_pcs_ops;
	mpcs->pcs.poll = true;
	mpcs->pcs.neg_mode = true;
	mpcs->interface = PHY_INTERFACE_MODE_NA;
	mpcs->neg_mode = -1;

	mpcs->clk = devm_clk_get_enabled(mpcs->dev, "usxgmii");
	if (IS_ERR(mpcs->clk))
		return PTR_ERR(mpcs->clk);

	mpcs->reset = devm_reset_control_get_shared(dev, "xfi");
	if (IS_ERR(mpcs->reset))
		return PTR_ERR(mpcs->reset);

	reset_control_deassert(mpcs->reset);

	ret = mtk_xfi_pextp_init(mpcs);
	if (ret)
		return ret;

	ret = mtk_xfi_pll_init(mpcs);
	if (ret)
		return ret;

	ret = mtk_sgmii_wrapper_init(mpcs);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mpcs);

	return 0;
}

static const struct of_device_id mtk_usxgmii_of_mtable[] = {
	{ .compatible = "mediatek,mt7988-usxgmiisys" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_usxgmii_of_mtable);

struct phylink_pcs *mtk_usxgmii_select_pcs(struct device_node *np, phy_interface_t mode)
{
	struct platform_device *pdev;
	struct mtk_usxgmii_pcs *mpcs;

	if (!np)
		return NULL;

	if (!of_device_is_available(np))
		return ERR_PTR(-ENODEV);

	if (!of_match_node(mtk_usxgmii_of_mtable, np))
		return ERR_PTR(-EINVAL);

	pdev = of_find_device_by_node(np);
	if (!pdev || !platform_get_drvdata(pdev)) {
		if (pdev)
			put_device(&pdev->dev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	mpcs = platform_get_drvdata(pdev);
	put_device(&pdev->dev);

	switch (mode) {
		case PHY_INTERFACE_MODE_1000BASEX:
		case PHY_INTERFACE_MODE_2500BASEX:
		case PHY_INTERFACE_MODE_SGMII:
			return mpcs->sgmii_pcs;
		case PHY_INTERFACE_MODE_5GBASER:
		case PHY_INTERFACE_MODE_10GBASER:
		case PHY_INTERFACE_MODE_USXGMII:
			return &mpcs->pcs;
		default:
			return NULL;
	}
}


static struct platform_driver mtk_usxgmii_driver = {
	.driver = {
		.name			= "mtk_usxgmii",
		.suppress_bind_attrs	= true,
		.of_match_table		= mtk_usxgmii_of_mtable,
	},
	.probe = mtk_usxgmii_probe,
//	.remove = mtk_usxgmii_remove,
};
module_platform_driver(mtk_usxgmii_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek USXGMII PCS driver");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
