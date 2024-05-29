#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4hc.h"

struct lz4hc_ctx {
	void *mem;
	s32 level;
	LZ4_streamDecode_t *dstrm;
	LZ4_streamHC_t *cstrm;

	/* Shared between C/D streams */
	void *dict;
	size_t dict_sz;
};

static int lz4hc_init_config(struct zcomp_config *config)
{
	if (config->level == ZCOMP_CONFIG_NO_LEVEL)
		config->level = LZ4HC_DEFAULT_CLEVEL;

	return 0;
}

static void lz4hc_release_config(struct zcomp_config *config)
{
}

static void lz4hc_destroy(void *ctx)
{
	struct lz4hc_ctx *zctx = ctx;

	kfree(zctx->dstrm);
	kfree(zctx->cstrm);
	vfree(zctx->mem);
	kfree(zctx);
}

static void *lz4hc_create(struct zcomp_config *config)
{
	struct lz4hc_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->level = config->level;
	if (!config->dict) {
		ctx->mem = vmalloc(LZ4HC_MEM_COMPRESS);
		if (!ctx->mem)
			goto error;
	} else {
		ctx->dstrm = kzalloc(sizeof(*ctx->dstrm), GFP_KERNEL);
		if (!ctx->dstrm)
			goto error;

		ctx->cstrm = kzalloc(sizeof(*ctx->cstrm), GFP_KERNEL);
		if (!ctx->cstrm)
			goto error;

		ctx->dict = config->dict;
		ctx->dict_sz = config->dict_sz;
	}

	return ctx;
error:
	lz4hc_destroy(ctx);
	return NULL;
}

static int lz4hc_compress(void *ctx, const unsigned char *src,
			  unsigned char *dst, size_t *dst_len)
{
	struct lz4hc_ctx *zctx = ctx;
	int ret;

	if (!zctx->cstrm) {
		ret = LZ4_compress_HC(src, dst, PAGE_SIZE, *dst_len,
				      zctx->level, zctx->mem);
	} else {
		/* Cstrm needs to be reset */
		LZ4_resetStreamHC(zctx->cstrm, zctx->level);
		ret = LZ4_loadDictHC(zctx->cstrm, zctx->dict, zctx->dict_sz);
		if (ret != zctx->dict_sz)
			return -EINVAL;
		ret = LZ4_compress_HC_continue(zctx->cstrm, src, dst,
					       PAGE_SIZE, *dst_len);
	}
	if (!ret)
		return -EINVAL;
	*dst_len = ret;
	return 0;
}

static int lz4hc_decompress(void *ctx, const unsigned char *src,
			    size_t src_len, unsigned char *dst)
{
	struct lz4hc_ctx *zctx = ctx;
	int dst_len = PAGE_SIZE;
	int ret;

	if (!zctx->dstrm) {
		ret = LZ4_decompress_safe(src, dst, src_len, dst_len);
	} else {
		/* Dstrm needs to be reset */
		ret = LZ4_setStreamDecode(zctx->dstrm, zctx->dict,
					  zctx->dict_sz);
		if (!ret)
			return -EINVAL;
		ret = LZ4_decompress_safe_continue(zctx->dstrm, src, dst,
						   src_len, dst_len);
	}
	if (ret < 0)
		return -EINVAL;
	return 0;
}

struct zcomp_backend backend_lz4hc = {
	.compress	= lz4hc_compress,
	.decompress	= lz4hc_decompress,
	.create_ctx	= lz4hc_create,
	.destroy_ctx	= lz4hc_destroy,
	.init_config	= lz4hc_init_config,
	.release_config	= lz4hc_release_config,
	.name		= "lz4hc",
};
