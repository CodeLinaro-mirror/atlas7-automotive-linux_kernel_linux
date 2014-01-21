/* Virtio ring implementation.
 *
 *  Copyright 2007 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_config.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/hrtimer.h>

#ifdef DEBUG
/* For development, we want to crash whenever the ring is screwed. */
#define BAD_RING(_vq, fmt, args...)				\
	do {							\
		dev_err(&(_vq)->vq.vdev->dev,			\
			"%s:"fmt, (_vq)->vq.name, ##args);	\
		BUG();						\
	} while (0)
/* Caller is supposed to guarantee no reentry. */
#define START_USE(_vq)						\
	do {							\
		if ((_vq)->in_use)				\
			panic("%s:in_use = %i\n",		\
			      (_vq)->vq.name, (_vq)->in_use);	\
		(_vq)->in_use = __LINE__;			\
	} while (0)
#define END_USE(_vq) \
	do { BUG_ON(!(_vq)->in_use); (_vq)->in_use = 0; } while(0)
#else
#define BAD_RING(_vq, fmt, args...)				\
	do {							\
		dev_err(&_vq->vq.vdev->dev,			\
			"%s:"fmt, (_vq)->vq.name, ##args);	\
		(_vq)->broken = true;				\
	} while (0)
#define START_USE(vq)
#define END_USE(vq)
#endif

struct vring_virtqueue
{
	struct virtqueue vq;

	/* Actual memory layout for this queue */
	struct vring vring;

	/* Can we use weak barriers? */
	bool weak_barriers;

	/* Other side has made a mess, don't try any more. */
	bool broken;

	/* Host supports indirect buffers */
	bool indirect;

	/* Host publishes avail event idx */
	bool event;

	/* Head of free buffer list. */
	unsigned int free_head;
	/* Number we've added since last sync. */
	unsigned int num_added;

	/* Last used index we've seen. */
	u16 last_used_idx;

#ifdef CONFIG_VIRTIO_BACKEND
	/* Last avail index we've seen */
	u16 last_avail_idx;

	/* Last used index value we have signalled on */
	u16 signalled_used;

	/* Last used index value we have signalled on */
	bool signalled_used_valid;

	/* Notification enabled? */
	bool notification;

	int inuse;

#endif /* CONFIG_VIRTIO_BACKEND */

	/* How to notify other side. FIXME: commonalize hcalls! */
	bool (*notify)(struct virtqueue *vq);

#ifdef DEBUG
	/* They're supposed to lock for us. */
	unsigned int in_use;

	/* Figure out if their kicks are too delayed. */
	bool last_add_time_valid;
	ktime_t last_add_time;
#endif

	/* Tokens for callbacks. */
	void *data[];
};

#define to_vvq(_vq) container_of(_vq, struct vring_virtqueue, vq)

static inline struct scatterlist *sg_next_chained(struct scatterlist *sg,
						  unsigned int *count)
{
	return sg_next(sg);
}

static inline struct scatterlist *sg_next_arr(struct scatterlist *sg,
					      unsigned int *count)
{
	if (--(*count) == 0)
		return NULL;
	return sg + 1;
}

/* Set up an indirect table of descriptors and add it to the queue. */
static inline int vring_add_indirect(struct vring_virtqueue *vq,
				     struct scatterlist *sgs[],
				     struct scatterlist *(*next)
				       (struct scatterlist *, unsigned int *),
				     unsigned int total_sg,
				     unsigned int total_out,
				     unsigned int total_in,
				     unsigned int out_sgs,
				     unsigned int in_sgs,
				     gfp_t gfp)
{
	struct vring_desc *desc;
	unsigned head;
	struct scatterlist *sg;
	int i, n;

	/*
	 * We require lowmem mappings for the descriptors because
	 * otherwise virt_to_phys will give us bogus addresses in the
	 * virtqueue.
	 */
	gfp &= ~(__GFP_HIGHMEM | __GFP_HIGH);

	desc = kmalloc(total_sg * sizeof(struct vring_desc), gfp);
	if (!desc)
		return -ENOMEM;

	/* Transfer entries from the sg lists into the indirect page */
	i = 0;
	for (n = 0; n < out_sgs; n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_out)) {
			desc[i].flags = VRING_DESC_F_NEXT;
			desc[i].addr = sg_phys(sg);
			desc[i].len = sg->length;
			desc[i].next = i+1;
			i++;
		}
	}
	for (; n < (out_sgs + in_sgs); n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_in)) {
			desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
			desc[i].addr = sg_phys(sg);
			desc[i].len = sg->length;
			desc[i].next = i+1;
			i++;
		}
	}
	BUG_ON(i != total_sg);

	/* Last one doesn't continue. */
	desc[i-1].flags &= ~VRING_DESC_F_NEXT;
	desc[i-1].next = 0;

	/* We're about to use a buffer */
	vq->vq.num_free--;

	/* Use a single buffer which doesn't continue */
	head = vq->free_head;
	vq->vring.desc[head].flags = VRING_DESC_F_INDIRECT;
	vq->vring.desc[head].addr = virt_to_phys(desc);
	/* kmemleak gives a false positive, as it's hidden by virt_to_phys */
	kmemleak_ignore(desc);
	vq->vring.desc[head].len = i * sizeof(struct vring_desc);

	/* Update free pointer */
	vq->free_head = vq->vring.desc[head].next;

	return head;
}

static inline int virtqueue_add(struct virtqueue *_vq,
				struct scatterlist *sgs[],
				struct scatterlist *(*next)
				  (struct scatterlist *, unsigned int *),
				unsigned int total_out,
				unsigned int total_in,
				unsigned int out_sgs,
				unsigned int in_sgs,
				void *data,
				gfp_t gfp)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct scatterlist *sg;
	unsigned int i, n, avail, uninitialized_var(prev), total_sg;
	int head;

	START_USE(vq);

	BUG_ON(data == NULL);

#ifdef DEBUG
	{
		ktime_t now = ktime_get();

		/* No kick or get, with .1 second between?  Warn. */
		if (vq->last_add_time_valid)
			WARN_ON(ktime_to_ms(ktime_sub(now, vq->last_add_time))
					    > 100);
		vq->last_add_time = now;
		vq->last_add_time_valid = true;
	}
#endif

	total_sg = total_in + total_out;

	/* If the host supports indirect descriptor tables, and we have multiple
	 * buffers, then go indirect. FIXME: tune this threshold */
	if (vq->indirect && total_sg > 1 && vq->vq.num_free) {
		head = vring_add_indirect(vq, sgs, next, total_sg, total_out,
					  total_in,
					  out_sgs, in_sgs, gfp);
		if (likely(head >= 0))
			goto add_head;
	}

	BUG_ON(total_sg > vq->vring.num);
	BUG_ON(total_sg == 0);

	if (vq->vq.num_free < total_sg) {
		pr_debug("Can't add buf len %i - avail = %i\n",
			 total_sg, vq->vq.num_free);
		/* FIXME: for historical reasons, we force a notify here if
		 * there are outgoing parts to the buffer.  Presumably the
		 * host should service the ring ASAP. */
		if (out_sgs)
			vq->notify(&vq->vq);
		END_USE(vq);
		return -ENOSPC;
	}

	/* We're about to use some buffers from the free list. */
	vq->vq.num_free -= total_sg;

	head = i = vq->free_head;
	for (n = 0; n < out_sgs; n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_out)) {
			vq->vring.desc[i].flags = VRING_DESC_F_NEXT;
			vq->vring.desc[i].addr = sg_phys(sg);
			vq->vring.desc[i].len = sg->length;
			prev = i;
			i = vq->vring.desc[i].next;
		}
	}
	for (; n < (out_sgs + in_sgs); n++) {
		for (sg = sgs[n]; sg; sg = next(sg, &total_in)) {
			vq->vring.desc[i].flags = VRING_DESC_F_NEXT|VRING_DESC_F_WRITE;
			vq->vring.desc[i].addr = sg_phys(sg);
			vq->vring.desc[i].len = sg->length;
			prev = i;
			i = vq->vring.desc[i].next;
		}
	}
	/* Last one doesn't continue. */
	vq->vring.desc[prev].flags &= ~VRING_DESC_F_NEXT;

	/* Update free pointer */
	vq->free_head = i;

add_head:
	/* Set token. */
	vq->data[head] = data;

	/* Put entry in available array (but don't update avail->idx until they
	 * do sync). */
	avail = (vq->vring.avail->idx & (vq->vring.num-1));
	vq->vring.avail->ring[avail] = head;

	/* Descriptors and available array need to be set before we expose the
	 * new available array entries. */
	virtio_wmb(vq->weak_barriers);
	vq->vring.avail->idx++;
	vq->num_added++;

	/* This is very unlikely, but theoretically possible.  Kick
	 * just in case. */
	if (unlikely(vq->num_added == (1 << 16) - 1))
		virtqueue_kick(_vq);

	pr_debug("Added buffer head %i to %p\n", head, vq);
	END_USE(vq);

	return 0;
}

/**
 * virtqueue_add_sgs - expose buffers to other end
 * @vq: the struct virtqueue we're talking about.
 * @sgs: array of terminated scatterlists.
 * @out_num: the number of scatterlists readable by other side
 * @in_num: the number of scatterlists which are writable (after readable ones)
 * @data: the token identifying the buffer.
 * @gfp: how to do memory allocations (if necessary).
 *
 * Caller must ensure we don't call this with other virtqueue operations
 * at the same time (except where noted).
 *
 * Returns zero or a negative error (ie. ENOSPC, ENOMEM).
 */
int virtqueue_add_sgs(struct virtqueue *_vq,
		      struct scatterlist *sgs[],
		      unsigned int out_sgs,
		      unsigned int in_sgs,
		      void *data,
		      gfp_t gfp)
{
	unsigned int i, total_out, total_in;

	/* Count them first. */
	for (i = total_out = total_in = 0; i < out_sgs; i++) {
		struct scatterlist *sg;
		for (sg = sgs[i]; sg; sg = sg_next(sg))
			total_out++;
	}
	for (; i < out_sgs + in_sgs; i++) {
		struct scatterlist *sg;
		for (sg = sgs[i]; sg; sg = sg_next(sg))
			total_in++;
	}
	return virtqueue_add(_vq, sgs, sg_next_chained,
			     total_out, total_in, out_sgs, in_sgs, data, gfp);
}
EXPORT_SYMBOL_GPL(virtqueue_add_sgs);

/**
 * virtqueue_add_outbuf - expose output buffers to other end
 * @vq: the struct virtqueue we're talking about.
 * @sgs: array of scatterlists (need not be terminated!)
 * @num: the number of scatterlists readable by other side
 * @data: the token identifying the buffer.
 * @gfp: how to do memory allocations (if necessary).
 *
 * Caller must ensure we don't call this with other virtqueue operations
 * at the same time (except where noted).
 *
 * Returns zero or a negative error (ie. ENOSPC, ENOMEM).
 */
int virtqueue_add_outbuf(struct virtqueue *vq,
			 struct scatterlist sg[], unsigned int num,
			 void *data,
			 gfp_t gfp)
{
	return virtqueue_add(vq, &sg, sg_next_arr, num, 0, 1, 0, data, gfp);
}
EXPORT_SYMBOL_GPL(virtqueue_add_outbuf);

/**
 * virtqueue_add_inbuf - expose input buffers to other end
 * @vq: the struct virtqueue we're talking about.
 * @sgs: array of scatterlists (need not be terminated!)
 * @num: the number of scatterlists writable by other side
 * @data: the token identifying the buffer.
 * @gfp: how to do memory allocations (if necessary).
 *
 * Caller must ensure we don't call this with other virtqueue operations
 * at the same time (except where noted).
 *
 * Returns zero or a negative error (ie. ENOSPC, ENOMEM).
 */
int virtqueue_add_inbuf(struct virtqueue *vq,
			struct scatterlist sg[], unsigned int num,
			void *data,
			gfp_t gfp)
{
	return virtqueue_add(vq, &sg, sg_next_arr, 0, num, 0, 1, data, gfp);
}
EXPORT_SYMBOL_GPL(virtqueue_add_inbuf);

/**
 * virtqueue_kick_prepare - first half of split virtqueue_kick call.
 * @vq: the struct virtqueue
 *
 * Instead of virtqueue_kick(), you can do:
 *	if (virtqueue_kick_prepare(vq))
 *		virtqueue_notify(vq);
 *
 * This is sometimes useful because the virtqueue_kick_prepare() needs
 * to be serialized, but the actual virtqueue_notify() call does not.
 */
bool virtqueue_kick_prepare(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	u16 new, old;
	bool needs_kick;

	START_USE(vq);
	/* We need to expose available array entries before checking avail
	 * event. */
	virtio_mb(vq->weak_barriers);

	old = vq->vring.avail->idx - vq->num_added;
	new = vq->vring.avail->idx;
	vq->num_added = 0;

#ifdef DEBUG
	if (vq->last_add_time_valid) {
		WARN_ON(ktime_to_ms(ktime_sub(ktime_get(),
					      vq->last_add_time)) > 100);
	}
	vq->last_add_time_valid = false;
#endif

	if (vq->event) {
		needs_kick = vring_need_event(vring_avail_event(&vq->vring),
					      new, old);
	} else {
		needs_kick = !(vq->vring.used->flags & VRING_USED_F_NO_NOTIFY);
	}
	END_USE(vq);
	return needs_kick;
}
EXPORT_SYMBOL_GPL(virtqueue_kick_prepare);

/**
 * virtqueue_notify - second half of split virtqueue_kick call.
 * @vq: the struct virtqueue
 *
 * This does not need to be serialized.
 *
 * Returns false if host notify failed or queue is broken, otherwise true.
 */
bool virtqueue_notify(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	if (unlikely(vq->broken))
		return false;

	/* Prod other side to tell it about changes. */
	if (!vq->notify(_vq)) {
		vq->broken = true;
		return false;
	}
	return true;
}
EXPORT_SYMBOL_GPL(virtqueue_notify);

/**
 * virtqueue_kick - update after add_buf
 * @vq: the struct virtqueue
 *
 * After one or more virtqueue_add_* calls, invoke this to kick
 * the other side.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns false if kick failed, otherwise true.
 */
bool virtqueue_kick(struct virtqueue *vq)
{
	if (virtqueue_kick_prepare(vq))
		return virtqueue_notify(vq);
	return true;
}
EXPORT_SYMBOL_GPL(virtqueue_kick);

static void detach_buf(struct vring_virtqueue *vq, unsigned int head)
{
	unsigned int i;

	/* Clear data ptr. */
	vq->data[head] = NULL;

	/* Put back on free list: find end */
	i = head;

	/* Free the indirect table */
	if (vq->vring.desc[i].flags & VRING_DESC_F_INDIRECT)
		kfree(phys_to_virt(vq->vring.desc[i].addr));

	while (vq->vring.desc[i].flags & VRING_DESC_F_NEXT) {
		i = vq->vring.desc[i].next;
		vq->vq.num_free++;
	}

	vq->vring.desc[i].next = vq->free_head;
	vq->free_head = head;
	/* Plus final descriptor */
	vq->vq.num_free++;
}

static inline bool more_used(const struct vring_virtqueue *vq)
{
	return vq->last_used_idx != vq->vring.used->idx;
}

static void *__virtqueue_get_buf(struct virtqueue *_vq,
			unsigned int *p_idx, unsigned int *len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	void *ret;
	unsigned int i;
	u16 last_used;

	START_USE(vq);

	if (unlikely(vq->broken)) {
		END_USE(vq);
		return NULL;
	}

	if (!more_used(vq)) {
		pr_debug("No more buffers in queue\n");
		END_USE(vq);
		return NULL;
	}

	/* Only get used array entries after they have been exposed by host. */
	virtio_rmb(vq->weak_barriers);

	last_used = (vq->last_used_idx & (vq->vring.num - 1));
	i = vq->vring.used->ring[last_used].id;
	*len = vq->vring.used->ring[last_used].len;
	if (p_idx)
		*p_idx = i;

	if (unlikely(i >= vq->vring.num)) {
		BAD_RING(vq, "id %u out of range\n", i);
		return NULL;
	}
	if (unlikely(!vq->data[i])) {
		BAD_RING(vq, "id %u is not a head!\n", i);
		return NULL;
	}

	/* detach_buf clears data, so grab it now. */
	ret = vq->data[i];
	detach_buf(vq, i);
	vq->last_used_idx++;
	/* If we expect an interrupt for the next entry, tell host
	 * by writing event index and flush out the write before
	 * the read in the next get_buf call. */
	if (!(vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT)) {
		vring_used_event(&vq->vring) = vq->last_used_idx;
		virtio_mb(vq->weak_barriers);
	}

#ifdef DEBUG
	vq->last_add_time_valid = false;
#endif

	END_USE(vq);
	return ret;
}

/**
 * virtqueue_get_buf - get the next used buffer
 * @vq: the struct virtqueue we're talking about.
 * @len: the length written into the buffer
 *
 * If the driver wrote data into the buffer, @len will be set to the
 * amount written.  This means you don't need to clear the buffer
 * beforehand to ensure there's no data leakage in the case of short
 * writes.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 *
 * Returns NULL if there are no used buffers, or the "data" token
 * handed to virtqueue_add_*().
 */
void *virtqueue_get_buf(struct virtqueue *_vq, unsigned int *len)
{
	return __virtqueue_get_buf(_vq, NULL, len);
}
EXPORT_SYMBOL_GPL(virtqueue_get_buf);


/**
 * virtqueue_get_buf_with_idx - get the next used buffer
 * @vq: the struct virtqueue we're talking about.
 * @p_idx: the pointer of index id of the grabbed buffer.
 * @len: the length written into the buffer
 *
 * This function is an extention of virtqueue_get_buf. It return
 * one more infomation of buffer's index.
 *
 * Returns NULL if there are no used buffers, or the "data" token
 * handed to virtqueue_add_*().
 */
void *virtqueue_get_buf_with_idx(struct virtqueue *_vq,
			unsigned int *p_idx, unsigned int *len)
{
	return __virtqueue_get_buf(_vq, p_idx, len);
}
EXPORT_SYMBOL_GPL(virtqueue_get_buf_with_idx);

/**
 * virtqueue_disable_cb - disable callbacks
 * @vq: the struct virtqueue we're talking about.
 *
 * Note that this is not necessarily synchronous, hence unreliable and only
 * useful as an optimization.
 *
 * Unlike other operations, this need not be serialized.
 */
void virtqueue_disable_cb(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
}
EXPORT_SYMBOL_GPL(virtqueue_disable_cb);

/**
 * virtqueue_enable_cb_prepare - restart callbacks after disable_cb
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks; it returns current queue state
 * in an opaque unsigned value. This value should be later tested by
 * virtqueue_poll, to detect a possible race between the driver checking for
 * more work, and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
unsigned virtqueue_enable_cb_prepare(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	u16 last_used_idx;

	START_USE(vq);

	/* We optimistically turn back on interrupts, then check if there was
	 * more to do. */
	/* Depending on the VIRTIO_RING_F_EVENT_IDX feature, we need to
	 * either clear the flags bit or point the event index at the next
	 * entry. Always do both to keep code simple. */
	vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
	vring_used_event(&vq->vring) = last_used_idx = vq->last_used_idx;
	END_USE(vq);
	return last_used_idx;
}
EXPORT_SYMBOL_GPL(virtqueue_enable_cb_prepare);

/**
 * virtqueue_poll - query pending used buffers
 * @vq: the struct virtqueue we're talking about.
 * @last_used_idx: virtqueue state (from call to virtqueue_enable_cb_prepare).
 *
 * Returns "true" if there are pending used buffers in the queue.
 *
 * This does not need to be serialized.
 */
bool virtqueue_poll(struct virtqueue *_vq, unsigned last_used_idx)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	virtio_mb(vq->weak_barriers);
	return (u16)last_used_idx != vq->vring.used->idx;
}
EXPORT_SYMBOL_GPL(virtqueue_poll);

/**
 * virtqueue_enable_cb - restart callbacks after disable_cb.
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks; it returns "false" if there are pending
 * buffers in the queue, to detect a possible race between the driver
 * checking for more work, and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
bool virtqueue_enable_cb(struct virtqueue *_vq)
{
	unsigned last_used_idx = virtqueue_enable_cb_prepare(_vq);
	return !virtqueue_poll(_vq, last_used_idx);
}
EXPORT_SYMBOL_GPL(virtqueue_enable_cb);

/**
 * virtqueue_enable_cb_delayed - restart callbacks after disable_cb.
 * @vq: the struct virtqueue we're talking about.
 *
 * This re-enables callbacks but hints to the other side to delay
 * interrupts until most of the available buffers have been processed;
 * it returns "false" if there are many pending buffers in the queue,
 * to detect a possible race between the driver checking for more work,
 * and enabling callbacks.
 *
 * Caller must ensure we don't call this with other virtqueue
 * operations at the same time (except where noted).
 */
bool virtqueue_enable_cb_delayed(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	u16 bufs;

	START_USE(vq);

	/* We optimistically turn back on interrupts, then check if there was
	 * more to do. */
	/* Depending on the VIRTIO_RING_F_USED_EVENT_IDX feature, we need to
	 * either clear the flags bit or point the event index at the next
	 * entry. Always do both to keep code simple. */
	vq->vring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
	/* TODO: tune this threshold */
	bufs = (u16)(vq->vring.avail->idx - vq->last_used_idx) * 3 / 4;
	vring_used_event(&vq->vring) = vq->last_used_idx + bufs;
	virtio_mb(vq->weak_barriers);
	if (unlikely((u16)(vq->vring.used->idx - vq->last_used_idx) > bufs)) {
		END_USE(vq);
		return false;
	}

	END_USE(vq);
	return true;
}
EXPORT_SYMBOL_GPL(virtqueue_enable_cb_delayed);

/**
 * virtqueue_detach_unused_buf - detach first unused buffer
 * @vq: the struct virtqueue we're talking about.
 *
 * Returns NULL or the "data" token handed to virtqueue_add_*().
 * This is not valid on an active queue; it is useful only for device
 * shutdown.
 */
void *virtqueue_detach_unused_buf(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	unsigned int i;
	void *buf;

	START_USE(vq);

	for (i = 0; i < vq->vring.num; i++) {
		if (!vq->data[i])
			continue;
		/* detach_buf clears data, so grab it now. */
		buf = vq->data[i];
		detach_buf(vq, i);
		vq->vring.avail->idx--;
		END_USE(vq);
		return buf;
	}
	/* That should have freed everything. */
	BUG_ON(vq->vq.num_free != vq->vring.num);

	END_USE(vq);
	return NULL;
}
EXPORT_SYMBOL_GPL(virtqueue_detach_unused_buf);

/**
 * virtqueue_set_used_buf - set a buffer as used
 * @vq: the struct virtqueue we're talking about.
 * @idx: the index id of the buffer.
 * @len: the length of the buffer
 *
 * Returns true if success, otherwise false.
 */
bool virtqueue_set_used_buf(struct virtqueue *_vq,
			unsigned int idx, unsigned int len)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	struct vring_used_elem *used;

	if ((idx > vq->vring.num) || (idx < 0))
		return false;

	/*
	* The virtqueue contains a ring of used buffers.  Get a pointer to the
	* next entry in that used ring.
	*/
	used = &vq->vring.used->ring[vq->vring.used->idx % vq->vring.num];
	used->id = idx;
	used->len = len;

	vq->vring.used->idx++;

	return true;
}
EXPORT_SYMBOL_GPL(virtqueue_set_used_buf);

irqreturn_t vring_interrupt(int irq, void *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

#ifndef CONFIG_VIRTIO_BACKEND
	/* Only check used desc in frontend for bidirectional VQ */
	if (!more_used(vq)) {
		pr_debug("virtqueue interrupt with no work for %p\n", vq);
		return IRQ_NONE;
	}
#endif

	if (unlikely(vq->broken))
		return IRQ_HANDLED;

	pr_debug("virtqueue callback for %p (%p)\n", vq, vq->vq.callback);
	if (vq->vq.callback)
		vq->vq.callback(&vq->vq);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(vring_interrupt);

struct virtqueue *vring_new_virtqueue(unsigned int index,
				      unsigned int num,
				      unsigned int vring_align,
				      struct virtio_device *vdev,
				      bool weak_barriers,
				      void *pages,
				      bool (*notify)(struct virtqueue *),
				      void (*callback)(struct virtqueue *),
				      const char *name)
{
	struct vring_virtqueue *vq;
	unsigned int i;

	/* We assume num is a power of 2. */
	if (num & (num - 1)) {
		dev_warn(&vdev->dev, "Bad virtqueue length %u\n", num);
		return NULL;
	}

	vq = kmalloc(sizeof(*vq) + sizeof(void *)*num, GFP_KERNEL);
	if (!vq)
		return NULL;

	vring_init(&vq->vring, num, pages, vring_align);
	vq->vq.callback = callback;
	vq->vq.vdev = vdev;
	vq->vq.name = name;
	vq->vq.num_free = num;
	vq->vq.index = index;
	vq->notify = notify;
	vq->weak_barriers = weak_barriers;
	vq->broken = false;
	vq->last_used_idx = 0;
	vq->num_added = 0;
	list_add_tail(&vq->vq.list, &vdev->vqs);
#ifdef DEBUG
	vq->in_use = false;
	vq->last_add_time_valid = false;
#endif

	vq->indirect = virtio_has_feature(vdev, VIRTIO_RING_F_INDIRECT_DESC);
	vq->event = virtio_has_feature(vdev, VIRTIO_RING_F_EVENT_IDX);

#ifndef CONFIG_VIRTIO_BACKEND
	/* No callback?  Tell other side not to bother us. */
	if (!callback)
		vq->vring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
#endif
	/* Put everything in free lists. */
	vq->free_head = 0;
	for (i = 0; i < num-1; i++) {
		/* Backend VIRTIO does not need init desc */
#ifndef CONFIG_VIRTIO_BACKEND
		vq->vring.desc[i].next = i+1;
#endif
		vq->data[i] = NULL;
	}
	vq->data[i] = NULL;

	return &vq->vq;
}
EXPORT_SYMBOL_GPL(vring_new_virtqueue);

void vring_del_virtqueue(struct virtqueue *vq)
{
	list_del(&vq->list);
	kfree(to_vvq(vq));
}
EXPORT_SYMBOL_GPL(vring_del_virtqueue);

/* Manipulates transport-specific feature bits. */
void vring_transport_features(struct virtio_device *vdev)
{
	unsigned int i;

	for (i = VIRTIO_TRANSPORT_F_START; i < VIRTIO_TRANSPORT_F_END; i++) {
		switch (i) {
		case VIRTIO_RING_F_INDIRECT_DESC:
			break;
		case VIRTIO_RING_F_EVENT_IDX:
			break;
		default:
			/* We don't understand this bit. */
			clear_bit(i, vdev->features);
		}
	}
}
EXPORT_SYMBOL_GPL(vring_transport_features);

/**
 * virtqueue_get_vring_size - return the size of the virtqueue's vring
 * @vq: the struct virtqueue containing the vring of interest.
 *
 * Returns the size of the vring.  This is mainly used for boasting to
 * userspace.  Unlike other operations, this need not be serialized.
 */
unsigned int virtqueue_get_vring_size(struct virtqueue *_vq)
{

	struct vring_virtqueue *vq = to_vvq(_vq);

	return vq->vring.num;
}
EXPORT_SYMBOL_GPL(virtqueue_get_vring_size);

bool virtqueue_is_broken(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	return vq->broken;
}
EXPORT_SYMBOL_GPL(virtqueue_is_broken);


/* The following code is merged from qemu. And these codes only can
 * be used in backend virtual driver.
 * The original source code is marked at following URL:
 * http://git.qemu.org/?p=qemu.git;a=blob_plain;f=hw/virtio/virtio.c;hb=refs/tags/v1.6.1
 */

#ifdef CONFIG_VIRTIO_BACKEND

#define VQ_ACCESS_RAW_MEMOEY	0

static inline u32 vring_desc_addr(u32 desc_pa, int i)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u32 *pa;
	pa = (u32 *)(desc_pa + sizeof(struct vring_desc) * i
		+ offsetof(struct vring_desc, addr));
	return *pa;
#else
	return ((struct vring_desc *)desc_pa)[i].addr;
#endif
}

static inline u32 vring_desc_len(u32 desc_pa, int i)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u32 *pa;
	pa = (u32 *)(desc_pa + sizeof(struct vring_desc) * i +
		offsetof(struct vring_desc, len));
	return *pa;
#else
	return ((struct vring_desc *)desc_pa)[i].len;
#endif
}

static inline u16 vring_desc_flags(u32 desc_pa, int i)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u16 *pa;
	pa = (u16 *)(desc_pa + sizeof(struct vring_desc) * i +
		offsetof(struct vring_desc, flags));
	return *pa;
#else
	return ((struct vring_desc *)desc_pa)[i].flags;
#endif
}

static inline u16 vring_desc_next(u32 desc_pa, int i)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u16 *pa;
	pa = (u16 *)(desc_pa + sizeof(struct vring_desc) * i +
		offsetof(struct vring_desc, next));
	return *pa;
#else
	return ((struct vring_desc *)desc_pa)[i].next;
#endif
}

static inline u16 vring_avail_flags(struct vring_virtqueue *vq)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u16 *pa;
	pa = (u16 *)((size_t)vq->vring.avail +
		offsetof(struct vring_avail, flags));
	return *pa;
#else
	return vq->vring.avail->flags;
#endif
}

static inline u16 vring_avail_idx(struct vring_virtqueue *vq)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u16 *pa;
	pa = (u16 *)((size_t)vq->vring.avail +
		offsetof(struct vring_avail, idx));
	return *pa;
#else
	return vq->vring.avail->idx;
#endif
}


static inline u16 vring_avail_ring(struct vring_virtqueue *vq, int i)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u16 *pa;
	pa = (u16 *)((size_t)vq->vring.avail +
		offsetof(struct vring_avail, ring[i]));
	return *pa;
#else
	return vq->vring.avail->ring[i];
#endif
}

static inline void vring_used_ring_id(struct vring_virtqueue *vq,
				int i, u32 val)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u32 *pa;
	pa = (u32 *)((size_t)vq->vring.used +
		offsetof(struct vring_used, ring[i].id));
	*pa = val;
#else
	vq->vring.used->ring[i].id = val;
#endif

	return;
}

static inline void vring_used_ring_len(struct vring_virtqueue *vq,
			int i, u32 val)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u32 *pa;
	pa = (u32 *)((size_t)vq->vring.used +
		offsetof(struct vring_used, ring[i].len));
	*pa = val;
#else
	vq->vring.used->ring[i].len = val;
#endif

	return;
}

static inline u16 vring_used_idx(struct vring_virtqueue *vq)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u16 *pa;
	pa = (u16 *)((size_t)vq->vring.used +
		offsetof(struct vring_used, idx));
	return *pa;
#else
	return vq->vring.used->idx;
#endif
}

static inline void vring_used_idx_set(struct vring_virtqueue *vq, u16 val)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u16 *pa;
	pa = (u16 *)((size_t)vq->vring.used +
		offsetof(struct vring_used, idx));
	*pa = val;
#else
	vq->vring.used->idx = val;
#endif
	return;
}

static inline void vring_used_flags_set_bit(struct vring_virtqueue *vq,
					int mask)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u32 *pa;
	pa = (u32 *)((size_t)vq->vring.used +
		offsetof(struct vring_used, flags));
	*pa |= mask;
#else
	vq->vring.used->flags |= mask;
#endif
	return;
}

static inline void vring_used_flags_unset_bit(struct vring_virtqueue *vq,
					int mask)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u32 *pa;
	pa = (u32 *)((size_t)vq->vring.used +
		offsetof(struct vring_used, flags));
	*pa = (*pa & (~mask));
#else
	vq->vring.used->flags &= (~mask);
#endif
	return;
}

static inline void vring_avail_event_backend(struct vring_virtqueue *vq,
					u16 val)
{
#ifdef VQ_ACCESS_RAW_MEMOEY
	u32 *pa;
#endif

	if (!vq->notification)
		return;

#ifdef VQ_ACCESS_RAW_MEMOEY
	pa = (u32 *)((size_t)vq->vring.used +
		offsetof(struct vring_used, ring[vq->vring.num].id));
	*pa = val;
#else
	vq->vring.used->ring[vq->vring.num].id = val;
#endif
	return;
}

void virtio_queue_set_notification(struct virtqueue *_vq, int enable)
{
	struct vring_virtqueue *vq = to_vvq(_vq);

	vq->notification = enable;

	/* Fix it: currently, we assume guest features are equal
	 * to local features
	 */
	/* if (_vq->vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX)) */
	if (_vq->vdev->features[0] & (1 << VIRTIO_RING_F_EVENT_IDX))
		vring_avail_event_backend(vq, vring_avail_idx(vq));
	else if (enable)
		vring_used_flags_unset_bit(vq, VRING_USED_F_NO_NOTIFY);
	else
		vring_used_flags_set_bit(vq, VRING_USED_F_NO_NOTIFY);

	/* Expose avail event/used flags before caller checks the avail idx. */
	if (enable)
		virtio_mb(vq->weak_barriers);

	return;
}

int virtio_queue_ready(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	return vq->vring.avail != 0;
}

int virtio_queue_empty(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	return vring_avail_idx(vq) == vq->last_avail_idx;
}

void virtqueue_fill(struct virtqueue *_vq,
			const struct virtqueue_element *elem,
			unsigned int len, unsigned int idx)
{
	unsigned int offset;
	int i;
	struct vring_virtqueue *vq = to_vvq(_vq);

	offset = 0;
	for (i = 0; i < elem->in_num; i++) {
		size_t size = min(len - offset, elem->in_sg[i].iov_len);
		iounmap(elem->in_sg[i].iov_base);
		offset += elem->in_sg[i].iov_len;
		BUG_ON(size != elem->in_sg[i].iov_len);
	}

	for (i = 0; i < elem->out_num; i++)
		iounmap(elem->out_sg[i].iov_base);

	idx = (idx + vring_used_idx(vq)) % vq->vring.num;

	/* Get a pointer to the next entry in the used ring. */
	vring_used_ring_id(vq, idx, elem->index);
	vring_used_ring_len(vq, idx, len);

	return;
}

void virtqueue_flush(struct virtqueue *_vq, unsigned int count)
{
	u16 old, cur;
	struct vring_virtqueue *vq = to_vvq(_vq);

	/* Make sure buffer is written before we update index. */
	virtio_wmb(vq->weak_barriers);

	old = vring_used_idx(vq);
	cur = old + count;
	vring_used_idx_set(vq, cur);
	vq->inuse -= count;
	if (unlikely((int16_t)(cur - vq->signalled_used) < (u16)(cur - old)))
		vq->signalled_used_valid = false;

	return;
}

void virtqueue_push(struct virtqueue *_vq,
		const struct virtqueue_element *elem, unsigned int len)
{
	virtqueue_fill(_vq, elem, len, 0);
	virtqueue_flush(_vq, 1);
}

static int virtqueue_num_heads(struct vring_virtqueue *vq, unsigned int idx)
{
	u16 num_heads = vring_avail_idx(vq) - idx;

	/* Check it isn't doing very strange things with descriptor numbers. */
	if (num_heads > vq->vring.num) {
		pr_err("Guest moved used index from %u to %u\n",
			idx, vring_avail_idx(vq));
		BUG_ON(1);
	}
	/* On success, callers read a descriptor at vq->last_avail_idx.
	 * Make sure descriptor read does not bypass avail index read. */
	if (num_heads)
		virtio_rmb(vq->weak_barriers);

	return num_heads;
}


static unsigned int virtqueue_get_head(struct vring_virtqueue *vq,
					unsigned int idx)
{
	unsigned int head;

	/* Grab the next descriptor number they're advertising, and increment
	 * the index we've seen. */
	head = vring_avail_ring(vq, idx % vq->vring.num);

	/* If their number is silly, that's a fatal mistake. */
	if (head >= vq->vring.num) {
		pr_err("Guest says index %u is available\n", head);
		BUG_ON(1);
	}

	return head;
}

static unsigned int virtqueue_next_desc(struct vring_virtqueue *vq,
					u32 desc_pa, unsigned int i,
					unsigned int max)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(vring_desc_flags(desc_pa, i) & VRING_DESC_F_NEXT))
		return max;

	/* Check they're not leading us off end of descriptors. */
	next = vring_desc_next(desc_pa, i);
	/* Make sure compiler knows to grab that: we don't want it changing! */
	virtio_wmb(vq->weak_barriers);

	if (next >= max) {
		pr_err("Desc next is %u\n", next);
		BUG_ON(1);
	}

	return next;
}

void virtqueue_get_avail_bytes(struct virtqueue *_vq, unsigned int *in_bytes,
				unsigned int *out_bytes,
				unsigned max_in_bytes, unsigned max_out_bytes)
{
	unsigned int idx;
	unsigned int total_bufs, in_total, out_total;
	struct vring_virtqueue *vq = to_vvq(_vq);

	idx = vq->last_avail_idx;

	total_bufs = in_total = out_total = 0;
	while (virtqueue_num_heads(vq, idx)) {
		unsigned int max, num_bufs, indirect = 0;
		u32 desc_pa;
		int i;

		max = vq->vring.num;
		num_bufs = total_bufs;
		i = virtqueue_get_head(vq, idx++);
		desc_pa = (u32)vq->vring.desc;

		if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_INDIRECT) {
			if (vring_desc_len(desc_pa, i) %
				sizeof(struct vring_desc)) {
				pr_err("Invalid size for indirect buffer table\n");
				BUG_ON(1);
			}

			/* If we've got too many
			 * that implies a descriptor loop.
			 */
			if (num_bufs >= max) {
				pr_err("Looped descriptor\n");
				BUG_ON(1);
			}

			/* loop over the indirect descriptor table */
			indirect = 1;
			max = vring_desc_len(desc_pa, i) /
					sizeof(struct vring_desc);
			desc_pa = vring_desc_addr(desc_pa, i);
			num_bufs = i = 0;
		}

		do {
			/* If we've got too many
			 * that implies a descriptor loop.
			 */
			if (++num_bufs > max) {
				pr_err("Looped descriptor\n");
				BUG_ON(1);
			}

			if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_WRITE)
				in_total += vring_desc_len(desc_pa, i);
			else
				out_total += vring_desc_len(desc_pa, i);

			if (in_total >= max_in_bytes &&
				out_total >= max_out_bytes)
				goto done;

		} while ((i = virtqueue_next_desc(vq, desc_pa, i, max)) != max);

		if (!indirect)
			total_bufs = num_bufs;
		else
			total_bufs++;
	}

done:
	if (in_bytes)
		*in_bytes = in_total;

	if (out_bytes)
		*out_bytes = out_total;

	return;
}

int virtqueue_avail_bytes(struct virtqueue *_vq,
			unsigned int in_bytes, unsigned int out_bytes)
{
	unsigned int in_total, out_total;

	virtqueue_get_avail_bytes(_vq, &in_total,
				&out_total, in_bytes, out_bytes);
	return in_bytes <= in_total && out_bytes <= out_total;
}

void virtqueue_map_sg(struct kvec *sg, u32 *addr, size_t num_sg,
			int is_write)
{
	unsigned int i;

	for (i = 0; i < num_sg; i++) {
		sg[i].iov_base = ioremap(addr[i], sg[i].iov_len);
		if (sg[i].iov_base == NULL) {
			pr_err("virtio: trying to map MMIO memory");
			BUG_ON(1);
		}
	}
	return;
}

int virtqueue_pop(struct virtqueue *_vq, struct virtqueue_element *elem)
{
	unsigned int i, head, max, is_indirect = 0;
	struct vring_virtqueue *vq = to_vvq(_vq);
	u32 desc_pa = (u32)vq->vring.desc;

	if (!virtqueue_num_heads(vq, vq->last_avail_idx))
		return 0;

	/* When we start there are none of either input nor output. */
	elem->out_num = elem->in_num = 0;

	max = vq->vring.num;

	i = head = virtqueue_get_head(vq, vq->last_avail_idx++);

	/* Fix it: currently, we assume guest features are equal
	 * to local features
	 */
	/* if (_vq->vdev->guest_features & (1 << VIRTIO_RING_F_EVENT_IDX)) */
	if (_vq->vdev->features[0] & (1 << VIRTIO_RING_F_EVENT_IDX))
		vring_avail_event_backend(vq, vring_avail_idx(vq));

	if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_INDIRECT) {
		int indirect_len;

		if (vring_desc_len(desc_pa, i) % sizeof(struct vring_desc)) {
			pr_err("Invalid size for indirect buffer table");
			BUG_ON(1);
		}
		/* loop over the indirect descriptor table */
		indirect_len = vring_desc_len(desc_pa, i);
		max = indirect_len / sizeof(struct vring_desc);
		desc_pa = (u32)ioremap((phys_addr_t)
			vring_desc_addr(desc_pa, i), indirect_len);
		if (desc_pa == (u32)NULL) {
			pr_err("virtio: trying to map MMIO memory, %s\n",
				__func__);
			BUG_ON(1);
		}
		is_indirect = 1;
		i = 0;
	}

	/* Collect all the descriptors */
	do {
		struct kvec *sg;

		if (vring_desc_flags(desc_pa, i) & VRING_DESC_F_WRITE) {
			if (elem->in_num >= ARRAY_SIZE(elem->in_sg)) {
				pr_err("Too many write descriptors in indirect table\n");
				BUG_ON(1);
			}
			elem->in_addr[elem->in_num] =
				vring_desc_addr(desc_pa, i);
			sg = &elem->in_sg[elem->in_num++];
		} else {
			if (elem->out_num >= ARRAY_SIZE(elem->out_sg)) {
				pr_err("Too many read descriptors in indirect table\n");
				BUG_ON(1);
			}
			elem->out_addr[elem->out_num] =
				vring_desc_addr(desc_pa, i);
			sg = &elem->out_sg[elem->out_num++];
		}

		sg->iov_len = vring_desc_len(desc_pa, i);

		/* If we've got too many, that implies a descriptor loop. */
		if ((elem->in_num + elem->out_num) > max) {
			pr_err("Looped descriptor\n");
			BUG_ON(1);
		}
	} while ((i = virtqueue_next_desc(vq, desc_pa, i, max)) != max);

	if (is_indirect)
		iounmap((void *)desc_pa);

	/* Now map what we have collected */
	virtqueue_map_sg(elem->in_sg, elem->in_addr, elem->in_num, 1);
	virtqueue_map_sg(elem->out_sg, elem->out_addr, elem->out_num, 0);

	elem->index = head;

	vq->inuse++;

	return elem->in_num + elem->out_num;
}


void virtqueue_iovec_init_external(struct virtqueue_iovector *vq_iov,
					struct kvec *iov, int niov)
{
	int i;

	vq_iov->base = NULL;
	vq_iov->iov = iov;
	vq_iov->n_iov = niov;
	vq_iov->size = 0;

	for (i = 0; i < niov; i++)
		vq_iov->size += iov[i].iov_len;

	if (i)
		vq_iov->base = iov[0].iov_base;

	return;
}

#endif /* CONFIG_VIRTIO_BACKEND */

MODULE_LICENSE("GPL");
