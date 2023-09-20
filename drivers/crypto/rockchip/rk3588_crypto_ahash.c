// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto acceleration support for Rockchip RK3588
 *
 * Copyright (c) 2022 Corentin Labbe <clabbe@baylibre.com>
 */
#include <asm/unaligned.h>
#include <linux/iopoll.h>
#include "rk3588_crypto.h"

static bool rk_ahash_need_fallback(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct rk_crypto_template *algt = container_of(alg, struct rk_crypto_template, alg.hash.base);
	struct scatterlist *sg;

	sg = areq->src;
	while (sg) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32))) {
			algt->stat_fb_align++;
			return true;
		}
		if (sg->length % 4) {
			algt->stat_fb_sglen++;
			return true;
		}
		sg = sg_next(sg);
	}
	return false;
}

static int rk_ahash_digest_fb(struct ahash_request *areq)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct rk_ahash_ctx *tfmctx = crypto_ahash_ctx(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct rk_crypto_template *algt = container_of(alg, struct rk_crypto_template, alg.hash.base);

	algt->stat_fb++;

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	rctx->fallback_req.base.flags = areq->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = areq->nbytes;
	rctx->fallback_req.src = areq->src;
	rctx->fallback_req.result = areq->result;

	return crypto_ahash_digest(&rctx->fallback_req);
}

static int zero_message_process(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct rk_crypto_template *algt = container_of(alg, struct rk_crypto_template, alg.hash.base);
	int digestsize = crypto_ahash_digestsize(tfm);

	switch (algt->rk_mode) {
	case RK_CRYPTO_SHA1:
		memcpy(req->result, sha1_zero_message_hash, digestsize);
		break;
	case RK_CRYPTO_SHA256:
		memcpy(req->result, sha256_zero_message_hash, digestsize);
		break;
	case RK_CRYPTO_SHA384:
		memcpy(req->result, sha384_zero_message_hash, digestsize);
		break;
	case RK_CRYPTO_SHA512:
		memcpy(req->result, sha512_zero_message_hash, digestsize);
		break;
	case RK_CRYPTO_MD5:
		memcpy(req->result, md5_zero_message_hash, digestsize);
		break;
	case RK_CRYPTO_SM3:
		memcpy(req->result, sm3_zero_message_hash, digestsize);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int rk_ahash_init(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

int rk_ahash_update(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;

	return crypto_ahash_update(&rctx->fallback_req);
}

int rk_ahash_final(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_final(&rctx->fallback_req);
}

int rk_ahash_finup(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_finup(&rctx->fallback_req);
}

int rk_ahash_import(struct ahash_request *req, const void *in)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&rctx->fallback_req, in);
}

int rk_ahash_export(struct ahash_request *req, void *out)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct rk_ahash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&rctx->fallback_req, out);
}

int rk_ahash_digest(struct ahash_request *req)
{
	struct rk_ahash_rctx *rctx = ahash_request_ctx(req);
	struct rk_crypto_dev *dev;
	struct crypto_engine *engine;

	if (rk_ahash_need_fallback(req))
		return rk_ahash_digest_fb(req);

	if (!req->nbytes)
		return zero_message_process(req);

	dev = get_rk_crypto();

	rctx->dev = dev;
	engine = dev->engine;

	return crypto_transfer_hash_request_to_engine(engine, req);
}

static int rk_hash_prepare(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct rk_crypto_dev *rkc = rctx->dev;
	int ret;

	ret = dma_map_sg(rkc->dev, areq->src, sg_nents(areq->src), DMA_TO_DEVICE);
	if (ret <= 0)
		return -EINVAL;

	rctx->nrsgs = ret;

	return 0;
}

static void rk_hash_unprepare(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct rk_crypto_dev *rkc = rctx->dev;

	dma_unmap_sg(rkc->dev, areq->src, rctx->nrsgs, DMA_TO_DEVICE);
}

int rk2_hash_run(struct crypto_engine *engine, void *breq)
{
	struct ahash_request *areq = container_of(breq, struct ahash_request, base);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct rk_ahash_rctx *rctx = ahash_request_ctx(areq);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct rk_crypto_template *algt = container_of(alg, struct rk_crypto_template, alg.hash.base);
	struct scatterlist *sgs = areq->src;
	struct rk_crypto_dev *rkc = rctx->dev;
	struct rk_crypto_lli *dd = &rkc->tl[0];
	int ddi = 0;
	int err = 0;
	unsigned int len = areq->nbytes;
	unsigned int todo;
	u32 v;
	int i;

	err = rk_hash_prepare(engine, breq);

	err = pm_runtime_resume_and_get(rkc->dev);
	if (err)
		return err;

	dev_dbg(rkc->dev, "%s %s len=%d\n", __func__,
		crypto_tfm_alg_name(areq->base.tfm), areq->nbytes);

	algt->stat_req++;
	rkc->nreq++;

	rctx->mode = algt->rk_mode;
	rctx->mode |= 0xffff0000;
	rctx->mode |= RK_CRYPTO_ENABLE | RK_CRYPTO_HW_PAD;
	writel(rctx->mode, rkc->reg + RK_CRYPTO_HASH_CTL);

	while (sgs && len > 0) {
		dd = &rkc->tl[ddi];

		todo = min(sg_dma_len(sgs), len);
		dd->src_addr = sg_dma_address(sgs);
		dd->src_len = todo;
		dd->dst_addr = 0;
		dd->dst_len = 0;
		dd->dma_ctrl = ddi << 24;
		dd->iv = 0;
		dd->next = rkc->t_phy + sizeof(struct rk_crypto_lli) * (ddi + 1);

		if (ddi == 0)
			dd->user = RK_LLI_CIPHER_START | RK_LLI_STRING_FIRST;
		else
			dd->user = 0;

		len -= todo;
		dd->dma_ctrl |= RK_LLI_DMA_CTRL_SRC_INT;
		if (len == 0) {
			dd->user |= RK_LLI_STRING_LAST;
			dd->dma_ctrl |= RK_LLI_DMA_CTRL_LAST;
		}
		dev_dbg(rkc->dev, "HASH SG %d sglen=%d user=%x dma=%x mode=%x len=%d todo=%d phy=%llx\n",
			ddi, sgs->length, dd->user, dd->dma_ctrl, rctx->mode, len, todo, rkc->t_phy);

		sgs = sg_next(sgs);
		ddi++;
	}
	dd->next = 1;
	writel(RK_CRYPTO_DMA_INT_LISTDONE | 0x7F, rkc->reg + RK_CRYPTO_DMA_INT_EN);

/*	writel(0x00030003, rkc->reg + RK_CRYPTO_FIFO_CTL);*/
	writel(rkc->t_phy, rkc->reg + RK_CRYPTO_DMA_LLI_ADDR);

	reinit_completion(&rkc->complete);
	rkc->status = 0;

	writel(RK_CRYPTO_DMA_CTL_START | 1 << 16, rkc->reg + RK_CRYPTO_DMA_CTL);

	wait_for_completion_interruptible_timeout(&rkc->complete,
						  msecs_to_jiffies(2000));
	if (!rkc->status) {
		dev_err(rkc->dev, "DMA timeout\n");
		err = -EFAULT;
		goto theend;
	}

	readl_poll_timeout_atomic(rkc->reg + RK_CRYPTO_HASH_VALID, v, v == 1,
				  10, 1000);

	for (i = 0; i < crypto_ahash_digestsize(tfm) / 4; i++) {
		v = readl(rkc->reg + RK_CRYPTO_HASH_DOUT_0 + i * 4);
		put_unaligned_le32(be32_to_cpu(v), areq->result + i * 4);
	}

theend:
	pm_runtime_put_autosuspend(rkc->dev);

	rk_hash_unprepare(engine, breq);

	local_bh_disable();
	crypto_finalize_hash_request(engine, breq, err);
	local_bh_enable();

	return 0;
}

int rk_hash_init_tfm(struct crypto_ahash *tfm)
{
	struct rk_ahash_ctx *tctx = crypto_ahash_ctx(tfm);
	const char *alg_name = crypto_ahash_alg_name(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct rk_crypto_template *algt = container_of(alg, struct rk_crypto_template, alg.hash.base);

	/* for fallback */
	tctx->fallback_tfm = crypto_alloc_ahash(alg_name, 0,
						CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tctx->fallback_tfm)) {
		dev_err(algt->dev->dev, "Could not load fallback driver.\n");
		return PTR_ERR(tctx->fallback_tfm);
	}

	crypto_ahash_set_reqsize(tfm,
				 sizeof(struct rk_ahash_rctx) +
				 crypto_ahash_reqsize(tctx->fallback_tfm));
	return 0;
}

void rk_hash_exit_tfm(struct crypto_ahash *tfm)
{
	struct rk_ahash_ctx *tctx = crypto_ahash_ctx(tfm);

	crypto_free_ahash(tctx->fallback_tfm);
}
