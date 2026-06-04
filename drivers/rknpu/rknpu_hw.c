// SPDX-License-Identifier: GPL-2.0
/*
 * RKNPU Hardware Abstraction Layer
 *
 * Low-level resource management extracted from rknpu_drv.c.
 * Shared between rknpu main driver and VFIO platform reset module.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/string.h>
#include <linux/version.h>

#ifndef FPGA_PLATFORM
#include <soc/rockchip/rockchip_iommu.h>
#endif

#include "include/rknpu_drv.h"

int rknpu_hw_power_on(struct rknpu_hw_resources *res)
{
	struct device *dev = res->dev;
	int ret = -EINVAL;

	/*
		VDD MEM 电源
	*/
#ifndef FPGA_PLATFORM
	if (res->vdd) {
		ret = regulator_enable(res->vdd);
		if (ret) {
			LOG_DEV_ERROR(dev,
				      "failed to enable vdd reg for rknpu, ret: %d\n",
				      ret);
			return ret;
		}
	}

	if (res->mem) {
		ret = regulator_enable(res->mem);
		if (ret) {
			LOG_DEV_ERROR(dev,
				      "failed to enable mem reg for rknpu, ret: %d\n",
				      ret);
			return ret;
		}
	}
#endif

	/*
		时钟
	*/

	ret = clk_bulk_prepare_enable(res->num_clks, res->clks);
	if (ret) {
		LOG_DEV_ERROR(dev, "failed to enable clk for rknpu, ret: %d\n",
			      ret);
		return ret;
	}


	/*
		三个npu电源
	*/
	if (res->multiple_domains) {
		if (res->genpd_dev_npu0) {
#if KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE
			ret = pm_runtime_resume_and_get(res->genpd_dev_npu0);
#else
			ret = pm_runtime_get_sync(res->genpd_dev_npu0);
#endif
			if (ret < 0) {
				LOG_DEV_ERROR(dev,
					      "failed to get pm runtime for npu0, ret: %d\n",
					      ret);
				goto out;
			}
		}
		if (res->genpd_dev_npu1) {
#if KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE
			ret = pm_runtime_resume_and_get(res->genpd_dev_npu1);
#else
			ret = pm_runtime_get_sync(res->genpd_dev_npu1);
#endif
			if (ret < 0) {
				LOG_DEV_ERROR(dev,
					      "failed to get pm runtime for npu1, ret: %d\n",
					      ret);
				goto out;
			}
		}
		if (res->genpd_dev_npu2) {
#if KERNEL_VERSION(5, 5, 0) < LINUX_VERSION_CODE
			ret = pm_runtime_resume_and_get(res->genpd_dev_npu2);
#else
			ret = pm_runtime_get_sync(res->genpd_dev_npu2);
#endif
			if (ret < 0) {
				LOG_DEV_ERROR(dev,
					      "failed to get pm runtime for npu2, ret: %d\n",
					      ret);
				goto out;
			}
		}
	}

	/*
	 * All power islands and clocks are guaranteed up now.  Mark
	 * hw_powered so the runtime-PM resume callback can distinguish
	 * a genuine resume from driver-core auto-resume on unbind
	 * (where islands are gated and touching MMIO would SError).
	 */
	res->hw_powered = true;

	
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		LOG_DEV_ERROR(dev,
			      "failed to get pm runtime for rknpu, ret: %d\n",
			      ret);
	}

	/*
	 * The rknpu_mmu IOMMU is marked with
	 * "rockchip,disable-device-link-resume" in DT, which makes
	 * its runtime-PM resume callback a no-op.  The master must
	 * explicitly enable the IOMMU here, while islands and clocks
	 * are held up.
	 */
	if (res->iommu_en) {
		ret = rockchip_iommu_enable(dev);
		if (ret)
			LOG_DEV_ERROR(dev,
				      "failed to enable iommu for rknpu, ret: %d\n",
				      ret);
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(rknpu_hw_power_on);

int rknpu_hw_power_off(struct rknpu_hw_resources *res)
{
	struct device *dev = res->dev;

	if (!res->hw_powered) {
		LOG_DEV_INFO(dev, "skip power off, hardware is already off\n");
		return 0;
	}

	/*
	 * Disable the IOMMU while power islands and clocks are still up.
	 * The IOMMU's runtime PM is a no-op (rockchip,disable-device-link-resume),
	 * so the master must idle the MMU before its registers get power-gated.
	 */
	if (res->iommu_en)
		rockchip_iommu_disable(dev);

	/*
	 * From this point on, NPU MMIO is no longer safe to access.
	 * Mark hw_powered false so the runtime-PM resume callback can detect
	 * the gated state and avoid touching registers (prevents SError on
	 * driver-core auto-resume during unbind).
	 */
	res->hw_powered = false;

	pm_runtime_put_sync(dev);

	if (res->multiple_domains) {
		if (res->genpd_dev_npu2)
			pm_runtime_put_sync(res->genpd_dev_npu2);
		if (res->genpd_dev_npu1)
			pm_runtime_put_sync(res->genpd_dev_npu1);
		if (res->genpd_dev_npu0)
			pm_runtime_put_sync(res->genpd_dev_npu0);
	}

	clk_bulk_disable_unprepare(res->num_clks, res->clks);

#ifndef FPGA_PLATFORM
	if (res->mem)
		regulator_disable(res->mem);

	if (res->vdd)
		regulator_disable(res->vdd);
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(rknpu_hw_power_off);

int rknpu_hw_acquire_resources(struct device *dev,
			       struct rknpu_hw_resources *res)
{
	struct device *virt_dev;
	int ret = 0;

	memset(res, 0, sizeof(*res));
	res->dev = dev;

	res->iommu_en = rknpu_is_iommu_enable(dev);
	if (res->iommu_en) {
		res->iommu_group = iommu_group_get(dev);
		if (!res->iommu_group)
			return -EINVAL;
	}

	res->num_clks = devm_clk_bulk_get_all(dev, &res->clks);
	if (res->num_clks < 1) {
		LOG_DEV_ERROR(dev, "failed to get clk source for rknpu\n");
#ifndef FPGA_PLATFORM
		ret = -ENODEV;
		goto err_put_group;
#endif
	}

#ifndef FPGA_PLATFORM
	res->vdd = devm_regulator_get_optional(dev, "rknpu");
	if (IS_ERR(res->vdd)) {
		if (PTR_ERR(res->vdd) != -ENODEV) {
			ret = PTR_ERR(res->vdd);
			LOG_DEV_ERROR(dev,
				      "failed to get vdd regulator: %d\n", ret);
			goto err_put_group;
		}
		res->vdd = NULL;
	}

	res->mem = devm_regulator_get_optional(dev, "mem");
	if (IS_ERR(res->mem)) {
		if (PTR_ERR(res->mem) != -ENODEV) {
			ret = PTR_ERR(res->mem);
			LOG_DEV_ERROR(dev,
				      "failed to get mem regulator: %d\n", ret);
			goto err_put_group;
		}
		res->mem = NULL;
	}
#endif

	mutex_init(&res->power_lock);
	mutex_init(&res->reset_lock);

#ifndef FPGA_PLATFORM
	{
		int num_srsts = of_count_phandle_with_args(dev->of_node,
							   "resets", "#reset-cells");
		if (num_srsts > 0) {
			int i;

			res->srsts = devm_kcalloc(dev, num_srsts,
						  sizeof(*res->srsts), GFP_KERNEL);
			if (!res->srsts) {
				ret = -ENOMEM;
				goto err_put_group;
			}

			for (i = 0; i < num_srsts; i++) {
				res->srsts[i] =
					devm_reset_control_get_exclusive_by_index(
						dev, i);
				if (IS_ERR(res->srsts[i])) {
					res->num_srsts = i;
					ret = PTR_ERR(res->srsts[i]);
					goto err_put_group;
				}
			}
			res->num_srsts = num_srsts;
		}
	}
#endif

	if (of_count_phandle_with_args(dev->of_node, "power-domains",
				       "#power-domain-cells") > 1) {
		virt_dev = dev_pm_domain_attach_by_name(dev, "npu0");
		if (!IS_ERR(virt_dev))
			res->genpd_dev_npu0 = virt_dev;
		virt_dev = dev_pm_domain_attach_by_name(dev, "npu1");
		if (!IS_ERR(virt_dev))
			res->genpd_dev_npu1 = virt_dev;
		/*
		 * npu2 exists only on 3-core NPU configs (RK3588).
		 * It is harmless to try the attach; it returns ERR_PTR
		 * on 2-core silicon like RK3583.
		 */
		virt_dev = dev_pm_domain_attach_by_name(dev, "npu2");
		if (!IS_ERR(virt_dev))
			res->genpd_dev_npu2 = virt_dev;
		res->multiple_domains = true;
	}

	pm_runtime_enable(dev);

	return 0;

err_put_group:
	if (res->iommu_en && res->iommu_group) {
		iommu_group_put(res->iommu_group);
		res->iommu_group = NULL;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(rknpu_hw_acquire_resources);

void rknpu_hw_release_resources(struct rknpu_hw_resources *res)
{
	pm_runtime_disable(res->dev);

	if (res->multiple_domains) {
		if (res->genpd_dev_npu0)
			dev_pm_domain_detach(res->genpd_dev_npu0, true);
		if (res->genpd_dev_npu1)
			dev_pm_domain_detach(res->genpd_dev_npu1, true);
		if (res->genpd_dev_npu2)
			dev_pm_domain_detach(res->genpd_dev_npu2, true);
	}

	if (res->iommu_en && res->iommu_group) {
		iommu_group_put(res->iommu_group);
		res->iommu_group = NULL;
	}

	/*
	 * Clocks, regulators, and resets are devm-managed and will be
	 * released automatically when the device is unbound.
	 */
}
EXPORT_SYMBOL_GPL(rknpu_hw_release_resources);

int rknpu_hw_reset(struct rknpu_hw_resources *res)
{
	int i, ret = 0;

	if (res->num_srsts <= 0)
		return -EINVAL;

	mutex_lock(&res->reset_lock);

	for (i = 0; i < res->num_srsts; i++) {
		if (!res->srsts[i])
			continue;
		ret = reset_control_assert(res->srsts[i]);
		if (ret) {
			LOG_DEV_ERROR(res->dev,
				      "failed to assert reset %d for rknpu: %d\n",
				      i, ret);
			goto out;
		}
	}

	udelay(10);

	for (i = 0; i < res->num_srsts; i++) {
		if (!res->srsts[i])
			continue;
		ret = reset_control_deassert(res->srsts[i]);
		if (ret) {
			LOG_DEV_ERROR(res->dev,
				      "failed to deassert reset %d for rknpu: %d\n",
				      i, ret);
			goto out;
		}
	}

	udelay(10);

out:
	mutex_unlock(&res->reset_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(rknpu_hw_reset);
