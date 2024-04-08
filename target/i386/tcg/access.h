/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Access guest memory in blocks. */

#ifndef X86_TCG_ACCESS_H
#define X86_TCG_ACCESS_H

/* An access covers at most sizeof(X86XSaveArea), at most 2 pages. */
typedef struct X86Access {
    target_ulong vaddr;
    void *haddr1;
    void *haddr2;
    uint16_t size;
    uint16_t size1;
    /*
     * If we can't access the host page directly, we'll have to do I/O access
     * via ld/st helpers. These are internal details, so we store the rest
     * to do the access here instead of passing it around in the helpers.
     */
    int mmu_idx;
    CPUX86State *env;
    uintptr_t ra;
} X86Access;

void access_prepare_mmu(X86Access *ret, CPUX86State *env,
                        vaddr vaddr, unsigned size,
                        MMUAccessType type, int mmu_idx, uintptr_t ra);
void access_prepare(X86Access *ret, CPUX86State *env, vaddr vaddr,
                    unsigned size, MMUAccessType type, uintptr_t ra);

uint8_t  access_ldb(X86Access *ac, vaddr addr);
uint16_t access_ldw(X86Access *ac, vaddr addr);
uint32_t access_ldl(X86Access *ac, vaddr addr);
uint64_t access_ldq(X86Access *ac, vaddr addr);

void access_stb(X86Access *ac, vaddr addr, uint8_t val);
void access_stw(X86Access *ac, vaddr addr, uint16_t val);
void access_stl(X86Access *ac, vaddr addr, uint32_t val);
void access_stq(X86Access *ac, vaddr addr, uint64_t val);

#endif
