/*
 * Demo on how to use /dev/crypto device for calculating a hash
 * at once, using init->hash, and compare it to using:
 * using init->update->final, and init->update->copy-> update -> final
 *                                        init->----\> update -> final
 *
 * Placed under public domain.
 *
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <crypto/cryptodev.h>
#include "sha-copy.h"

int sha_ctx_init(struct cryptodev_ctx* ctx, int cfd, const uint8_t *key, unsigned int key_size)
{
	struct session_info_op siop;

	memset(ctx, 0, sizeof(*ctx));
	ctx->cfd = cfd;

	if (key == NULL)
		ctx->sess.mac = CRYPTO_SHA1;
	else {
		ctx->sess.mac = CRYPTO_SHA1_HMAC;
		ctx->sess.mackeylen = key_size;
		ctx->sess.mackey = (void*)key;
	}
	if (ioctl(ctx->cfd, CIOCGSESSION, &ctx->sess)) {
		perror("ioctl(CIOCGSESSION)");
		return -1;
	}
#ifdef DEBUG
	fprintf(stderr, "sha_ctx_init:   cfd=%d, ses=%04x\n", ctx->cfd, ctx->sess.ses);
#endif

	siop.ses = ctx->sess.ses;
	if (ioctl(ctx->cfd, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return -1;
	}
	printf("Got %s with driver %s\n",
			siop.hash_info.cra_name, siop.hash_info.cra_driver_name);
	if (!(siop.flags & SIOP_FLAG_KERNEL_DRIVER_ONLY)) {
		printf("Note: This is not an accelerated cipher\n");
	}
	return 0;
}

static int sha_call_crypt(struct cryptodev_ctx* ctx, const void* text,
                   size_t size, void *digest, unsigned int flags)
{
	struct crypt_op cryp;

	memset(&cryp, 0, sizeof(cryp));

	/* Fill out the fields with text, size, digest result and flags */
	cryp.ses = ctx->sess.ses;
	cryp.len = size;
	cryp.src = (void*)text;
	cryp.mac = digest;
	cryp.flags = flags;
#ifdef DEBUG
	fprintf(stderr, "sha_call_crypt: cfd=%d, ses=%04x, CIOCCRYPT(len=%d, src='%s', flags=%04x)\n",
		ctx->cfd, ctx->sess.ses, cryp.len, (char *)cryp.src, cryp.flags);
#endif
	return ioctl(ctx->cfd, CIOCCRYPT, &cryp);
}

int sha_hash(struct cryptodev_ctx* ctx, const void* text, size_t size, void* digest)
{
#ifdef DEBUG
	fprintf(stderr, "sha_hash:       cfd=%d, ses=%04x, text='%s', size=%ld\n",
		ctx->cfd, ctx->sess.ses, (char *) text, size);
#endif
	if (sha_call_crypt(ctx, text, size, digest, 0)) {
		perror("sha_hash: ioctl(CIOCCRYPT)");
		return -1;
	}

	return 0;
}

int sha_update(struct cryptodev_ctx* ctx, const void* text, size_t size)
{
#ifdef DEBUG
	fprintf(stderr, "sha_update:     cfd=%d, ses=%04x, text='%s', size=%ld\n",
		ctx->cfd, ctx->sess.ses, (char *) text, size);
#endif
	if (sha_call_crypt(ctx, text, size, NULL, COP_FLAG_UPDATE)) {
		perror("sha_update: ioctl(CIOCCRYPT)");
		return -1;
	}

	return 0;
}

int sha_copy(struct cryptodev_ctx* to_ctx, const struct cryptodev_ctx* from_ctx)
{
	struct cphash_op cphash;

#ifdef DEBUG
	fprintf(stderr, "sha_copy: from= cfd=%d, ses=%04x\n"
			"            to= cfd=%d, ses=%04x\n",
		from_ctx->cfd, from_ctx->sess.ses, to_ctx->cfd, to_ctx->sess.ses);
#endif
	memset(&cphash, 0, sizeof(cphash));

	cphash.src_ses = from_ctx->sess.ses;
	cphash.dst_ses = to_ctx->sess.ses;
	if (ioctl(to_ctx->cfd, CIOCCPHASH, &cphash)) {
		perror("ioctl(CIOCCPHASH)");
		return -1;
	}

	return 0;
}

int sha_final(struct cryptodev_ctx* ctx, const void* text, size_t size, void* digest)
{
#ifdef DEBUG
	fprintf(stderr, "sha_final:      cfd=%d, ses=%04x, text='%s', size=%ld\n",
		ctx->cfd, ctx->sess.ses, (char *) text, size);
#endif
	if (sha_call_crypt(ctx, text, size, digest, COP_FLAG_FINAL)) {
		perror("sha_final: ioctl(CIOCCRYPT)");
		return -1;
	}

	return 0;
}

void sha_ctx_deinit(struct cryptodev_ctx* ctx)
{
#ifdef DEBUG
	fprintf(stderr, "sha_ctx_deinit: cfd=%d, ses=%04x\n", ctx->cfd, ctx->sess.ses);
#endif
	if (ioctl(ctx->cfd, CIOCFSESSION, &ctx->sess.ses)) {
		perror("ioctl(CIOCFSESSION)");
	}
}

static int print_digest(uint8_t *digest, uint8_t *expected)
{
	int i;

	if (memcmp(digest, expected, 20) != 0) {
		fprintf(stderr, "SHA1 hashing failed\n");
	}

	printf("digest: ");
	for (i = 0; i < 20; i++) {
		printf("%02x:", *digest);
		digest++;
	}
	printf("\n");
}

int
main()
{
	int cfd = -1;
	struct cryptodev_ctx ctx1, ctx2;
	uint8_t digest[20];
	char text[] = "The quick brown fox jumps over the lazy dog";
	char text1[] = "The quick brown fox";
	char text2[] = " jumps over the lazy dog";
	char text3[] = " jumps over the lazy dogs";
	uint8_t expected[] = "\x2f\xd4\xe1\xc6\x7a\x2d\x28\xfc\xed\x84\x9e\xe1\xbb\x76\xe7\x39\x1b\x93\xeb\x12";
	uint8_t expected2[] = "\xf8\xc3\xc5\x41\x25\x7a\x6c\x31\xf6\xfb\xc6\x97\xa5\x0f\x46\xd9\xfc\x8b\xcc\x30";

	/* Open the crypto device */
	cfd = open("/dev/crypto", O_RDWR, 0);
	if (cfd < 0) {
		perror("open(/dev/crypto)");
		return 1;
	}

	/* Set close-on-exec (not really neede here) */
	if (fcntl(cfd, F_SETFD, 1) == -1) {
		perror("fcntl(F_SETFD)");
		return 1;
	}

	printf("Computing digest in one operation\n");
	sha_ctx_init(&ctx1, cfd, NULL, 0);
	sha_hash(&ctx1, text, strlen(text), digest);
	sha_ctx_deinit(&ctx1);
	print_digest(digest, expected);

	printf("\n\nComputing digest using update/final\n");
	sha_ctx_init(&ctx1, cfd, NULL, 0);
	sha_update(&ctx1, text1, strlen(text1));
	sha_final(&ctx1, text2, strlen(text2), digest);
	sha_ctx_deinit(&ctx1);
	print_digest(digest, expected);

	printf("\n\nComputing digest using update/copy/final\n");
	sha_ctx_init(&ctx1, cfd, NULL, 0);
	sha_update(&ctx1, text1, strlen(text1));
	sha_ctx_init(&ctx2, cfd, NULL, 0);
	sha_copy(&ctx2, &ctx1);
	printf("\nOriginal operation:\n");
	sha_update(&ctx1, text2, strlen(text2));
	sha_final(&ctx1, NULL, 0, digest);
	print_digest(digest, expected);
	printf("\nCopied operation:\n");
	sha_final(&ctx2, text2, strlen(text2), digest);
	sha_ctx_deinit(&ctx1);
	sha_ctx_deinit(&ctx2);
	print_digest(digest, expected);

	printf("\n\nComputing digest using update/copy/final with different texts\n");
	sha_ctx_init(&ctx1, cfd, NULL, 0);
	sha_update(&ctx1, text1, strlen(text1));
	sha_ctx_init(&ctx2, cfd, NULL, 0);
	sha_copy(&ctx2, &ctx1);
	printf("\nOriginal operation, with original text:\n");
	sha_update(&ctx1, text2, strlen(text2));
	sha_final(&ctx1, NULL, 0, digest);
	print_digest(digest, expected);
	printf("\nCopied operation, with different text:\n");
	sha_update(&ctx2, text3, strlen(text3));
	sha_final(&ctx2, NULL, 0, digest);
	sha_ctx_deinit(&ctx1);
	sha_ctx_deinit(&ctx2);
	print_digest(digest, expected2);

	/* Close the original descriptor */
	if (close(cfd)) {
		perror("close(cfd)");
		return 1;
	}

	return 0;
}
