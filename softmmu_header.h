/*
 *  Software MMU support
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#if DATA_SIZE == 8
#define SUFFIX q
#define DATA_TYPE uint64_t
#elif DATA_SIZE == 4
#define SUFFIX l
#define DATA_TYPE uint32_t
#elif DATA_SIZE == 2
#define SUFFIX w
#define DATA_TYPE uint16_t
#define DATA_STYPE int16_t
#elif DATA_SIZE == 1
#define SUFFIX b
#define DATA_TYPE uint8_t
#define DATA_STYPE int8_t
#else
#error unsupported data size
#endif

#if MEMUSER == 0
#define MEMSUFFIX _kernel
#else
#define MEMSUFFIX _user
#endif

#if DATA_SIZE == 8
#define RES_TYPE uint64_t
#else
#define RES_TYPE int
#endif


#if MEMUSER == 0
DATA_TYPE __attribute((regparm(1))) glue(glue(__ld, SUFFIX), _mmu)(unsigned long addr);
void __attribute((regparm(2))) glue(glue(__st, SUFFIX), _mmu)(unsigned long addr, DATA_TYPE v);
#endif

static inline int glue(glue(ldu, SUFFIX), MEMSUFFIX)(void *ptr)
{
    int index;
    RES_TYPE res;
    unsigned long addr, physaddr;
    addr = (unsigned long)ptr;
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    if (__builtin_expect(env->tlb_read[MEMUSER][index].address != 
                         (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))), 0)) {
        res = glue(glue(__ld, SUFFIX), _mmu)(addr);
    } else {
        physaddr = addr + env->tlb_read[MEMUSER][index].addend;
        res = glue(glue(ldu, SUFFIX), _raw)((uint8_t *)physaddr);
    }
    return res;
}

#if DATA_SIZE <= 2
static inline int glue(glue(lds, SUFFIX), MEMSUFFIX)(void *ptr)
{
    int res, index;
    unsigned long addr, physaddr;
    addr = (unsigned long)ptr;
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    if (__builtin_expect(env->tlb_read[MEMUSER][index].address != 
                         (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))), 0)) {
        res = (DATA_STYPE)glue(glue(__ld, SUFFIX), _mmu)(addr);
    } else {
        physaddr = addr + env->tlb_read[MEMUSER][index].addend;
        res = glue(glue(lds, SUFFIX), _raw)((uint8_t *)physaddr);
    }
    return res;
}
#endif

static inline void glue(glue(st, SUFFIX), MEMSUFFIX)(void *ptr, RES_TYPE v)
{
    int index;
    unsigned long addr, physaddr;
    addr = (unsigned long)ptr;
    index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    if (__builtin_expect(env->tlb_write[MEMUSER][index].address != 
                         (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))), 0)) {
        glue(glue(__st, SUFFIX), _mmu)(addr, v);
    } else {
        physaddr = addr + env->tlb_write[MEMUSER][index].addend;
        glue(glue(st, SUFFIX), _raw)((uint8_t *)physaddr, v);
    }
}

#undef RES_TYPE
#undef DATA_TYPE
#undef DATA_STYPE
#undef SUFFIX
#undef DATA_SIZE
#undef MEMSUFFIX
