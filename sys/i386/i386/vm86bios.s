/*-
 * Copyright (c) 1998 Jonathan Lemon
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: vm86bios.s,v 1.13 1999/05/12 21:38:46 luoqi Exp $
 */

#include <machine/asmacros.h>		/* miscellaneous asm macros */
#include <machine/trap.h>

#include "assym.s"

#define SCR_NEWPTD	PCB_ESI		/* readability macros */ 
#define SCR_VMFRAME	PCB_EBP		/* see vm86.c for explanation */
#define SCR_STACK	PCB_ESP
#define SCR_PGTABLE	PCB_EBX
#define SCR_ARGFRAME	PCB_EIP
#define SCR_TSS0	PCB_SPARE
#define SCR_TSS1	(PCB_SPARE+4)

	.data
	ALIGN_DATA

	.globl	_in_vm86call, _vm86pcb

_in_vm86call:		.long	0
_vm86pcb:		.long	0

	.text

/*
 * vm86_bioscall(struct trapframe_vm86 *vm86)
 */
ENTRY(vm86_bioscall)
	movl	_vm86pcb,%edx		/* scratch data area */
	movl	4(%esp),%eax
	movl	%eax,SCR_ARGFRAME(%edx)	/* save argument pointer */
	pushl	%ebx
	pushl	%ebp
	pushl	%esi
	pushl	%edi
	pushl	%gs

#ifdef SMP	
	pushl	%edx
	ALIGN_LOCK			/* Get global lock */
	popl	%edx
#endif

#if NNPX > 0
	movl	_curproc,%ecx
	cmpl	%ecx,_npxproc		/* do we need to save fp? */
	jne	1f
	testl	%ecx,%ecx
	je 	1f			/* no curproc/npxproc */
	pushl	%edx
	movl	P_ADDR(%ecx),%ecx
	addl	$PCB_SAVEFPU,%ecx
	pushl	%ecx
	call	_npxsave
	popl	%ecx
	popl	%edx			/* recover our pcb */
#endif

1:
	movl	SCR_VMFRAME(%edx),%ebx	/* target frame location */
	movl	%ebx,%edi		/* destination */
	movl    SCR_ARGFRAME(%edx),%esi	/* source (set on entry) */
	movl	$VM86_FRAMESIZE/4,%ecx	/* sizeof(struct vm86frame)/4 */
	cld
	rep
	movsl				/* copy frame to new stack */

	movl	_curpcb,%eax
	pushl	%eax			/* save curpcb */
	movl	%edx,_curpcb		/* set curpcb to vm86pcb */

	movl	_tss_gdt,%ebx		/* entry in GDT */
	movl	0(%ebx),%eax
	movl	%eax,SCR_TSS0(%edx)	/* save first word */
	movl	4(%ebx),%eax
	andl    $~0x200, %eax		/* flip 386BSY -> 386TSS */
	movl	%eax,SCR_TSS1(%edx)	/* save second word */

	movl	PCB_EXT(%edx),%edi	/* vm86 tssd entry */
	movl	0(%edi),%eax
	movl	%eax,0(%ebx)
	movl	4(%edi),%eax
	movl	%eax,4(%ebx)
	movl	$GPROC0_SEL*8,%esi	/* GSEL(entry, SEL_KPL) */
	ltr	%si

	movl	%cr3,%eax
	pushl	%eax			/* save address space */
	movl	_IdlePTD,%ecx
	movl	%ecx,%ebx
	addl	$KERNBASE,%ebx		/* va of Idle PTD */
	movl	0(%ebx),%eax
	pushl	%eax			/* old ptde != 0 when booting */
	pushl	%ebx			/* keep for reuse */

	movl	%esp,SCR_STACK(%edx)	/* save current stack location */

	movl	SCR_NEWPTD(%edx),%eax	/* mapping for vm86 page table */
	movl	%eax,0(%ebx)		/* ... install as PTD entry 0 */

	movl	%ecx,%cr3		/* new page tables */
	movl	SCR_VMFRAME(%edx),%esp	/* switch to new stack */
	
	call	_vm86_prepcall		/* finish setup */

	movl	$1,_in_vm86call		/* set flag for trap() */

	/*
	 * Return via _doreti
	 */
#ifdef SMP
	ECPL_LOCK
#ifdef CPL_AND_CML
#error Not ready for CPL_AND_CML
#endif
	pushl	_cpl			/* cpl to restore */
	ECPL_UNLOCK
#else
	pushl	_cpl			/* cpl to restore */
#endif
	subl	$4,%esp			/* dummy unit */
	MPLOCKED incb _intr_nesting_level
	MEXITCOUNT
	jmp	_doreti


/*
 * vm86_biosret(struct trapframe_vm86 *vm86)
 */
ENTRY(vm86_biosret)
	movl	_vm86pcb,%edx		/* data area */

	movl	4(%esp),%esi		/* source */
	movl	SCR_ARGFRAME(%edx),%edi	/* destination */
	movl	$VM86_FRAMESIZE/4,%ecx	/* size */
	cld
	rep
	movsl				/* copy frame to original frame */

	movl	SCR_STACK(%edx),%esp	/* back to old stack */
	popl	%ebx			/* saved va of Idle PTD */
	popl	%eax
	movl	%eax,0(%ebx)		/* restore old pte */
	popl	%eax
	movl	%eax,%cr3		/* install old page table */

	movl	$0,_in_vm86call		/* reset trapflag */

	movl	_tss_gdt,%ebx		/* entry in GDT */
	movl	SCR_TSS0(%edx),%eax
	movl	%eax,0(%ebx)		/* restore first word */
	movl	SCR_TSS1(%edx),%eax
	movl	%eax,4(%ebx)		/* restore second word */
	movl	$GPROC0_SEL*8,%esi	/* GSEL(entry, SEL_KPL) */
	ltr	%si
	
	popl	_curpcb			/* restore curpcb/curproc */
	movl	SCR_ARGFRAME(%edx),%edx	/* original stack frame */
	movl	TF_TRAPNO(%edx),%eax	/* return (trapno) */

	popl	%gs
	popl	%edi
	popl	%esi
	popl	%ebp
	popl	%ebx
	ret				/* back to our normal program */
