// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>

#include "backend_zstd.h"

struct zstd_ctx_data {
	zstd_custom_mem ctx_mem;
	zstd_cdict *cdict;
	zstd_ddict *ddict;
};

struct zstd_ctx {
	zstd_cctx *cctx;
	zstd_dctx *dctx;
	void *cctx_mem;
	void *dctx_mem;
	struct zstd_ctx_data *ctx_data;
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

static int zstd_init_config(struct zcomp_config *config)
{
	struct zstd_ctx_data *ctx_data = config->private;
	zstd_compression_parameters params;

	/* Already initialized */
	if (ctx_data)
		return 0;

	if (config->level == ZCOMP_CONFIG_NO_LEVEL)
		config->level = zstd_default_clevel();

	if (config->dict_sz == 0)
		return 0;

	ctx_data = kzalloc(sizeof(*ctx_data), GFP_KERNEL);
	if (!ctx_data)
		return -ENOMEM;

	ctx_data->ctx_mem.customAlloc = zstd_ctx_alloc;
	ctx_data->ctx_mem.customFree = zstd_ctx_free;

	params = zstd_get_cparams(config->level, PAGE_SIZE, config->dict_sz);

	ctx_data->cdict = zstd_create_cdict_advanced(config->dict,
						     config->dict_sz,
						     ZSTD_dlm_byRef,
						     ZSTD_dct_auto,
						     params,
						     ctx_data->ctx_mem);
	if (!ctx_data->cdict)
		goto error;

	ctx_data->ddict = zstd_create_ddict_advanced(config->dict,
						     config->dict_sz,
						     ZSTD_dlm_byRef,
						     ZSTD_dct_auto,
						     ctx_data->ctx_mem);
	if (!ctx_data->ddict)
		goto error;

	config->private = ctx_data;
	return 0;

error:
	zstd_free_cdict(ctx_data->cdict);
	zstd_free_ddict(ctx_data->ddict);
	kfree(ctx_data);
	return -EINVAL;
}

static void zstd_release_config(struct zcomp_config *config)
{
	struct zstd_ctx_data *ctx_data = config->private;

	if (!ctx_data)
		return;

	config->private = NULL;
	zstd_free_cdict(ctx_data->cdict);
	zstd_free_ddict(ctx_data->ddict);
	kfree(ctx_data);
}

static void zstd_destroy(void *ctx)
{
	struct zstd_ctx *zctx = ctx;

	/* Don't free zctx->ctx_data, it's done in release_config() */
	if (zctx->cctx_mem)
		vfree(zctx->cctx_mem);
	else
		zstd_free_cctx(zctx->cctx);
	if (zctx->dctx_mem)
		vfree(zctx->dctx_mem);
	else
		zstd_free_dctx(zctx->dctx);
	kfree(zctx);
}

static void *zstd_create(struct zcomp_config *config)
{
	struct zstd_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->ctx_data = config->private;
	ctx->level = config->level;

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
		struct zstd_ctx_data *ctx_data = ctx->ctx_data;

		ctx->cctx = zstd_create_cctx_advanced(ctx_data->ctx_mem);
		if (!ctx->cctx)
			goto error;

		ctx->dctx = zstd_create_dctx_advanced(ctx_data->ctx_mem);
		if (!ctx->dctx)
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
	struct zstd_ctx_data *ctx_data = zctx->ctx_data;
	const zstd_parameters params = zstd_get_params(zctx->level, PAGE_SIZE);
	size_t ret;

	if (!ctx_data)
		ret = zstd_compress_cctx(zctx->cctx, dst, *dst_len,
					 src, PAGE_SIZE, &params);
	else
		ret = zstd_compress_using_cdict(zctx->cctx, dst, *dst_len,
						src, PAGE_SIZE,
						ctx_data->cdict);
	if (zstd_is_error(ret))
		return -EINVAL;
	*dst_len = ret;
	return 0;
}

static int zstd_decompress(void *ctx, const unsigned char *src, size_t src_len,
			   unsigned char *dst)
{
	struct zstd_ctx *zctx = ctx;
	struct zstd_ctx_data *ctx_data = zctx->ctx_data;
	size_t ret;

	if (!ctx_data)
		ret = zstd_decompress_dctx(zctx->dctx, dst, PAGE_SIZE,
					   src, src_len);
	else
		ret = zstd_decompress_using_ddict(zctx->dctx, dst, PAGE_SIZE,
						  src, src_len,
						  ctx_data->ddict);
	if (zstd_is_error(ret))
		return -EINVAL;
	return 0;
}

struct zcomp_backend backend_zstd = {
	.compress	= zstd_compress,
	.decompress	= zstd_decompress,
	.create_ctx	= zstd_create,
	.destroy_ctx	= zstd_destroy,
	.init_config	= zstd_init_config,
	.release_config	= zstd_release_config,
	.name		= "zstd",
};
