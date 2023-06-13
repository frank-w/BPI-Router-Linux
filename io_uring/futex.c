// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "../kernel/futex/futex.h"
#include "io_uring.h"
#include "rsrc.h"
#include "futex.h"

struct io_futex {
	struct file	*file;
	union {
		u32 __user			*uaddr;
		struct futex_waitv __user	*uwaitv;
	};
	unsigned long	futex_val;
	unsigned long	futex_mask;
	unsigned long	futexv_owned;
	u32		futex_flags;
	unsigned int	futex_nr;
};

struct io_futex_data {
	union {
		struct futex_q		q;
		struct io_cache_entry	cache;
	};
	struct io_kiocb	*req;
};

void io_futex_cache_init(struct io_ring_ctx *ctx)
{
	io_alloc_cache_init(&ctx->futex_cache, IO_NODE_ALLOC_CACHE_MAX,
				sizeof(struct io_futex_data));
}

static void io_futex_cache_entry_free(struct io_cache_entry *entry)
{
	kfree(container_of(entry, struct io_futex_data, cache));
}

void io_futex_cache_free(struct io_ring_ctx *ctx)
{
	io_alloc_cache_free(&ctx->futex_cache, io_futex_cache_entry_free);
}

static void __io_futex_complete(struct io_kiocb *req, struct io_tw_state *ts)
{
	req->async_data = NULL;
	hlist_del_init(&req->hash_node);
	io_req_task_complete(req, ts);
}

static void io_futex_complete(struct io_kiocb *req, struct io_tw_state *ts)
{
	struct io_futex_data *ifd = req->async_data;
	struct io_ring_ctx *ctx = req->ctx;

	io_tw_lock(ctx, ts);
	if (!io_alloc_cache_put(&ctx->futex_cache, &ifd->cache))
		kfree(ifd);
	__io_futex_complete(req, ts);
}

static void io_futexv_complete(struct io_kiocb *req, struct io_tw_state *ts)
{
	struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);
	struct futex_vector *futexv = req->async_data;
	struct io_ring_ctx *ctx = req->ctx;
	int res = 0;

	io_tw_lock(ctx, ts);

	res = futex_unqueue_multiple(futexv, iof->futex_nr);
	if (res != -1)
		io_req_set_res(req, res, 0);

	kfree(req->async_data);
	req->flags &= ~REQ_F_ASYNC_DATA;
	__io_futex_complete(req, ts);
}

static bool io_futexv_claimed(struct io_futex *iof)
{
	return test_bit(0, &iof->futexv_owned);
}

static bool io_futexv_claim(struct io_futex *iof)
{
	if (test_bit(0, &iof->futexv_owned) ||
	    test_and_set_bit(0, &iof->futexv_owned))
		return false;
	return true;
}

static bool __io_futex_cancel(struct io_ring_ctx *ctx, struct io_kiocb *req)
{
	/* futex wake already done or in progress */
	if (req->opcode == IORING_OP_FUTEX_WAIT) {
		struct io_futex_data *ifd = req->async_data;

		if (!futex_unqueue(&ifd->q))
			return false;
		req->io_task_work.func = io_futex_complete;
	} else {
		struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);

		if (!io_futexv_claim(iof))
			return false;
		req->io_task_work.func = io_futexv_complete;
	}

	hlist_del_init(&req->hash_node);
	io_req_set_res(req, -ECANCELED, 0);
	io_req_task_work_add(req);
	return true;
}

int io_futex_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd,
		    unsigned int issue_flags)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	int nr = 0;

	if (cd->flags & (IORING_ASYNC_CANCEL_FD|IORING_ASYNC_CANCEL_FD_FIXED))
		return -ENOENT;

	io_ring_submit_lock(ctx, issue_flags);
	hlist_for_each_entry_safe(req, tmp, &ctx->futex_list, hash_node) {
		if (req->cqe.user_data != cd->data &&
		    !(cd->flags & IORING_ASYNC_CANCEL_ANY))
			continue;
		if (__io_futex_cancel(ctx, req))
			nr++;
		if (!(cd->flags & IORING_ASYNC_CANCEL_ALL))
			break;
	}
	io_ring_submit_unlock(ctx, issue_flags);

	if (nr)
		return nr;

	return -ENOENT;
}

bool io_futex_remove_all(struct io_ring_ctx *ctx, struct task_struct *task,
			 bool cancel_all)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	bool found = false;

	lockdep_assert_held(&ctx->uring_lock);

	hlist_for_each_entry_safe(req, tmp, &ctx->futex_list, hash_node) {
		if (!io_match_task_safe(req, task, cancel_all))
			continue;
		__io_futex_cancel(ctx, req);
		found = true;
	}

	return found;
}

int io_futex_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);
	u32 flags;

	if (unlikely(sqe->fd || sqe->len || sqe->buf_index || sqe->file_index))
		return -EINVAL;

	iof->uaddr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	iof->futex_val = READ_ONCE(sqe->addr2);
	iof->futex_mask = READ_ONCE(sqe->addr3);
	flags = READ_ONCE(sqe->futex_flags);

	if (flags & ~FUTEX2_VALID_MASK)
		return -EINVAL;

	iof->futex_flags = futex2_to_flags(flags);
	if (!futex_flags_valid(iof->futex_flags))
		return -EINVAL;

	if (!futex_validate_input(iof->futex_flags, iof->futex_val) ||
	    !futex_validate_input(iof->futex_flags, iof->futex_mask))
		return -EINVAL;

	return 0;
}

static void io_futex_wakev_fn(struct wake_q_head *wake_q, struct futex_q *q)
{
	struct io_kiocb *req = q->wake_data;
	struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);

	if (!io_futexv_claim(iof))
		return;
	if (unlikely(!__futex_wake_mark(q)))
		return;

	io_req_set_res(req, 0, 0);
	req->io_task_work.func = io_futexv_complete;
	io_req_task_work_add(req);
}

int io_futexv_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);
	struct futex_vector *futexv;
	int ret;

	/* No flags or mask supported for waitv */
	if (unlikely(sqe->fd || sqe->buf_index || sqe->file_index ||
		     sqe->addr2 || sqe->addr3))
		return -EINVAL;

	iof->uaddr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	iof->futex_nr = READ_ONCE(sqe->len);
	if (!iof->futex_nr || iof->futex_nr > FUTEX_WAITV_MAX)
		return -EINVAL;

	futexv = kcalloc(iof->futex_nr, sizeof(*futexv), GFP_KERNEL);
	if (!futexv)
		return -ENOMEM;

	ret = futex_parse_waitv(futexv, iof->uwaitv, iof->futex_nr,
				io_futex_wakev_fn, req);
	if (ret) {
		kfree(futexv);
		return ret;
	}

	iof->futexv_owned = 0;
	req->flags |= REQ_F_ASYNC_DATA;
	req->async_data = futexv;
	return 0;
}

static void io_futex_wake_fn(struct wake_q_head *wake_q, struct futex_q *q)
{
	struct io_futex_data *ifd = container_of(q, struct io_futex_data, q);
	struct io_kiocb *req = ifd->req;

	if (unlikely(!__futex_wake_mark(q)))
		return;

	io_req_set_res(req, 0, 0);
	req->io_task_work.func = io_futex_complete;
	io_req_task_work_add(req);
}

static struct io_futex_data *io_alloc_ifd(struct io_ring_ctx *ctx)
{
	struct io_cache_entry *entry;

	entry = io_alloc_cache_get(&ctx->futex_cache);
	if (entry)
		return container_of(entry, struct io_futex_data, cache);

	return kmalloc(sizeof(struct io_futex_data), GFP_NOWAIT);
}

int io_futexv_wait(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);
	struct futex_vector *futexv = req->async_data;
	struct io_ring_ctx *ctx = req->ctx;
	int ret, woken = -1;

	io_ring_submit_lock(ctx, issue_flags);

	ret = futex_wait_multiple_setup(futexv, iof->futex_nr, &woken);

	/*
	 * The above call leaves us potentially non-running. This is fine
	 * for the sync syscall as it'll be blocking unless we already got
	 * one of the futexes woken, but it obviously won't work for an async
	 * invocation. Mark us runnable again.
	 */
	__set_current_state(TASK_RUNNING);

	/*
	 * We got woken while setting up, let that side do the completion
	 */
	if (io_futexv_claimed(iof)) {
skip:
		io_ring_submit_unlock(ctx, issue_flags);
		return IOU_ISSUE_SKIP_COMPLETE;
	}

	/*
	 * 0 return means that we successfully setup the waiters, and that
	 * nobody triggered a wakeup while we were doing so. < 0 or 1 return
	 * is either an error or we got a wakeup while setting up.
	 */
	if (!ret) {
		hlist_add_head(&req->hash_node, &ctx->futex_list);
		goto skip;
	}

	io_ring_submit_unlock(ctx, issue_flags);
	if (ret < 0)
		req_set_fail(req);
	else if (woken != -1)
		ret = woken;
	io_req_set_res(req, ret, 0);
	kfree(futexv);
	req->flags &= ~REQ_F_ASYNC_DATA;
	return IOU_OK;
}

int io_futex_wait(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_futex_data *ifd = NULL;
	struct futex_hash_bucket *hb;
	int ret;

	if (!iof->futex_mask) {
		ret = -EINVAL;
		goto done;
	}

	io_ring_submit_lock(ctx, issue_flags);
	ifd = io_alloc_ifd(ctx);
	if (!ifd) {
		ret = -ENOMEM;
		goto done_unlock;
	}

	req->async_data = ifd;
	ifd->q = futex_q_init;
	ifd->q.bitset = iof->futex_mask;
	ifd->q.wake = io_futex_wake_fn;
	ifd->req = req;

	ret = futex_wait_setup(iof->uaddr, iof->futex_val,
			       futex2_to_flags(iof->futex_flags), &ifd->q, &hb);
	if (!ret) {
		hlist_add_head(&req->hash_node, &ctx->futex_list);
		io_ring_submit_unlock(ctx, issue_flags);

		futex_queue(&ifd->q, hb);
		return IOU_ISSUE_SKIP_COMPLETE;
	}

done_unlock:
	io_ring_submit_unlock(ctx, issue_flags);
done:
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	kfree(ifd);
	return IOU_OK;
}

int io_futex_wake(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_futex *iof = io_kiocb_to_cmd(req, struct io_futex);
	int ret;

	ret = futex_wake(iof->uaddr, futex2_to_flags(iof->futex_flags),
			 iof->futex_val, iof->futex_mask);
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}
