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
 * - The main loop is scaled down to 48 bytes per iteration since the increase
 *   in load ops changes the balance; little cores barely notice the difference,
 *   so big cores can benefit from keeping the loop relatively short.
 * - Similarly, preferring source rather than destination alignment works out
 *   better on average.
 * - The 33-128 byte cases are reworked to better balance the stores with the
 *   doubled-up load ops, and keep a more consistent access pattern.
 * - The 0-3 byte sequence is replaced with the one borrowed from clear_user,
 *   since LDTRB lacks a register-offset addressing mode.
 */

#define U_pre(x...)	USER(L(fixup_pre), x)
#define U_dst(x...)	USER(L(fixup_dst), x)
#define U_S1(x...)	USER(L(fixup_s1), x)
#define U_M16(x...)	USER(L(fixup_m16), x)
#define U_M32(x...)	USER(L(fixup_m32), x)
#define U_M64(x...)	USER(L(fixup_m64), x)
#define U_L32(x...)	USER(L(fixup_l32), x)
#define U_L48(x...)	USER(L(fixup_l48), x)
#define U_L64(x...)	USER(L(fixup_l64), x)

SYM_FUNC_START(__arch_copy_from_user)
	add	srcend, src, count
	add	dstend, dstin, count
	cmp	count, 128
	b.hi	L(copy_long)
	cmp	count, 32
	b.hi	L(copy32_128)

	/* Small copies: 0..32 bytes.  */
	cmp	count, 16
	b.lo	L(copy16)
U_pre(	ldtr	A_l, [src])
U_pre(	ldtr	A_h, [src, 8])
U_pre(	ldtr	D_l, [srcend, -16])
U_pre(	ldtr	D_h, [srcend, -8])
	stp	A_l, A_h, [dstin]
	stp	D_l, D_h, [dstend, -16]
	mov	x0, #0
	ret

	/* Copy 8-15 bytes.  */
L(copy16):
	tbz	count, 3, L(copy8)
U_pre(	ldtr	A_l, [src])
U_pre(	ldtr	A_h, [srcend, -8])
	str	A_l, [dstin]
	str	A_h, [dstend, -8]
	mov	x0, #0
	ret

	.p2align 3
	/* Copy 4-7 bytes.  */
L(copy8):
	tbz	count, 2, L(copy4)
U_pre(	ldtr	A_lw, [src])
U_pre(	ldtr	B_lw, [srcend, -4])
	str	A_lw, [dstin]
	str	B_lw, [dstend, -4]
	mov	x0, #0
	ret

	/* Copy 0..3 bytes.  */
L(copy4):
	tbz	count, #1, L(copy1)
U_pre(	ldtrh	A_lw, [src])
	strh	A_lw, [dstin]
L(copy1):
	tbz	count, #0, L(copy0)
U_S1(	ldtrb	A_lw, [srcend, -1])
	strb	A_lw, [dstend, -1]
L(copy0):
	mov	x0, #0
	ret

	.p2align 4
	/* Medium copies: 33..128 bytes.  */
L(copy32_128):
U_pre(	ldtr	A_l, [src])
U_pre(	ldtr	A_h, [src, 8])
U_pre(	ldtr	B_l, [src, 16])
U_pre(	ldtr	B_h, [src, 24])
	stp	A_l, A_h, [dstin]
	stp	B_l, B_h, [dstin, 16]
U_M32(	ldtr	C_l, [srcend, -32])
U_M32(	ldtr	C_h, [srcend, -24])
U_M32(	ldtr	D_l, [srcend, -16])
U_M32(	ldtr	D_h, [srcend, -8])
	cmp	count, 64
	b.ls	L(copy64)
U_M32(	ldtr	E_l, [src, 32])
U_M32(	ldtr	E_h, [src, 40])
U_M32(	ldtr	F_l, [src, 48])
U_M32(	ldtr	F_h, [src, 56])
	stp	E_l, E_h, [dstin, 32]
	stp	F_l, F_h, [dstin, 48]
U_M64(	ldtr	A_l, [srcend, -64])
U_M64(	ldtr	A_h, [srcend, -56])
U_M64(	ldtr	B_l, [srcend, -48])
U_M64(	ldtr	B_h, [srcend, -40])
	stp	A_l, A_h, [dstend, -64]
	stp	B_l, B_h, [dstend, -48]
L(copy64):
	stp	C_l, C_h, [dstend, -32]
	stp	D_l, D_h, [dstend, -16]
	mov	x0, #0
	ret

	.p2align 4
	/* Copy more than 128 bytes.  */
L(copy_long):
	/* Copy 16 bytes and then align src to 16-byte alignment.  */

U_pre(	ldtr	D_l, [src])
U_pre(	ldtr	D_h, [src, 8])
	and	tmp1, src, 15
	bic	src, src, 15
	sub	dst, dstin, tmp1
	add	count, count, tmp1	/* Count is now 16 too large.  */
U_pre(	ldtr	A_l, [src, 16])
U_pre(	ldtr	A_h, [src, 24])
	stp	D_l, D_h, [dstin]
U_M16(	ldtr	B_l, [src, 32])
U_M16(	ldtr	B_h, [src, 40])
U_M16(	ldtr	C_l, [src, 48])
U_M16(	ldtr	C_h, [src, 56])
	add	src, src, #48
	subs	count, count, 96 + 16	/* Test and readjust count.  */
	b.ls	L(copy48_from_end)

L(loop48):
	stp	A_l, A_h, [dst, 16]
U_L32(	ldtr	A_l, [src, 16])
U_L32(	ldtr	A_h, [src, 24])
	stp	B_l, B_h, [dst, 32]
U_L48(	ldtr	B_l, [src, 32])
U_L48(	ldtr	B_h, [src, 40])
	stp	C_l, C_h, [dst, 48]!
U_dst(	ldtr	C_l, [src, 48])
U_dst(	ldtr	C_h, [src, 56])
	add	src, src, #48
	subs	count, count, 48
	b.hi	L(loop48)

	/* Write the last iteration and copy 48 bytes from the end.  */
L(copy48_from_end):
	stp	A_l, A_h, [dst, 16]
U_L32(	ldtr	A_l, [srcend, -48])
U_L32(	ldtr	A_h, [srcend, -40])
	stp	B_l, B_h, [dst, 32]
U_L48(	ldtr	B_l, [srcend, -32])
U_L48(	ldtr	B_h, [srcend, -24])
	stp	C_l, C_h, [dst, 48]
U_L64(	ldtr	C_l, [srcend, -16])
U_L64(	ldtr	C_h, [srcend, -8])
	stp	A_l, A_h, [dstend, -48]
	stp	B_l, B_h, [dstend, -32]
	stp	C_l, C_h, [dstend, -16]
	mov	x0, #0
	ret

	/* Fixups... */

	/*
	 * Fault before anything has been written, but progress may have
	 * been possible; realign dst and retry a single byte to confirm.
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

	/* Medium: Faults after n bytes beyond dstin have been written */
L(fixup_m64):
	add	dstin, dstin, #32
L(fixup_m32):
	add	dstin, dstin, #16
L(fixup_m16):
	add	dst, dstin, #16
	b	L(fixup_dst)

	/* Large: Faults after n bytes beyond dst have been written */
L(fixup_l64):
	add	dst, dst, #16
L(fixup_l48):
	add	dst, dst, #16
L(fixup_l32):
	add	dst, dst, #32
	b	L(fixup_dst)

SYM_FUNC_END(__arch_copy_from_user)
EXPORT_SYMBOL(__arch_copy_from_user)
