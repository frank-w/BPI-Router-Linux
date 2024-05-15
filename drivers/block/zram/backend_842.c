// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sw842.h>
#include <linux/vmalloc.h>

#include "backend_842.h"

struct sw842_ctx {
	void *mem;
};

static int init_config_842(struct zcomp_config *config)
{
	return 0;
}

static void release_config_842(struct zcomp_config *config)
{
}

static void destroy_842(void *ctx)
{
	struct sw842_ctx *zctx = ctx;

	kfree(zctx->mem);
	kfree(zctx);
}

static void *create_842(struct zcomp_config *config)
{
	struct sw842_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->mem = kmalloc(SW842_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx->mem)
		goto error;

	return ctx;

error:
	destroy_842(ctx);
	return NULL;
}

static int compress_842(void *ctx, const unsigned char *src,
			unsigned char *dst, size_t *dst_len)
{
	struct sw842_ctx *zctx = ctx;
	unsigned int dlen = *dst_len;
	int ret;

	ret = sw842_compress(src, PAGE_SIZE, dst, &dlen, zctx->mem);
	if (ret == 0)
		*dst_len = dlen;
	return ret;
}

static int decompress_842(void *ctx, const unsigned char *src, size_t src_len,
			  unsigned char *dst)
{
	unsigned int dlen = PAGE_SIZE;

	return sw842_decompress(src, src_len, dst, &dlen);
}

struct zcomp_backend backend_842 = {
	.compress	= compress_842,
	.decompress	= decompress_842,
	.create_ctx	= create_842,
	.destroy_ctx	= destroy_842,
	.init_config	= init_config_842,
	.release_config	= release_config_842,
	.name		= "842",
};
