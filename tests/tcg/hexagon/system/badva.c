/*
 *  Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "crt0/hexagon_standalone.h"

#define DEBUG 0

int err;
#include "../hex_test.h"

/* volatile because it is written through different MMU mappings */
typedef volatile int mmu_variable;
mmu_variable data0 = 0xdeadbeef;
mmu_variable data1 = 0xabcdef01;

#define ONE_MB (1 << 20)
#define INVALID_BADVA 0xbadabada

static uint32_t read_badva(void)
{
    uint32_t ret;
    __asm__ __volatile__("%0 = badva\n\t" : "=r"(ret));
    return ret;
}

static uint32_t read_badva0(void)
{
    uint32_t ret;
    __asm__ __volatile__("%0 = badva0\n\t" : "=r"(ret));
    return ret;
}

static uint32_t read_badva1(void)
{
    uint32_t ret;
    __asm__ __volatile__("%0 = badva1\n\t" : "=r"(ret));
    return ret;
}

static uint32_t read_ssr(void)
{
    uint32_t ret;
    __asm__ __volatile__("%0 = ssr\n\t" : "=r"(ret));
    return ret;
}

static void write_badva0(uint32_t val)
{
    __asm__ __volatile__("badva0=%0;" : : "r"(val));
    return;
}

static void write_badva1(uint32_t val)
{
    __asm__ __volatile__("badva1=%0;" : : "r"(val));
    return;
}

#define SSR_V0_BIT 20
#define SSR_V1_BIT 21
#define SSR_BVS_BIT 21

static uint32_t read_ssr_v0(void)
{
    return (read_ssr() >> SSR_V0_BIT) & 0x1;
}

static uint32_t read_ssr_v1(void)
{
    return (read_ssr() >> SSR_V1_BIT) & 0x1;
}

static uint32_t read_ssr_bvs(void)
{
    return (read_ssr() >> SSR_BVS_BIT) & 0x1;
}

static void dual_store(mmu_variable *p, mmu_variable *q, uint32_t pval,
                       uint32_t qval)
{
#if DEBUG
    printf("dual_store:\t0x%p, 0x%p, 0x%lx, 0x%lx\n", p, q, pval, qval);
#endif

    __asm__ __volatile__("r6 = #0\n\t"
                         "badva0 = r6\n\t"
                         "badva1 = r6\n\t"
                         "r6 = ssr\n\t"
                         "r6 = clrbit(r6, #%4) // V0\n\t"
                         "r6 = clrbit(r6, #%5) // V1\n\t"
                         "r6 = clrbit(r6, #%6) // BVS\n\t"
                         "ssr = r6\n\t"
                         "{\n\t"
                         "    memw(%0) = %2    // slot 1\n\t"
                         "    memw(%1) = %3    // slot 0\n\t"
                         "}\n\t"
                         : "=m"(*p), "=m"(*q)
                         : "r"(pval), "r"(qval), "i"(SSR_V0_BIT),
                           "i"(SSR_V1_BIT), "i"(SSR_BVS_BIT)
                         : "r6");
}

static void dual_load(mmu_variable *p, mmu_variable *q, uint32_t *pval,
                      uint32_t *qval)
{
    uint32_t val0, val1;

#if DEBUG
    printf("dual_load:\t0x%p, 0x%p\n", p, q);
#endif

    __asm__ __volatile__("r6 = #0\n\t"
                         "badva0 = r6\n\t"
                         "badva1 = r6\n\t"
                         "r6 = ssr\n\t"
                         "r6 = clrbit(r6, #%4) // V0\n\t"
                         "r6 = clrbit(r6, #%5) // V1\n\t"
                         "r6 = clrbit(r6, #%6) // BVS\n\t"
                         "ssr = r6\n\t"
                         "{\n\t"
                         "    %1 = memw(%3)    // slot 1\n\t"
                         "    %0 = memw(%2)    // slot 0\n\t"
                         "}\n\t"
                         : "=r"(val0), "=r"(val1)
                         : "m"(*p), "m"(*q), "i"(SSR_V0_BIT), "i"(SSR_V1_BIT),
                           "i"(SSR_BVS_BIT)
                         : "r6");

#if DEBUG
    printf("\t\t0x%lx, 0x%lx\n", val0, val1);
#endif

    *pval = val0;
    *qval = val1;
}

static void load_store(mmu_variable *p, mmu_variable *q, uint32_t *pval,
                       uint32_t qval)
{
    uint32_t val;

#if DEBUG
    printf("load_store:\t0x%p, 0x%p, 0x%lx\n", p, q, qval);
#endif

    __asm__ __volatile__("r6 = #0\n\t"
                         "badva0 = r6\n\t"
                         "badva1 = r6\n\t"
                         "r6 = ssr\n\t"
                         "r6 = clrbit(r6, #%4) // V0\n\t"
                         "r6 = clrbit(r6, #%5) // V1\n\t"
                         "r6 = clrbit(r6, #%6) // BVS\n\t"
                         "ssr = r6\n\t"
                         "{\n\t"
                         "    %0 = memw(%2)    // slot 1\n\t"
                         "    memw(%1) = %3    // slot 0\n\t"
                         "}\n\t"
                         : "=r"(val), "=m"(*q)
                         : "m"(*p), "r"(qval), "i"(SSR_V0_BIT), "i"(SSR_V1_BIT),
                           "i"(SSR_BVS_BIT)
                         : "r6");

#if DEBUG
    printf("\t\t0x%lx\n", val);
#endif

    *pval = val;
}

enum {
    TLB_U = (1 << 0),
    TLB_R = (1 << 1),
    TLB_W = (1 << 2),
    TLB_X = (1 << 3),
};

uint32_t add_trans_pgsize(uint32_t page_size_bits)
{
    switch (page_size_bits) {
    case 12: /* 4KB   */
        return 1;
    case 14: /* 16KB  */
        return 2;
    case 16: /* 64KB  */
        return 4;
    case 18: /* 256KB */
        return 8;
    case 20: /* 1MB   */
        return 16;
    case 22: /* 4MB   */
        return 32;
    case 24: /* 16MB  */
        return 64;
    default:
        return 1;
    }
}

int mb_counter = 1;

static mmu_variable *map_data_address(mmu_variable *p, uint32_t data_offset)
{
    uint32_t page_size_bits = 12;
    uint32_t page_size = 1 << page_size_bits;
    uint32_t page_align = ~(page_size - 1);

    uint32_t data_addr = (uint32_t)p;
    uint32_t data_page = data_addr & page_align;

    uint32_t new_data_page = data_page + data_offset;
    uint32_t read_data_addr = data_addr + data_offset;
    unsigned int data_perm = TLB_X | TLB_W | TLB_U;
    add_translation((void *)new_data_page, (void *)data_page, 0);

    return (mmu_variable *)read_data_addr;
}

static void test_dual_store(void)
{
    data0 = 0x12345678;
    data1 = 0x87654321;

    mmu_variable *new_data0 = map_data_address(&data0, mb_counter * ONE_MB);
    mb_counter++;
    mmu_variable *new_data1 = map_data_address(&data1, mb_counter * ONE_MB);
    mb_counter++;

    dual_store(new_data0, new_data1, 0x1, 0x2);
    if (read_badva() == (uint32_t)new_data0) {
        check32(read_badva0(), (uint32_t)new_data0);
        check32(read_badva1(), INVALID_BADVA);
        check32(read_ssr_v0(), 1);
        check32(read_ssr_v1(), 0);
        check32(read_ssr_bvs(), 0);
    } else if (read_badva() == (uint32_t)new_data1) {
        check32(read_badva0(), INVALID_BADVA);
        check32(read_badva1(), (uint32_t)new_data1);
        check32(read_ssr_v0(), 0);
        check32(read_ssr_v1(), 1);
        check32(read_ssr_bvs(), 1);
    } else {
        /* Something went wrong! */
        check32(0, 1);
    }
    check32(data0, 0x1);
    check32(data1, 0x2);
}

static void test_dual_load(void)
{
    uint32_t val0, val1;

    data0 = 0xaabbccdd;
    data1 = 0xeeff0011;

    mmu_variable *new_data0 = map_data_address(&data0, mb_counter * ONE_MB);
    mb_counter++;
    mmu_variable *new_data1 = map_data_address(&data1, mb_counter * ONE_MB);
    mb_counter++;

    dual_load(new_data0, new_data1, &val0, &val1);
    if (read_badva() == (uint32_t)new_data0) {
        check32(read_badva0(), (uint32_t)new_data0);
        check32(read_badva1(), INVALID_BADVA);
        check32(read_ssr_v0(), 1);
        check32(read_ssr_v1(), 0);
        check32(read_ssr_bvs(), 0);
    } else if (read_badva() == (uint32_t)new_data1) {
        check32(read_badva0(), INVALID_BADVA);
        check32(read_badva1(), (uint32_t)new_data1);
        check32(read_ssr_v0(), 0);
        check32(read_ssr_v1(), 1);
        check32(read_ssr_bvs(), 1);
    } else {
        /* Something went wrong! */
        check32(0, 1);
    }
    check32(val0, 0xaabbccdd);
    check32(val1, 0xeeff0011);
}

static void test_load_store(void)
{
    uint32_t val;

    data0 = 0x11223344;
    data1 = 0x55667788;

    mmu_variable *new_data0 = map_data_address(&data0, mb_counter * ONE_MB);
    mb_counter++;
    mmu_variable *new_data1 = map_data_address(&data1, mb_counter * ONE_MB);
    mb_counter++;

    load_store(new_data0, new_data1, &val, 0x123);
    if (read_badva() == (uint32_t)new_data1) {
        check32(read_badva0(), (uint32_t)new_data1);
        check32(read_badva1(), INVALID_BADVA);
        check32(read_ssr_v0(), 1);
        check32(read_ssr_v1(), 0);
        check32(read_ssr_bvs(), 0);
    } else if (read_badva() == (uint32_t)new_data0) {
        check32(read_badva0(), INVALID_BADVA);
        check32(read_badva1(), (uint32_t)new_data0);
        check32(read_ssr_v0(), 0);
        check32(read_ssr_v1(), 1);
        check32(read_ssr_bvs(), 1);
    } else {
        /* Something went wrong! */
        check32(0, 1);
    }
    check32(val, 0x11223344);
    check32(data1, 0x123);
}
static void test_badva_write(void)
{
    uint32_t va = 0x11223344;
    write_badva0(va);
    check32(read_badva(), va);
}

int main()
{
    puts("Hexagon badva test");

    test_dual_store();
    test_dual_load();
    test_load_store();
    test_badva_write();

    printf("%s\n", ((err) ? "FAIL" : "PASS"));
    return err;
}
