/* 
 * Copyright (c) 2015-2021, Extrems' Corner.org
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <ogc/machine/asm.h>

	.section	.text.dsi_exceptionhandler,"ax",@progbits
	.globl	dsi_exceptionhandler
dsi_exceptionhandler:
	stw		%r11,GPR1_OFFSET(%sp)
	stmw	%r13,GPR13_OFFSET(%sp)
	psq_st	%f0,PSR0_OFFSET(%sp),0,0
	stfd	%f0,FPR0_OFFSET(%sp)
	psq_st	%f1,PSR1_OFFSET(%sp),0,0
	stfd	%f1,FPR1_OFFSET(%sp)
	psq_st	%f2,PSR2_OFFSET(%sp),0,0
	stfd	%f2,FPR2_OFFSET(%sp)
	psq_st	%f3,PSR3_OFFSET(%sp),0,0
	stfd	%f3,FPR3_OFFSET(%sp)
	psq_st	%f4,PSR4_OFFSET(%sp),0,0
	stfd	%f4,FPR4_OFFSET(%sp)
	psq_st	%f5,PSR5_OFFSET(%sp),0,0
	stfd	%f5,FPR5_OFFSET(%sp)
	psq_st	%f6,PSR6_OFFSET(%sp),0,0
	stfd	%f6,FPR6_OFFSET(%sp)
	psq_st	%f7,PSR7_OFFSET(%sp),0,0
	stfd	%f7,FPR7_OFFSET(%sp)
	psq_st	%f8,PSR8_OFFSET(%sp),0,0
	stfd	%f8,FPR8_OFFSET(%sp)
	psq_st	%f9,PSR9_OFFSET(%sp),0,0
	stfd	%f9,FPR9_OFFSET(%sp)
	psq_st	%f10,PSR10_OFFSET(%sp),0,0
	stfd	%f10,FPR10_OFFSET(%sp)
	psq_st	%f11,PSR11_OFFSET(%sp),0,0
	stfd	%f11,FPR11_OFFSET(%sp)
	psq_st	%f12,PSR12_OFFSET(%sp),0,0
	stfd	%f12,FPR12_OFFSET(%sp)
	psq_st	%f13,PSR13_OFFSET(%sp),0,0
	stfd	%f13,FPR13_OFFSET(%sp)
	mffs	%f0
	stfd	%f0,FPSCR_OFFSET(%sp)

	mfdsisr	%r3
	mfdar	%r4
	bl		vm_dsi_handler
	cmpwi	%r3,0
	bne		1f

	mr		%r3,%sp
	b		c_default_exceptionhandler

1:	lfd		%f0,FPSCR_OFFSET(%sp)
	mtfsf	0xFF,%f0
	psq_l	%f13,PSR13_OFFSET(%sp),0,0
	lfd		%f13,FPR13_OFFSET(%sp)
	psq_l	%f12,PSR12_OFFSET(%sp),0,0
	lfd		%f12,FPR12_OFFSET(%sp)
	psq_l	%f11,PSR11_OFFSET(%sp),0,0
	lfd		%f11,FPR11_OFFSET(%sp)
	psq_l	%f10,PSR10_OFFSET(%sp),0,0
	lfd		%f10,FPR10_OFFSET(%sp)
	psq_l	%f9,PSR9_OFFSET(%sp),0,0
	lfd		%f9,FPR9_OFFSET(%sp)
	psq_l	%f8,PSR8_OFFSET(%sp),0,0
	lfd		%f8,FPR8_OFFSET(%sp)
	psq_l	%f7,PSR7_OFFSET(%sp),0,0
	lfd		%f7,FPR7_OFFSET(%sp)
	psq_l	%f6,PSR6_OFFSET(%sp),0,0
	lfd		%f6,FPR6_OFFSET(%sp)
	psq_l	%f5,PSR5_OFFSET(%sp),0,0
	lfd		%f5,FPR5_OFFSET(%sp)
	psq_l	%f4,PSR4_OFFSET(%sp),0,0
	lfd		%f4,FPR4_OFFSET(%sp)
	psq_l	%f3,PSR3_OFFSET(%sp),0,0
	lfd		%f3,FPR3_OFFSET(%sp)
	psq_l	%f2,PSR2_OFFSET(%sp),0,0
	lfd		%f2,FPR2_OFFSET(%sp)
	psq_l	%f1,PSR1_OFFSET(%sp),0,0
	lfd		%f1,FPR1_OFFSET(%sp)
	psq_l	%f0,PSR0_OFFSET(%sp),0,0
	lfd		%f0,FPR0_OFFSET(%sp)
	lwz		%r0,CR_OFFSET(%sp)
	mtcr	%r0
	lwz		%r0,LR_OFFSET(%sp)
	mtlr	%r0
	lwz		%r0,CTR_OFFSET(%sp)
	mtctr	%r0
	lwz		%r0,XER_OFFSET(%sp)
	mtxer	%r0
	lwz		%r0,SRR0_OFFSET(%sp)
	mtsrr0	%r0
	lwz		%r0,SRR1_OFFSET(%sp)
	mtsrr1	%r0
	lwz		%r12,GPR12_OFFSET(%sp)
	lwz		%r11,GPR11_OFFSET(%sp)
	lwz		%r10,GPR10_OFFSET(%sp)
	lwz		%r9,GPR9_OFFSET(%sp)
	lwz		%r8,GPR8_OFFSET(%sp)
	lwz		%r7,GPR7_OFFSET(%sp)
	lwz		%r6,GPR6_OFFSET(%sp)
	lwz		%r5,GPR5_OFFSET(%sp)
	lwz		%r4,GPR4_OFFSET(%sp)
	lwz		%r3,GPR3_OFFSET(%sp)
	lwz		%r2,GPR2_OFFSET(%sp)
	lwz		%r0,GPR0_OFFSET(%sp)
	addi	%sp,%sp,EXCEPTION_FRAME_END
	rfi
