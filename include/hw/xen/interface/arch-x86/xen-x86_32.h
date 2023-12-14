/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * xen-x86_32.h
 *
 * Guest OS interface to x86 32-bit Xen.
 *
 * Copyright (c) 2004-2007, K A Fraser
 */

#ifndef __XEN_PUBLIC_ARCH_X86_XEN_X86_32_H__
#define __XEN_PUBLIC_ARCH_X86_XEN_X86_32_H__

/*
 * Hypercall interface:
 *  Input:  %ebx, %ecx, %edx, %esi, %edi, %ebp (arguments 1-6)
 *  Output: %eax
 * Access is via hypercall page (set up by guest loader or via a Xen MSR):
 *  call hypercall_page + hypercall-number * 32
 * Clobbered: Argument registers (e.g., 2-arg hypercall clobbers %ebx,%ecx)
 */

/*
 * These flat segments are in the Xen-private section of every GDT. Since these
 * are also present in the initial GDT, many OSes will be able to avoid
 * installing their own GDT.
 */
#define FLAT_RING1_CS 0xe019    /* GDT index 259 */
#define FLAT_RING1_DS 0xe021    /* GDT index 260 */
#define FLAT_RING1_SS 0xe021    /* GDT index 260 */
#define FLAT_RING3_CS 0xe02b    /* GDT index 261 */
#define FLAT_RING3_DS 0xe033    /* GDT index 262 */
#define FLAT_RING3_SS 0xe033    /* GDT index 262 */

#define FLAT_KERNEL_CS FLAT_RING1_CS
#define FLAT_KERNEL_DS FLAT_RING1_DS
#define FLAT_KERNEL_SS FLAT_RING1_SS
#define FLAT_USER_CS    FLAT_RING3_CS
#define FLAT_USER_DS    FLAT_RING3_DS
#define FLAT_USER_SS    FLAT_RING3_SS

#define __HYPERVISOR_VIRT_START_PAE    0xF5800000
#define __MACH2PHYS_VIRT_START_PAE     0xF5800000
#define __MACH2PHYS_VIRT_END_PAE       0xF6800000
#define HYPERVISOR_VIRT_START_PAE      xen_mk_ulong(__HYPERVISOR_VIRT_START_PAE)
#define MACH2PHYS_VIRT_START_PAE       xen_mk_ulong(__MACH2PHYS_VIRT_START_PAE)
#define MACH2PHYS_VIRT_END_PAE         xen_mk_ulong(__MACH2PHYS_VIRT_END_PAE)

/* Non-PAE bounds are obsolete. */
#define __HYPERVISOR_VIRT_START_NONPAE 0xFC000000
#define __MACH2PHYS_VIRT_START_NONPAE  0xFC000000
#define __MACH2PHYS_VIRT_END_NONPAE    0xFC400000
#define HYPERVISOR_VIRT_START_NONPAE   \
    xen_mk_ulong(__HYPERVISOR_VIRT_START_NONPAE)
#define MACH2PHYS_VIRT_START_NONPAE    \
    xen_mk_ulong(__MACH2PHYS_VIRT_START_NONPAE)
#define MACH2PHYS_VIRT_END_NONPAE      \
    xen_mk_ulong(__MACH2PHYS_VIRT_END_NONPAE)

#define __HYPERVISOR_VIRT_START __HYPERVISOR_VIRT_START_PAE
#define __MACH2PHYS_VIRT_START  __MACH2PHYS_VIRT_START_PAE
#define __MACH2PHYS_VIRT_END    __MACH2PHYS_VIRT_END_PAE

#ifndef HYPERVISOR_VIRT_START
#define HYPERVISOR_VIRT_START xen_mk_ulong(__HYPERVISOR_VIRT_START)
#endif

#define MACH2PHYS_VIRT_START  xen_mk_ulong(__MACH2PHYS_VIRT_START)
#define MACH2PHYS_VIRT_END    xen_mk_ulong(__MACH2PHYS_VIRT_END)
#define MACH2PHYS_NR_ENTRIES  ((MACH2PHYS_VIRT_END-MACH2PHYS_VIRT_START)>>2)
#ifndef machine_to_phys_mapping
#define machine_to_phys_mapping ((unsigned long *)MACH2PHYS_VIRT_START)
#endif

/* 32-/64-bit invariability for control interfaces (domctl/sysctl). */
#if defined(__XEN__) || defined(__XEN_TOOLS__)
#undef ___DEFINE_XEN_GUEST_HANDLE
#define ___DEFINE_XEN_GUEST_HANDLE(name, type)                  \
    typedef struct { type *p; }                                 \
        __guest_handle_ ## name;                                \
    typedef struct { union { type *p; uint64_aligned_t q; }; }  \
        __guest_handle_64_ ## name
#undef set_xen_guest_handle_raw
#define set_xen_guest_handle_raw(hnd, val)                  \
    do { if ( sizeof(hnd) == 8 ) *(uint64_t *)&(hnd) = 0;   \
         (hnd).p = val;                                     \
    } while ( 0 )
#define  int64_aligned_t  int64_t __attribute__((aligned(8)))
#define uint64_aligned_t uint64_t __attribute__((aligned(8)))
#define __XEN_GUEST_HANDLE_64(name) __guest_handle_64_ ## name
#define XEN_GUEST_HANDLE_64(name) __XEN_GUEST_HANDLE_64(name)
#endif

#ifndef __ASSEMBLY__

#if defined(XEN_GENERATING_COMPAT_HEADERS)
/* nothing */
#elif defined(__XEN__) || defined(__XEN_TOOLS__)
/* Anonymous unions include all permissible names (e.g., al/ah/ax/eax). */
#define __DECL_REG_LO8(which) union { \
    uint32_t e ## which ## x; \
    uint16_t which ## x; \
    struct { \
        uint8_t which ## l; \
        uint8_t which ## h; \
    }; \
}
#define __DECL_REG_LO16(name) union { \
    uint32_t e ## name, _e ## name; \
    uint16_t name; \
}
#else
/* Other sources must always use the proper 32-bit name (e.g., eax). */
#define __DECL_REG_LO8(which) uint32_t e ## which ## x
#define __DECL_REG_LO16(name) uint32_t e ## name
#endif

struct cpu_user_regs {
    __DECL_REG_LO8(b);
    __DECL_REG_LO8(c);
    __DECL_REG_LO8(d);
    __DECL_REG_LO16(si);
    __DECL_REG_LO16(di);
    __DECL_REG_LO16(bp);
    __DECL_REG_LO8(a);
    uint16_t error_code;    /* private */
    uint16_t entry_vector;  /* private */
    __DECL_REG_LO16(ip);
    uint16_t cs;
    uint8_t  saved_upcall_mask;
    uint8_t  _pad0;
    __DECL_REG_LO16(flags); /* eflags.IF == !saved_upcall_mask */
    __DECL_REG_LO16(sp);
    uint16_t ss, _pad1;
    uint16_t es, _pad2;
    uint16_t ds, _pad3;
    uint16_t fs, _pad4;
    uint16_t gs, _pad5;
};
typedef struct cpu_user_regs cpu_user_regs_t;
DEFINE_XEN_GUEST_HANDLE(cpu_user_regs_t);

#undef __DECL_REG_LO8
#undef __DECL_REG_LO16

/*
 * Page-directory addresses above 4GB do not fit into architectural %cr3.
 * When accessing %cr3, or equivalent field in vcpu_guest_context, guests
 * must use the following accessor macros to pack/unpack valid MFNs.
 */
#define xen_pfn_to_cr3(pfn) (((unsigned)(pfn) << 12) | ((unsigned)(pfn) >> 20))
#define xen_cr3_to_pfn(cr3) (((unsigned)(cr3) >> 12) | ((unsigned)(cr3) << 20))

struct arch_vcpu_info {
    unsigned long cr2;
    unsigned long pad[5]; /* sizeof(vcpu_info_t) == 64 */
};
typedef struct arch_vcpu_info arch_vcpu_info_t;

struct xen_callback {
    unsigned long cs;
    unsigned long eip;
};
typedef struct xen_callback xen_callback_t;

#endif /* !__ASSEMBLY__ */

#endif /* __XEN_PUBLIC_ARCH_X86_XEN_X86_32_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
