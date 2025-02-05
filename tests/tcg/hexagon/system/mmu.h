/*
 *  Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MMU_H
#define MMU_H
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "crt0/hexagon_standalone.h"

/*
 * Helpers for MMU tests
 */

#define TARGET_PAGE_BITS            12
#ifndef TLB_NOT_FOUND
#define TLB_NOT_FOUND               (1 << 31)
#endif

static inline uint32_t page_start(uint32_t addr, uint32_t page_size_bits)
{
    uint32_t page_size = 1 << page_size_bits;
    uint32_t page_align = ~(page_size - 1);
    return addr & page_align;
}

/*
 * The Hexagon standalone runtime leaves TLB entries 1-5 reserved for
 * user-defined entries.  We'll set them up to map virtual addresses at
 * 1MB offsets above the actual physical address
 *     PA == VA - (entry_num * 1MB)
 *
 * We'll define some macros/functions to help with the manipulation
 */

#define ONE_MB                      (1 << 20)
#define TWO_MB                      (2 * ONE_MB)
#define THREE_MB                    (3 * ONE_MB)
#define FOUR_MB                     (4 * ONE_MB)
#define FIVE_MB                     (5 * ONE_MB)

#define ONE_MB_ENTRY                1
#define TWO_MB_ENTRY                2
#define THREE_MB_ENTRY              3
#define FOUR_MB_ENTRY               4
#define FIVE_MB_ENTRY               5

static inline uint32_t tlb_entry_num(uint32_t va)
{
    return va >> 20;
}

#define fZXTN(N, M, VAL) ((VAL) & ((1LL << (N)) - 1))
#define fEXTRACTU_BITS(INREG, WIDTH, OFFSET) \
    (fZXTN(WIDTH, 32, (INREG >> OFFSET)))

#define fINSERT_BITS(REG, WIDTH, OFFSET, INVAL) \
    do { \
        REG = ((REG) & ~(((1LL << (WIDTH)) - 1) << (OFFSET))) | \
           (((INVAL) & ((1LL << (WIDTH)) - 1)) << (OFFSET)); \
    } while (0)

#define GET_FIELD(ENTRY, FIELD) \
    fEXTRACTU_BITS(ENTRY, reg_field_info[FIELD].width, \
                   reg_field_info[FIELD].offset)
#define SET_FIELD(ENTRY, FIELD, VAL) \
    fINSERT_BITS(ENTRY, reg_field_info[FIELD].width, \
                 reg_field_info[FIELD].offset, (VAL))

typedef struct {
    int offset;
    int width;
} reg_field_t;

enum reg_fields_enum {
#define DEF_REG_FIELD(TAG, NAME, START, WIDTH, DESCRIPTION) \
    TAG,
#include "reg_fields_def.h"
    NUM_REG_FIELDS
#undef DEF_REG_FIELD
};

static const reg_field_t reg_field_info[] = {
#define DEF_REG_FIELD(TAG, NAME, START, WIDTH, DESCRIPTION)    \
      { START, WIDTH },

#include "reg_fields_def.h"

      { 0, 0 }
#undef DEF_REG_FIELD
};

/*
 * PPD (physical page descriptor) is formed by putting the PTE_PA35 field
 * in the MSB of the PPD
 */
#define GET_PPD(ENTRY) \
    ((GET_FIELD((ENTRY), PTE_PPD) | \
     (GET_FIELD((ENTRY), PTE_PA35) << reg_field_info[PTE_PPD].width)))

#define NUM_PGSIZE_TYPES (SHIFT_1G + 1)

static const char *pgsize_str(PageSize pgsize)
{
    static const char *size_str[NUM_PGSIZE_TYPES] = {
        "4K",
        "16K",
        "64K",
        "256K",
        "1M",
        "4M",
        "16M",
        "64M",
        "256M",
        "1G"
    };
    assert(pgsize);
    return size_str[__builtin_ctz(pgsize)];
}

static const uint64_t encmask_2_mask[] = {
    0x0fffLL,                           /* 4k,   0000 */
    0x3fffLL,                           /* 16k,  0001 */
    0xffffLL,                           /* 64k,  0010 */
    0x3ffffLL,                          /* 256k, 0011 */
    0xfffffLL,                          /* 1m,   0100 */
    0x3fffffLL,                         /* 4m,   0101 */
    0xffffffLL,                         /* 16M,  0110 */
    0xffffffffLL,                       /* RSVD, 0111 */
};

static inline int hex_tlb_pgsize(uint64_t entry)
{
    assert(entry != 0);
    int size = __builtin_ctzll(entry);
    assert(size < NUM_PGSIZE_TYPES);
    return size;
}

static inline uint32_t hex_tlb_page_size(uint64_t entry)
{
    return 1 << (TARGET_PAGE_BITS + 2 * hex_tlb_pgsize(entry));
}

static inline uint64_t hex_tlb_phys_page_num(uint64_t entry)
{
    uint32_t ppd = GET_PPD(entry);
    return ppd >> 1;
}

static inline uint64_t hex_tlb_phys_addr(uint64_t entry)
{
    uint64_t pagemask = encmask_2_mask[hex_tlb_pgsize(entry)];
    uint64_t pagenum = hex_tlb_phys_page_num(entry);
    uint64_t PA = (pagenum << TARGET_PAGE_BITS) & (~pagemask);
    return PA;
}

static inline uint64_t hex_tlb_virt_addr(uint64_t entry)
{
    return GET_FIELD(entry, PTE_VPN) << TARGET_PAGE_BITS;
}

static inline uint64_t create_mmu_entry(uint8_t G, uint8_t A0, uint8_t A1,
                                        uint8_t ASID, uint32_t VA,
                                        uint8_t X, int8_t W, uint8_t R,
                                        uint8_t U, uint8_t C, uint64_t PA,
                                        PageSize SZ)
{
    uint64_t entry = 0;
    SET_FIELD(entry, PTE_V, 1);
    SET_FIELD(entry, PTE_G, G);
    SET_FIELD(entry, PTE_ATR0, A0);
    SET_FIELD(entry, PTE_ATR1, A1);
    SET_FIELD(entry, PTE_ASID, ASID);
    SET_FIELD(entry, PTE_VPN, VA >> TARGET_PAGE_BITS);
    SET_FIELD(entry, PTE_X, X);
    SET_FIELD(entry, PTE_W, W);
    SET_FIELD(entry, PTE_R, R);
    SET_FIELD(entry, PTE_U, U);
    SET_FIELD(entry, PTE_C, C);
    SET_FIELD(entry, PTE_PA35, (PA >> (TARGET_PAGE_BITS + 35)) & 1);
    SET_FIELD(entry, PTE_PPD, ((PA >> (TARGET_PAGE_BITS - 1))));
    entry |= SZ;
    return entry;
}

static inline uint64_t tlbr(uint32_t i)
{
    uint64_t ret;
    asm volatile ("%0 = tlbr(%1)\n\t" : "=r"(ret) : "r"(i));
    return ret;
}

static inline uint32_t ctlbw(uint64_t entry, uint32_t idx)
{
    uint32_t ret;
    asm volatile ("%0 = ctlbw(%1, %2)\n\t" : "=r"(ret) : "r"(entry), "r"(idx));
    return ret;
}

static inline uint32_t tlbp(uint32_t asid, uint32_t VA)
{
    uint32_t x = ((asid & 0x7f) << 20) | ((VA >> 12) & 0xfffff);
    uint32_t ret;
    asm volatile ("%0 = tlbp(%1)\n\t" : "=r"(ret) : "r"(x));
    return ret;
}

static inline void tlbw(uint64_t entry, uint32_t idx)
{
    asm volatile ("tlbw(%0, %1)\n\t" :: "r"(entry), "r"(idx));
}

static inline uint32_t tlboc(uint64_t entry)
{
    uint32_t ret;
    asm volatile ("%0 = tlboc(%1)\n\t" : "=r"(ret) : "r"(entry));
    return ret;
}

void tlbinvasid(uint32_t entry_hi)
{
    asm volatile ("tlbinvasid(%0)\n\t" :: "r"(entry_hi));
}

static inline void enter_user_mode(void)
{
    asm volatile ("r0 = ssr\n\t"
                  "r0 = clrbit(r0, #17) // EX\n\t"
                  "r0 = setbit(r0, #16) // UM\n\t"
                  "r0 = clrbit(r0, #19) // GM\n\t"
                  "ssr = r0\n\t" : : : "r0");
}

static inline void enter_kernel_mode(void)
{
    asm volatile ("r0 = ssr\n\t"
                  "r0 = clrbit(r0, #17) // EX\n\t"
                  "r0 = clrbit(r0, #16) // UM\n\t"
                  "r0 = clrbit(r0, #19) // GM\n\t"
                  "ssr = r0\n\t" : : : "r0");
}

static inline uint32_t *getevb()
{
    uint32_t reg;
    asm volatile ("%0 = evb\n\t" : "=r"(reg));
    return (uint32_t *)reg;
}

static inline void setevb(void *new_evb)
{
    asm volatile("evb = %0\n\t" : : "r"(new_evb));
}

static inline uint32_t getbadva()
{
    uint32_t badva;
    asm volatile ("%0 = badva\n\t" : "=r"(badva));
    return badva;
}

static void inc_elr(uint32_t inc)
{

    asm volatile ("r1 = %0\n\t"
                  "r2 = elr\n\t"
                  "r1 = add(r2, r1)\n\t"
                  "elr = r1\n\t"
                  :  : "r"(inc) : "r1", "r2");
}

static inline void do_coredump(void)
{
    asm volatile("r0 = #2\n\t"
                 "stid = r0\n\t"
                 "jump __coredump\n\t" : : : "r0");
}

static inline uint32_t getssr(void)
{
    uint32_t ret;
    asm volatile ("%0 = ssr\n\t" : "=r"(ret));
    return ret;
}

static inline void setssr(uint32_t new_ssr)
{
    asm volatile ("ssr = %0\n\t" :: "r"(new_ssr));
}

static inline void set_asid(uint32_t asid)
{
    uint32_t ssr = getssr();
    SET_FIELD(ssr, SSR_ASID, asid);
    setssr(ssr);
}

int err;
#include "../hex_test.h"

static void *old_evb;

typedef uint64_t exception_vector[2];
static exception_vector my_exceptions;

static inline void clear_exception_vector(exception_vector excp)
{
    excp[0] = 0;
    excp[1] = 0;
}

static inline void set_exception_vector_bit(exception_vector excp, uint32_t bit)
{
    if (bit < 64) {
        excp[0] |= 1LL << bit;
    } else if (bit < 128) {
        excp[1] |= 1LL << (bit - 64);
    }
}

#define check_exception_vector(excp, expect) \
    do { \
        check64(excp[0], expect[0]); \
        check64(excp[1], expect[1]); \
    } while (0)

static inline void print_exception_vector(exception_vector excp)
{
    printf("exceptions (0x%016llx 0x%016llx):", excp[1], excp[0]);
    for (int i = 0; i < 64; i++) {
        if (excp[0] & (1LL << i)) {
            printf(" 0x%x", i);
        }
    }
    for (int i = 0; i < 64; i++) {
        if (excp[1] & (1LL << i)) {
            printf(" 0x%x", i + 64);
        }
    }
    printf("\n");
}

/* volatile because it is written through different MMU mappings */
typedef volatile int mmu_variable;
mmu_variable data = 0xdeadbeef;

typedef int (*func_t)(void);
/* volatile because it will be invoked via different MMU mappings */
typedef volatile func_t mmu_func_t;

/*
 * Create a function that returns its (virtual) address
 * Write it fully in assembly so we don't have to worry about
 * which optimization level we are compiled with
 */
extern int func_return_pc(void);
asm(
".global func_return_pc\n"
".balign 4\n"
".type func_return_pc, @function\n"
"func_return_pc:\n"
"    r0 = pc\n"
"    jumpr r31\n"
".size func_return_pc, . - func_return_pc\n"
);

enum {
    TLB_U = (1 << 0),
    TLB_R = (1 << 1),
    TLB_W = (1 << 2),
    TLB_X = (1 << 3),
};

#define HEX_CAUSE_FETCH_NO_XPAGE                  0x011
#define HEX_CAUSE_FETCH_NO_UPAGE                  0x012
#define HEX_CAUSE_PRIV_NO_READ                    0x022
#define HEX_CAUSE_PRIV_NO_WRITE                   0x023
#define HEX_CAUSE_PRIV_NO_UREAD                   0x024
#define HEX_CAUSE_PRIV_NO_UWRITE                  0x025
#define HEX_CAUSE_IMPRECISE_MULTI_TLB_MATCH       0x044
#define HEX_CAUSE_TLBMISSX_NORMAL                 0x060
#define HEX_CAUSE_TLBMISSX_NEXTPAGE               0x061
#define HEX_CAUSE_TLBMISSRW_READ                  0x070
#define HEX_CAUSE_TLBMISSRW_WRITE                 0x071

/*
 * The following lets us override the default exception handlers
 * This can be handy for adding code to check that they are called as well
 * as special handling needed for the test to succeed.
 *
 * MY_EVENT_HANDLE           Use this to define your own event handler
 * DEFAULT_EVENT_HANDLE      Use this to point to the default handler
 * my_event_vectors          New event vector table
 * install_my_event_vectors  Change from the default event handlers
 */

extern void *my_event_vectors;

#define MY_EVENT_HANDLE(name, helper) \
void name(void) \
{ \
    asm volatile("crswap(sp, sgp0)\n\t" \
                 "memd(sp++#8) = r1:0\n\t" \
                 "memd(sp++#8) = r3:2\n\t" \
                 "memd(sp++#8) = r5:4\n\t" \
                 "memd(sp++#8) = r7:6\n\t" \
                 "memd(sp++#8) = r9:8\n\t" \
                 "memd(sp++#8) = r11:10\n\t" \
                 "memd(sp++#8) = r13:12\n\t" \
                 "memd(sp++#8) = r15:14\n\t" \
                 "memd(sp++#8) = r17:16\n\t" \
                 "memd(sp++#8) = r19:18\n\t" \
                 "memd(sp++#8) = r21:20\n\t" \
                 "memd(sp++#8) = r23:22\n\t" \
                 "memd(sp++#8) = r25:24\n\t" \
                 "memd(sp++#8) = r27:26\n\t" \
                 "memd(sp++#8) = r31:30\n\t" \
                 "r0 = ssr\n\t" \
                 "call " #helper "\n\t" \
                 "sp = add(sp, #-8)\n\t" \
                 "r31:30 = memd(sp++#-8)\n\t" \
                 "r27:26 = memd(sp++#-8)\n\t" \
                 "r25:24 = memd(sp++#-8)\n\t" \
                 "r23:22 = memd(sp++#-8)\n\t" \
                 "r21:20 = memd(sp++#-8)\n\t" \
                 "r19:18 = memd(sp++#-8)\n\t" \
                 "r17:16 = memd(sp++#-8)\n\t" \
                 "r15:14 = memd(sp++#-8)\n\t" \
                 "r13:12 = memd(sp++#-8)\n\t" \
                 "r11:10 = memd(sp++#-8)\n\t" \
                 "r9:8 = memd(sp++#-8)\n\t" \
                 "r7:6 = memd(sp++#-8)\n\t" \
                 "r5:4 = memd(sp++#-8)\n\t" \
                 "r3:2 = memd(sp++#-8)\n\t" \
                 "r1:0 = memd(sp)\n\t" \
                 "crswap(sp, sgp0);\n\t" \
                 "rte\n\t"); \
}

#ifndef NO_DEFAULT_EVENT_HANDLES

#define DEFAULT_EVENT_HANDLE(name, offset) \
void name(void) \
{ \
    asm volatile("r0 = %0\n\t" \
                 "r0 = add(r0, #" #offset ")\n\t" \
                 "jumpr r0\n\t" \
                 : : "r"(old_evb) : "r0"); \
}


/* Use these values as the offset for DEFAULT_EVENT_HANDLE */
asm (
".set HANDLE_RESET_OFFSET,               0x00\n\t"
".set HANDLE_NMI_OFFSET,                 0x04\n\t"
".set HANDLE_ERROR_OFFSET,               0x08\n\t"
".set HANDLE_RSVD_OFFSET,                0x0c\n\t"
".set HANDLE_TLBMISSX_OFFSET,            0x10\n\t"
".set HANDLE_TLBMISSRW_OFFSET,           0x18\n\t"
".set HANDLE_TRAP0_OFFSET,               0x20\n\t"
".set HANDLE_TRAP1_OFFSET,               0x24\n\t"
".set HANDLE_FPERROR_OFFSET,             0x28\n\t"
".set HANDLE_INT_OFFSET,                 0x40\n\t"
);

asm(
".align 0x1000\n\t"
"my_event_vectors:\n\t"
    "jump my_event_handle_reset\n\t"
    "jump my_event_handle_nmi\n\t"
    "jump my_event_handle_error\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_tlbmissx\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_tlbmissrw\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_trap0\n\t"
    "jump my_event_handle_trap1\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_fperror\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_rsvd\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
    "jump my_event_handle_int\n\t"
);

#define DEFAULT_EVENT_HANDLES \
DEFAULT_EVENT_HANDLE(my_event_handle_error,       HANDLE_ERROR_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_nmi,         HANDLE_NMI_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissrw,   HANDLE_TLBMISSRW_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissx,    HANDLE_TLBMISSX_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_reset,       HANDLE_RESET_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd,        HANDLE_RSVD_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_trap0,       HANDLE_TRAP0_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_trap1,       HANDLE_TRAP1_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_int,         HANDLE_INT_OFFSET) \
DEFAULT_EVENT_HANDLE(my_event_handle_fperror,     HANDLE_FPERROR_OFFSET)

#endif /* NO_DEFAULT_EVENT_HANDLES */

/* When a permission error happens, add the permission to the TLB entry */
void my_event_handle_error_helper(uint32_t ssr)
{
    uint32_t cause = GET_FIELD(ssr, SSR_CAUSE);
    uint32_t badva = getbadva();
    uint32_t entry_num = tlb_entry_num(badva);
    uint64_t entry;

    set_exception_vector_bit(my_exceptions, cause);

    switch (cause) {
    case HEX_CAUSE_FETCH_NO_XPAGE:
        entry = tlbr(entry_num);
        SET_FIELD(entry, PTE_X, 1);
        tlbw(entry, entry_num);
        break;
    case HEX_CAUSE_FETCH_NO_UPAGE:
        entry = tlbr(entry_num);
        SET_FIELD(entry, PTE_U, 1);
        tlbw(entry, entry_num);
        break;
    case HEX_CAUSE_PRIV_NO_READ:
        entry = tlbr(entry_num);
        SET_FIELD(entry, PTE_R, 1);
        tlbw(entry, entry_num);
        break;
    case HEX_CAUSE_PRIV_NO_WRITE:
        entry = tlbr(entry_num);
        SET_FIELD(entry, PTE_W, 1);
        tlbw(entry, entry_num);
        break;
    case HEX_CAUSE_PRIV_NO_UREAD:
        entry = tlbr(entry_num);
        SET_FIELD(entry, PTE_U, 1);
        tlbw(entry, entry_num);
        break;
    case HEX_CAUSE_PRIV_NO_UWRITE:
        entry = tlbr(entry_num);
        SET_FIELD(entry, PTE_U, 1);
        tlbw(entry, entry_num);
        break;
    default:
        do_coredump();
        break;
    }
}

void my_event_handle_nmi_helper(uint32_t ssr)
{
    uint32_t cause = GET_FIELD(ssr, SSR_CAUSE);

    set_exception_vector_bit(my_exceptions, cause);

    switch (cause) {
    case HEX_CAUSE_IMPRECISE_MULTI_TLB_MATCH:
        break;
    default:
        do_coredump();
        break;
    }
}

/*
 * When a TLB miss happens, create a mapping
 * We'll set different read/write/execute permissions
 * for different entry numbers.
 */
void my_event_handle_tlbmissrw_helper(uint32_t ssr)
{
    uint32_t cause = GET_FIELD(ssr, SSR_CAUSE);
    uint32_t badva = getbadva();
    uint32_t entry_num = tlb_entry_num(badva);
    uint32_t VA = page_start(badva, TARGET_PAGE_BITS);
    uint32_t PA = VA - (entry_num * ONE_MB);

    uint64_t entry =
        create_mmu_entry(1, 0, 0, 0, VA, 0, 0, 0, 1, 0x3, PA, PAGE_4K);
    if (entry_num == TWO_MB_ENTRY) {
        SET_FIELD(entry, PTE_R, 1);
    }
    if (entry_num == THREE_MB_ENTRY) {
        SET_FIELD(entry, PTE_W, 1);
    }

    set_exception_vector_bit(my_exceptions, cause);

    switch (cause) {
    case HEX_CAUSE_TLBMISSRW_READ:
        tlbw(entry, entry_num);
        break;
    case HEX_CAUSE_TLBMISSRW_WRITE:
        tlbw(entry, entry_num);
        break;
    default:
        do_coredump();
        break;
    }
}

void my_event_handle_tlbmissx_helper(uint32_t ssr)
{
    uint32_t cause = GET_FIELD(ssr, SSR_CAUSE);
    uint32_t badva = getbadva();
    uint32_t entry_num = tlb_entry_num(badva);
    uint32_t VA = page_start(badva, TARGET_PAGE_BITS);
    uint32_t PA = VA - (entry_num * ONE_MB);

    uint64_t entry =
        create_mmu_entry(1, 0, 0, 0, VA, 0, 0, 0, 1, 0x3, PA, PAGE_4K);

    set_exception_vector_bit(my_exceptions, cause);

    switch (cause) {
    case HEX_CAUSE_TLBMISSX_NORMAL:
        tlbw(entry, entry_num);
        break;
    default:
        do_coredump();
        break;
    }
}

static inline void install_my_event_vectors(void)
{
    old_evb = getevb();
    setevb(&my_event_vectors);
}

#define MAKE_GOTO(name) \
void goto_##name(void) \
{ \
    asm volatile("r0 = ##" #name "\n\t" \
                 "jumpr r0\n\t" \
                 : : : "r0"); \
}

#define MAKE_ERR_HANDLER(name, helper_fn) \
    MY_EVENT_HANDLE(name, helper_fn) \
    MAKE_GOTO(name)

#define INSTALL_ERR_HANDLER(name) { \
    /*
     * Install our own privelege exception handler.
     * The normal behavior is to coredump
     * Read and decode the jump displacemnts from evb
     * ASSUME negative displacement which is the standard.
     */ \
    uint32_t *evb_err = getevb() + 2; \
    uint32_t err_distance = -(0xfe000000 | *evb_err) << 1; \
    uint32_t err_handler = (uint32_t)evb_err - err_distance; \
    memcpy((void *)err_handler, goto_##name, 12); \
} while (0)

static inline void remove_trans(int index)
{
    uint64_t entry = tlbr(index);
    SET_FIELD(entry, PTE_V, 0);
    tlbw(entry, index);
}

static inline void clear_overlapping_entry(unsigned int asid, uint32_t va)
{
    int32_t index = tlbp(asid, va);
    if (index != TLB_NOT_FOUND) {
        remove_trans(index);
    }
}

static void add_trans(int index, uint32_t va, uint64_t pa,
                      PageSize page_size, uint8_t xwru,
                      unsigned int asid, uint8_t V, uint8_t G)
{
    if (V) {
        clear_overlapping_entry(asid, va);
    }
    assert(!add_translation_extended(index, (void *)va, pa, page_size,
                                     xwru, 0, asid, 0,
                                     ((V & 1) << 1) | (G & 1)));
}

#endif
