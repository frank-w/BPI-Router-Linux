/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2022, Arm Limited.
 */

#include <linux/linkage.h>

#include <asm/asm-uaccess.h>
#include <asm/assembler.h>

/* Assumptions:
 *
 * ARMv8-a, AArch64, unaligned accesses.
 *
 */

#define L(label) .L ## label

#define dstin	x0
#define src	x1
#define count	x2
#define dst	x3
#define srcend	x4
#define dstend	x5
#define A_l	x6
#define A_lw	w6
#define A_h	x7
#define B_l	x8
#define B_lw	w8
#define B_h	x9
#define C_l	x10
#define C_lw	w10
#define C_h	x11
#define D_l	x12
#define D_h	x13
#define E_l	x14
#define E_h	x15
#define F_l	x16
#define F_h	x17
#define tmp1	x14

/*
 * Derived from memcpy with various adjustments:
 *
 * - memmove parts are removed since user and kernel pointers won't overlap.
 * - Contrary to copy_from_user, although big cores still aren't particularly
 *   keen on the increased instruction count in the main loop, processing fewer
 *   than 64 bytes per iteration here hurts little cores more.
 * - The medium-size cases are reworked to better balance the loads with the
 *   doubled-up store ops, avoid potential out-of-sequence faults, and preserve
 *   the input arguments for the sake of fault handling.
 * - The 0-3 byte sequence is replaced with the one borrowed from clear_user,
 *   since STTRB lacks a register-offset addressing mode.
 */

#define U_pre(x...)	USER(L(fixup_pre), x)
#define U_dst(x...)	USER(L(fixup_dst), x)

#define U_S1(x...)	USER(L(fixup_s1), x)
#define U_S4(x...)	USER(L(fixup_s4), x)
#define U_S8(x...)	USER(L(fixup_s8), x)
#define U_ST8(x...)	USER(L(fixup_st8), x)
#define U_S16(x...)	USER(L(fixup_s16), x)
#define U_M24(x...)	USER(L(fixup_m24), x)
#define U_M32(x...)	USER(L(fixup_m32), x)
#define U_M40(x...)	USER(L(fixup_m40), x)
#define U_M48(x...)	USER(L(fixup_m48), x)
#define U_M56(x...)	USER(L(fixup_m56), x)
#define U_M64(x...)	USER(L(fixup_m64), x)
#define U_MT8(x...)	USER(L(fixup_mt8), x)
#define U_MT16(x...)	USER(L(fixup_mt16), x)
#define U_MT24(x...)	USER(L(fixup_mt24), x)
#define U_MT32(x...)	USER(L(fixup_mt32), x)
#define U_MT40(x...)	USER(L(fixup_mt40), x)
#define U_MT48(x...)	USER(L(fixup_mt48), x)
#define U_MT56(x...)	USER(L(fixup_mt56), x)

#define U_L16(x...)	USER(L(fixup_l16), x)
#define U_L24(x...)	USER(L(fixup_l24), x)
#define U_L32(x...)	USER(L(fixup_l32), x)
#define U_L40(x...)	USER(L(fixup_l40), x)
#define U_L48(x...)	USER(L(fixup_l48), x)
#define U_L56(x...)	USER(L(fixup_l56), x)
#define U_L64(x...)	USER(L(fixup_l64), x)
#define U_L72(x...)	USER(L(fixup_l72), x)
#define U_LT8(x...)	USER(L(fixup_lt8), x)
#define U_LT16(x...)	USER(L(fixup_lt16), x)
#define U_LT24(x...)	USER(L(fixup_lt24), x)
#define U_LT32(x...)	USER(L(fixup_lt32), x)
#define U_LT40(x...)	USER(L(fixup_lt40), x)
#define U_LT48(x...)	USER(L(fixup_lt48), x)
#define U_LT56(x...)	USER(L(fixup_lt56), x)
#define U_LT64(x...)	USER(L(fixup_lt64), x)

SYM_FUNC_START(__arch_copy_to_user)
	add	srcend, src, count
	add	dstend, dstin, count
	cmp	count, 128
	b.hi	L(copy_long)
	cmp	count, 32
	b.hi	L(copy32_128)

	/* Small copies: 0..32 bytes.  */
	cmp	count, 16
	b.lo	L(copy16)
	ldp	A_l, A_h, [src]
	ldp	D_l, D_h, [srcend, -16]
U_pre(	sttr	A_l, [dstin])
U_S8(	sttr	A_h, [dstin, 8])
U_S16(	sttr	D_l, [dstend, -16])
U_ST8(	sttr	D_h, [dstend, -8])
	mov	x0, #0
	ret

	/* Copy 8-15 bytes.  */
L(copy16):
	tbz	count, 3, L(copy8)
	ldr	A_l, [src]
	ldr	A_h, [srcend, -8]
U_pre(	sttr	A_l, [dstin])
U_S8(	sttr	A_h, [dstend, -8])
	mov	x0, #0
	ret

	.p2align 3
	/* Copy 4-7 bytes.  */
L(copy8):
	tbz	count, 2, L(copy4)
	ldr	A_lw, [src]
	ldr	B_lw, [srcend, -4]
U_pre(	sttr	A_lw, [dstin])
U_S4(	sttr	B_lw, [dstend, -4])
	mov	x0, #0
	ret

	/* Copy 0..3 bytes.  */
L(copy4):
	tbz	count, #1, L(copy1)
	ldrh	A_lw, [src]
U_pre(	sttrh	A_lw, [dstin])
L(copy1):
	tbz	count, #0, L(copy0)
	ldrb	A_lw, [srcend, -1]
U_S1(	sttrb	A_lw, [dstend, -1])
L(copy0):
	mov	x0, #0
	ret

	.p2align 4
	/* Medium copies: 33..128 bytes.  */
L(copy32_128):
	ldp	A_l, A_h, [src]
	ldp	B_l, B_h, [src, 16]
U_pre(	sttr	A_l, [dstin])
U_S8(	sttr	A_h, [dstin, 8])
U_S16(	sttr	B_l, [dstin, 16])
U_M24(	sttr	B_h, [dstin, 24])
	ldp	C_l, C_h, [srcend, -32]
	ldp	D_l, D_h, [srcend, -16]
	cmp	count, 64
	b.ls	L(copy64)
	ldp	E_l, E_h, [src, 32]
	ldp	F_l, F_h, [src, 48]
U_M32(	sttr	E_l, [dstin, 32])
U_M40(	sttr	E_h, [dstin, 40])
U_M48(	sttr	F_l, [dstin, 48])
U_M56(	sttr	F_h, [dstin, 56])
	ldp	A_l, A_h, [srcend, -64]
	ldp	B_l, B_h, [srcend, -48]
U_M64(	sttr	A_l, [dstend, -64])
U_MT56(	sttr	A_h, [dstend, -56])
U_MT48(	sttr	B_l, [dstend, -48])
U_MT40(	sttr	B_h, [dstend, -40])
L(copy64):
U_MT32(	sttr	C_l, [dstend, -32])
U_MT24(	sttr	C_h, [dstend, -24])
U_MT16(	sttr	D_l, [dstend, -16])
U_MT8(	sttr	D_h, [dstend, -8])
	mov	x0, #0
	ret

	.p2align 4
	nop
	nop
	nop
	/* Copy more than 128 bytes.  */
L(copy_long):
	/* Copy 16 bytes and then align dst to 16-byte alignment.  */

	ldp	D_l, D_h, [src]
	and	tmp1, dstin, 15
	bic	dst, dstin, 15
	sub	src, src, tmp1
	add	count, count, tmp1	/* Count is now 16 too large.  */
	ldp	A_l, A_h, [src, 16]
U_pre(	sttr	D_l, [dstin])
U_S8(	sttr	D_h, [dstin, 8])
	ldp	B_l, B_h, [src, 32]
	ldp	C_l, C_h, [src, 48]
	ldp	D_l, D_h, [src, 64]!
	subs	count, count, 128 + 16	/* Test and readjust count.  */
	b.ls	L(copy64_from_end)

L(loop64):
U_L16(	sttr	A_l, [dst, 16])
U_L24(	sttr	A_h, [dst, 24])
	ldp	A_l, A_h, [src, 16]
U_L32(	sttr	B_l, [dst, 32])
U_L40(	sttr	B_h, [dst, 40])
	ldp	B_l, B_h, [src, 32]
U_L48(	sttr	C_l, [dst, 48])
U_L56(	sttr	C_h, [dst, 56])
	ldp	C_l, C_h, [src, 48]
U_L64(	sttr	D_l, [dst, 64])
U_L72(	sttr	D_h, [dst, 72])
	add	dst, dst, #64
	ldp	D_l, D_h, [src, 64]!
	subs	count, count, 64
	b.hi	L(loop64)

	/* Write the last iteration and copy 64 bytes from the end.  */
L(copy64_from_end):
	ldp	E_l, E_h, [srcend, -64]
U_L16(	sttr	A_l, [dst, 16])
U_L24(	sttr	A_h, [dst, 24])
	ldp	A_l, A_h, [srcend, -48]
U_L32(	sttr	B_l, [dst, 32])
U_L40(	sttr	B_h, [dst, 40])
	ldp	B_l, B_h, [srcend, -32]
U_L48(	sttr	C_l, [dst, 48])
U_L56(	sttr	C_h, [dst, 56])
	ldp	C_l, C_h, [srcend, -16]
U_L64(	sttr	D_l, [dst, 64])
U_L72(	sttr	D_h, [dst, 72])
U_LT64(	sttr	E_l, [dstend, -64])
U_LT56(	sttr	E_h, [dstend, -56])
U_LT48(	sttr	A_l, [dstend, -48])
U_LT40(	sttr	A_h, [dstend, -40])
U_LT32(	sttr	B_l, [dstend, -32])
U_LT24(	sttr	B_h, [dstend, -24])
U_LT16(	sttr	C_l, [dstend, -16])
U_LT8(	sttr	C_h, [dstend, -8])
	mov	x0, #0
	ret

	/* Fixups... */

	/*
	 * Fault on the first write, but progress may have been possible;
	 * realign dst and retry a single byte to confirm.
	 */
L(fixup_pre):
	mov	dst, dstin
U_dst(	ldtrb	A_lw, [src])
	strb	A_lw, [dst], #1
L(fixup_dst):
	sub	x0, dstend, dst
	ret

	/* Small: Fault with 1 byte remaining, regardless of count */
L(fixup_s1):
	mov	x0, #1
	ret

	/* Small tail case: Fault 8 bytes before dstend, >=16 bytes written */
L(fixup_st8):
	sub	dst, dstend, #8
	add	dstin, dstin, #16
L(fixup_tail):
	cmp	dst, dstin
	csel	dst, dst, dstin, hi
	b	L(fixup_dst)

	/* Small/medium: Faults n bytes past dtsin, that much written */
L(fixup_m64):
	add	dstin, dstin, #8
L(fixup_m56):
	add	dstin, dstin, #8
L(fixup_m48):
	add	dstin, dstin, #8
L(fixup_m40):
	add	dstin, dstin, #8
L(fixup_m32):
	add	dstin, dstin, #8
L(fixup_m24):
	add	dstin, dstin, #8
L(fixup_s16):
	add	dstin, dstin, #8
L(fixup_s8):
	add	dstin, dstin, #4
L(fixup_s4):
	add	dst, dstin, #4
	b	L(fixup_dst)

	/*
	 * Medium tail cases: Faults n bytes before dstend, 64 or 32 bytes
	 * past dstin written, depending on original count
	 */
L(fixup_mt56):
	sub	count, count, #8
L(fixup_mt48):
	sub	count, count, #8
L(fixup_mt40):
	sub	count, count, #8
L(fixup_mt32):
	sub	count, count, #8
L(fixup_mt24):
	sub	count, count, #8
L(fixup_mt16):
	sub	count, count, #8
L(fixup_mt8):
	add	count, count, #8
	add	dst, dstin, count

	sub	tmp1, dstend, dstin
	cmp	tmp1, #64
	add	tmp1, dstin, #64
	add	dstin, dstin, #32
	csel	dstin, dstin, tmp1, ls
	b	L(fixup_tail)

	/* Large: Faults n bytes past dst, at least 16 bytes past dstin written */
L(fixup_l72):
	add	dst, dst, #8
L(fixup_l64):
	add	dst, dst, #8
L(fixup_l56):
	add	dst, dst, #8
L(fixup_l48):
	add	dst, dst, #8
L(fixup_l40):
	add	dst, dst, #8
L(fixup_l32):
	add	dst, dst, #8
L(fixup_l24):
	add	dst, dst, #8
L(fixup_l16):
	add	dst, dst, #16
	add	dstin, dstin, #16
	b	L(fixup_tail)

	/* Large tail: Faults n bytes before dstend, 80 bytes past dst written */
L(fixup_lt64):
	sub	count, count, #8
L(fixup_lt56):
	sub	count, count, #8
L(fixup_lt48):
	sub	count, count, #8
L(fixup_lt40):
	sub	count, count, #8
L(fixup_lt32):
	sub	count, count, #8
L(fixup_lt24):
	sub	count, count, #8
L(fixup_lt16):
	sub	count, count, #8
L(fixup_lt8):
	add	count, count, #56	/* Count was also off by 64 */
	add	dstin, dst, #80
	add	dst, dst, count
	b	L(fixup_tail)

SYM_FUNC_END(__arch_copy_to_user)
EXPORT_SYMBOL(__arch_copy_to_user)
