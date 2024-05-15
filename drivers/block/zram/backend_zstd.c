// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>

#include "backend_zstd.h"

struct zstd_ctx {
	zstd_cctx *cctx;
	zstd_dctx *dctx;
	void *cctx_mem;
	void *dctx_mem;
	zstd_custom_mem ctx_mem;
	zstd_cdict *cdict;
	zstd_ddict *ddict;
	s32 level;
};

/*
 * Cctx allocator.customAlloc() can be called from zcomp_compress() under
 * local-lock (per-CPU compression stream), in which case we must use
 * GFP_ATOMIC.
 */
static void *zstd_ctx_alloc(void *opaque, size_t size)
{
	if (!preemptible())
		return kvzalloc(size, GFP_ATOMIC);

	return kvzalloc(size, GFP_KERNEL);
}

static void zstd_ctx_free(void *opaque, void *address)
{
	kvfree(address);
}

static void zstd_destroy(void *ctx)
{
	struct zstd_ctx *zctx = ctx;

	if (zctx->cctx_mem)
		vfree(zctx->cctx_mem);
	else
		zstd_free_cctx(zctx->cctx);

	if (zctx->dctx_mem)
		vfree(zctx->dctx_mem);
	else
		zstd_free_dctx(zctx->dctx);

	zstd_free_cdict(zctx->cdict);
	zstd_free_ddict(zctx->ddict);
	kfree(zctx);
}

static void *zstd_create(struct zcomp_config *config)
{
	struct zstd_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	if (config->level != ZCOMP_CONFIG_NO_LEVEL)
		ctx->level = config->level;
	else
		ctx->level = zstd_default_clevel();

	ctx->ctx_mem.customAlloc = zstd_ctx_alloc;
	ctx->ctx_mem.customFree = zstd_ctx_free;

	if (config->dict_sz == 0) {
		zstd_parameters params;
		size_t sz;

		params = zstd_get_params(ctx->level, PAGE_SIZE);
		sz = zstd_cctx_workspace_bound(&params.cParams);
		ctx->cctx_mem = vzalloc(sz);
		if (!ctx->cctx_mem)
			goto error;

		ctx->cctx = zstd_init_cctx(ctx->cctx_mem, sz);
		if (!ctx->cctx)
			goto error;

		sz = zstd_dctx_workspace_bound();
		ctx->dctx_mem = vzalloc(sz);
		if (!ctx->dctx_mem)
			goto error;

		ctx->dctx = zstd_init_dctx(ctx->dctx_mem, sz);
		if (!ctx->dctx)
			goto error;
	} else {
		zstd_compression_parameters params;

		ctx->cctx = zstd_create_cctx_advanced(ctx->ctx_mem);
		if (!ctx->cctx)
			goto error;

		ctx->dctx = zstd_create_dctx_advanced(ctx->ctx_mem);
		if (!ctx->dctx)
			goto error;

		params = zstd_get_cparams(ctx->level, PAGE_SIZE,
					  config->dict_sz);

		ctx->cdict = zstd_create_cdict_advanced(config->dict,
							config->dict_sz,
							ZSTD_dlm_byRef,
							ZSTD_dct_auto,
							params,
							ctx->ctx_mem);
		if (!ctx->cdict)
			goto error;

		ctx->ddict = zstd_create_ddict_advanced(config->dict,
							config->dict_sz,
							ZSTD_dlm_byRef,
							ZSTD_dct_auto,
							ctx->ctx_mem);
		if (!ctx->ddict)
			goto error;
	}

	return ctx;

error:
	zstd_destroy(ctx);
	return NULL;
}

static int zstd_compress(void *ctx, const unsigned char *src,
			 unsigned char *dst, size_t *dst_len)
{
	struct zstd_ctx *zctx = ctx;
	const zstd_parameters params = zstd_get_params(zctx->level, PAGE_SIZE);
	size_t ret;

	if (!zctx->cdict)
		ret = zstd_compress_cctx(zctx->cctx, dst, *dst_len,
					 src, PAGE_SIZE, &params);
	else
		ret = zstd_compress_using_cdict(zctx->cctx, dst, *dst_len,
						src, PAGE_SIZE, zctx->cdict);
	if (zstd_is_error(ret))
		return -EINVAL;
	*dst_len = ret;
	return 0;
}

static int zstd_decompress(void *ctx, const unsigned char *src, size_t src_len,
			   unsigned char *dst)
{
	struct zstd_ctx *zctx = ctx;
	size_t ret;

	if (!zctx->ddict)
		ret = zstd_decompress_dctx(zctx->dctx, dst, PAGE_SIZE,
					   src, src_len);
	else
		ret = zstd_decompress_using_ddict(zctx->dctx, dst, PAGE_SIZE,
						  src, src_len, zctx->ddict);
	if (zstd_is_error(ret))
		return -EINVAL;
	return 0;
}

struct zcomp_backend backend_zstd = {
	.compress	= zstd_compress,
	.decompress	= zstd_decompress,
	.create_ctx	= zstd_create,
	.destroy_ctx	= zstd_destroy,
	.name		= "zstd",
};
