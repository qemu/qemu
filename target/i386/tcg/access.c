/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Access guest memory in blocks. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/target_page.h"
#include "access.h"


void access_prepare_mmu(X86Access *ret, CPUX86State *env,
                        vaddr vaddr, unsigned size,
                        MMUAccessType type, int mmu_idx, uintptr_t ra)
{
    int size1, size2;
    void *haddr1, *haddr2;

    assert(size > 0 && size <= TARGET_PAGE_SIZE);

    size1 = MIN(size, -(vaddr | TARGET_PAGE_MASK)),
    size2 = size - size1;

    memset(ret, 0, sizeof(*ret));
    ret->vaddr = vaddr;
    ret->size = size;
    ret->size1 = size1;
    ret->mmu_idx = mmu_idx;
    ret->env = env;
    ret->ra = ra;

    haddr1 = probe_access(env, vaddr, size1, type, mmu_idx, ra);
    ret->haddr1 = haddr1;

    if (unlikely(size2)) {
        haddr2 = probe_access(env, vaddr + size1, size2, type, mmu_idx, ra);
        if (haddr2 == haddr1 + size1) {
            ret->size1 = size;
        } else {
#ifdef CONFIG_USER_ONLY
            g_assert_not_reached();
#else
            ret->haddr2 = haddr2;
#endif
        }
    }
}

void access_prepare(X86Access *ret, CPUX86State *env, vaddr vaddr,
                    unsigned size, MMUAccessType type, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);
    access_prepare_mmu(ret, env, vaddr, size, type, mmu_idx, ra);
}

static void *access_ptr(X86Access *ac, vaddr addr, unsigned len)
{
    vaddr offset = addr - ac->vaddr;

    assert(addr >= ac->vaddr);

    /* No haddr means probe_access wants to force slow path */
    if (!ac->haddr1) {
        return NULL;
    }

#ifdef CONFIG_USER_ONLY
    assert(offset <= ac->size1 - len);
    return ac->haddr1 + offset;
#else
    if (likely(offset <= ac->size1 - len)) {
        return ac->haddr1 + offset;
    }
    assert(offset <= ac->size - len);
    /*
     * If the address is not naturally aligned, it might span both pages.
     * Only return ac->haddr2 if the area is entirely within the second page,
     * otherwise fall back to slow accesses.
     */
    if (likely(offset >= ac->size1)) {
        return ac->haddr2 + (offset - ac->size1);
    }
    return NULL;
#endif
}

uint8_t access_ldb(X86Access *ac, vaddr addr)
{
    void *p = access_ptr(ac, addr, sizeof(uint8_t));

    if (likely(p)) {
        return ldub_p(p);
    }
    return cpu_ldub_mmuidx_ra(ac->env, addr, ac->mmu_idx, ac->ra);
}

uint16_t access_ldw(X86Access *ac, vaddr addr)
{
    void *p = access_ptr(ac, addr, sizeof(uint16_t));

    if (likely(p)) {
        return lduw_le_p(p);
    }
    return cpu_lduw_le_mmuidx_ra(ac->env, addr, ac->mmu_idx, ac->ra);
}

uint32_t access_ldl(X86Access *ac, vaddr addr)
{
    void *p = access_ptr(ac, addr, sizeof(uint32_t));

    if (likely(p)) {
        return ldl_le_p(p);
    }
    return cpu_ldl_le_mmuidx_ra(ac->env, addr, ac->mmu_idx, ac->ra);
}

uint64_t access_ldq(X86Access *ac, vaddr addr)
{
    void *p = access_ptr(ac, addr, sizeof(uint64_t));

    if (likely(p)) {
        return ldq_le_p(p);
    }
    return cpu_ldq_le_mmuidx_ra(ac->env, addr, ac->mmu_idx, ac->ra);
}

void access_stb(X86Access *ac, vaddr addr, uint8_t val)
{
    void *p = access_ptr(ac, addr, sizeof(uint8_t));

    if (likely(p)) {
        stb_p(p, val);
    } else {
        cpu_stb_mmuidx_ra(ac->env, addr, val, ac->mmu_idx, ac->ra);
    }
}

void access_stw(X86Access *ac, vaddr addr, uint16_t val)
{
    void *p = access_ptr(ac, addr, sizeof(uint16_t));

    if (likely(p)) {
        stw_le_p(p, val);
    } else {
        cpu_stw_le_mmuidx_ra(ac->env, addr, val, ac->mmu_idx, ac->ra);
    }
}

void access_stl(X86Access *ac, vaddr addr, uint32_t val)
{
    void *p = access_ptr(ac, addr, sizeof(uint32_t));

    if (likely(p)) {
        stl_le_p(p, val);
    } else {
        cpu_stl_le_mmuidx_ra(ac->env, addr, val, ac->mmu_idx, ac->ra);
    }
}

void access_stq(X86Access *ac, vaddr addr, uint64_t val)
{
    void *p = access_ptr(ac, addr, sizeof(uint64_t));

    if (likely(p)) {
        stq_le_p(p, val);
    } else {
        cpu_stq_le_mmuidx_ra(ac->env, addr, val, ac->mmu_idx, ac->ra);
    }
}
