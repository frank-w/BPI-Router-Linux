// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>

struct mtk_ccifreq_platform_data {
	int min_volt_shift;
	int max_volt_shift;
	int proc_max_volt;
	int sram_min_volt;
	int sram_max_volt;
};

struct mtk_ccifreq_drv {
	struct device *dev;
	struct devfreq *devfreq;
	struct regulator *proc_reg;
	struct regulator *sram_reg;
	struct clk *cci_clk;
	struct clk *inter_clk;
	int inter_voltage;
	unsigned long pre_freq;
	/* Avoid race condition for regulators between notify and policy */
	struct mutex reg_lock;
	struct notifier_block opp_nb;
	const struct mtk_ccifreq_platform_data *soc_data;
	int vtrack_max;
};

static int mtk_ccifreq_set_voltage(struct mtk_ccifreq_drv *drv, int new_voltage)
{
	const struct mtk_ccifreq_platform_data *soc_data = drv->soc_data;
	struct device *dev = drv->dev;
	int pre_voltage, pre_vsram, new_vsram, vsram, voltage, ret;
	int retry_max = drv->vtrack_max;

	if (!drv->sram_reg) {
		ret = regulator_set_voltage(drv->proc_reg, new_voltage,
					    drv->soc_data->proc_max_volt);
		return ret;
	}

	pre_voltage = regulator_get_voltage(drv->proc_reg);
	if (pre_voltage < 0) {
		dev_err(dev, "invalid vproc value: %d\n", pre_voltage);
		return pre_voltage;
	}

	pre_vsram = regulator_get_voltage(drv->sram_reg);
	if (pre_vsram < 0) {
		dev_err(dev, "invalid vsram value: %d\n", pre_vsram);
		return pre_vsram;
	}

	new_vsram = clamp(new_voltage + soc_data->min_volt_shift,
			  soc_data->sram_min_volt, soc_data->sram_max_volt);

	do {
		if (pre_voltage <= new_voltage) {
			vsram = clamp(pre_voltage + soc_data->max_volt_shift,
				      soc_data->sram_min_volt, new_vsram);
			ret = regulator_set_voltage(drv->sram_reg, vsram,
						    soc_data->sram_max_volt);
			if (ret)
				return ret;

			if (vsram == soc_data->sram_max_volt ||
			    new_vsram == soc_data->sram_min_volt)
				voltage = new_voltage;
			else
				voltage = vsram - soc_data->min_volt_shift;

			ret = regulator_set_voltage(drv->proc_reg, voltage,
						    soc_data->proc_max_volt);
			if (ret) {
				regulator_set_voltage(drv->sram_reg, pre_vsram,
						      soc_data->sram_max_volt);
				return ret;
			}
		} else {
			voltage = max(new_voltage,
				      pre_vsram - soc_data->max_volt_shift);
			ret = regulator_set_voltage(drv->proc_reg, voltage,
						    soc_data->proc_max_volt);
			if (ret)
				return ret;

			if (voltage == new_voltage)
				vsram = new_vsram;
			else
				vsram = max(new_vsram,
					    voltage + soc_data->min_volt_shift);

			ret = regulator_set_voltage(drv->sram_reg, vsram,
						    soc_data->sram_max_volt);
			if (ret) {
				regulator_set_voltage(drv->proc_reg, pre_voltage,
						      soc_data->proc_max_volt);
				return ret;
			}
		}

		pre_voltage = voltage;
		pre_vsram = vsram;

		if (--retry_max < 0) {
			dev_err(dev,
				"over loop count, failed to set voltage\n");
			return -EINVAL;
		}
	} while (voltage != new_voltage || vsram != new_vsram);

	return 0;
}

static int mtk_ccifreq_target(struct device *dev, unsigned long *freq,
			      u32 flags)
{
	struct mtk_ccifreq_drv *drv = dev_get_drvdata(dev);
	struct clk *cci_pll;
	struct dev_pm_opp *opp;
	unsigned long opp_rate;
	int voltage, pre_voltage, inter_voltage, target_voltage, ret;

	if (!drv)
		return -EINVAL;

	if (drv->pre_freq == *freq)
		return 0;

	mutex_lock(&drv->reg_lock);

	inter_voltage = drv->inter_voltage;
	cci_pll = clk_get_parent(drv->cci_clk);

	opp_rate = *freq;
	opp = devfreq_recommended_opp(dev, &opp_rate, 1);
	if (IS_ERR(opp)) {
		dev_err(dev, "failed to find opp for freq: %ld\n", opp_rate);
		ret = PTR_ERR(opp);
		goto out_unlock;
	}

	voltage = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	pre_voltage = regulator_get_voltage(drv->proc_reg);
	if (pre_voltage < 0) {
		dev_err(dev, "invalid vproc value: %d\n", pre_voltage);
		ret = pre_voltage;
		goto out_unlock;
	}

	/* scale up: set voltage first then freq. */
	target_voltage = max(inter_voltage, voltage);
	if (pre_voltage <= target_voltage) {
		ret = mtk_ccifreq_set_voltage(drv, target_voltage);
		if (ret) {
			dev_err(dev, "failed to scale up voltage\n");
			goto out_restore_voltage;
		}
	}

	/* switch the cci clock to intermediate clock source. */
	ret = clk_set_parent(drv->cci_clk, drv->inter_clk);
	if (ret) {
		dev_err(dev, "failed to re-parent cci clock\n");
		goto out_restore_voltage;
	}

	/* set the original clock to target rate. */
	ret = clk_set_rate(cci_pll, *freq);
	if (ret) {
		dev_err(dev, "failed to set cci pll rate: %d\n", ret);
		clk_set_parent(drv->cci_clk, cci_pll);
		goto out_restore_voltage;
	}

	/* switch the cci clock back to the original clock source. */
	ret = clk_set_parent(drv->cci_clk, cci_pll);
	if (ret) {
		dev_err(dev, "failed to re-parent cci clock\n");
		mtk_ccifreq_set_voltage(drv, inter_voltage);
		goto out_unlock;
	}

	/*
	 * If the new voltage is lower than the intermediate voltage or the
	 * original voltage, scale down to the new voltage.
	 */
	if (voltage < inter_voltage || voltage < pre_voltage) {
		ret = mtk_ccifreq_set_voltage(drv, voltage);
		if (ret) {
			dev_err(dev, "failed to scale down voltage\n");
			goto out_unlock;
		}
	}

	drv->pre_freq = *freq;
	mutex_unlock(&drv->reg_lock);

	return 0;

out_restore_voltage:
	mtk_ccifreq_set_voltage(drv, pre_voltage);

out_unlock:
	mutex_unlock(&drv->reg_lock);
	return ret;
}

static int mtk_ccifreq_opp_notifier(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	struct dev_pm_opp *opp = data;
	struct mtk_ccifreq_drv *drv;
	unsigned long freq, volt;

	drv = container_of(nb, struct mtk_ccifreq_drv, opp_nb);

	if (event == OPP_EVENT_ADJUST_VOLTAGE) {
		mutex_lock(&drv->reg_lock);
		freq = dev_pm_opp_get_freq(opp);

		/* current opp item is changed */
		if (freq == drv->pre_freq) {
			volt = dev_pm_opp_get_voltage(opp);
			mtk_ccifreq_set_voltage(drv, volt);
		}
		mutex_unlock(&drv->reg_lock);
	}

	return 0;
}

static struct devfreq_dev_profile mtk_ccifreq_profile = {
	.target = mtk_ccifreq_target,
};

static void mtk_ccifreq_regulator_disable(void *data)
{
	regulator_disable(data);
}

static void mtk_ccifreq_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static void mtk_ccifreq_opp_of_remove_table(void *data)
{
	dev_pm_opp_of_remove_table(data);
}

static int mtk_ccifreq_probe(struct platform_device *pdev)
{
	struct devfreq_passive_data *passive_data;
	struct device *dev = &pdev->dev;
	struct cpufreq_policy *policy;
	struct mtk_ccifreq_drv *drv;
	struct dev_pm_opp *opp;
	unsigned long rate, opp_volt;
	int ret;

	/*
	 * Check if cpufreq is available before touching any regulators.
	 * The passive devfreq governor needs cpufreq as its parent and
	 * will return -EPROBE_DEFER if it is not yet registered. If we
	 * enable the proc regulator (CPU core power) before this check,
	 * the subsequent probe failure causes devres to disable that
	 * regulator, potentially cutting CPU core voltage and hanging
	 * the system.
	 */
	policy = cpufreq_cpu_get(0);
	if (!policy)
		return -EPROBE_DEFER;
	cpufreq_cpu_put(policy);

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->dev = dev;
	drv->soc_data = (const struct mtk_ccifreq_platform_data *)
				of_device_get_match_data(&pdev->dev);
	mutex_init(&drv->reg_lock);
	platform_set_drvdata(pdev, drv);

	drv->cci_clk = devm_clk_get(dev, "cci");
	if (IS_ERR(drv->cci_clk))
		return dev_err_probe(dev, PTR_ERR(drv->cci_clk),
				     "failed to get cci clk\n");

	drv->inter_clk = devm_clk_get(dev, "intermediate");
	if (IS_ERR(drv->inter_clk))
		return dev_err_probe(dev, PTR_ERR(drv->inter_clk),
				     "failed to get intermediate clk\n");

	drv->proc_reg = devm_regulator_get_optional(dev, "proc");
	if (IS_ERR(drv->proc_reg))
		return dev_err_probe(dev, PTR_ERR(drv->proc_reg),
				     "failed to get proc regulator\n");

	ret = regulator_enable(drv->proc_reg);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable proc regulator\n");

	ret = devm_add_action_or_reset(dev, mtk_ccifreq_regulator_disable,
				       drv->proc_reg);
	if (ret)
		return ret;

	drv->sram_reg = devm_regulator_get_optional(dev, "sram");
	if (IS_ERR(drv->sram_reg)) {
		if (PTR_ERR(drv->sram_reg) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		drv->sram_reg = NULL;
	} else {
		ret = regulator_enable(drv->sram_reg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to enable sram regulator\n");

		ret = devm_add_action_or_reset(dev,
					       mtk_ccifreq_regulator_disable,
					       drv->sram_reg);
		if (ret)
			return ret;
	}

	/*
	 * We assume min voltage is 0 and tracking target voltage using
	 * min_volt_shift for each iteration.
	 * The retry_max is 3 times of expected iteration count.
	 */
	drv->vtrack_max = 3 * DIV_ROUND_UP(max(drv->soc_data->sram_max_volt,
					       drv->soc_data->proc_max_volt),
					   drv->soc_data->min_volt_shift);

	ret = clk_prepare_enable(drv->cci_clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, mtk_ccifreq_clk_disable_unprepare,
				       drv->cci_clk);
	if (ret)
		return ret;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add opp table\n");

	ret = devm_add_action_or_reset(dev, mtk_ccifreq_opp_of_remove_table,
				       dev);
	if (ret)
		return ret;

	rate = clk_get_rate(drv->inter_clk);
	opp = dev_pm_opp_find_freq_ceil(dev, &rate);
	if (IS_ERR(opp))
		return dev_err_probe(dev, PTR_ERR(opp),
				     "failed to get intermediate opp\n");

	drv->inter_voltage = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	rate = U32_MAX;
	opp = dev_pm_opp_find_freq_floor(drv->dev, &rate);
	if (IS_ERR(opp))
		return dev_err_probe(dev, PTR_ERR(opp),
				     "failed to get opp\n");

	opp_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);
	ret = mtk_ccifreq_set_voltage(drv, opp_volt);
	if (ret) {
		dev_err(dev, "failed to scale to highest voltage %lu in proc_reg\n",
			opp_volt);
		return ret;
	}

	passive_data = devm_kzalloc(dev, sizeof(*passive_data), GFP_KERNEL);
	if (!passive_data)
		return -ENOMEM;

	passive_data->parent_type = CPUFREQ_PARENT_DEV;
	drv->devfreq = devm_devfreq_add_device(dev, &mtk_ccifreq_profile,
					       DEVFREQ_GOV_PASSIVE,
					       passive_data);
	if (IS_ERR(drv->devfreq))
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "failed to add devfreq device: %ld\n",
				     PTR_ERR(drv->devfreq));

	drv->opp_nb.notifier_call = mtk_ccifreq_opp_notifier;
	ret = dev_pm_opp_register_notifier(dev, &drv->opp_nb);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register opp notifier\n");

	return 0;
}

static void mtk_ccifreq_remove(struct platform_device *pdev)
{
	struct mtk_ccifreq_drv *drv = platform_get_drvdata(pdev);

	dev_pm_opp_unregister_notifier(&pdev->dev, &drv->opp_nb);
}

static const struct mtk_ccifreq_platform_data mt8183_platform_data = {
	.min_volt_shift = 100000,
	.max_volt_shift = 200000,
	.proc_max_volt = 1150000,
};

static const struct mtk_ccifreq_platform_data mt8186_platform_data = {
	.min_volt_shift = 100000,
	.max_volt_shift = 250000,
	.proc_max_volt = 1118750,
	.sram_min_volt = 850000,
	.sram_max_volt = 1118750,
};

static const struct of_device_id mtk_ccifreq_machines[] = {
	{ .compatible = "mediatek,mt8183-cci", .data = &mt8183_platform_data },
	{ .compatible = "mediatek,mt8186-cci", .data = &mt8186_platform_data },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_ccifreq_machines);

static struct platform_driver mtk_ccifreq_platdrv = {
	.probe	= mtk_ccifreq_probe,
	.remove = mtk_ccifreq_remove,
	.driver = {
		.name = "mtk-ccifreq",
		.of_match_table = mtk_ccifreq_machines,
	},
};
module_platform_driver(mtk_ccifreq_platdrv);

MODULE_DESCRIPTION("MediaTek CCI devfreq driver");
MODULE_AUTHOR("Jia-Wei Chang <jia-wei.chang@mediatek.com>");
MODULE_LICENSE("GPL v2");
