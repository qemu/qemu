/*
 * QEMU HAXM support
 *
 * Copyright (c) 2011 Intel Corporation
 *  Written by:
 *  Jiang Yunhong<yunhong.jiang@intel.com>
 *  Xin Xiaohui<xiaohui.xin@intel.com>
 *  Zhang Xiantao<xiantao.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* Interface with HAX kernel module */

#ifndef HAX_INTERFACE_H
#define HAX_INTERFACE_H

/* fx_layout has 3 formats table 3-56, 512bytes */
struct fx_layout {
    uint16_t fcw;
    uint16_t fsw;
    uint8_t ftw;
    uint8_t res1;
    uint16_t fop;
    union {
        struct {
            uint32_t fip;
            uint16_t fcs;
            uint16_t res2;
        };
        uint64_t fpu_ip;
    };
    union {
        struct {
            uint32_t fdp;
            uint16_t fds;
            uint16_t res3;
        };
        uint64_t fpu_dp;
    };
    uint32_t mxcsr;
    uint32_t mxcsr_mask;
    uint8_t st_mm[8][16];
    uint8_t mmx_1[8][16];
    uint8_t mmx_2[8][16];
    uint8_t pad[96];
} __attribute__ ((aligned(8)));

struct vmx_msr {
    uint64_t entry;
    uint64_t value;
} __attribute__ ((__packed__));

/*
 * Fixed array is not good, but it makes Mac support a bit easier by avoiding
 * memory map or copyin staff.
 */
#define HAX_MAX_MSR_ARRAY 0x20
struct hax_msr_data {
    uint16_t nr_msr;
    uint16_t done;
    uint16_t pad[2];
    struct vmx_msr entries[HAX_MAX_MSR_ARRAY];
} __attribute__ ((__packed__));

union interruptibility_state_t {
    uint32_t raw;
    struct {
        uint32_t sti_blocking:1;
        uint32_t movss_blocking:1;
        uint32_t smi_blocking:1;
        uint32_t nmi_blocking:1;
        uint32_t reserved:28;
    };
    uint64_t pad;
};

typedef union interruptibility_state_t interruptibility_state_t;

/* Segment descriptor */
struct segment_desc_t {
    uint16_t selector;
    uint16_t _dummy;
    uint32_t limit;
    uint64_t base;
    union {
        struct {
            uint32_t type:4;
            uint32_t desc:1;
            uint32_t dpl:2;
            uint32_t present:1;
            uint32_t:4;
            uint32_t available:1;
            uint32_t long_mode:1;
            uint32_t operand_size:1;
            uint32_t granularity:1;
            uint32_t null:1;
            uint32_t:15;
        };
        uint32_t ar;
    };
    uint32_t ipad;
};

typedef struct segment_desc_t segment_desc_t;

struct vcpu_state_t {
    union {
        uint64_t _regs[16];
        struct {
            union {
                struct {
                    uint8_t _al, _ah;
                };
                uint16_t _ax;
                uint32_t _eax;
                uint64_t _rax;
            };
            union {
                struct {
                    uint8_t _cl, _ch;
                };
                uint16_t _cx;
                uint32_t _ecx;
                uint64_t _rcx;
            };
            union {
                struct {
                    uint8_t _dl, _dh;
                };
                uint16_t _dx;
                uint32_t _edx;
                uint64_t _rdx;
            };
            union {
                struct {
                    uint8_t _bl, _bh;
                };
                uint16_t _bx;
                uint32_t _ebx;
                uint64_t _rbx;
            };
            union {
                uint16_t _sp;
                uint32_t _esp;
                uint64_t _rsp;
            };
            union {
                uint16_t _bp;
                uint32_t _ebp;
                uint64_t _rbp;
            };
            union {
                uint16_t _si;
                uint32_t _esi;
                uint64_t _rsi;
            };
            union {
                uint16_t _di;
                uint32_t _edi;
                uint64_t _rdi;
            };

            uint64_t _r8;
            uint64_t _r9;
            uint64_t _r10;
            uint64_t _r11;
            uint64_t _r12;
            uint64_t _r13;
            uint64_t _r14;
            uint64_t _r15;
        };
    };

    union {
        uint32_t _eip;
        uint64_t _rip;
    };

    union {
        uint32_t _eflags;
        uint64_t _rflags;
    };

    segment_desc_t _cs;
    segment_desc_t _ss;
    segment_desc_t _ds;
    segment_desc_t _es;
    segment_desc_t _fs;
    segment_desc_t _gs;
    segment_desc_t _ldt;
    segment_desc_t _tr;

    segment_desc_t _gdt;
    segment_desc_t _idt;

    uint64_t _cr0;
    uint64_t _cr2;
    uint64_t _cr3;
    uint64_t _cr4;

    uint64_t _dr0;
    uint64_t _dr1;
    uint64_t _dr2;
    uint64_t _dr3;
    uint64_t _dr6;
    uint64_t _dr7;
    uint64_t _pde;

    uint32_t _efer;

    uint32_t _sysenter_cs;
    uint64_t _sysenter_eip;
    uint64_t _sysenter_esp;

    uint32_t _activity_state;
    uint32_t pad;
    interruptibility_state_t _interruptibility_state;
};

/* HAX exit status */
enum exit_status {
    /* IO port request */
    HAX_EXIT_IO = 1,
    /* MMIO instruction emulation */
    HAX_EXIT_MMIO,
    /* QEMU emulation mode request, currently means guest enter non-PG mode */
    HAX_EXIT_REAL,
    /*
     * Interrupt window open, qemu can inject interrupt now
     * Also used when signal pending since at that time qemu usually need
     * check interrupt
     */
    HAX_EXIT_INTERRUPT,
    /* Unknown vmexit, mostly trigger reboot */
    HAX_EXIT_UNKNOWN_VMEXIT,
    /* HALT from guest */
    HAX_EXIT_HLT,
    /* Reboot request, like because of tripple fault in guest */
    HAX_EXIT_STATECHANGE,
    /* the vcpu is now only paused when destroy, so simply return to hax */
    HAX_EXIT_PAUSED,
    HAX_EXIT_FAST_MMIO,
};

/*
 * The interface definition:
 * 1. vcpu_run execute will return 0 on success, otherwise mean failed
 * 2. exit_status return the exit reason, as stated in enum exit_status
 * 3. exit_reason is the vmx exit reason
 */
struct hax_tunnel {
    uint32_t _exit_reason;
    uint32_t _exit_flag;
    uint32_t _exit_status;
    uint32_t user_event_pending;
    int ready_for_interrupt_injection;
    int request_interrupt_window;
    union {
        struct {
            /* 0: read, 1: write */
#define HAX_EXIT_IO_IN  1
#define HAX_EXIT_IO_OUT 0
            uint8_t _direction;
            uint8_t _df;
            uint16_t _size;
            uint16_t _port;
            uint16_t _count;
            uint8_t _flags;
            uint8_t _pad0;
            uint16_t _pad1;
            uint32_t _pad2;
            uint64_t _vaddr;
        } pio;
        struct {
            uint64_t gla;
        } mmio;
        struct {
        } state;
    };
} __attribute__ ((__packed__));

struct hax_module_version {
    uint32_t compat_version;
    uint32_t cur_version;
} __attribute__ ((__packed__));

/* This interface is support only after API version 2 */
struct hax_qemu_version {
    /* Current API version in QEMU */
    uint32_t cur_version;
    /* The minimum API version supported by QEMU */
    uint32_t min_version;
} __attribute__ ((__packed__));

/* The mac specfic interface to qemu, mostly is ioctl related */
struct hax_tunnel_info {
    uint64_t va;
    uint64_t io_va;
    uint16_t size;
    uint16_t pad[3];
} __attribute__ ((__packed__));

struct hax_alloc_ram_info {
    uint32_t size;
    uint32_t pad;
    uint64_t va;
} __attribute__ ((__packed__));

struct hax_ramblock_info {
    uint64_t start_va;
    uint64_t size;
    uint64_t reserved;
} __attribute__ ((__packed__));

#define HAX_RAM_INFO_ROM     0x01 /* Read-Only */
#define HAX_RAM_INFO_INVALID 0x80 /* Unmapped, usually used for MMIO */
struct hax_set_ram_info {
    uint64_t pa_start;
    uint32_t size;
    uint8_t flags;
    uint8_t pad[3];
    uint64_t va;
} __attribute__ ((__packed__));

#define HAX_CAP_STATUS_WORKING     0x1
#define HAX_CAP_STATUS_NOTWORKING  0x0
#define HAX_CAP_WORKSTATUS_MASK    0x1

#define HAX_CAP_FAILREASON_VT      0x1
#define HAX_CAP_FAILREASON_NX      0x2

#define HAX_CAP_MEMQUOTA           0x2
#define HAX_CAP_UG                 0x4
#define HAX_CAP_64BIT_RAMBLOCK     0x8

struct hax_capabilityinfo {
    /* bit 0: 1 - working
     *        0 - not working, possibly because NT/NX disabled
     * bit 1: 1 - memory limitation working
     *        0 - no memory limitation
     */
    uint16_t wstatus;
    /* valid when not working
     * bit 0: VT not enabeld
     * bit 1: NX not enabled*/
    uint16_t winfo;
    uint32_t pad;
    uint64_t mem_quota;
} __attribute__ ((__packed__));

struct hax_fastmmio {
    uint64_t gpa;
    union {
        uint64_t value;
        uint64_t gpa2;  /* since HAX API v4 */
    };
    uint8_t size;
    uint8_t direction;
    uint16_t reg_index;
    uint32_t pad0;
    uint64_t _cr0;
    uint64_t _cr2;
    uint64_t _cr3;
    uint64_t _cr4;
} __attribute__ ((__packed__));
#endif
