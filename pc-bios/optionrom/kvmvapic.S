#
# Local APIC acceleration for Windows XP and related guests
#
# Copyright 2011 Red Hat, Inc. and/or its affiliates
#
# Author: Avi Kivity <avi@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2, or (at your
# option) any later version.  See the COPYING file in the top-level directory.
#

#include "optionrom.h"

OPTION_ROM_START

	# clear vapic area: firmware load using rep insb may cause
	# stale tpr/isr/irr data to corrupt the vapic area.
	push %es
	push %cs
	pop %es
	xor %ax, %ax
	mov $vapic_size/2, %cx
	lea vapic, %di
	cld
	rep stosw
	pop %es

	# announce presence to the hypervisor
	mov $vapic_base, %ax
	out %ax, $0x7e

	lret

	.code32
vapic_size = 2*4096

.macro fixup delta=-4
777:
	.text 1
	.long 777b + \delta  - vapic_base
	.text 0
.endm

.macro reenable_vtpr
	out %al, $0x7e
.endm

.text 1
	fixup_start = .
.text 0

.align 16

vapic_base:
	.ascii "kvm aPiC"

	/* relocation data */
	.long vapic_base	; fixup
	.long fixup_start	; fixup
	.long fixup_end		; fixup

	.long vapic		; fixup
	.long vapic_size
vcpu_shift:
	.long 0
real_tpr:
	.long 0
	.long up_set_tpr	; fixup
	.long up_set_tpr_eax	; fixup
	.long up_get_tpr_eax	; fixup
	.long up_get_tpr_ecx	; fixup
	.long up_get_tpr_edx	; fixup
	.long up_get_tpr_ebx	; fixup
	.long 0 /* esp. won't work. */
	.long up_get_tpr_ebp	; fixup
	.long up_get_tpr_esi	; fixup
	.long up_get_tpr_edi	; fixup
	.long up_get_tpr_stack  ; fixup
	.long mp_set_tpr	; fixup
	.long mp_set_tpr_eax	; fixup
	.long mp_get_tpr_eax	; fixup
	.long mp_get_tpr_ecx	; fixup
	.long mp_get_tpr_edx	; fixup
	.long mp_get_tpr_ebx	; fixup
	.long 0 /* esp. won't work. */
	.long mp_get_tpr_ebp	; fixup
	.long mp_get_tpr_esi	; fixup
	.long mp_get_tpr_edi	; fixup
	.long mp_get_tpr_stack  ; fixup

.macro kvm_hypercall
	.byte 0x0f, 0x01, 0xc1
.endm

kvm_hypercall_vapic_poll_irq = 1

pcr_cpu = 0x51

.align 64

mp_get_tpr_eax:
	pushf
	cli
	reenable_vtpr
	push %ecx

	fs/movzbl pcr_cpu, %eax

	mov vcpu_shift, %ecx	; fixup
	shl %cl, %eax
	testb $1, vapic+4(%eax)	; fixup delta=-5
	jz mp_get_tpr_bad
	movzbl vapic(%eax), %eax ; fixup

mp_get_tpr_out:
	pop %ecx
	popf
	ret

mp_get_tpr_bad:
	mov real_tpr, %eax	; fixup
	mov (%eax), %eax
	jmp mp_get_tpr_out

mp_get_tpr_ebx:
	mov %eax, %ebx
	call mp_get_tpr_eax
	xchg %eax, %ebx
	ret

mp_get_tpr_ecx:
	mov %eax, %ecx
	call mp_get_tpr_eax
	xchg %eax, %ecx
	ret

mp_get_tpr_edx:
	mov %eax, %edx
	call mp_get_tpr_eax
	xchg %eax, %edx
	ret

mp_get_tpr_esi:
	mov %eax, %esi
	call mp_get_tpr_eax
	xchg %eax, %esi
	ret

mp_get_tpr_edi:
	mov %eax, %edi
	call mp_get_tpr_edi
	xchg %eax, %edi
	ret

mp_get_tpr_ebp:
	mov %eax, %ebp
	call mp_get_tpr_eax
	xchg %eax, %ebp
	ret

mp_get_tpr_stack:
	call mp_get_tpr_eax
	xchg %eax, 4(%esp)
	ret

mp_set_tpr_eax:
	push %eax
	call mp_set_tpr
	ret

mp_set_tpr:
	pushf
	push %eax
	push %ecx
	push %edx
	push %ebx
	cli
	reenable_vtpr

mp_set_tpr_failed:
	fs/movzbl pcr_cpu, %edx

	mov vcpu_shift, %ecx	; fixup
	shl %cl, %edx

	testb $1, vapic+4(%edx)	; fixup delta=-5
	jz mp_set_tpr_bad

	mov vapic(%edx), %eax	; fixup

	mov %eax, %ebx
	mov 24(%esp), %bl

	/* %ebx = new vapic (%bl = tpr, %bh = isr, %b3 = irr) */

	lock cmpxchg %ebx, vapic(%edx) ; fixup
	jnz mp_set_tpr_failed

	/* compute ppr */
	cmp %bh, %bl
	jae mp_tpr_is_bigger
mp_isr_is_bigger:
	mov %bh, %bl
mp_tpr_is_bigger:
	/* %bl = ppr */
	rol $8, %ebx
	/* now: %bl = irr, %bh = ppr */
	cmp %bh, %bl
	ja mp_set_tpr_poll_irq

mp_set_tpr_out:
	pop %ebx
	pop %edx
	pop %ecx
	pop %eax
	popf
	ret $4

mp_set_tpr_poll_irq:
	mov $kvm_hypercall_vapic_poll_irq, %eax
	kvm_hypercall
	jmp mp_set_tpr_out

mp_set_tpr_bad:
	mov 24(%esp), %ecx
	mov real_tpr, %eax	; fixup
	mov %ecx, (%eax)
	jmp mp_set_tpr_out

up_get_tpr_eax:
	reenable_vtpr
	movzbl vapic, %eax ; fixup
	ret

up_get_tpr_ebx:
	reenable_vtpr
	movzbl vapic, %ebx ; fixup
	ret

up_get_tpr_ecx:
	reenable_vtpr
	movzbl vapic, %ecx ; fixup
	ret

up_get_tpr_edx:
	reenable_vtpr
	movzbl vapic, %edx ; fixup
	ret

up_get_tpr_esi:
	reenable_vtpr
	movzbl vapic, %esi ; fixup
	ret

up_get_tpr_edi:
	reenable_vtpr
	movzbl vapic, %edi ; fixup
	ret

up_get_tpr_ebp:
	reenable_vtpr
	movzbl vapic, %ebp ; fixup
	ret

up_get_tpr_stack:
	reenable_vtpr
	movzbl vapic, %eax ; fixup
	xchg %eax, 4(%esp)
	ret

up_set_tpr_eax:
	push %eax
	call up_set_tpr
	ret

up_set_tpr:
	pushf
	push %eax
	push %ebx
	reenable_vtpr

up_set_tpr_failed:
	mov vapic, %eax	; fixup

	mov %eax, %ebx
	mov 16(%esp), %bl

	/* %ebx = new vapic (%bl = tpr, %bh = isr, %b3 = irr) */

	lock cmpxchg %ebx, vapic ; fixup
	jnz up_set_tpr_failed

	/* compute ppr */
	cmp %bh, %bl
	jae up_tpr_is_bigger
up_isr_is_bigger:
	mov %bh, %bl
up_tpr_is_bigger:
	/* %bl = ppr */
	rol $8, %ebx
	/* now: %bl = irr, %bh = ppr */
	cmp %bh, %bl
	ja up_set_tpr_poll_irq

up_set_tpr_out:
	pop %ebx
	pop %eax
	popf
	ret $4

up_set_tpr_poll_irq:
	mov $kvm_hypercall_vapic_poll_irq, %eax
	kvm_hypercall
	jmp up_set_tpr_out

.text 1
	fixup_end = .
.text 0

/*
 * vapic format:
 *  per-vcpu records of size 2^vcpu shift.
 *     byte 0: tpr (r/w)
 *     byte 1: highest in-service interrupt (isr) (r/o); bits 3:0 are zero
 *     byte 2: zero (r/o)
 *     byte 3: highest pending interrupt (irr) (r/o)
 */
.text 2

.align 128

vapic:
. = . + vapic_size

OPTION_ROM_END
