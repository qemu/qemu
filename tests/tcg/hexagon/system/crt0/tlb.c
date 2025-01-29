/*
 *  Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include "hexagon_standalone.h"

/*
 * The following 2 functions use global addressing mode
 * to avoid GP relative overflows.
 */
static inline uint32_t get_tlb_fixed_entries(void)
{
  uint32_t *addr;
  asm volatile ("%0=##_tlb_fixed_entries\n\t"
                : "=r"(addr));
  return *addr;
}
static inline uint32_t *get_UPTE_START(void)
{
  uint32_t addr;
  asm volatile ("%0=##__UPTE_START\n\t"
                : "=r"(addr));
  return (uint32_t *)addr;
}

static inline uint32_t get_ssr(void)
{
  uint32_t reg;
  asm volatile ("%0=ssr\n\t"
                : "=r"(reg));
  return reg;
}


static inline int64_t read_tlb_entry(int index)
{
  uint64_t reg;
  asm volatile ("%[reg]=tlbr(%[index])"
                : [reg] "=r" (reg)
                : [index] "r" (index));
  asm volatile ("isync");
  return reg;
}


static inline void write_tlb_entry(TLBEntry tlb, int index)
{
  uint64_t entry = tlb.raw;
  asm volatile ("tlblock\n"
                "tlbw(%[entry], %[index])\n"
                "isync\n"
                "tlbunlock\n"
                :
                : [entry] "r" (entry), [index] "r" (index));
}

static inline int32_t  tlb_probe(uint32_t va)
{
  uint32_t VirtualPageNumber = va >> 12;
  uint32_t ASID = (get_ssr() >> 8) & 0x7f;
  uint32_t probe = ((ASID << 20) | VirtualPageNumber) & 0x7ffffff;
  uint32_t result = 0;
  asm volatile ("%[result]=tlbp(%[probe])"
                : [result] "=r" (result)
                : [probe] "r" (probe));

  return result;
}


static inline void tlb_invalidate(uint32_t va)
{
  int entry = tlb_probe(va);
  if (entry == TLB_NOT_FOUND) {
    return;
  }

  TLBEntry tlb;
  tlb.raw = read_tlb_entry(entry);
  tlb.raw = tlb.raw & ~(1ull << 63); /* Clear the V bit. */
  write_tlb_entry(tlb, entry);
}


static inline TLBEntry basic_entry(uint32_t va, uint64_t pa, PageSize pagesize)
{
  TLBEntry T;
  uint64_t  PPN;
  T.raw = 0ull;
  T.VirtualPage = va >> 12;  /* 63-51 */
#if __HEXAGON_ARCH__ > 72
  T.PPN_EX = (pa & (3ull << 36)) >> 36;
#endif
  T.EP = (pa & (1ull << 35)) >> 35;
  PPN = pa >> 12ull;
  PPN = (PPN << 1ull) | pagesize;
  if (pagesize == 1) {
    T.S = 1;
  }
  T.raw |= PPN;
  return T;
}
/*
 * function: mkentry
 * description:
 *  - Given just a Physical Address (pa) and a Virtual Address (va)
 *  create a default entry.
 *  - A user wanting to change the cache attributes or permissions
 *  can do so prior to writing the entry.
 */
static TLBEntry mkentry(uint32_t va, uint64_t pa, PageSize pagesize)
{

  /* Make an entry and set some reasonable defaults */
  TLBEntry T = basic_entry(va, pa, pagesize);

  T.CacheAttr = 0x7;
  T.XWRU = 0x6;
  T.VG = 0x3;
  return T;
}

int add_translation_extended(int index, void *va, uint64_t pa,
                             unsigned int page_size, unsigned int xwru,
                             unsigned int cccc, unsigned int asid,
                             unsigned int aa, unsigned int vg)
{
  uint32_t num_entries = get_tlb_fixed_entries();

  if ((index < 1) || (index > (num_entries - 1))) {
    return -1;
  }

  tlb_invalidate((uint32_t)va);
  TLBEntry T;
  T = basic_entry((uint32_t)va, pa, page_size);
  T.ASID = ((uint64_t)asid & 0x7f);
  T.CacheAttr = ((uint64_t)cccc & 0xf);
  T.XWRU = ((uint64_t)xwru & 0xf);
  T.VG = ((uint64_t)vg & 0x3);
#if __HEXAGON_ARCH__ < 73
  T.raw |= ((uint64_t)aa & 0x3) << 59ull;
#endif
  write_tlb_entry(T, index);

  return 0;
}


void add_translation_fixed(int index, void *va, void *pa, int cccc,
                           int permissions)
{
  tlb_invalidate((uint32_t)va);
  add_translation_extended(index, va, (uint64_t)pa, PAGE_1M, permissions, cccc,
                           0, 0, 3);
}

/*
 * The following deals with the PTE software structure. The actual entry will
 * not be placed into the TLB until an address fault occurrs.
 */

typedef union {
    struct {
        uint16_t cache:4;
        uint16_t pa:12;
    };
    uint16_t PTE_raw;
} SMALL_PTE;

static SMALL_PTE *findPTEAddr(uint32_t va)
{
    uint32_t *PTE = get_UPTE_START();
    int index = va >> 20;
    return (SMALL_PTE *)PTE + index;
}
static SMALL_PTE findPTEValue(uint32_t va)
{
    SMALL_PTE *A = findPTEAddr(va);
    return *A;
}

/* This function adds a translation into the mapping table, see above */
/* Because we use 1MB pages, we only need to translate 12 bits.       */
/* We keep those 12 bits plus 4 bits (where we keep the C field,      */
/* see the System-level architecture spec on TLB entries) in          */
/* a 16-bit entry in the table.                                       */
/* We index into the table using the upper 12 bits.                   */
/* As a note, 2 bytes x 2^12 entries == 8KB table                     */
void add_translation(void *va, void *pa, int cccc)
{
    SMALL_PTE *S = findPTEAddr((uint32_t)va);
    S->pa = (uint32_t)pa >> 20;
    S->cache = cccc;
}
