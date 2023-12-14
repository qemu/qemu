/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * xen-x86_64.h
 *
 * Guest OS interface to x86 64-bit Xen.
 *
 * Copyright (c) 2004-2006, K A Fraser
 */

#ifndef __XEN_PUBLIC_ARCH_X86_XEN_X86_64_H__
#define __XEN_PUBLIC_ARCH_X86_XEN_X86_64_H__

/*
 * Hypercall interface:
 *  Input:  %rdi, %rsi, %rdx, %r10, %r8, %r9 (arguments 1-6)
 *  Output: %rax
 * Access is via hypercall page (set up by guest loader or via a Xen MSR):
 *  call hypercall_page + hypercall-number * 32
 * Clobbered: argument registers (e.g., 2-arg hypercall clobbers %rdi,%rsi)
 */

/*
 * 64-bit segment selectors
 * These flat segments are in the Xen-private section of every GDT. Since these
 * are also present in the initial GDT, many OSes will be able to avoid
 * installing their own GDT.
 */

#define FLAT_RING3_CS32 0xe023  /* GDT index 260 */
#define FLAT_RING3_CS64 0xe033  /* GDT index 262 */
#define FLAT_RING3_DS32 0xe02b  /* GDT index 261 */
#define FLAT_RING3_DS64 0x0000  /* NULL selector */
#define FLAT_RING3_SS32 0xe02b  /* GDT index 261 */
#define FLAT_RING3_SS64 0xe02b  /* GDT index 261 */

#define FLAT_KERNEL_DS64 FLAT_RING3_DS64
#define FLAT_KERNEL_DS32 FLAT_RING3_DS32
#define FLAT_KERNEL_DS   FLAT_KERNEL_DS64
#define FLAT_KERNEL_CS64 FLAT_RING3_CS64
#define FLAT_KERNEL_CS32 FLAT_RING3_CS32
#define FLAT_KERNEL_CS   FLAT_KERNEL_CS64
#define FLAT_KERNEL_SS64 FLAT_RING3_SS64
#define FLAT_KERNEL_SS32 FLAT_RING3_SS32
#define FLAT_KERNEL_SS   FLAT_KERNEL_SS64

#define FLAT_USER_DS64 FLAT_RING3_DS64
#define FLAT_USER_DS32 FLAT_RING3_DS32
#define FLAT_USER_DS   FLAT_USER_DS64
#define FLAT_USER_CS64 FLAT_RING3_CS64
#define FLAT_USER_CS32 FLAT_RING3_CS32
#define FLAT_USER_CS   FLAT_USER_CS64
#define FLAT_USER_SS64 FLAT_RING3_SS64
#define FLAT_USER_SS32 FLAT_RING3_SS32
#define FLAT_USER_SS   FLAT_USER_SS64

#define __HYPERVISOR_VIRT_START 0xFFFF800000000000
#define __HYPERVISOR_VIRT_END   0xFFFF880000000000
#define __MACH2PHYS_VIRT_START  0xFFFF800000000000
#define __MACH2PHYS_VIRT_END    0xFFFF804000000000

#ifndef HYPERVISOR_VIRT_START
#define HYPERVISOR_VIRT_START xen_mk_ulong(__HYPERVISOR_VIRT_START)
#define HYPERVISOR_VIRT_END   xen_mk_ulong(__HYPERVISOR_VIRT_END)
#endif

#define MACH2PHYS_VIRT_START  xen_mk_ulong(__MACH2PHYS_VIRT_START)
#define MACH2PHYS_VIRT_END    xen_mk_ulong(__MACH2PHYS_VIRT_END)
#define MACH2PHYS_NR_ENTRIES  ((MACH2PHYS_VIRT_END-MACH2PHYS_VIRT_START)>>3)
#ifndef machine_to_phys_mapping
#define machine_to_phys_mapping ((unsigned long *)HYPERVISOR_VIRT_START)
#endif

/*
 * int HYPERVISOR_set_segment_base(unsigned int which, unsigned long base)
 *  @which == SEGBASE_*  ;  @base == 64-bit base address
 * Returns 0 on success.
 */
#define SEGBASE_FS          0
#define SEGBASE_GS_USER     1
#define SEGBASE_GS_KERNEL   2
#define SEGBASE_GS_USER_SEL 3 /* Set user %gs specified in base[15:0] */

/*
 * int HYPERVISOR_iret(void)
 * All arguments are on the kernel stack, in the following format.
 * Never returns if successful. Current kernel context is lost.
 * The saved CS is mapped as follows:
 *   RING0 -> RING3 kernel mode.
 *   RING1 -> RING3 kernel mode.
 *   RING2 -> RING3 kernel mode.
 *   RING3 -> RING3 user mode.
 * However RING0 indicates that the guest kernel should return to iteself
 * directly with
 *      orb   $3,1*8(%rsp)
 *      iretq
 * If flags contains VGCF_in_syscall:
 *   Restore RAX, RIP, RFLAGS, RSP.
 *   Discard R11, RCX, CS, SS.
 * Otherwise:
 *   Restore RAX, R11, RCX, CS:RIP, RFLAGS, SS:RSP.
 * All other registers are saved on hypercall entry and restored to user.
 */
/* Guest exited in SYSCALL context? Return to guest with SYSRET? */
#define _VGCF_in_syscall 8
#define VGCF_in_syscall  (1<<_VGCF_in_syscall)
#define VGCF_IN_SYSCALL  VGCF_in_syscall

#ifndef __ASSEMBLY__

struct iret_context {
    /* Top of stack (%rsp at point of hypercall). */
    uint64_t rax, r11, rcx, flags, rip, cs, rflags, rsp, ss;
    /* Bottom of iret stack frame. */
};

#if defined(__XEN__) || defined(__XEN_TOOLS__)
/* Anonymous unions include all permissible names (e.g., al/ah/ax/eax/rax). */
#define __DECL_REG_LOHI(which) union { \
    uint64_t r ## which ## x; \
    uint32_t e ## which ## x; \
    uint16_t which ## x; \
    struct { \
        uint8_t which ## l; \
        uint8_t which ## h; \
    }; \
}
#define __DECL_REG_LO8(name) union { \
    uint64_t r ## name; \
    uint32_t e ## name; \
    uint16_t name; \
    uint8_t name ## l; \
}
#define __DECL_REG_LO16(name) union { \
    uint64_t r ## name; \
    uint32_t e ## name; \
    uint16_t name; \
}
#define __DECL_REG_HI(num) union { \
    uint64_t r ## num; \
    uint32_t r ## num ## d; \
    uint16_t r ## num ## w; \
    uint8_t r ## num ## b; \
}
#elif defined(__GNUC__) && !defined(__STRICT_ANSI__)
/* Anonymous union includes both 32- and 64-bit names (e.g., eax/rax). */
#define __DECL_REG(name) union { \
    uint64_t r ## name, e ## name; \
    uint32_t _e ## name; \
}
#else
/* Non-gcc sources must always use the proper 64-bit name (e.g., rax). */
#define __DECL_REG(name) uint64_t r ## name
#endif

#ifndef __DECL_REG_LOHI
#define __DECL_REG_LOHI(name) __DECL_REG(name ## x)
#define __DECL_REG_LO8        __DECL_REG
#define __DECL_REG_LO16       __DECL_REG
#define __DECL_REG_HI(num)    uint64_t r ## num
#endif

struct cpu_user_regs {
    __DECL_REG_HI(15);
    __DECL_REG_HI(14);
    __DECL_REG_HI(13);
    __DECL_REG_HI(12);
    __DECL_REG_LO8(bp);
    __DECL_REG_LOHI(b);
    __DECL_REG_HI(11);
    __DECL_REG_HI(10);
    __DECL_REG_HI(9);
    __DECL_REG_HI(8);
    __DECL_REG_LOHI(a);
    __DECL_REG_LOHI(c);
    __DECL_REG_LOHI(d);
    __DECL_REG_LO8(si);
    __DECL_REG_LO8(di);
    uint32_t error_code;    /* private */
    uint32_t entry_vector;  /* private */
    __DECL_REG_LO16(ip);
    uint16_t cs, _pad0[1];
    uint8_t  saved_upcall_mask;
    uint8_t  _pad1[3];
    __DECL_REG_LO16(flags); /* rflags.IF == !saved_upcall_mask */
    __DECL_REG_LO8(sp);
    uint16_t ss, _pad2[3];
    uint16_t es, _pad3[3];
    uint16_t ds, _pad4[3];
    uint16_t fs, _pad5[3];
    uint16_t gs, _pad6[3];
};
typedef struct cpu_user_regs cpu_user_regs_t;
DEFINE_XEN_GUEST_HANDLE(cpu_user_regs_t);

#undef __DECL_REG
#undef __DECL_REG_LOHI
#undef __DECL_REG_LO8
#undef __DECL_REG_LO16
#undef __DECL_REG_HI

#define xen_pfn_to_cr3(pfn) ((unsigned long)(pfn) << 12)
#define xen_cr3_to_pfn(cr3) ((unsigned long)(cr3) >> 12)

struct arch_vcpu_info {
    unsigned long cr2;
    unsigned long pad; /* sizeof(vcpu_info_t) == 64 */
};
typedef struct arch_vcpu_info arch_vcpu_info_t;

typedef unsigned long xen_callback_t;

#endif /* !__ASSEMBLY__ */

#endif /* __XEN_PUBLIC_ARCH_X86_XEN_X86_64_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
