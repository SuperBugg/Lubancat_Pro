/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_VIRTIO_NPU_H
#define _UAPI_LINUX_VIRTIO_NPU_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define VIRTIO_NPU_CMD_GET_INFO		1
#define VIRTIO_NPU_CMD_PING		2
#define VIRTIO_NPU_CMD_INFER_DUMMY	3

#define VIRTIO_NPU_STATUS_OK		0
#define VIRTIO_NPU_STATUS_UNSUPP	1
#define VIRTIO_NPU_STATUS_BAD_REQ	2
#define VIRTIO_NPU_STATUS_IOERR	3

struct virtio_npu_req {
	__le32 cmd;
	__le32 flags;
	__le32 input_len;
	__le32 output_len;
};

struct virtio_npu_resp {
	__le32 status;
	__le32 value;
};

struct virtio_npu_ioc_ping {
	__u32 status;
	__u32 value;
};

#define VIRTIO_NPU_IOC_MAGIC		'N'
#define VIRTIO_NPU_IOC_PING \
	_IOWR(VIRTIO_NPU_IOC_MAGIC, 0x01, struct virtio_npu_ioc_ping)
#define VIRTIO_NPU_IOC_GET_INFO \
	_IOWR(VIRTIO_NPU_IOC_MAGIC, 0x02, struct virtio_npu_ioc_ping)

#endif /* _UAPI_LINUX_VIRTIO_NPU_H */
