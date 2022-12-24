#ifndef SHA_COPY_H
#define SHA_COPY_H

#include <stdint.h>

struct cryptodev_ctx {
	int cfd;
	struct session_op sess;
};

int sha_ctx_init(struct cryptodev_ctx* ctx, int cfd, const uint8_t *key, unsigned int key_size);
int sha_hash(struct cryptodev_ctx* ctx, const void* text, size_t size, void* digest);
int sha_update(struct cryptodev_ctx* ctx, const void* text, size_t size);
int sha_copy(struct cryptodev_ctx* to_ctx, const struct cryptodev_ctx* from_ctx);
int sha_final(struct cryptodev_ctx* ctx, const void* text, size_t size, void* digest);
void sha_ctx_deinit(struct cryptodev_ctx* ctx);

#endif /* SHA_COPY_H */
