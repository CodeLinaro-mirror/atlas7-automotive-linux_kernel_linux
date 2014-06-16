/*
 * Virtio clock frontend driver
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/limits.h>
#include <linux/of.h>

#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc_dualos.h>

#include <linux/virtio_clk.h>

/**
 * struct virtio_clock - virtio clock device
 * @clk_data: virtio clock data
 * @vdev: the virtio device
 * @vq: virtio clock virtqueue
 *
 */
struct virtio_clk {
	struct clk_onecell_data clk_data;
	struct clk_ops ops;
	struct virtio_device *vdev;
	struct virtqueue *vq;
	struct device_node *np;
	int maxclk;
	spinlock_t vq_lock;
	char *np_str;
};

struct virtio_clk_unit {
	int index;
	struct clk_hw hw;
	struct clk_init_data init;
	struct virtio_clk *vclk;
};
#define to_unitclk(_hw) container_of(_hw, struct virtio_clk_unit, hw)

/*
 * Transfer device driver clock operation requests to real clock
 * controller in backend OS
 */
static int __virtio_clk_transfer_req(struct virtio_clk *vclk,
			struct virtio_clk_req *clk_req)
{
	struct virtqueue *vq = vclk->vq;
	struct scatterlist sg;
	unsigned long flags;
	int err, len;
	struct virtio_clk_req *req_done = NULL;

	sg_init_one(&sg, clk_req, sizeof(*clk_req));

	spin_lock_irqsave(&vclk->vq_lock, flags);

	err = virtqueue_add_inbuf(vq, &sg, 1, clk_req, GFP_ATOMIC);
	/* Tell Host to go! */
	virtqueue_kick(vq);
	if (err)
		goto req_exit;

	do {
		req_done = virtqueue_get_buf(vq, &len);
		if (req_done)
			break;

		if (virtqueue_is_broken(vq)) {
			err = -EPIPE;
			goto req_exit;
		}
		cpu_relax();
	} while (1);

	err = req_done->status;

req_exit:
	spin_unlock_irqrestore(&vclk->vq_lock, flags);

	return err;
}


static int __virtio_clk_ops_common(struct clk_hw *hw, int opcode)
{
	int err;
	struct virtio_clk *vclk;
	struct virtio_clk_req *vclk_req;
	struct virtio_clk_unit *vclk_unit;

	vclk_unit = to_unitclk(hw);
	vclk = vclk_unit->vclk;

	vclk_req = kzalloc(sizeof(*vclk_req), GFP_ATOMIC);
	if (!vclk_req) {
		dev_err(&vclk->vdev->dev, "Out of Memory!\n");
		return -ENOMEM;
	}

	vclk_req->clk_index = vclk_unit->index;
	vclk_req->clk_op_code = opcode;

	err = __virtio_clk_transfer_req(vclk, vclk_req);

	kfree(vclk_req);

	if (err)
		dev_err(&vclk->vdev->dev,
			"%s opcode=%08x err=%d\n",
			hw->init->name, opcode, err);
	return err;
}

static int virtio_clk_ops_prepare(struct clk_hw *hw)
{
	return __virtio_clk_ops_common(hw, VIRT_CLK_PREPARE);
}

static void virtio_clk_ops_unprepare(struct clk_hw *hw)
{
	__virtio_clk_ops_common(hw, VIRT_CLK_UNPREPARE);
}

static int virtio_clk_ops_is_prepared(struct clk_hw *hw)
{
	return __virtio_clk_ops_common(hw, VIRT_CLK_IS_PREPARED);
}

static void virtio_clk_ops_unprepare_unused(struct clk_hw *hw)
{
	 __virtio_clk_ops_common(hw, VIRT_CLK_UNPREPARE_UNUSED);
}

static int virtio_clk_ops_enable(struct clk_hw *hw)
{
	return __virtio_clk_ops_common(hw, VIRT_CLK_ENABLE);
}

static void virtio_clk_ops_disable(struct clk_hw *hw)
{
	__virtio_clk_ops_common(hw, VIRT_CLK_DISABLE);
}

static int virtio_clk_ops_is_enabled(struct clk_hw *hw)
{
	return __virtio_clk_ops_common(hw, VIRT_CLK_IS_ENABLED);
}

static void virtio_clk_ops_disable_unused(struct clk_hw *hw)
{
	__virtio_clk_ops_common(hw, VIRT_CLK_DISABLE_UNUSED);
}

static void virtio_clk_ops_init(struct clk_hw *hw)
{
	__virtio_clk_ops_common(hw, VIRT_CLK_INIT);
}

static unsigned long virtio_clk_ops_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	int err;
	struct virtio_clk *vclk;
	struct virtio_clk_req *vclk_req;
	struct virtio_clk_unit *vclk_unit;

	vclk_unit = to_unitclk(hw);
	vclk = vclk_unit->vclk;

	vclk_req = kzalloc(sizeof(*vclk_req), GFP_ATOMIC);
	if (!vclk_req) {
		dev_err(&vclk->vdev->dev, "Out of Memory!\n");
		return -ENOMEM;
	}

	vclk_req->clk_index = vclk_unit->index;
	vclk_req->clk_op_code = VIRT_CLK_RECALC_RATE;
	vclk_req->data.parent_rate = parent_rate;
	err = __virtio_clk_transfer_req(vclk, vclk_req);

	kfree(vclk_req);

	return err;
}

static long virtio_clk_ops_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	int err;
	struct virtio_clk *vclk;
	struct virtio_clk_req *vclk_req;
	struct virtio_clk_unit *vclk_unit;

	vclk_unit = to_unitclk(hw);
	vclk = vclk_unit->vclk;

	vclk_req = kzalloc(sizeof(*vclk_req), GFP_ATOMIC);
	if (!vclk_req) {
		dev_err(&vclk->vdev->dev, "Out of Memory!\n");
		return -ENOMEM;
	}

	vclk_req->clk_index = vclk_unit->index;
	vclk_req->clk_op_code = VIRT_CLK_ROUND_RATE;
	vclk_req->data.raw[0] = rate;
	vclk_req->data.raw[1] = *prate;
	err = __virtio_clk_transfer_req(vclk, vclk_req);

	kfree(vclk_req);

	return err;
}

static long virtio_clk_ops_determine_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *best_parent_rate,
					struct clk **best_parent_clk)
{
	int err;
	struct virtio_clk *vclk;
	struct virtio_clk_req *vclk_req;
	struct virtio_clk_unit *vclk_unit;

	vclk_unit = to_unitclk(hw);
	vclk = vclk_unit->vclk;

	vclk_req = kzalloc(sizeof(*vclk_req), GFP_ATOMIC);
	if (!vclk_req) {
		dev_err(&vclk->vdev->dev, "Out of Memory!\n");
		return -ENOMEM;
	}

	vclk_req->clk_index = vclk_unit->index;
	vclk_req->clk_op_code = VIRT_CLK_DETERMINE_RATE;
	vclk_req->data.raw[0] = rate;
	vclk_req->data.raw[1] = *best_parent_rate;
	err = __virtio_clk_transfer_req(vclk, vclk_req);

	kfree(vclk_req);

	return err;
}

static int virtio_clk_ops_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	int err;
	struct virtio_clk *vclk;
	struct virtio_clk_req *vclk_req;
	struct virtio_clk_unit *vclk_unit;

	vclk_unit = to_unitclk(hw);
	vclk = vclk_unit->vclk;

	vclk_req = kzalloc(sizeof(*vclk_req), GFP_ATOMIC);
	if (!vclk_req) {
		dev_err(&vclk->vdev->dev, "Out of Memory!\n");
		return -ENOMEM;
	}

	vclk_req->clk_index = vclk_unit->index;
	vclk_req->clk_op_code = VIRT_CLK_SET_RATE;
	vclk_req->data.raw[0] = rate;
	vclk_req->data.raw[1] = parent_rate;
	err = __virtio_clk_transfer_req(vclk, vclk_req);

	kfree(vclk_req);

	return err;
}

static unsigned long virtio_clk_ops_recalc_accuracy(struct clk_hw *hw,
			   unsigned long parent_accuracy)
{
	int err;
	struct virtio_clk *vclk;
	struct virtio_clk_req *vclk_req;
	struct virtio_clk_unit *vclk_unit;

	vclk_unit = to_unitclk(hw);
	vclk = vclk_unit->vclk;

	vclk_req = kzalloc(sizeof(*vclk_req), GFP_ATOMIC);
	if (!vclk_req) {
		dev_err(&vclk->vdev->dev, "Out of Memory!\n");
		return -ENOMEM;
	}

	vclk_req->clk_index = vclk_unit->index;
	vclk_req->clk_op_code = VIRT_CLK_RECALC_ACCURACY;
	vclk_req->data.parent_accuracy = parent_accuracy;
	err = __virtio_clk_transfer_req(vclk, vclk_req);

	kfree(vclk_req);

	return err;
}

/* virtqueue incoming data intterrupt IRQ */
static void virtio_clk_vq_isr(struct virtqueue *vq)
{
	dev_dbg(&vq->vdev->dev, "%s\n", __func__);
	/* Wake up the blocked read or write context */
}

static struct clk *__virtio_clk_register_unit(struct virtio_clk *vclk,
					int index)
{
	struct clk *clk;
	struct virtio_clk_unit *unit;
	char name[16];

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);
	if (!unit) {
		pr_err("could not alloc unit clock %s\n",
			name);
		return ERR_PTR(-ENOMEM);
	}

	snprintf(name, 16, "clk_%d", index);
	unit->init.name = name;
	unit->init.parent_names = NULL;
	unit->init.num_parents = 0;
	unit->init.ops = &vclk->ops;
	unit->init.flags = 0;

	unit->hw.init = &unit->init;
	unit->vclk = vclk;
	unit->index = index;

	clk = clk_register(NULL, &unit->hw);
	if (IS_ERR(clk))
		kfree(unit);

	return clk;
}

static int __virtio_clk_find_match_node(struct virtio_device *vdev,
					struct virtio_clk *vclk)
{
	struct clk **clks, *clk;
	int idx;

	vclk->np = of_find_compatible_node(NULL, NULL, vclk->np_str);
	if (!vclk->np) {
		dev_err(&vdev->dev,
			"Could not find match virtio clk controller\n");
		return -EINVAL;
	}

	clks = kzalloc(sizeof(clk) * vclk->maxclk, GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	for (idx = 0; idx < vclk->maxclk; idx++) {
		clk = __virtio_clk_register_unit(vclk, idx);
		if (IS_ERR(clk)) {
			dev_err(&vdev->dev,
				"register clock unit [%d] failed, err=%ld\n",
				idx, PTR_ERR(clk));
			continue;
		}
		clks[idx] = clk;
	}

	vclk->clk_data.clks = clks;
	vclk->clk_data.clk_num = vclk->maxclk;

	of_clk_add_provider(vclk->np,
		of_clk_src_onecell_get, &vclk->clk_data);

	return 0;
}


/* virtual clock backend online handler */
static int virtio_clk_turn_online(struct virtio_device *vdev)
{
	struct virtio_clk *vclk = vdev->priv;

	return __virtio_clk_find_match_node(vdev, vclk);
}

static int virtio_clk_mmio(struct virtio_device *vdev, u32 offset)
{
	u32 value_u = 0;

	switch (offset) {
	/************* RPROC Defined MMIO Handlers *****************/
	case MMIO_BACK_ONLINE:
		value_u = virtio_clk_turn_online(vdev);
		break;

	case MMIO_BACK_OFFLINE:
		break;

	/*************** Customized MMIO Handlers *******************/
	default:
		dev_err(&vdev->dev, "Bad MMIO offset:%d\n", offset);
		break;
	}
	return 0;
}

static void __virtio_clk_init_ops(struct virtio_device *vdev,
				struct virtio_clk *vclk)
{
	if (virtio_has_feature(vdev, VIRTIO_CLK_F_PREPARE))
		vclk->ops.prepare = virtio_clk_ops_prepare;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_UNPREPARE))
		vclk->ops.unprepare = virtio_clk_ops_unprepare;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_IS_PREPARED))
		vclk->ops.is_prepared = virtio_clk_ops_is_prepared;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_UNPREPARE_UNUSED))
		vclk->ops.unprepare_unused = virtio_clk_ops_unprepare_unused;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_ENABLE))
		vclk->ops.enable = virtio_clk_ops_enable;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_DISABLE))
		vclk->ops.disable = virtio_clk_ops_disable;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_IS_ENABLED))
		vclk->ops.is_enabled = virtio_clk_ops_is_enabled;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_DISABLE_UNUSED))
		vclk->ops.disable_unused = virtio_clk_ops_disable_unused;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_RECALC_RATE))
		vclk->ops.recalc_rate = virtio_clk_ops_recalc_rate;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_ROUND_RATE))
		vclk->ops.round_rate = virtio_clk_ops_round_rate;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_DETERMINE_RATE))
		vclk->ops.determine_rate = virtio_clk_ops_determine_rate;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_SET_RATE))
		vclk->ops.set_rate = virtio_clk_ops_set_rate;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_RECALC_ACCURACY))
		vclk->ops.recalc_accuracy = virtio_clk_ops_recalc_accuracy;

	if (virtio_has_feature(vdev, VIRTIO_CLK_F_INIT))
		vclk->ops.init = virtio_clk_ops_init;
}

static int virtio_clk_hw_init(struct virtio_device *vdev,
				struct virtio_clk *vclk)
{
	int len;

	len = virtio_cread32(vdev, VCLK_MMIO_UNIT_LEN);
	if (!len) {
		dev_err(&vdev->dev,
			"Haven't specify the node in dts!\n");
		return -EINVAL;
	}

	vclk->np_str = kzalloc(sizeof(char) * len, GFP_KERNEL);
	if (!vclk->np_str)
		return -ENOMEM;

	virtio_cread_bytes(vdev, VCLK_MMIO_UNIT_DATA, vclk->np_str, len);

	/* read virtio clock units from from config data */
	vclk->maxclk = virtio_cread32(vdev, VCLK_MMIO_UNIT_NUM);

	__virtio_clk_init_ops(vdev, vclk);

	return 0;
}

static int virtio_clk_probe(struct virtio_device *vdev)
{
	vq_callback_t *vq_cbs[] = {virtio_clk_vq_isr};
	const char *names[1];
	struct virtqueue *vqs[1];
	struct virtio_clk *vclk;
	int err = 0;

	vclk = kzalloc(sizeof(*vclk), GFP_KERNEL);
	if (!vclk)
		return -ENOMEM;

	vclk->vdev = vdev;
	names[0] = "virtio_clk_vq_isr";

	spin_lock_init(&vclk->vq_lock);

	/* We use a bidirectional virtqueue, so we expect one virtqueues */
	err = vdev->config->find_vqs(vdev, 1, vqs, vq_cbs, names);
	if (err)
		goto free_vclk;

	vclk->vq = vqs[0];

	/* suppress "tx-complete" interrupts */
	virtqueue_disable_cb(vclk->vq);

	vdev->priv = vclk;

	rproc_set_mmio_handler(vdev, virtio_clk_mmio);

	err = virtio_clk_hw_init(vdev, vclk);
	if (err)
		goto del_vqs;

	/* Tell the remote processor, front is online. */
	virtio_cwrite32(vdev, MMIO_FRONT_ONLINE, vdev->index);

	dev_info(&vdev->dev, "online\n");
	return 0;

del_vqs:
	vdev->config->del_vqs(vclk->vdev);

free_vclk:
	kfree(vclk);

	return err;
}

static void virtio_clk_remove(struct virtio_device *vdev)
{
	struct virtio_clk *vclk = vdev->priv;

	kfree(vclk->clk_data.clks);
	kfree(vclk->np_str);

	vdev->config->reset(vdev);

	vdev->config->del_vqs(vclk->vdev);

	kfree(vclk);
}


/* Setting the VIRTIO TYPE ID for this virtual device driver */
static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CLOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/*
 * We make virtio clock frontend driver to support all clock hardware
 * characteristics. The real features are depended on backend device
 * implement.
 */
static unsigned int features[] = {
	VIRTIO_CLK_F_PREPARE,
	VIRTIO_CLK_F_UNPREPARE,
	VIRTIO_CLK_F_IS_PREPARED,
	VIRTIO_CLK_F_UNPREPARE_UNUSED,
	VIRTIO_CLK_F_ENABLE,
	VIRTIO_CLK_F_DISABLE,
	VIRTIO_CLK_F_IS_ENABLED,
	VIRTIO_CLK_F_DISABLE_UNUSED,
	VIRTIO_CLK_F_RECALC_RATE,
	VIRTIO_CLK_F_ROUND_RATE,
	VIRTIO_CLK_F_DETERMINE_RATE,
	VIRTIO_CLK_F_SET_RATE,
	VIRTIO_CLK_F_SET_PARENT,
	VIRTIO_CLK_F_GET_PARENT,
	VIRTIO_CLK_F_RECALC_ACCURACY,
	VIRTIO_CLK_F_INIT,
};

static struct virtio_driver virtio_clk_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_clk_probe,
	.remove = virtio_clk_remove,
};

static int __init virtio_clk_init(void)
{
	return register_virtio_driver(&virtio_clk_driver);
}
module_init(virtio_clk_init);

static void __exit virtio_clk_fini(void)
{
	unregister_virtio_driver(&virtio_clk_driver);
}
module_exit(virtio_clk_fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio clock frontend driver");
MODULE_LICENSE("GPL v2");
