/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_VIRTIO_NPU_H
#define _UAPI_LINUX_VIRTIO_NPU_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define VIRTIO_NPU_CMD_GET_INFO		1
#define VIRTIO_NPU_CMD_PING		2
#define VIRTIO_NPU_CMD_INFER_DUMMY	3
#define VIRTIO_NPU_CMD_INFER_RAW	4

#define VIRTIO_NPU_STATUS_OK		0
#define VIRTIO_NPU_STATUS_UNSUPP	1
#define VIRTIO_NPU_STATUS_BAD_REQ	2
#define VIRTIO_NPU_STATUS_IOERR		3


#define VIRTIO_NPU_MAX_IO_SIZE		(8 * 1024 * 1024)

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

struct virtio_npu_ioc_infer_dummy{
    __u64 input;                    //输入数据的地址，可能是图片、tensor buffer、模型/config buffer等
    __u32 input_len;                //输入数据的长度
    __u64 output;                   //输出数据的地址，可能是输出tensor buffer等
    __u32 output_len;               //输出数据的长度
    __u32 status;                   //返回状态
    __u32 actual_output_len;        //实际输出数据的长度
};

struct virtio_npu_ioc_infer_raw {
	__u64 input;
	__u32 input_len;
	__u64 output;
	__u32 output_len;
	__u32 status;
	__u32 actual_output_len;
};


#define VIRTIO_NPU_IOC_MAGIC		'N'
#define VIRTIO_NPU_IOC_PING \
	_IOWR(VIRTIO_NPU_IOC_MAGIC, 0x01, struct virtio_npu_ioc_ping)
#define VIRTIO_NPU_IOC_GET_INFO \
	_IOWR(VIRTIO_NPU_IOC_MAGIC, 0x02, struct virtio_npu_ioc_ping)

#define VIRTIO_NPU_IOC_INFER_DUMMY \
	_IOWR(VIRTIO_NPU_IOC_MAGIC, 0x03, struct virtio_npu_ioc_infer_dummy)

#define VIRTIO_NPU_IOC_INFER_RAW \
	_IOWR(VIRTIO_NPU_IOC_MAGIC, 0x04, struct virtio_npu_ioc_infer_raw)

#endif /* _UAPI_LINUX_VIRTIO_NPU_H */
