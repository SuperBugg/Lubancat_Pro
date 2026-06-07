// SPDX-License-Identifier: GPL-2.0
/*
 * Experimental virtio NPU frontend driver.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_npu.h>

#include <linux/completion.h>
#include <linux/scatterlist.h>

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

// //resp是kvmtool写回guest响应
// struct virtio_npu_resp{
//     __le32 status;  //  成功/失败
//     __le32 value;   // 返回值
// };

struct virtio_npu{
    struct virtio_device *vdev;
    struct virtqueue *vq;
    struct mutex lock;
    struct miscdevice miscdev;
};

struct virtio_npu_req_ctx{
    struct completion done;
    struct virtio_npu_req req;
    struct virtio_npu_resp resp;

    unsigned int used_len;
	int status;
};

static const struct file_operations virtio_npu_fops;




static void virtio_npu_done(struct virtqueue *vq)
{
    unsigned int len;
    
    struct virtio_npu_req_ctx *ctx;
    while((ctx = virtqueue_get_buf(vq,&len))!=NULL)
    {
        ctx->used_len = len;
        complete(&ctx->done);
    }

}

//发送cmd，填写req协议
static int virtio_npu_send_cmd(struct virtio_npu *vnpu,u32 cmd,u32 *value)
{
    struct virtio_npu_req_ctx *ctx;
	struct scatterlist req_sg;
	struct scatterlist resp_sg;
    struct scatterlist *sgs[2];

    int ret;

    /*
        栈内存为什么不能拿去做virtio descriptor buffer？
    */
    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    init_completion(&ctx->done);
    //req是guest发给kvmtool的请求头
    ctx->req.cmd = cpu_to_le32(cmd);
    ctx->req.flags = 0;
    //我这次请求准备了多大的响应缓冲区
    ctx->req.output_len = cpu_to_le32(sizeof(ctx->resp));

    sg_init_one(&req_sg,&ctx->req,sizeof(ctx->req));
    sg_init_one(&resp_sg, &ctx->resp, sizeof(ctx->resp));
    /*
        ctx
        ├── req
        │   ├── cmd        已填：cmd
        │   ├── flags      已填：0
        │   ├── input_len  现在因为 memset 是 0
        │   └── output_len 已填：sizeof(ctx.resp)
        │
        └── resp
            ├── status     现在是 0，因为 memset
            └── value      现在是 0，因为 memset
    */

    /*
    当前这个最小协议里，确实只有“请求头”和“响应头”。还没有真正的 NPU 输入数据，比如图片、tensor、模型 buffer。
    out:
    req header
    input tensor buffer
    model/config buffer

    in:
    resp header
    output tensor buffer
    */

    sgs[0] = &req_sg;
    sgs[1] = &resp_sg;

    mutex_lock(&vnpu->lock);

    ret = virtqueue_add_sgs(vnpu->vq,sgs,1,1,ctx,GFP_KERNEL);
    if(ret){
        mutex_unlock(&vnpu->lock);
        kfree(ctx);
        return ret;
    }

    virtqueue_kick(vnpu->vq);

    mutex_unlock(&vnpu->lock);

    wait_for_completion(&ctx->done);
    
    if(ctx->used_len < sizeof(ctx->resp)) {
        kfree(ctx);
        return -EIO;
    }

    if (le32_to_cpu(ctx->resp.status) != VIRTIO_NPU_STATUS_OK) {
        kfree(ctx);
		return -EIO;
    }

	if (value)
		*value = le32_to_cpu(ctx->resp.value);

    kfree(ctx);
    return 0;
}



static int virtio_npu_probe(struct virtio_device *vdev)
{
    struct virtio_npu *vnpu;

    int err;
    //vnpu = kzalloc(&vdev->dev,sizeof(*vnpu),GFP_KERNEL);
    vnpu = devm_kzalloc(&vdev->dev,sizeof(*vnpu),GFP_KERNEL);
    if(!vnpu)
        return -ENOMEM;

    vnpu->vdev = vdev;

    //这里要传地址
    mutex_init(&vnpu->lock);

    //设备的私有数据结构体？
    vdev->priv = vnpu;

    vnpu->vq = virtio_find_single_vq(vdev,virtio_npu_done,"request");
    if(IS_ERR(vnpu->vq)){
        err = PTR_ERR(vnpu->vq);
        goto err_find;
    }




    /* Register misc device for user-space interaction */
    vnpu->miscdev.minor = MISC_DYNAMIC_MINOR;
    vnpu->miscdev.name = "virtio_npu";
    vnpu->miscdev.fops = &virtio_npu_fops;
    vnpu->miscdev.parent = &vdev->dev;

    //driver 已经准备好，virtio device 可以开始工作。
    virtio_device_ready(vdev);

    err = misc_register(&vnpu->miscdev);
    if (err)
        goto err_misc;

    dev_info(&vdev->dev, "virtio-npu frontend probed\n");
    return 0;
    
err_find:
    return err;

err_misc:
    virtio_reset_device(vdev);
    vdev->config->del_vqs(vdev);
    return err;
}

static void virtio_npu_remove(struct virtio_device *vdev)
{
    struct virtio_npu *vnpu = vdev->priv;

    misc_deregister(&vnpu->miscdev);
    virtio_reset_device(vdev);
    vdev->config->del_vqs(vdev);
    mutex_destroy(&vnpu->lock);
}


static const struct virtio_device_id virtio_npu_id_table[] = {
	{ VIRTIO_ID_NPU, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_npu_driver = {
	.driver.name	= "virtio_npu",
	.driver.owner	= THIS_MODULE,
	.id_table	= virtio_npu_id_table,
	.probe		= virtio_npu_probe,
	.remove		= virtio_npu_remove,
};



static long virtio_npu_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct virtio_npu *vnpu;
    struct virtio_npu_ioc_ping data;
    u32 value;
    int ret;

    //file->private_data存放struct miscdevice miscdev;字符设备结构体
    vnpu = container_of(file->private_data,struct virtio_npu,miscdev);

    switch(cmd){
        case VIRTIO_NPU_IOC_PING:
            ret = virtio_npu_send_cmd(vnpu,VIRTIO_NPU_CMD_PING,&value);
            if(ret)                
                return ret;
            data.status = VIRTIO_NPU_STATUS_OK;
            data.value = value;

            if(copy_to_user((void __user *)arg,&data,sizeof(data)))
                return -EFAULT;
            return 0;

        case VIRTIO_NPU_IOC_GET_INFO:
            ret = virtio_npu_send_cmd(vnpu,VIRTIO_NPU_CMD_GET_INFO,&value);
            if(ret)
                return ret;
            data.status = VIRTIO_NPU_STATUS_OK;
            data.value = value;

            if(copy_to_user((void __user *)arg,&data,sizeof(data)))
                return -EFAULT;
            return 0;

        default:
            return -EINVAL;
    }

}

static const struct file_operations virtio_npu_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = virtio_npu_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};



module_virtio_driver(virtio_npu_driver);
MODULE_DEVICE_TABLE(virtio, virtio_npu_id_table);
MODULE_DESCRIPTION("Experimental virtio NPU frontend driver");
MODULE_LICENSE("GPL");
