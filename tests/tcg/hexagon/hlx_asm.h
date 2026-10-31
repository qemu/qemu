/*
 * HLX intrinsics encoded with .word directives
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Stand-ins for the HLX intrinsics of hlx_hexagon_protos.h, for toolchains
 * that cannot assemble v85 or HLX.  Every instruction is a pre-encoded
 * packet emitted with .word, like the v85 instructions of atomics.c.
 *
 * The compiler does not know about the L registers, so an HLX_Vector is
 * the 2KB contents of one and every intrinsic is self-contained: it copies
 * its operands to a 2KB-aligned staging area, loads them into fixed L
 * registers, executes the instruction and stores the result back to the
 * staging area.  The loads and stores therefore use HLX memory accesses
 * too, and the intrinsics are not reentrant.
 *
 * Register assignment:
 *   Lu = L0, Luu = L1:0   Lv = L2, Lvv = L3:2   Lx = L4, Lxx = L5:4
 *   Ld = L6, Ldd = L7:6   Rt = r10, Rtt = r13:12
 *   r0..r7 address the staging slots, r9 holds a user address.
 *
 * The packets were assembled with the Hexagon SDK 21.0.04 assembler
 *   hexagon-llvm-mc -mcpu=hexagonv85 -mattr=+hlx
 * from the "Assembly Syntax" of each intrinsic, shown in the comments.
 * Immediates are patched into the packet in the assembler.
 */

#ifndef HLX_ASM_H
#define HLX_ASM_H

#include <stdint.h>
#include <string.h>

#define HLX_ASM_VEC_BYTES 2048

typedef struct {
    uint16_t h[HLX_ASM_VEC_BYTES / sizeof(uint16_t)];
} HLX_Vector;

typedef struct {
    HLX_Vector lo;
    HLX_Vector hi;
} HLX_VectorPair;

/* Operand slots: u0 u1 v0 v1 x0 x1 d0 d1, then the scalar operands. */
struct hlx_scalars {
    uint32_t rt;
    uint32_t pad;
    uint64_t rtt;
    uint32_t addr;
};

#define HLX_ASM_SCALARS_OFFSET (8 * HLX_ASM_VEC_BYTES)

#define HLX_ASM_STAGE_BYTES (HLX_ASM_SCALARS_OFFSET + HLX_ASM_VEC_BYTES)

/*
 * The staging area is BSS by default.  Tests that need the HLX memory
 * accesses to go to VTCM point hlx_stage at a VTCM allocation instead.
 */
static uint8_t hlx_stage_bss[HLX_ASM_STAGE_BYTES]
    __attribute__((aligned(HLX_ASM_VEC_BYTES), unused));
static uint8_t *hlx_stage __attribute__((unused)) = hlx_stage_bss;

#define HLX_SCALARS \
    ((struct hlx_scalars *)(hlx_stage + HLX_ASM_SCALARS_OFFSET))

static inline void hlx_put_v(int slot, HLX_Vector v)
{
    memcpy(hlx_stage + slot * HLX_ASM_VEC_BYTES, &v, sizeof(v));
}

static inline void hlx_put_vp(int slot, HLX_VectorPair v)
{
    hlx_put_v(slot, v.lo);
    hlx_put_v(slot + 1, v.hi);
}

static inline HLX_Vector hlx_get_v(int slot)
{
    HLX_Vector v;

    memcpy(&v, hlx_stage + slot * HLX_ASM_VEC_BYTES, sizeof(v));
    return v;
}

static inline HLX_VectorPair hlx_get_vp(int slot)
{
    HLX_VectorPair v = { hlx_get_v(slot), hlx_get_v(slot + 1) };

    return v;
}

/* Place the low @bits of @imm at bit @pos of an instruction word. */
#define HLX_IMM(imm, bits, pos) (((imm) & ((1 << (bits)) - 1)) << (pos))

#define HLX_STR_(x) #x
#define HLX_STR(x) HLX_STR_(x)

#define HLX_CLOBBERS "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", \
                     "r9", "r10", "r12", "r13", "memory"

/* r0..r7 = staging slots, r9 = user address, r10 = Rt, r13:12 = Rtt */
#define HLX_PTRS \
    "r0 = %[stage]\n" \
    "r1 = add(r0, #2048)\n" \
    "r2 = add(r0, #4096)\n" \
    "r3 = add(r0, #6144)\n" \
    "r4 = add(r0, #8192)\n" \
    "r5 = add(r0, #10240)\n" \
    "r6 = add(r0, #12288)\n" \
    "r7 = add(r0, #14336)\n" \
    "r8 = add(r0, #16384)\n" \
    "r10 = memw(r8 + #0)\n" \
    "r13:12 = memd(r8 + #8)\n" \
    "r9 = memw(r8 + #16)\n"

/* Ln.tmp = mem(rn), copied to Ln within the packet */
#define HLX_LD0 \
    ".word 0xe5007820\n" \
    ".word 0x9800d000\n"

#define HLX_LD1 \
    ".word 0xe5007862\n" \
    ".word 0x9801d001\n"

#define HLX_LD2 \
    ".word 0xe50078a4\n" \
    ".word 0x9802d002\n"

#define HLX_LD3 \
    ".word 0xe50078e6\n" \
    ".word 0x9803d003\n"

#define HLX_LD4 \
    ".word 0xe5007928\n" \
    ".word 0x9804d004\n"

#define HLX_LD5 \
    ".word 0xe500796a\n" \
    ".word 0x9805d005\n"

/* mem(rn) = Ln.new */
#define HLX_ST4 \
    ".word 0xe5007928\n" \
    ".word 0xa484c004\n"

#define HLX_ST5 \
    ".word 0xe500796a\n" \
    ".word 0xa485c005\n"

#define HLX_ST6 \
    ".word 0xe50079ac\n" \
    ".word 0xa486c006\n"

#define HLX_ST7 \
    ".word 0xe50079ee\n" \
    ".word 0xa487c007\n"

/* L6.tmp = mem(r9), and mem(r9) = L0.new */
#define HLX_LD_MEM \
    ".word 0xe50079ac\n" \
    ".word 0x9809d006\n"

#define HLX_ST_MEM \
    ".word 0xe5007820\n" \
    ".word 0xa489c000\n"

/* L6.tmp = mem(Rs32) */
static inline HLX_Vector Q6_L_tmp_mem_A(void *base)
{
    HLX_SCALARS->addr = (uint32_t)(uintptr_t)base;
    asm volatile(HLX_PTRS HLX_LD_MEM HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* mem(Rs32) = Ku16.new */
static inline void Q6_mem_Ku16_new_AL(void *base, HLX_Vector ku)
{
    hlx_put_v(0, ku);
    HLX_SCALARS->addr = (uint32_t)(uintptr_t)base;
    asm volatile(HLX_PTRS HLX_LD0 HLX_ST_MEM
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
}

/* Ld16 = lo(Luu16) and Ld16 = hi(Luu16) are register renames */
static inline HLX_Vector Q6_L_lo_LW(HLX_VectorPair luu)
{
    return luu.lo;
}

static inline HLX_Vector Q6_L_hi_LW(HLX_VectorPair luu)
{
    return luu.hi;
}

/* Ld16.uh=abs(Lu16.h) */
static inline HLX_Vector Q6_Luh_abs_Lh(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe501e82c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=abs(Lu16.h):sat */
static inline HLX_Vector Q6_Lh_abs_Lh_sat(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe501e82d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uh=abs(Luu16.h) */
static inline HLX_VectorPair Q6_LWuh_abs_LWh(HLX_VectorPair luu)
{
    hlx_put_vp(0, luu);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xef00f02c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.h=abs(Luu16.h):sat */
static inline HLX_VectorPair Q6_LWh_abs_LWh_sat(HLX_VectorPair luu)
{
    hlx_put_vp(0, luu);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xef00f02d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=abs(Luu16.w) */
static inline HLX_VectorPair Q6_LWuw_abs_LWw(HLX_VectorPair luu)
{
    hlx_put_vp(0, luu);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xef00f00c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=abs(Luu16.w):sat */
static inline HLX_VectorPair Q6_LWw_abs_LWw_sat(HLX_VectorPair luu)
{
    hlx_put_vp(0, luu);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xef00f00d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.uh=absdiff(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Luh_absdiff_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04e40c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=absdiff(Lu16.uh,Lv16.uh) */
static inline HLX_Vector Q6_Luh_absdiff_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04e42c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uw=absdiff(Luu16.uw,Lvv16.uw) */
static inline HLX_VectorPair
Q6_LWuw_absdiff_LWuwLWuw(HLX_VectorPair luu,
                         HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04e42d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=absdiff(Luu16.w,Lvv16.w) */
static inline HLX_VectorPair
Q6_LWuw_absdiff_LWwLWw(HLX_VectorPair luu,
                       HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04e40d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=add(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lh_add_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504ec0c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=add(Lu16.h,Lv16.h):sat */
static inline HLX_Vector Q6_Lh_add_LhLh_sat(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504ec0d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.h=add(Luu16.h,Lvv16.h) */
static inline HLX_VectorPair
Q6_LWh_add_LWhLWh(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e80c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.h=add(Luu16.h,Lvv16.h):sat */
static inline HLX_VectorPair
Q6_LWh_add_LWhLWh_sat(HLX_VectorPair luu,
                      HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e80d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.uh=add(Lu16.uh,Lv16.uh):sat */
static inline HLX_Vector Q6_Luh_add_LuhLuh_sat(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504ec2c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=add(Lu16.h,Lv16.h):sat */
static inline HLX_Vector Q6_Luh_add_LhLh_sat(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504ec2d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uh=add(Luu16.h,Lvv16.h):sat */
static inline HLX_VectorPair
Q6_LWuh_add_LWhLWh_sat(HLX_VectorPair luu,
                       HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e82d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uh=add(Luu16.uh,Lvv16.uh):sat */
static inline HLX_VectorPair
Q6_LWuh_add_LWuhLWuh_sat(HLX_VectorPair luu,
                         HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e82c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=add(Luu16.uw,Lvv16.uw):sat */
static inline HLX_VectorPair
Q6_LWuw_add_LWuwLWuw_sat(HLX_VectorPair luu,
                         HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e02c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=add(Luu16.w,Lvv16.w):sat */
static inline HLX_VectorPair
Q6_LWuw_add_LWwLWw_sat(HLX_VectorPair luu,
                       HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e02d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=add(Luu16.w,Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_add_LWwLWw(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e00c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=add(Luu16.w,Lvv16.w):sat */
static inline HLX_VectorPair
Q6_LWw_add_LWwLWw_sat(HLX_VectorPair luu,
                      HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e00d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=add(Lu16.uh,Lv16.uh) */
static inline HLX_VectorPair Q6_LWw_add_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe524e80c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=add(Luu16.w,Lv16.h) */
static inline HLX_VectorPair
Q6_LWw_add_LWwLh(HLX_VectorPair luu,
                 HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xe504fc2c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=add(Luu16.w,Lv16.uh) */
static inline HLX_VectorPair
Q6_LWw_add_LWwLuh(HLX_VectorPair luu,
                  HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xe504fc2d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16=and(Lu16,Lv16) */
static inline HLX_Vector Q6_L_and_LL(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504f40c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16=and(Luu16,Lvv16) */
static inline HLX_VectorPair
Q6_LW_and_LWLW(HLX_VectorPair luu,
               HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef24e00c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16=andn(Lu16,Lv16) */
static inline HLX_Vector Q6_L_andn_LL(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504f40d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16=andn(Luu16,Lvv16) */
static inline HLX_VectorPair
Q6_LW_andn_LWLW(HLX_VectorPair luu,
                HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef24e40c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=Lu16.h */
static inline HLX_Vector Q6_Lh_equals_Lh(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe500f82c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=Rt32.h */
static inline HLX_Vector Q6_Lh_equals_Rh(uint32_t rt)
{
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS
                 ".word 0xe52ae42c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.w=Rt32.w */
static inline HLX_VectorPair Q6_LWw_equals_Rw(uint32_t rt)
{
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS
                 ".word 0xe52ae42d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=#s5 */
#define Q6_Lh_equals_Is(is5) \
    ({ \
        asm volatile(HLX_PTRS \
                     ".word " HLX_STR(0xe520e82d | HLX_IMM(is5, 5, 16)) "\n" \
                     HLX_ST6 \
                     : : [stage] "r"(hlx_stage) : HLX_CLOBBERS); \
        hlx_get_v(6); \
    })

/* Ld16.uh=#u5 */
#define Q6_Luh_equals_I(iu5) \
    ({ \
        asm volatile(HLX_PTRS \
                     ".word " HLX_STR(0xe520e82c | HLX_IMM(iu5, 5, 16)) "\n" \
                     HLX_ST6 \
                     : : [stage] "r"(hlx_stage) : HLX_CLOBBERS); \
        hlx_get_v(6); \
    })

/* Ld16.h=avg(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lh_avg_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04e80c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=avg(Lu16.h,Lv16.h):rnd */
static inline HLX_Vector Q6_Lh_avg_LhLh_rnd(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04ec0c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=avg(Lu16.uh,Lv16.uh) */
static inline HLX_Vector Q6_Luh_avg_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04e82c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=avg(Lu16.uh,Lv16.uh):rnd */
static inline HLX_Vector Q6_Luh_avg_LuhLuh_rnd(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04ec2c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uw=avg(Luu16.uw,Lvv16.uw) */
static inline HLX_VectorPair
Q6_LWuw_avg_LWuwLWuw(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04e82d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=avg(Luu16.uw,Lvv16.uw):rnd */
static inline HLX_VectorPair
Q6_LWuw_avg_LWuwLWuw_rnd(HLX_VectorPair luu,
                         HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04ec2d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=avg(Luu16.w,Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_avg_LWwLWw(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04e80d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=avg(Luu16.w,Lvv16.w):rnd */
static inline HLX_VectorPair
Q6_LWw_avg_LWwLWw_rnd(HLX_VectorPair luu,
                      HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04ec0d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=cmp.gt(Lu16.h, Lv16.h) */
static inline HLX_Vector Q6_Lh_cmp_gt_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504e00d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.h=cmp.gt(Luu16.h, Lvv16.h) */
static inline HLX_VectorPair
Q6_LWh_cmp_gt_LWhLWh(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f42c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=cmp.gt(Lu16.uh, Lv16.uh) */
static inline HLX_Vector Q6_Lh_cmp_gt_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504e02c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.h=cmp.gt(Luu16.uh, Lvv16.uh) */
static inline HLX_VectorPair
Q6_LWh_cmp_gt_LWuhLWuh(HLX_VectorPair luu,
                       HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f42d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=cmp.gt(Luu16.uw, Lvv16.uw) */
static inline HLX_VectorPair
Q6_LWw_cmp_gt_LWuwLWuw(HLX_VectorPair luu,
                       HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f40c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=cmp.gt(Luu16.w, Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_cmp_gt_LWwLWw(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f40d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16=combine(Lu16,Lv16) */
static inline HLX_VectorPair Q6_LW_combine_LL(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xef24e02d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.b=packhi(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lb_packhi_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04f40d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.b=packlo(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lb_packlo_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04f40c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.h=unpackhi(Lu16.b) */
static inline HLX_VectorPair Q6_LWh_unpackhi_Lb(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed00f42d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.h=unpacklo(Lu16.b) */
static inline HLX_VectorPair Q6_LWh_unpacklo_Lb(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed00f42c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=zxt(Lu16.uh) */
static inline HLX_VectorPair Q6_LWuw_zxt_Luh(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe501e80c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=sxt(Lu16.h) */
static inline HLX_VectorPair Q6_LWw_sxt_Lh(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe501e80d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=lut4(Lu16.uh,Rtt32.h) */
static inline HLX_Vector Q6_Lh_lut4_LuhPh(HLX_Vector lu, uint64_t rtt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rtt = rtt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed0ce00c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=lut4(Lu16.uh,Rtt32.h):mask */
static inline HLX_Vector Q6_Lh_lut4_LuhPh_mask(HLX_Vector lu, uint64_t rtt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rtt = rtt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed0ce02d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.w=lut4(Lu16.uh,Rtt32.uh):>>1 */
static inline HLX_VectorPair
Q6_LWw_lut4_LuhPuh_rs1(HLX_Vector lu,
                       uint64_t rtt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rtt = rtt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed0ce00d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=lut4(Lu16.uh,Rtt32.uh):>>1:neg */
static inline HLX_VectorPair
Q6_LWw_lut4_LuhPuh_rs1_neg(HLX_Vector lu,
                           uint64_t rtt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rtt = rtt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed0ce02c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=max(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lh_max_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504e40c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.h=max(Luu16.h,Lvv16.h) */
static inline HLX_VectorPair
Q6_LWh_max_LWhLWh(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f82c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.uh=max(Lu16.uh,Lv16.uh) */
static inline HLX_Vector Q6_Luh_max_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504e42c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uh=max(Luu16.uh,Lvv16.uh) */
static inline HLX_VectorPair
Q6_LWuh_max_LWuhLWuh(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f82d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=max(Luu16.uw,Lvv16.uw) */
static inline HLX_VectorPair
Q6_LWuw_max_LWuwLWuw(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04fc2d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=max(Luu16.w,Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_max_LWwLWw(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04fc2c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=min(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lh_min_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504e40d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.h=min(Luu16.h,Lvv16.h) */
static inline HLX_VectorPair
Q6_LWh_min_LWhLWh(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f80c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.uh=min(Lu16.uh,Lv16.uh) */
static inline HLX_Vector Q6_Luh_min_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504e42d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uh=min(Luu16.uh,Lvv16.uh) */
static inline HLX_VectorPair
Q6_LWuh_min_LWuhLWuh(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04f80d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=min(Luu16.uw,Lvv16.uw) */
static inline HLX_VectorPair
Q6_LWuw_min_LWuwLWuw(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04fc0d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=min(Luu16.w,Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_min_LWwLWw(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04fc0c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Lxx16.uw=add(mpy(Lu16.uh,hi(Lxx16).uh),Lvv16.w) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_hi_LuhLWuhLWw(HLX_Vector lu,
                              HLX_VectorPair lxx,
                              HLX_VectorPair lvv)
{
    hlx_put_v(0, lu);
    hlx_put_vp(4, lxx);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5 HLX_LD2 HLX_LD3
                 ".word 0xe7e4f409\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=mpy(Lu16.h,Lv16.h) */
static inline HLX_VectorPair
Q6_LWw_mpyacc_LhLh(HLX_VectorPair lxx,
                   HLX_Vector lu,
                   HLX_Vector lv)
{
    hlx_put_vp(4, lxx);
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0 HLX_LD2
                 ".word 0xe704e829\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.uw=add(mpy(Lu16.h,Lv16.uh),hi(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_hi_LhLuhLWuh(HLX_Vector lu,
                             HLX_Vector lv,
                             HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2 HLX_LD4 HLX_LD5
                 ".word 0xe704f029\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=mpy(Lu16.h,Lv16.uh) */
static inline HLX_VectorPair
Q6_LWw_mpyacc_LhLuh(HLX_VectorPair lxx,
                    HLX_Vector lu,
                    HLX_Vector lv)
{
    hlx_put_vp(4, lxx);
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0 HLX_LD2
                 ".word 0xe704e809\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w=add(mpy(Lu16.h,Lv16.uh),lo(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWw_add_mpy_lo_LhLuhLWuh(HLX_Vector lu,
                            HLX_Vector lv,
                            HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2 HLX_LD4 HLX_LD5
                 ".word 0xe704ec29\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=mpy(Lu16.uh,Lv16.uh) */
static inline HLX_VectorPair
Q6_LWw_mpyacc_LuhLuh(HLX_VectorPair lxx,
                     HLX_Vector lu,
                     HLX_Vector lv)
{
    hlx_put_vp(4, lxx);
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0 HLX_LD2
                 ".word 0xe704e428\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.uw=add(mpy(Lu16.uh,Lv16.uh),hi(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_hi_LuhLuhLWuh(HLX_Vector lu,
                              HLX_Vector lv,
                              HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2 HLX_LD4 HLX_LD5
                 ".word 0xe704f009\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.uw=add(mpy(Lu16.uh,Lv16.uh),lo(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_lo_LuhLuhLWuh(HLX_Vector lu,
                              HLX_Vector lv,
                              HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2 HLX_LD4 HLX_LD5
                 ".word 0xe704ec09\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w=add(mpy(Lu16.uh,hi(Lxx16).h),Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_add_mpy_hi_LuhLWhLWw(HLX_Vector lu,
                            HLX_VectorPair lxx,
                            HLX_VectorPair lvv)
{
    hlx_put_v(0, lu);
    hlx_put_vp(4, lxx);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5 HLX_LD2 HLX_LD3
                 ".word 0xe7e4f429\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.uw=add(mpy(Lu16.uh,hi(Lxx16).uh),Rt32.w) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_hi_LuhLWuhRw(HLX_Vector lu,
                             HLX_VectorPair lxx,
                             uint32_t rt)
{
    hlx_put_v(0, lu);
    hlx_put_vp(4, lxx);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5
                 ".word 0xe7eaf408\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=mpy(Lu16.h,Rt32.h) */
static inline HLX_VectorPair
Q6_LWw_mpyacc_LhRh(HLX_VectorPair lxx,
                   HLX_Vector lu,
                   uint32_t rt)
{
    hlx_put_vp(4, lxx);
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0
                 ".word 0xe70ae828\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.uw=add(mpy(Lu16.h,Rt32.uh),hi(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_hi_LhRuhLWuh(HLX_Vector lu,
                             uint32_t rt,
                             HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5
                 ".word 0xe70af028\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=mpy(Lu16.h,Rt32.uh) */
static inline HLX_VectorPair
Q6_LWw_mpyacc_LhRuh(HLX_VectorPair lxx,
                    HLX_Vector lu,
                    uint32_t rt)
{
    hlx_put_vp(4, lxx);
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0
                 ".word 0xe70ae808\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w=add(mpy(Lu16.h,Rt32.uh),lo(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWw_add_mpy_lo_LhRuhLWuh(HLX_Vector lu,
                            uint32_t rt,
                            HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5
                 ".word 0xe70aec28\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=mpy(Lu16.uh,Rt32.uh) */
static inline HLX_VectorPair
Q6_LWw_mpyacc_LuhRuh(HLX_VectorPair lxx,
                     HLX_Vector lu,
                     uint32_t rt)
{
    hlx_put_vp(4, lxx);
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0
                 ".word 0xe70ae429\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.uw=add(mpy(Lu16.uh,Rt32.uh),hi(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_hi_LuhRuhLWuh(HLX_Vector lu,
                              uint32_t rt,
                              HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5
                 ".word 0xe70af008\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.uw=add(mpy(Lu16.uh,Rt32.uh),lo(Lxx16).uh) */
static inline HLX_VectorPair
Q6_LWuw_add_mpy_lo_LuhRuhLWuh(HLX_Vector lu,
                              uint32_t rt,
                              HLX_VectorPair lxx)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    hlx_put_vp(4, lxx);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5
                 ".word 0xe70aec08\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w=add(mpy(Lu16.uh,hi(Lxx16).h),Rt32.w) */
static inline HLX_VectorPair
Q6_LWw_add_mpy_hi_LuhLWhRw(HLX_Vector lu,
                           HLX_VectorPair lxx,
                           uint32_t rt)
{
    hlx_put_v(0, lu);
    hlx_put_vp(4, lxx);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD4 HLX_LD5
                 ".word 0xe7eaf428\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Ldd16.uw=mpy(Lu16.uh,Lv16.uh) */
static inline HLX_VectorPair Q6_LWuw_mpy_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe704e00d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=mpy(Lu16.h,Lv16.h) */
static inline HLX_VectorPair Q6_LWw_mpy_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe704e40c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=mpy(Lu16.h,Lv16.uh) */
static inline HLX_VectorPair Q6_LWw_mpy_LhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe704e02d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=mpy(Lu16.uh,Rt32.uh) */
static inline HLX_VectorPair Q6_LWuw_mpy_LuhRuh(HLX_Vector lu, uint32_t rt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe70ae00c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=mpy(Lu16.h,Rt32.h) */
static inline HLX_VectorPair Q6_LWw_mpy_LhRh(HLX_Vector lu, uint32_t rt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe70ae40d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=mpy(Lu16.h,Rt32.uh) */
static inline HLX_VectorPair Q6_LWw_mpy_LhRuh(HLX_Vector lu, uint32_t rt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe70ae02c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Lx16.h=mux(Lx16.h,Lu16.h,Lv16.h) */
static inline HLX_Vector
Q6_Lh_mux_LhLhLh(HLX_Vector lx,
                 HLX_Vector lu,
                 HLX_Vector lv)
{
    hlx_put_v(4, lx);
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD0 HLX_LD2
                 ".word 0xe504e008\n"
                 HLX_ST4
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(4);
}

/* Ld16.h=navg(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lh_navg_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04f00c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=navg(Lu16.uh,Lv16.uh) */
static inline HLX_Vector Q6_Lh_navg_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed04f02c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.w=navg(Luu16.uw,Lvv16.uw) */
static inline HLX_VectorPair
Q6_LWw_navg_LWuwLWuw(HLX_VectorPair luu,
                     HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04f02d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=navg(Luu16.w,Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_navg_LWwLWw(HLX_VectorPair luu,
                   HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xed04f00d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16=not(Lu16) */
static inline HLX_Vector Q6_L_not_L(HLX_Vector lu)
{
    hlx_put_v(0, lu);
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xe500f42c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16=not(Luu16) */
static inline HLX_VectorPair Q6_LW_not_LW(HLX_VectorPair luu)
{
    hlx_put_vp(0, luu);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xef20e40d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16=or(Lu16,Lv16) */
static inline HLX_Vector Q6_L_or_LL(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504f80c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16=or(Luu16,Lvv16) */
static inline HLX_VectorPair
Q6_LW_or_LWLW(HLX_VectorPair luu,
              HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef24e00d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16=orn(Lu16,Lv16) */
static inline HLX_Vector Q6_L_orn_LL(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504f80d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16=orn(Luu16,Lvv16) */
static inline HLX_VectorPair
Q6_LW_orn_LWLW(HLX_VectorPair luu,
               HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef24e42c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=shr(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lh_shr_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed24e80c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=shr(Luu16.w,Lv16.h):rnd:sat */
static inline HLX_Vector
Q6_Lh_shr_LWwLh_rnd_sat(HLX_VectorPair luu,
                        HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24f40c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=shr(Luu16.w,Lv16.h):sat */
static inline HLX_Vector Q6_Lh_shr_LWwLh_sat(HLX_VectorPair luu, HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24f02c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=shr(Luu16.w,Lv16.uh) */
static inline HLX_Vector Q6_Lh_shr_LWwLuh(HLX_VectorPair luu, HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24f00c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Lu16.uh,Lv16.h) */
static inline HLX_Vector Q6_Luh_shr_LuhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xed24e82c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.uw,Lv16.h):rnd:sat */
static inline HLX_Vector
Q6_Luh_shr_LWuwLh_rnd_sat(HLX_VectorPair luu,
                          HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24f82c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.uw,Lv16.h):sat */
static inline HLX_Vector
Q6_Luh_shr_LWuwLh_sat(HLX_VectorPair luu,
                      HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24fc0c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.w,Lv16.h):rnd:sat */
static inline HLX_Vector
Q6_Luh_shr_LWwLh_rnd_sat(HLX_VectorPair luu,
                         HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24f80c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.w,Lv16.h):sat */
static inline HLX_Vector
Q6_Luh_shr_LWwLh_sat(HLX_VectorPair luu,
                     HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24f42c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uw=shr(Luu16.uw,Lv16.h) */
static inline HLX_VectorPair
Q6_LWuw_shr_LWuwLh(HLX_VectorPair luu,
                   HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24ec0c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=shr(Luu16.w,Lv16.h) */
static inline HLX_VectorPair
Q6_LWw_shr_LWwLh(HLX_VectorPair luu,
                 HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed24ec2c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Lx16.h+=shr(Lu16.h,Lv16.h) */
static inline HLX_Vector
Q6_Lh_shracc_LhLh(HLX_Vector lx,
                  HLX_Vector lu,
                  HLX_Vector lv)
{
    hlx_put_v(4, lx);
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD0 HLX_LD2
                 ".word 0xed04f828\n"
                 HLX_ST4
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(4);
}

/* Lx16.h+=shr(Lu16.uh,Lv16.h) */
static inline HLX_Vector
Q6_Lh_shracc_LuhLh(HLX_Vector lx,
                   HLX_Vector lu,
                   HLX_Vector lv)
{
    hlx_put_v(4, lx);
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD0 HLX_LD2
                 ".word 0xed04f808\n"
                 HLX_ST4
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(4);
}

/* Lxx16.w+=shr(Luu16.uw,Lv16.h) */
static inline HLX_VectorPair
Q6_LWw_shracc_LWuwLh(HLX_VectorPair lxx,
                     HLX_VectorPair luu,
                     HLX_Vector lv)
{
    hlx_put_vp(4, lxx);
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed04fc08\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=shr(Luu16.w,Lv16.h) */
static inline HLX_VectorPair
Q6_LWw_shracc_LWwLh(HLX_VectorPair lxx,
                    HLX_VectorPair luu,
                    HLX_Vector lv)
{
    hlx_put_vp(4, lxx);
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xed04fc28\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lx16.h+=shr(Lu16.h,Rt32.h) */
static inline HLX_Vector
Q6_Lh_shracc_LhRh(HLX_Vector lx,
                  HLX_Vector lu,
                  uint32_t rt)
{
    hlx_put_v(4, lx);
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD0
                 ".word 0xed0af829\n"
                 HLX_ST4
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(4);
}

/* Lx16.h+=shr(Lu16.uh,Rt32.h) */
static inline HLX_Vector
Q6_Lh_shracc_LuhRh(HLX_Vector lx,
                   HLX_Vector lu,
                   uint32_t rt)
{
    hlx_put_v(4, lx);
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD0
                 ".word 0xed0af809\n"
                 HLX_ST4
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(4);
}

/* Lxx16.w+=shr(Luu16.uw,Rt32.h) */
static inline HLX_VectorPair
Q6_LWw_shracc_LWuwRh(HLX_VectorPair lxx,
                     HLX_VectorPair luu,
                     uint32_t rt)
{
    hlx_put_vp(4, lxx);
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0 HLX_LD1
                 ".word 0xed0afc09\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Lxx16.w+=shr(Luu16.w,Rt32.h) */
static inline HLX_VectorPair
Q6_LWw_shracc_LWwRh(HLX_VectorPair lxx,
                    HLX_VectorPair luu,
                    uint32_t rt)
{
    hlx_put_vp(4, lxx);
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD4 HLX_LD5 HLX_LD0 HLX_LD1
                 ".word 0xed0afc29\n"
                 HLX_ST4 HLX_ST5
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(4);
}

/* Ld16.h=shr(Lu16.h,Rt32.h) */
static inline HLX_Vector Q6_Lh_shr_LhRh(HLX_Vector lu, uint32_t rt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed2ae80d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=shr(Luu16.w,Rt32.h):rnd:sat */
static inline HLX_Vector
Q6_Lh_shr_LWwRh_rnd_sat(HLX_VectorPair luu,
                        uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2af40d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=shr(Luu16.w,Rt32.h):sat */
static inline HLX_Vector Q6_Lh_shr_LWwRh_sat(HLX_VectorPair luu, uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2af02d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=shr(Luu16.w,Rt32.uh) */
static inline HLX_Vector Q6_Lh_shr_LWwRuh(HLX_VectorPair luu, uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2af00d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Lu16.uh,Rt32.h) */
static inline HLX_Vector Q6_Luh_shr_LuhRh(HLX_Vector lu, uint32_t rt)
{
    hlx_put_v(0, lu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0
                 ".word 0xed2ae82d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.uw,Rt32.h):rnd:sat */
static inline HLX_Vector
Q6_Luh_shr_LWuwRh_rnd_sat(HLX_VectorPair luu,
                          uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2af82d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.uw,Rt32.h):sat */
static inline HLX_Vector Q6_Luh_shr_LWuwRh_sat(HLX_VectorPair luu, uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2afc0d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.w,Rt32.h):rnd:sat */
static inline HLX_Vector
Q6_Luh_shr_LWwRh_rnd_sat(HLX_VectorPair luu,
                         uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2af80d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=shr(Luu16.w,Rt32.h):sat */
static inline HLX_Vector Q6_Luh_shr_LWwRh_sat(HLX_VectorPair luu, uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2af42d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uw=shr(Luu16.uw,Rt32.h) */
static inline HLX_VectorPair
Q6_LWuw_shr_LWuwRh(HLX_VectorPair luu,
                   uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2aec0d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=shr(Luu16.w,Rt32.h) */
static inline HLX_VectorPair Q6_LWw_shr_LWwRh(HLX_VectorPair luu, uint32_t rt)
{
    hlx_put_vp(0, luu);
    HLX_SCALARS->rt = rt;
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1
                 ".word 0xed2aec2d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=sub(Lu16.h,Lv16.h) */
static inline HLX_Vector Q6_Lh_sub_LhLh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504f00c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.h=sub(Lu16.h,Lv16.h):sat */
static inline HLX_Vector Q6_Lh_sub_LhLh_sat(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504f00d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.h=sub(Luu16.h,Lvv16.h) */
static inline HLX_VectorPair
Q6_LWh_sub_LWhLWh(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04ec0c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.h=sub(Luu16.h,Lvv16.h):sat */
static inline HLX_VectorPair
Q6_LWh_sub_LWhLWh_sat(HLX_VectorPair luu,
                      HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04ec0d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16.h=sub(ilog2(Lu16.h),#u4) */
#define Q6_Lh_sub_ilog2_LhI(lu, iu4) \
    ({ \
        hlx_put_v(0, lu); \
        asm volatile(HLX_PTRS HLX_LD0 \
                     ".word " HLX_STR(0xed20e40c | HLX_IMM(iu4, 4, 17)) "\n" \
                     HLX_ST6 \
                     : : [stage] "r"(hlx_stage) : HLX_CLOBBERS); \
        hlx_get_v(6); \
    })

/* Ld16.h=sub(ilog2(Lu16.uh),#u4) */
#define Q6_Lh_sub_ilog2_LuhI(lu, iu4) \
    ({ \
        hlx_put_v(0, lu); \
        asm volatile(HLX_PTRS HLX_LD0 \
                     ".word " HLX_STR(0xed20e40d | HLX_IMM(iu4, 4, 17)) "\n" \
                     HLX_ST6 \
                     : : [stage] "r"(hlx_stage) : HLX_CLOBBERS); \
        hlx_get_v(6); \
    })

/* Ld16.h=sub(ilog2(Luu16.uw),#u5) */
#define Q6_Lh_sub_ilog2_LWuwI(luu, iu5) \
    ({ \
        hlx_put_vp(0, luu); \
        asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 \
                     ".word " HLX_STR(0xed20e42d | HLX_IMM(iu5, 5, 16)) "\n" \
                     HLX_ST6 \
                     : : [stage] "r"(hlx_stage) : HLX_CLOBBERS); \
        hlx_get_v(6); \
    })

/* Ld16.h=sub(ilog2(Luu16.w),#u5) */
#define Q6_Lh_sub_ilog2_LWwI(luu, iu5) \
    ({ \
        hlx_put_vp(0, luu); \
        asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 \
                     ".word " HLX_STR(0xed20e42c | HLX_IMM(iu5, 5, 16)) "\n" \
                     HLX_ST6 \
                     : : [stage] "r"(hlx_stage) : HLX_CLOBBERS); \
        hlx_get_v(6); \
    })

/* Ld16.uh=sub(Lu16.uh,Lv16.uh):sat */
static inline HLX_Vector Q6_Luh_sub_LuhLuh_sat(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504fc0c\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ld16.uh=sub(Lu16.h,Lv16.h):sat */
static inline HLX_Vector Q6_Luh_sub_LhLh_sat(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504fc0d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16.uh=sub(Luu16.h,Lvv16.h):sat */
static inline HLX_VectorPair
Q6_LWuh_sub_LWhLWh_sat(HLX_VectorPair luu,
                       HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04ec2d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uh=sub(Luu16.uh,Lvv16.uh):sat */
static inline HLX_VectorPair
Q6_LWuh_sub_LWuhLWuh_sat(HLX_VectorPair luu,
                         HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04ec2c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=sub(Luu16.uw,Lvv16.uw):sat */
static inline HLX_VectorPair
Q6_LWuw_sub_LWuwLWuw_sat(HLX_VectorPair luu,
                         HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e42c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.uw=sub(Luu16.w,Lvv16.w):sat */
static inline HLX_VectorPair
Q6_LWuw_sub_LWwLWw_sat(HLX_VectorPair luu,
                       HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e42d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=sub(Luu16.w,Lvv16.w) */
static inline HLX_VectorPair
Q6_LWw_sub_LWwLWw(HLX_VectorPair luu,
                  HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e40c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=sub(Luu16.w,Lvv16.w):sat */
static inline HLX_VectorPair
Q6_LWw_sub_LWwLWw_sat(HLX_VectorPair luu,
                      HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef04e40d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=sub(Lu16.uh,Lv16.uh) */
static inline HLX_VectorPair Q6_LWw_sub_LuhLuh(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe524e80d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=sub(Luu16.w,Lv16.h) */
static inline HLX_VectorPair
Q6_LWw_sub_LWwLh(HLX_VectorPair luu,
                 HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xe524e00c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ldd16.w=sub(Luu16.w,Lv16.uh) */
static inline HLX_VectorPair
Q6_LWw_sub_LWwLuh(HLX_VectorPair luu,
                  HLX_Vector lv)
{
    hlx_put_vp(0, luu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2
                 ".word 0xe524e00d\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

/* Ld16=xor(Lu16,Lv16) */
static inline HLX_Vector Q6_L_xor_LL(HLX_Vector lu, HLX_Vector lv)
{
    hlx_put_v(0, lu);
    hlx_put_v(2, lv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD2
                 ".word 0xe504f42d\n"
                 HLX_ST6
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_v(6);
}

/* Ldd16=xor(Luu16,Lvv16) */
static inline HLX_VectorPair
Q6_LW_xor_LWLW(HLX_VectorPair luu,
               HLX_VectorPair lvv)
{
    hlx_put_vp(0, luu);
    hlx_put_vp(2, lvv);
    asm volatile(HLX_PTRS HLX_LD0 HLX_LD1 HLX_LD2 HLX_LD3
                 ".word 0xef24e02c\n"
                 HLX_ST6 HLX_ST7
                 : : [stage] "r"(hlx_stage) : HLX_CLOBBERS);
    return hlx_get_vp(6);
}

#endif /* HLX_ASM_H */
