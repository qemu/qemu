/*
 * dyngen helpers
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

int __op_param1, __op_param2, __op_param3;
#if defined(__sparc__) || defined(__arm__)
  void __op_gen_label1(){}
  void __op_gen_label2(){}
  void __op_gen_label3(){}
#else
  int __op_gen_label1, __op_gen_label2, __op_gen_label3;
#endif
int __op_jmp0, __op_jmp1, __op_jmp2, __op_jmp3;

#if defined(__i386__) || defined(__x86_64__) || defined(__s390__)
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
}
#elif defined(__ia64__)
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    while (start < stop) {
	asm volatile ("fc %0" :: "r"(start));
	start += 32;
    }
    asm volatile (";;sync.i;;srlz.i;;");
}
#elif defined(__powerpc__)

#define MIN_CACHE_LINE_SIZE 8 /* conservative value */

static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    unsigned long p;

    start &= ~(MIN_CACHE_LINE_SIZE - 1);
    stop = (stop + MIN_CACHE_LINE_SIZE - 1) & ~(MIN_CACHE_LINE_SIZE - 1);

    for (p = start; p < stop; p += MIN_CACHE_LINE_SIZE) {
        asm volatile ("dcbst 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");
    for (p = start; p < stop; p += MIN_CACHE_LINE_SIZE) {
        asm volatile ("icbi 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");
    asm volatile ("isync" : : : "memory");
}
#elif defined(__alpha__)
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    asm ("imb");
}
#elif defined(__sparc__)
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
	unsigned long p;

	p = start & ~(8UL - 1UL);
	stop = (stop + (8UL - 1UL)) & ~(8UL - 1UL);

	for (; p < stop; p += 8)
		__asm__ __volatile__("flush\t%0" : : "r" (p));
}
#elif defined(__arm__)
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    register unsigned long _beg __asm ("a1") = start;
    register unsigned long _end __asm ("a2") = stop;
    register unsigned long _flg __asm ("a3") = 0;
    __asm __volatile__ ("swi 0x9f0002" : : "r" (_beg), "r" (_end), "r" (_flg));
}
#elif defined(__mc68000)

# include <asm/cachectl.h>
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    cacheflush(start,FLUSH_SCOPE_LINE,FLUSH_CACHE_BOTH,stop-start+16);
}
#elif defined(__mips__)

#include <sys/cachectl.h>
static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
    _flush_cache ((void *)start, stop - start, BCACHE);
}
#else
#error unsupported CPU
#endif

#ifdef __alpha__

register int gp asm("$29");

static inline void immediate_ldah(void *p, int val) {
    uint32_t *dest = p;
    long high = ((val >> 16) + ((val >> 15) & 1)) & 0xffff;

    *dest &= ~0xffff;
    *dest |= high;
    *dest |= 31 << 16;
}
static inline void immediate_lda(void *dest, int val) {
    *(uint16_t *) dest = val;
}
void fix_bsr(void *p, int offset) {
    uint32_t *dest = p;
    *dest &= ~((1 << 21) - 1);
    *dest |= (offset >> 2) & ((1 << 21) - 1);
}

#endif /* __alpha__ */

#ifdef __arm__

#define ARM_LDR_TABLE_SIZE 1024

typedef struct LDREntry {
    uint8_t *ptr;
    uint32_t *data_ptr;
    unsigned type:2;
} LDREntry;

static LDREntry arm_ldr_table[1024];
static uint32_t arm_data_table[ARM_LDR_TABLE_SIZE];

extern char exec_loop;

static inline void arm_reloc_pc24(uint32_t *ptr, uint32_t insn, int val)
{
    *ptr = (insn & ~0xffffff) | ((insn + ((val - (int)ptr) >> 2)) & 0xffffff);
}

static uint8_t *arm_flush_ldr(uint8_t *gen_code_ptr,
                              LDREntry *ldr_start, LDREntry *ldr_end,
                              uint32_t *data_start, uint32_t *data_end,
                              int gen_jmp)
{
    LDREntry *le;
    uint32_t *ptr;
    int offset, data_size, target;
    uint8_t *data_ptr;
    uint32_t insn;
    uint32_t mask;

    data_size = (data_end - data_start) << 2;

    if (gen_jmp) {
        /* generate branch to skip the data */
        if (data_size == 0)
            return gen_code_ptr;
        target = (long)gen_code_ptr + data_size + 4;
        arm_reloc_pc24((uint32_t *)gen_code_ptr, 0xeafffffe, target);
        gen_code_ptr += 4;
    }

    /* copy the data */
    data_ptr = gen_code_ptr;
    memcpy(gen_code_ptr, data_start, data_size);
    gen_code_ptr += data_size;

    /* patch the ldr to point to the data */
    for(le = ldr_start; le < ldr_end; le++) {
        ptr = (uint32_t *)le->ptr;
        offset = ((unsigned long)(le->data_ptr) - (unsigned long)data_start) +
            (unsigned long)data_ptr -
            (unsigned long)ptr - 8;
        if (offset < 0) {
            fprintf(stderr, "Negative constant pool offset\n");
            abort();
        }
        switch (le->type) {
          case 0: /* ldr */
            mask = ~0x00800fff;
            if (offset >= 4096) {
                fprintf(stderr, "Bad ldr offset\n");
                abort();
            }
            break;
          case 1: /* ldc */
            mask = ~0x008000ff;
            if (offset >= 1024 ) {
                fprintf(stderr, "Bad ldc offset\n");
                abort();
            }
            break;
          case 2: /* add */
            mask = ~0xfff;
            if (offset >= 1024 ) {
                fprintf(stderr, "Bad add offset\n");
                abort();
            }
            break;
          default:
            fprintf(stderr, "Bad pc relative fixup\n");
            abort();
          }
        insn = *ptr & mask;
        switch (le->type) {
          case 0: /* ldr */
            insn |= offset | 0x00800000;
            break;
          case 1: /* ldc */
            insn |= (offset >> 2) | 0x00800000;
            break;
          case 2: /* add */
            insn |= (offset >> 2) | 0xf00;
            break;
          }
        *ptr = insn;
    }
    return gen_code_ptr;
}

#endif /* __arm__ */

#ifdef __ia64

/* Patch instruction with "val" where "mask" has 1 bits. */
static inline void ia64_patch (uint64_t insn_addr, uint64_t mask, uint64_t val)
{
    uint64_t m0, m1, v0, v1, b0, b1, *b = (uint64_t *) (insn_addr & -16);
#   define insn_mask ((1UL << 41) - 1)
    unsigned long shift;

    b0 = b[0]; b1 = b[1];
    shift = 5 + 41 * (insn_addr % 16); /* 5 template, 3 x 41-bit insns */
    if (shift >= 64) {
	m1 = mask << (shift - 64);
	v1 = val << (shift - 64);
    } else {
	m0 = mask << shift; m1 = mask >> (64 - shift);
	v0 = val  << shift; v1 = val >> (64 - shift);
	b[0] = (b0 & ~m0) | (v0 & m0);
    }
    b[1] = (b1 & ~m1) | (v1 & m1);
}

static inline void ia64_patch_imm60 (uint64_t insn_addr, uint64_t val)
{
	ia64_patch(insn_addr,
		   0x011ffffe000UL,
		   (  ((val & 0x0800000000000000UL) >> 23) /* bit 59 -> 36 */
		    | ((val & 0x00000000000fffffUL) << 13) /* bit 0 -> 13 */));
	ia64_patch(insn_addr - 1, 0x1fffffffffcUL, val >> 18);
}

static inline void ia64_imm64 (void *insn, uint64_t val)
{
    /* Ignore the slot number of the relocation; GCC and Intel
       toolchains differed for some time on whether IMM64 relocs are
       against slot 1 (Intel) or slot 2 (GCC).  */
    uint64_t insn_addr = (uint64_t) insn & ~3UL;

    ia64_patch(insn_addr + 2,
	       0x01fffefe000UL,
	       (  ((val & 0x8000000000000000UL) >> 27) /* bit 63 -> 36 */
		| ((val & 0x0000000000200000UL) <<  0) /* bit 21 -> 21 */
		| ((val & 0x00000000001f0000UL) <<  6) /* bit 16 -> 22 */
		| ((val & 0x000000000000ff80UL) << 20) /* bit  7 -> 27 */
		| ((val & 0x000000000000007fUL) << 13) /* bit  0 -> 13 */)
	    );
    ia64_patch(insn_addr + 1, 0x1ffffffffffUL, val >> 22);
}

static inline void ia64_imm60b (void *insn, uint64_t val)
{
    /* Ignore the slot number of the relocation; GCC and Intel
       toolchains differed for some time on whether IMM64 relocs are
       against slot 1 (Intel) or slot 2 (GCC).  */
    uint64_t insn_addr = (uint64_t) insn & ~3UL;

    if (val + ((uint64_t) 1 << 59) >= (1UL << 60))
	fprintf(stderr, "%s: value %ld out of IMM60 range\n",
		__FUNCTION__, (int64_t) val);
    ia64_patch_imm60(insn_addr + 2, val);
}

static inline void ia64_imm22 (void *insn, uint64_t val)
{
    if (val + (1 << 21) >= (1 << 22))
	fprintf(stderr, "%s: value %li out of IMM22 range\n",
		__FUNCTION__, (int64_t)val);
    ia64_patch((uint64_t) insn, 0x01fffcfe000UL,
	       (  ((val & 0x200000UL) << 15) /* bit 21 -> 36 */
		| ((val & 0x1f0000UL) <<  6) /* bit 16 -> 22 */
		| ((val & 0x00ff80UL) << 20) /* bit  7 -> 27 */
		| ((val & 0x00007fUL) << 13) /* bit  0 -> 13 */));
}

/* Like ia64_imm22(), but also clear bits 20-21.  For addl, this has
   the effect of turning "addl rX=imm22,rY" into "addl
   rX=imm22,r0".  */
static inline void ia64_imm22_r0 (void *insn, uint64_t val)
{
    if (val + (1 << 21) >= (1 << 22))
	fprintf(stderr, "%s: value %li out of IMM22 range\n",
		__FUNCTION__, (int64_t)val);
    ia64_patch((uint64_t) insn, 0x01fffcfe000UL | (0x3UL << 20),
	       (  ((val & 0x200000UL) << 15) /* bit 21 -> 36 */
		| ((val & 0x1f0000UL) <<  6) /* bit 16 -> 22 */
		| ((val & 0x00ff80UL) << 20) /* bit  7 -> 27 */
		| ((val & 0x00007fUL) << 13) /* bit  0 -> 13 */));
}

static inline void ia64_imm21b (void *insn, uint64_t val)
{
    if (val + (1 << 20) >= (1 << 21))
	fprintf(stderr, "%s: value %li out of IMM21b range\n",
		__FUNCTION__, (int64_t)val);
    ia64_patch((uint64_t) insn, 0x11ffffe000UL,
	       (  ((val & 0x100000UL) << 16) /* bit 20 -> 36 */
		| ((val & 0x0fffffUL) << 13) /* bit  0 -> 13 */));
}

static inline void ia64_nop_b (void *insn)
{
    ia64_patch((uint64_t) insn, (1UL << 41) - 1, 2UL << 37);
}

static inline void ia64_ldxmov(void *insn, uint64_t val)
{
    if (val + (1 << 21) < (1 << 22))
	ia64_patch((uint64_t) insn, 0x1fff80fe000UL, 8UL << 37);
}

static inline int ia64_patch_ltoff(void *insn, uint64_t val,
				   int relaxable)
{
    if (relaxable && (val + (1 << 21) < (1 << 22))) {
	ia64_imm22_r0(insn, val);
	return 0;
    }
    return 1;
}

struct ia64_fixup {
    struct ia64_fixup *next;
    void *addr;			/* address that needs to be patched */
    long value;
};

#define IA64_PLT(insn, plt_index)			\
do {							\
    struct ia64_fixup *fixup = alloca(sizeof(*fixup));	\
    fixup->next = plt_fixes;				\
    plt_fixes = fixup;					\
    fixup->addr = (insn);				\
    fixup->value = (plt_index);				\
    plt_offset[(plt_index)] = 1;			\
} while (0)

#define IA64_LTOFF(insn, val, relaxable)			\
do {								\
    if (ia64_patch_ltoff(insn, val, relaxable)) {		\
	struct ia64_fixup *fixup = alloca(sizeof(*fixup));	\
	fixup->next = ltoff_fixes;				\
	ltoff_fixes = fixup;					\
	fixup->addr = (insn);					\
	fixup->value = (val);					\
    }								\
} while (0)

static inline void ia64_apply_fixes (uint8_t **gen_code_pp,
				     struct ia64_fixup *ltoff_fixes,
				     uint64_t gp,
				     struct ia64_fixup *plt_fixes,
				     int num_plts,
				     unsigned long *plt_target,
				     unsigned int *plt_offset)
{
    static const uint8_t plt_bundle[] = {
	0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,	/* nop 0; movl r1=GP */
	0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x60,

	0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,	/* nop 0; brl IP */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0
    };
    uint8_t *gen_code_ptr = *gen_code_pp, *plt_start, *got_start;
    uint64_t *vp;
    struct ia64_fixup *fixup;
    unsigned int offset = 0;
    struct fdesc {
	long ip;
	long gp;
    } *fdesc;
    int i;

    if (plt_fixes) {
	plt_start = gen_code_ptr;

	for (i = 0; i < num_plts; ++i) {
	    if (plt_offset[i]) {
		plt_offset[i] = offset;
		offset += sizeof(plt_bundle);

		fdesc = (struct fdesc *) plt_target[i];
		memcpy(gen_code_ptr, plt_bundle, sizeof(plt_bundle));
		ia64_imm64 (gen_code_ptr + 0x02, fdesc->gp);
		ia64_imm60b(gen_code_ptr + 0x12,
			    (fdesc->ip - (long) (gen_code_ptr + 0x10)) >> 4);
		gen_code_ptr += sizeof(plt_bundle);
	    }
	}

	for (fixup = plt_fixes; fixup; fixup = fixup->next)
	    ia64_imm21b(fixup->addr,
			((long) plt_start + plt_offset[fixup->value]
			 - ((long) fixup->addr & ~0xf)) >> 4);
    }

    got_start = gen_code_ptr;

    /* First, create the GOT: */
    for (fixup = ltoff_fixes; fixup; fixup = fixup->next) {
	/* first check if we already have this value in the GOT: */
	for (vp = (uint64_t *) got_start; vp < (uint64_t *) gen_code_ptr; ++vp)
	    if (*vp == fixup->value)
		break;
	if (vp == (uint64_t *) gen_code_ptr) {
	    /* Nope, we need to put the value in the GOT: */
	    *vp = fixup->value;
	    gen_code_ptr += 8;
	}
	ia64_imm22(fixup->addr, (long) vp - gp);
    }
    /* Keep code ptr aligned. */
    if ((long) gen_code_ptr & 15)
	gen_code_ptr += 8;
    *gen_code_pp = gen_code_ptr;
}

#endif
