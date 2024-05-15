/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 */

#ifndef _ZCOMP_H_
#define _ZCOMP_H_

#include <linux/local_lock.h>

struct zcomp_strm {
	/* The members ->buffer and ->tfm are protected by ->lock. */
	local_lock_t lock;
	/* compression/decompression buffer */
	void *buffer;
	void *ctx;
};

#define ZCOMP_CONFIG_NO_LEVEL	INT_MIN

struct zcomp_config {
	s32 level;
	size_t dict_sz;
	void *dict;
};

struct zcomp_backend {
	int (*compress)(void *ctx, const unsigned char *src,
			unsigned char *dst, size_t *dst_len);

	int (*decompress)(void *ctx, const unsigned char *src, size_t src_len,
			  unsigned char *dst);

	void *(*create_ctx)(struct zcomp_config *config);
	void (*destroy_ctx)(void *ctx);

	const char *name;
};

/* dynamic per-device compression frontend */
struct zcomp {
	struct zcomp_strm __percpu *stream;
	struct zcomp_backend *backend;
	struct zcomp_config *config;
	struct hlist_node node;
};

int zcomp_cpu_up_prepare(unsigned int cpu, struct hlist_node *node);
int zcomp_cpu_dead(unsigned int cpu, struct hlist_node *node);
ssize_t zcomp_available_show(const char *comp, char *buf);
bool zcomp_available_algorithm(const char *comp);

struct zcomp *zcomp_create(const char *alg, struct zcomp_config *config);
void zcomp_destroy(struct zcomp *comp);

struct zcomp_strm *zcomp_stream_get(struct zcomp *comp);
void zcomp_stream_put(struct zcomp *comp);

int zcomp_compress(struct zcomp *comp, struct zcomp_strm *zstrm,
		   const void *src, unsigned int *dst_len);
int zcomp_decompress(struct zcomp *comp, struct zcomp_strm *zstrm,
		     const void *src, unsigned int src_len, void *dst);

#endif /* _ZCOMP_H_ */
