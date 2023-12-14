/*
 *  Copyright(c) 2021-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HVX_MISC_H
#define HVX_MISC_H

static inline void check(int line, int i, int j,
                         uint64_t result, uint64_t expect)
{
    if (result != expect) {
        printf("ERROR at line %d: [%d][%d] 0x%016llx != 0x%016llx\n",
               line, i, j, result, expect);
        err++;
    }
}

#define MAX_VEC_SIZE_BYTES         128

typedef union {
    uint64_t ud[MAX_VEC_SIZE_BYTES / 8];
    int64_t   d[MAX_VEC_SIZE_BYTES / 8];
    uint32_t uw[MAX_VEC_SIZE_BYTES / 4];
    int32_t   w[MAX_VEC_SIZE_BYTES / 4];
    uint16_t uh[MAX_VEC_SIZE_BYTES / 2];
    int16_t   h[MAX_VEC_SIZE_BYTES / 2];
    uint8_t  ub[MAX_VEC_SIZE_BYTES / 1];
    int8_t    b[MAX_VEC_SIZE_BYTES / 1];
} MMVector;

#define BUFSIZE      16
#define OUTSIZE      16
#define MASKMOD      3

MMVector buffer0[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector buffer1[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector mask[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector output[OUTSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector expect[OUTSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));

#define CHECK_OUTPUT_FUNC(FIELD, FIELDSZ) \
static inline void check_output_##FIELD(int line, size_t num_vectors) \
{ \
    for (int i = 0; i < num_vectors; i++) { \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / FIELDSZ; j++) { \
            check(line, i, j, output[i].FIELD[j], expect[i].FIELD[j]); \
        } \
    } \
}

CHECK_OUTPUT_FUNC(d,  8)
CHECK_OUTPUT_FUNC(w,  4)
CHECK_OUTPUT_FUNC(h,  2)
CHECK_OUTPUT_FUNC(b,  1)

static inline void init_buffers(void)
{
    int counter0 = 0;
    int counter1 = 17;
    for (int i = 0; i < BUFSIZE; i++) {
        for (int j = 0; j < MAX_VEC_SIZE_BYTES; j++) {
            buffer0[i].b[j] = counter0++;
            buffer1[i].b[j] = counter1++;
        }
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            mask[i].w[j] = (i + j % MASKMOD == 0) ? 0 : 1;
        }
    }
}

#define VEC_OP1(ASM, EL, IN, OUT) \
    asm("v2 = vmem(%0 + #0)\n\t" \
        "v2" #EL " = " #ASM "(v2" #EL ")\n\t" \
        "vmem(%1 + #0) = v2\n\t" \
        : : "r"(IN), "r"(OUT) : "v2", "memory")

#define VEC_OP2(ASM, EL, IN0, IN1, OUT) \
    asm("v2 = vmem(%0 + #0)\n\t" \
        "v3 = vmem(%1 + #0)\n\t" \
        "v2" #EL " = " #ASM "(v2" #EL ", v3" #EL ")\n\t" \
        "vmem(%2 + #0) = v2\n\t" \
        : : "r"(IN0), "r"(IN1), "r"(OUT) : "v2", "v3", "memory")

#define TEST_VEC_OP1(NAME, ASM, EL, FIELD, FIELDSZ, OP) \
static inline void test_##NAME(void) \
{ \
    void *pin = buffer0; \
    void *pout = output; \
    for (int i = 0; i < BUFSIZE; i++) { \
        VEC_OP1(ASM, EL, pin, pout); \
        pin += sizeof(MMVector); \
        pout += sizeof(MMVector); \
    } \
    for (int i = 0; i < BUFSIZE; i++) { \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / FIELDSZ; j++) { \
            expect[i].FIELD[j] = OP buffer0[i].FIELD[j]; \
        } \
    } \
    check_output_##FIELD(__LINE__, BUFSIZE); \
}

#define TEST_VEC_OP2(NAME, ASM, EL, FIELD, FIELDSZ, OP) \
static inline void test_##NAME(void) \
{ \
    void *p0 = buffer0; \
    void *p1 = buffer1; \
    void *pout = output; \
    for (int i = 0; i < BUFSIZE; i++) { \
        VEC_OP2(ASM, EL, p0, p1, pout); \
        p0 += sizeof(MMVector); \
        p1 += sizeof(MMVector); \
        pout += sizeof(MMVector); \
    } \
    for (int i = 0; i < BUFSIZE; i++) { \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / FIELDSZ; j++) { \
            expect[i].FIELD[j] = buffer0[i].FIELD[j] OP buffer1[i].FIELD[j]; \
        } \
    } \
    check_output_##FIELD(__LINE__, BUFSIZE); \
}

#define THRESHOLD        31

#define PRED_OP2(ASM, IN0, IN1, OUT, INV) \
    asm("r4 = #%3\n\t" \
        "v1.b = vsplat(r4)\n\t" \
        "v2 = vmem(%0 + #0)\n\t" \
        "q0 = vcmp.gt(v2.b, v1.b)\n\t" \
        "v3 = vmem(%1 + #0)\n\t" \
        "q1 = vcmp.gt(v3.b, v1.b)\n\t" \
        "q2 = " #ASM "(q0, " INV "q1)\n\t" \
        "r4 = #0xff\n\t" \
        "v1.b = vsplat(r4)\n\t" \
        "if (q2) vmem(%2 + #0) = v1\n\t" \
        : : "r"(IN0), "r"(IN1), "r"(OUT), "i"(THRESHOLD) \
        : "r4", "v1", "v2", "v3", "q0", "q1", "q2", "memory")

#define TEST_PRED_OP2(NAME, ASM, OP, INV) \
static inline void test_##NAME(bool invert) \
{ \
    void *p0 = buffer0; \
    void *p1 = buffer1; \
    void *pout = output; \
    memset(output, 0, sizeof(expect)); \
    for (int i = 0; i < BUFSIZE; i++) { \
        PRED_OP2(ASM, p0, p1, pout, INV); \
        p0 += sizeof(MMVector); \
        p1 += sizeof(MMVector); \
        pout += sizeof(MMVector); \
    } \
    for (int i = 0; i < BUFSIZE; i++) { \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES; j++) { \
            bool p0 = (buffer0[i].b[j] > THRESHOLD); \
            bool p1 = (buffer1[i].b[j] > THRESHOLD); \
            if (invert) { \
                expect[i].b[j] = (p0 OP !p1) ? 0xff : 0x00; \
            } else { \
                expect[i].b[j] = (p0 OP p1) ? 0xff : 0x00; \
            } \
        } \
    } \
    check_output_b(__LINE__, BUFSIZE); \
}

#endif
