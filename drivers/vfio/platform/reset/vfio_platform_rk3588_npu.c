// SPDX-License-Identifier: GPL-2.0
/*
 * VFIO platform reset driver for RK3588 NPU
 *
 * Provides device-level reset for RK3588 NPU when it is bound to
 * vfio-platform.  The NPU cannot be reset by writing a MMIO register
 * alone — it requires a full power-cycle of the PD_NPUTOP/PD_NPU1/PD_NPU2
 * power islands.  The shared rknpu_hw_* API implements this sequence.
 *
 * Copyright (C) 2026
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "../vfio_platform_private.h"
#include "../../../rknpu/include/rknpu_drv.h"

static int vfio_platform_rk3588_npu_reset(struct vfio_platform_device *vdev)
{
	struct rknpu_hw_resources *res = vdev->reset_opaque;
	int ret;

	dev_info(vdev->device, "CHJ vfio-rknpu-reset: enter\n");


	if (!res) {
		dev_info(vdev->device,
			 "CHJ vfio-rknpu-reset: alloc/acquire resources enter\n");
		res = kzalloc(sizeof(*res), GFP_KERNEL);
		if (!res)
			return -ENOMEM;

		ret = rknpu_hw_acquire_resources(vdev->device, res);
		if (ret) {
			dev_err(vdev->device,
				"failed to acquire NPU hw resources: %d\n", ret);
			kfree(res);
			return ret;
		}
		vdev->reset_opaque = res;
		dev_info(vdev->device,
			 "CHJ vfio-rknpu-reset: acquire resources ok\n");
	} else {
		dev_info(vdev->device,
			 "CHJ vfio-rknpu-reset: reuse cached resources\n");
	}

	dev_info(vdev->device, "CHJ vfio-rknpu-reset: power_off enter\n");
	rknpu_hw_power_off(res);
	dev_info(vdev->device, "CHJ vfio-rknpu-reset: power_off ok\n");

	dev_info(vdev->device, "CHJ vfio-rknpu-reset: power_on enter\n");
	ret = rknpu_hw_power_on(res);
	dev_info(vdev->device, "CHJ vfio-rknpu-reset: power_on ret=%d\n",
		 ret);

	return ret;
}

static void vfio_platform_rk3588_npu_release(struct vfio_platform_device *vdev)
{
	struct rknpu_hw_resources *res = vdev->reset_opaque;

	if (!res)
		return;

	dev_info(vdev->device, "CHJ vfio-rknpu-reset: release resources\n");
	rknpu_hw_release_resources(res);
	kfree(res);
	vdev->reset_opaque = NULL;
}

MODULE_ALIAS("vfio-reset:rockchip,rk3588-rknpu");

static struct vfio_platform_reset_node vfio_platform_rk3588_npu_reset_node = {
	.owner = THIS_MODULE,
	.compat = "rockchip,rk3588-rknpu",
	.of_reset = vfio_platform_rk3588_npu_reset,
	.of_release = vfio_platform_rk3588_npu_release,
	.irq_flags = IRQF_SHARED,
};

static int __init vfio_platform_rk3588_npu_reset_module_init(void)
{
	__vfio_platform_register_reset(&vfio_platform_rk3588_npu_reset_node);
	return 0;
}

static void __exit vfio_platform_rk3588_npu_reset_module_exit(void)
{
	vfio_platform_unregister_reset("rockchip,rk3588-rknpu",
				       vfio_platform_rk3588_npu_reset);
}

module_init(vfio_platform_rk3588_npu_reset_module_init);
module_exit(vfio_platform_rk3588_npu_reset_module_exit);

MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("VFIO platform reset for RK3588 NPU");
