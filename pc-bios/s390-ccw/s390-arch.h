/*
 * S390 Basic Architecture
 *
 * Copyright (c) 2019 Jason J. Herne <jjherne@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390_ARCH_H
#define S390_ARCH_H

typedef struct PSW {
    uint64_t mask;
    uint64_t addr;
} __attribute__ ((aligned(8))) PSW;
_Static_assert(sizeof(struct PSW) == 16, "PSW size incorrect");

/* Older PSW format used by LPSW instruction */
typedef struct PSWLegacy {
    uint32_t mask;
    uint32_t addr;
} __attribute__ ((aligned(8))) PSWLegacy;
_Static_assert(sizeof(struct PSWLegacy) == 8, "PSWLegacy size incorrect");

/* s390 psw bit masks */
#define PSW_MASK_IOINT      0x0200000000000000ULL
#define PSW_MASK_SHORTPSW   0x0008000000000000ULL
#define PSW_MASK_WAIT       0x0002000000000000ULL
#define PSW_MASK_EAMODE     0x0000000100000000ULL
#define PSW_MASK_BAMODE     0x0000000080000000ULL
#define PSW_MASK_SHORT_ADDR 0x000000007fffffffULL
#define PSW_MASK_64         (PSW_MASK_EAMODE | PSW_MASK_BAMODE)

/* Low core mapping */
typedef struct LowCore {
    /* prefix area: defined by architecture */
    PSWLegacy       ipl_psw;                  /* 0x000 */
    uint32_t        ccw1[2];                  /* 0x008 */
    union {
        uint32_t        ccw2[2];                  /* 0x010 */
        struct {
            uint32_t reserved10;
            uint32_t ptr_iplb;
        };
    };
    uint8_t         pad1[0x80 - 0x18];        /* 0x018 */
    uint32_t        ext_params;               /* 0x080 */
    uint16_t        cpu_addr;                 /* 0x084 */
    uint16_t        ext_int_code;             /* 0x086 */
    uint16_t        svc_ilen;                 /* 0x088 */
    uint16_t        svc_code;                 /* 0x08a */
    uint16_t        pgm_ilen;                 /* 0x08c */
    uint16_t        pgm_code;                 /* 0x08e */
    uint32_t        data_exc_code;            /* 0x090 */
    uint16_t        mon_class_num;            /* 0x094 */
    uint16_t        per_perc_atmid;           /* 0x096 */
    uint64_t        per_address;              /* 0x098 */
    uint8_t         exc_access_id;            /* 0x0a0 */
    uint8_t         per_access_id;            /* 0x0a1 */
    uint8_t         op_access_id;             /* 0x0a2 */
    uint8_t         ar_access_id;             /* 0x0a3 */
    uint8_t         pad2[0xA8 - 0xA4];        /* 0x0a4 */
    uint64_t        trans_exc_code;           /* 0x0a8 */
    uint64_t        monitor_code;             /* 0x0b0 */
    uint16_t        subchannel_id;            /* 0x0b8 */
    uint16_t        subchannel_nr;            /* 0x0ba */
    uint32_t        io_int_parm;              /* 0x0bc */
    uint32_t        io_int_word;              /* 0x0c0 */
    uint8_t         pad3[0xc8 - 0xc4];        /* 0x0c4 */
    uint32_t        stfl_fac_list;            /* 0x0c8 */
    uint8_t         pad4[0xe8 - 0xcc];        /* 0x0cc */
    uint64_t        mcic;                     /* 0x0e8 */
    uint8_t         pad5[0xf4 - 0xf0];        /* 0x0f0 */
    uint32_t        external_damage_code;     /* 0x0f4 */
    uint64_t        failing_storage_address;  /* 0x0f8 */
    uint8_t         pad6[0x110 - 0x100];      /* 0x100 */
    uint64_t        per_breaking_event_addr;  /* 0x110 */
    uint8_t         pad7[0x120 - 0x118];      /* 0x118 */
    PSW             restart_old_psw;          /* 0x120 */
    PSW             external_old_psw;         /* 0x130 */
    PSW             svc_old_psw;              /* 0x140 */
    PSW             program_old_psw;          /* 0x150 */
    PSW             mcck_old_psw;             /* 0x160 */
    PSW             io_old_psw;               /* 0x170 */
    uint8_t         pad8[0x1a0 - 0x180];      /* 0x180 */
    PSW             restart_new_psw;          /* 0x1a0 */
    PSW             external_new_psw;         /* 0x1b0 */
    PSW             svc_new_psw;              /* 0x1c0 */
    PSW             program_new_psw;          /* 0x1d0 */
    PSW             mcck_new_psw;             /* 0x1e0 */
    PSW             io_new_psw;               /* 0x1f0 */
} __attribute__((packed, aligned(8192))) LowCore;

extern LowCore *lowcore;

static inline void set_prefix(uint32_t address)
{
    asm volatile("spx %0" : : "m" (address) : "memory");
}

static inline uint32_t store_prefix(void)
{
    uint32_t address;

    asm volatile("stpx %0" : "=m" (address));
    return address;
}

#endif
