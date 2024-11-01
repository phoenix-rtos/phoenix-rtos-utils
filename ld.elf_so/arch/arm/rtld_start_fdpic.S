/*	$NetBSD: rtld_start.S,v 1.12 2013/12/03 00:19:56 matt Exp $	*/

/*-
 * Copyright (c) 1998, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas and by Charles M. Hannum.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

	.syntax unified

	.text

	.thumb_func
	.align 2
	.globl _rtld_start
	.type _rtld_start,%function
	.thumb
_rtld_start:
	/* TODO: bti c on armv8 */
	ldr	r6, .Lgot
	ldr	r5, .Ldynamic
	add	r6, r6, pc		/* r6 = &GOT[0]; pc = &LPIC1; */
	adds	r0, r6, r5		/* arg0 = &_DYNAMIC */
.LPIC1:
	movs	r1, r8			/* arg1 = loadmapInterp */
	bl	_rtld_relocate_nonplt_self

	movs	r3, r7			/* loadmapProg */
	movs	r2, r8			/* loadmap */
	movs	r1, 0			/* relocbase */
	mov	r0, sp			/* sp */
	bl	_rtld			/* call the shared loader */

	mov		pc, r0

	.align	0
.Lgot:
	.word	_GLOBAL_OFFSET_TABLE_ - .LPIC1
.Ldynamic:
	.word	_DYNAMIC(GOTOFF)
.size _rtld_start, .-_rtld_start


/*
 * ip        -- the GOT address (FDPIC value) for the resolver's GOT
 * SP[0]     -- funcdesc_value_reloc_offset(foo)
 * R9        -- the GOT address for the caller's GOT
 */
	.align 2
	.globl _rtld_bind_start
	.type _rtld_bind_start,%function
	.arm
_rtld_bind_start:
	stmdb	sp!,{r0-r3,sl,fp,lr}	/* 8 byte aligned (rel offset on stack) */

	ldr r1, [sp, #28]   /* funcdesc_value_reloc_offset */

	mov	r0, r9	        /* GOT of object needing resolution */
	mov r9, ip

	bl	_rtld_bind		/* Call the binder */

    ldr ip, [r0]        /* Get FDPIC register and function location from the descriptor */
    ldr r9, [r0, #4]

	ldmia	sp!,{r0-r3,sl,fp,lr}	/* restore the stack */
	add sp, sp, 4       /* remove funcdesc value from stack */
	mov		pc, ip			/* jump to the new address */
	.size _rtld_bind_start, .-_rtld_bind_start
