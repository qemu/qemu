/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

/*
 * This example tests the HVX scatter/gather instructions
 *
 * See section 5.13 of the V68 HVX Programmer's Reference
 *
 * There are 3 main classes operations
 *     _16                 16-bit elements and 16-bit offsets
 *     _32                 32-bit elements and 32-bit offsets
 *     _16_32              16-bit elements and 32-bit offsets
 *
 * There are also masked and accumulate versions
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

typedef long HVX_Vector       __attribute__((__vector_size__(128)))
                              __attribute__((aligned(128)));
typedef long HVX_VectorPair   __attribute__((__vector_size__(256)))
                              __attribute__((aligned(128)));
typedef long HVX_VectorPred   __attribute__((__vector_size__(128)))
                              __attribute__((aligned(128)));

#define VSCATTER_16(BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermh_128B((int)BASE, RGN, OFF, VALS)
#define VSCATTER_16_MASKED(MASK, BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermhq_128B(MASK, (int)BASE, RGN, OFF, VALS)
#define VSCATTER_32(BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermw_128B((int)BASE, RGN, OFF, VALS)
#define VSCATTER_32_MASKED(MASK, BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermwq_128B(MASK, (int)BASE, RGN, OFF, VALS)
#define VSCATTER_16_32(BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermhw_128B((int)BASE, RGN, OFF, VALS)
#define VSCATTER_16_32_MASKED(MASK, BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermhwq_128B(MASK, (int)BASE, RGN, OFF, VALS)
#define VSCATTER_16_ACC(BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermh_add_128B((int)BASE, RGN, OFF, VALS)
#define VSCATTER_32_ACC(BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermw_add_128B((int)BASE, RGN, OFF, VALS)
#define VSCATTER_16_32_ACC(BASE, RGN, OFF, VALS) \
    __builtin_HEXAGON_V6_vscattermhw_add_128B((int)BASE, RGN, OFF, VALS)

#define VGATHER_16(DSTADDR, BASE, RGN, OFF) \
    __builtin_HEXAGON_V6_vgathermh_128B(DSTADDR, (int)BASE, RGN, OFF)
#define VGATHER_16_MASKED(DSTADDR, MASK, BASE, RGN, OFF) \
    __builtin_HEXAGON_V6_vgathermhq_128B(DSTADDR, MASK, (int)BASE, RGN, OFF)
#define VGATHER_32(DSTADDR, BASE, RGN, OFF) \
    __builtin_HEXAGON_V6_vgathermw_128B(DSTADDR, (int)BASE, RGN, OFF)
#define VGATHER_32_MASKED(DSTADDR, MASK, BASE, RGN, OFF) \
    __builtin_HEXAGON_V6_vgathermwq_128B(DSTADDR, MASK, (int)BASE, RGN, OFF)
#define VGATHER_16_32(DSTADDR, BASE, RGN, OFF) \
    __builtin_HEXAGON_V6_vgathermhw_128B(DSTADDR, (int)BASE, RGN, OFF)
#define VGATHER_16_32_MASKED(DSTADDR, MASK, BASE, RGN, OFF) \
    __builtin_HEXAGON_V6_vgathermhwq_128B(DSTADDR, MASK, (int)BASE, RGN, OFF)

#define VSHUFF_H(V) \
    __builtin_HEXAGON_V6_vshuffh_128B(V)
#define VSPLAT_H(X) \
    __builtin_HEXAGON_V6_lvsplath_128B(X)
#define VAND_VAL(PRED, VAL) \
    __builtin_HEXAGON_V6_vandvrt_128B(PRED, VAL)
#define VDEAL_H(V) \
    __builtin_HEXAGON_V6_vdealh_128B(V)

int err;

/* define the number of rows/cols in a square matrix */
#define MATRIX_SIZE 64

/* define the size of the scatter buffer */
#define SCATTER_BUFFER_SIZE (MATRIX_SIZE * MATRIX_SIZE)

/* fake vtcm - put buffers together and force alignment */
static struct {
    unsigned short vscatter16[SCATTER_BUFFER_SIZE];
    unsigned short vgather16[MATRIX_SIZE];
    unsigned int   vscatter32[SCATTER_BUFFER_SIZE];
    unsigned int   vgather32[MATRIX_SIZE];
    unsigned short vscatter16_32[SCATTER_BUFFER_SIZE];
    unsigned short vgather16_32[MATRIX_SIZE];
} vtcm __attribute__((aligned(0x10000)));

/* declare the arrays of reference values */
unsigned short vscatter16_ref[SCATTER_BUFFER_SIZE];
unsigned short vgather16_ref[MATRIX_SIZE];
unsigned int   vscatter32_ref[SCATTER_BUFFER_SIZE];
unsigned int   vgather32_ref[MATRIX_SIZE];
unsigned short vscatter16_32_ref[SCATTER_BUFFER_SIZE];
unsigned short vgather16_32_ref[MATRIX_SIZE];

/* declare the arrays of offsets */
unsigned short half_offsets[MATRIX_SIZE];
unsigned int   word_offsets[MATRIX_SIZE];

/* declare the arrays of values */
unsigned short half_values[MATRIX_SIZE];
unsigned short half_values_acc[MATRIX_SIZE];
unsigned short half_values_masked[MATRIX_SIZE];
unsigned int   word_values[MATRIX_SIZE];
unsigned int   word_values_acc[MATRIX_SIZE];
unsigned int   word_values_masked[MATRIX_SIZE];

/* declare the arrays of predicates */
unsigned short half_predicates[MATRIX_SIZE];
unsigned int   word_predicates[MATRIX_SIZE];

/* make this big enough for all the intrinsics */
const size_t region_len = sizeof(vtcm);

/* optionally add sync instructions */
#define SYNC_VECTOR 1

static void sync_scatter(void *addr)
{
#if SYNC_VECTOR
    /*
     * Do the scatter release followed by a dummy load to complete the
     * synchronization.  Normally the dummy load would be deferred as
     * long as possible to minimize stalls.
     */
    asm volatile("vmem(%0 + #0):scatter_release\n" : : "r"(addr));
    /* use volatile to force the load */
    volatile HVX_Vector vDummy = *(HVX_Vector *)addr; vDummy = vDummy;
#endif
}

static void sync_gather(void *addr)
{
#if SYNC_VECTOR
    /* use volatile to force the load */
    volatile HVX_Vector vDummy = *(HVX_Vector *)addr; vDummy = vDummy;
#endif
}

/* optionally print the results */
#define PRINT_DATA 0

#define FILL_CHAR       '.'

/* fill vtcm scratch with ee */
void prefill_vtcm_scratch(void)
{
    memset(&vtcm, FILL_CHAR, sizeof(vtcm));
}

/* create byte offsets to be a diagonal of the matrix with 16 bit elements */
void create_offsets_values_preds_16(void)
{
    unsigned short half_element = 0;
    unsigned short half_element_masked = 0;
    char letter = 'A';
    char letter_masked = '@';

    for (int i = 0; i < MATRIX_SIZE; i++) {
        half_offsets[i] = i * (2 * MATRIX_SIZE + 2);

        half_element = 0;
        half_element_masked = 0;
        for (int j = 0; j < 2; j++) {
            half_element |= letter << j * 8;
            half_element_masked |= letter_masked << j * 8;
        }

        half_values[i] = half_element;
        half_values_acc[i] = ((i % 10) << 8) + (i % 10);
        half_values_masked[i] = half_element_masked;

        letter++;
        /* reset to 'A' */
        if (letter == 'M') {
            letter = 'A';
        }

        half_predicates[i] = (i % 3 == 0 || i % 5 == 0) ? ~0 : 0;
    }
}

/* create byte offsets to be a diagonal of the matrix with 32 bit elements */
void create_offsets_values_preds_32(void)
{
    unsigned int word_element = 0;
    unsigned int word_element_masked = 0;
    char letter = 'A';
    char letter_masked = '&';

    for (int i = 0; i < MATRIX_SIZE; i++) {
        word_offsets[i] = i * (4 * MATRIX_SIZE + 4);

        word_element = 0;
        word_element_masked = 0;
        for (int j = 0; j < 4; j++) {
            word_element |= letter << j * 8;
            word_element_masked |= letter_masked << j * 8;
        }

        word_values[i] = word_element;
        word_values_acc[i] = ((i % 10) << 8) + (i % 10);
        word_values_masked[i] = word_element_masked;

        letter++;
        /* reset to 'A' */
        if (letter == 'M') {
            letter = 'A';
        }

        word_predicates[i] = (i % 4 == 0 || i % 7 == 0) ? ~0 : 0;
    }
}

/*
 * create byte offsets to be a diagonal of the matrix with 16 bit elements
 * and 32 bit offsets
 */
void create_offsets_values_preds_16_32(void)
{
    unsigned short half_element = 0;
    unsigned short half_element_masked = 0;
    char letter = 'D';
    char letter_masked = '$';

    for (int i = 0; i < MATRIX_SIZE; i++) {
        word_offsets[i] = i * (2 * MATRIX_SIZE + 2);

        half_element = 0;
        half_element_masked = 0;
        for (int j = 0; j < 2; j++) {
            half_element |= letter << j * 8;
            half_element_masked |= letter_masked << j * 8;
        }

        half_values[i] = half_element;
        half_values_acc[i] = ((i % 10) << 8) + (i % 10);
        half_values_masked[i] = half_element_masked;

        letter++;
        /* reset to 'A' */
        if (letter == 'P') {
            letter = 'D';
        }

        half_predicates[i] = (i % 2 == 0 || i % 13 == 0) ? ~0 : 0;
    }
}

/* scatter the 16 bit elements using intrinsics */
void vector_scatter_16(void)
{
    /* copy the offsets and values to vectors */
    HVX_Vector offsets = *(HVX_Vector *)half_offsets;
    HVX_Vector values = *(HVX_Vector *)half_values;

    VSCATTER_16(&vtcm.vscatter16, region_len, offsets, values);

    sync_scatter(vtcm.vscatter16);
}

/* scatter-accumulate the 16 bit elements using intrinsics */
void vector_scatter_16_acc(void)
{
    /* copy the offsets and values to vectors */
    HVX_Vector offsets = *(HVX_Vector *)half_offsets;
    HVX_Vector values = *(HVX_Vector *)half_values_acc;

    VSCATTER_16_ACC(&vtcm.vscatter16, region_len, offsets, values);

    sync_scatter(vtcm.vscatter16);
}

/* scatter the 16 bit elements using intrinsics */
void vector_scatter_16_masked(void)
{
    /* copy the offsets and values to vectors */
    HVX_Vector offsets = *(HVX_Vector *)half_offsets;
    HVX_Vector values = *(HVX_Vector *)half_values_masked;
    HVX_Vector pred_reg = *(HVX_Vector *)half_predicates;
    HVX_VectorPred preds = VAND_VAL(pred_reg, ~0);

    VSCATTER_16_MASKED(preds, &vtcm.vscatter16, region_len, offsets, values);

    sync_scatter(vtcm.vscatter16);
}

/* scatter the 32 bit elements using intrinsics */
void vector_scatter_32(void)
{
    /* copy the offsets and values to vectors */
    HVX_Vector offsetslo = *(HVX_Vector *)word_offsets;
    HVX_Vector offsetshi = *(HVX_Vector *)&word_offsets[MATRIX_SIZE / 2];
    HVX_Vector valueslo = *(HVX_Vector *)word_values;
    HVX_Vector valueshi = *(HVX_Vector *)&word_values[MATRIX_SIZE / 2];

    VSCATTER_32(&vtcm.vscatter32, region_len, offsetslo, valueslo);
    VSCATTER_32(&vtcm.vscatter32, region_len, offsetshi, valueshi);

    sync_scatter(vtcm.vscatter32);
}

/* scatter-acc the 32 bit elements using intrinsics */
void vector_scatter_32_acc(void)
{
    /* copy the offsets and values to vectors */
    HVX_Vector offsetslo = *(HVX_Vector *)word_offsets;
    HVX_Vector offsetshi = *(HVX_Vector *)&word_offsets[MATRIX_SIZE / 2];
    HVX_Vector valueslo = *(HVX_Vector *)word_values_acc;
    HVX_Vector valueshi = *(HVX_Vector *)&word_values_acc[MATRIX_SIZE / 2];

    VSCATTER_32_ACC(&vtcm.vscatter32, region_len, offsetslo, valueslo);
    VSCATTER_32_ACC(&vtcm.vscatter32, region_len, offsetshi, valueshi);

    sync_scatter(vtcm.vscatter32);
}

/* scatter the 32 bit elements using intrinsics */
void vector_scatter_32_masked(void)
{
    /* copy the offsets and values to vectors */
    HVX_Vector offsetslo = *(HVX_Vector *)word_offsets;
    HVX_Vector offsetshi = *(HVX_Vector *)&word_offsets[MATRIX_SIZE / 2];
    HVX_Vector valueslo = *(HVX_Vector *)word_values_masked;
    HVX_Vector valueshi = *(HVX_Vector *)&word_values_masked[MATRIX_SIZE / 2];
    HVX_Vector pred_reglo = *(HVX_Vector *)word_predicates;
    HVX_Vector pred_reghi = *(HVX_Vector *)&word_predicates[MATRIX_SIZE / 2];
    HVX_VectorPred predslo = VAND_VAL(pred_reglo, ~0);
    HVX_VectorPred predshi = VAND_VAL(pred_reghi, ~0);

    VSCATTER_32_MASKED(predslo, &vtcm.vscatter32, region_len, offsetslo,
                       valueslo);
    VSCATTER_32_MASKED(predshi, &vtcm.vscatter32, region_len, offsetshi,
                       valueshi);

    sync_scatter(vtcm.vscatter16);
}

/* scatter the 16 bit elements with 32 bit offsets using intrinsics */
void vector_scatter_16_32(void)
{
    HVX_VectorPair offsets;
    HVX_Vector values;

    /* get the word offsets in a vector pair */
    offsets = *(HVX_VectorPair *)word_offsets;

    /* these values need to be shuffled for the scatter */
    values = *(HVX_Vector *)half_values;
    values = VSHUFF_H(values);

    VSCATTER_16_32(&vtcm.vscatter16_32, region_len, offsets, values);

    sync_scatter(vtcm.vscatter16_32);
}

/* scatter-acc the 16 bit elements with 32 bit offsets using intrinsics */
void vector_scatter_16_32_acc(void)
{
    HVX_VectorPair offsets;
    HVX_Vector values;

    /* get the word offsets in a vector pair */
    offsets = *(HVX_VectorPair *)word_offsets;

    /* these values need to be shuffled for the scatter */
    values = *(HVX_Vector *)half_values_acc;
    values = VSHUFF_H(values);

    VSCATTER_16_32_ACC(&vtcm.vscatter16_32, region_len, offsets, values);

    sync_scatter(vtcm.vscatter16_32);
}

/* masked scatter the 16 bit elements with 32 bit offsets using intrinsics */
void vector_scatter_16_32_masked(void)
{
    HVX_VectorPair offsets;
    HVX_Vector values;
    HVX_Vector pred_reg;

    /* get the word offsets in a vector pair */
    offsets = *(HVX_VectorPair *)word_offsets;

    /* these values need to be shuffled for the scatter */
    values = *(HVX_Vector *)half_values_masked;
    values = VSHUFF_H(values);

    pred_reg = *(HVX_Vector *)half_predicates;
    pred_reg = VSHUFF_H(pred_reg);
    HVX_VectorPred preds = VAND_VAL(pred_reg, ~0);

    VSCATTER_16_32_MASKED(preds, &vtcm.vscatter16_32, region_len, offsets,
                          values);

    sync_scatter(vtcm.vscatter16_32);
}

/* gather the elements from the scatter16 buffer */
void vector_gather_16(void)
{
    HVX_Vector *vgather = (HVX_Vector *)&vtcm.vgather16;
    HVX_Vector offsets = *(HVX_Vector *)half_offsets;

    VGATHER_16(vgather, &vtcm.vscatter16, region_len, offsets);

    sync_gather(vgather);
}

static unsigned short gather_16_masked_init(void)
{
    char letter = '?';
    return letter | (letter << 8);
}

void vector_gather_16_masked(void)
{
    HVX_Vector *vgather = (HVX_Vector *)&vtcm.vgather16;
    HVX_Vector offsets = *(HVX_Vector *)half_offsets;
    HVX_Vector pred_reg = *(HVX_Vector *)half_predicates;
    HVX_VectorPred preds = VAND_VAL(pred_reg, ~0);

    *vgather = VSPLAT_H(gather_16_masked_init());
    VGATHER_16_MASKED(vgather, preds, &vtcm.vscatter16, region_len, offsets);

    sync_gather(vgather);
}

/* gather the elements from the scatter32 buffer */
void vector_gather_32(void)
{
    HVX_Vector *vgatherlo = (HVX_Vector *)&vtcm.vgather32;
    HVX_Vector *vgatherhi =
        (HVX_Vector *)((int)&vtcm.vgather32 + (MATRIX_SIZE * 2));
    HVX_Vector offsetslo = *(HVX_Vector *)word_offsets;
    HVX_Vector offsetshi = *(HVX_Vector *)&word_offsets[MATRIX_SIZE / 2];

    VGATHER_32(vgatherlo, &vtcm.vscatter32, region_len, offsetslo);
    VGATHER_32(vgatherhi, &vtcm.vscatter32, region_len, offsetshi);

    sync_gather(vgatherhi);
}

static unsigned int gather_32_masked_init(void)
{
    char letter = '?';
    return letter | (letter << 8) | (letter << 16) | (letter << 24);
}

void vector_gather_32_masked(void)
{
    HVX_Vector *vgatherlo = (HVX_Vector *)&vtcm.vgather32;
    HVX_Vector *vgatherhi =
        (HVX_Vector *)((int)&vtcm.vgather32 + (MATRIX_SIZE * 2));
    HVX_Vector offsetslo = *(HVX_Vector *)word_offsets;
    HVX_Vector offsetshi = *(HVX_Vector *)&word_offsets[MATRIX_SIZE / 2];
    HVX_Vector pred_reglo = *(HVX_Vector *)word_predicates;
    HVX_VectorPred predslo = VAND_VAL(pred_reglo, ~0);
    HVX_Vector pred_reghi = *(HVX_Vector *)&word_predicates[MATRIX_SIZE / 2];
    HVX_VectorPred predshi = VAND_VAL(pred_reghi, ~0);

    *vgatherlo = VSPLAT_H(gather_32_masked_init());
    *vgatherhi = VSPLAT_H(gather_32_masked_init());
    VGATHER_32_MASKED(vgatherlo, predslo, &vtcm.vscatter32, region_len,
                      offsetslo);
    VGATHER_32_MASKED(vgatherhi, predshi, &vtcm.vscatter32, region_len,
                      offsetshi);

    sync_gather(vgatherlo);
    sync_gather(vgatherhi);
}

/* gather the elements from the scatter16_32 buffer */
void vector_gather_16_32(void)
{
    HVX_Vector *vgather;
    HVX_VectorPair offsets;
    HVX_Vector values;

    /* get the vtcm address to gather from */
    vgather = (HVX_Vector *)&vtcm.vgather16_32;

    /* get the word offsets in a vector pair */
    offsets = *(HVX_VectorPair *)word_offsets;

    VGATHER_16_32(vgather, &vtcm.vscatter16_32, region_len, offsets);

    /* deal the elements to get the order back */
    values = *(HVX_Vector *)vgather;
    values = VDEAL_H(values);

    /* write it back to vtcm address */
    *(HVX_Vector *)vgather = values;
}

void vector_gather_16_32_masked(void)
{
    HVX_Vector *vgather;
    HVX_VectorPair offsets;
    HVX_Vector pred_reg;
    HVX_VectorPred preds;
    HVX_Vector values;

    /* get the vtcm address to gather from */
    vgather = (HVX_Vector *)&vtcm.vgather16_32;

    /* get the word offsets in a vector pair */
    offsets = *(HVX_VectorPair *)word_offsets;
    pred_reg = *(HVX_Vector *)half_predicates;
    pred_reg = VSHUFF_H(pred_reg);
    preds = VAND_VAL(pred_reg, ~0);

   *vgather = VSPLAT_H(gather_16_masked_init());
   VGATHER_16_32_MASKED(vgather, preds, &vtcm.vscatter16_32, region_len,
                        offsets);

    /* deal the elements to get the order back */
    values = *(HVX_Vector *)vgather;
    values = VDEAL_H(values);

    /* write it back to vtcm address */
    *(HVX_Vector *)vgather = values;
}

static void check_buffer(const char *name, void *c, void *r, size_t size)
{
    char *check = (char *)c;
    char *ref = (char *)r;
    for (int i = 0; i < size; i++) {
        if (check[i] != ref[i]) {
            printf("ERROR %s [%d]: 0x%x (%c) != 0x%x (%c)\n", name, i,
                   check[i], check[i], ref[i], ref[i]);
            err++;
        }
    }
}

/*
 * These scalar functions are the C equivalents of the vector functions that
 * use HVX
 */

/* scatter the 16 bit elements using C */
void scalar_scatter_16(unsigned short *vscatter16)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vscatter16[half_offsets[i] / 2] = half_values[i];
    }
}

void check_scatter_16()
{
    memset(vscatter16_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned short));
    scalar_scatter_16(vscatter16_ref);
    check_buffer(__func__, vtcm.vscatter16, vscatter16_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned short));
}

/* scatter the 16 bit elements using C */
void scalar_scatter_16_acc(unsigned short *vscatter16)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vscatter16[half_offsets[i] / 2] += half_values_acc[i];
    }
}

void check_scatter_16_acc()
{
    memset(vscatter16_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned short));
    scalar_scatter_16(vscatter16_ref);
    scalar_scatter_16_acc(vscatter16_ref);
    check_buffer(__func__, vtcm.vscatter16, vscatter16_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned short));
}

/* scatter the 16 bit elements using C */
void scalar_scatter_16_masked(unsigned short *vscatter16)
{
    for (int i = 0; i < MATRIX_SIZE; i++) {
        if (half_predicates[i]) {
            vscatter16[half_offsets[i] / 2] = half_values_masked[i];
        }
    }

}

void check_scatter_16_masked()
{
    memset(vscatter16_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned short));
    scalar_scatter_16(vscatter16_ref);
    scalar_scatter_16_acc(vscatter16_ref);
    scalar_scatter_16_masked(vscatter16_ref);
    check_buffer(__func__, vtcm.vscatter16, vscatter16_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned short));
}

/* scatter the 32 bit elements using C */
void scalar_scatter_32(unsigned int *vscatter32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vscatter32[word_offsets[i] / 4] = word_values[i];
    }
}

void check_scatter_32()
{
    memset(vscatter32_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned int));
    scalar_scatter_32(vscatter32_ref);
    check_buffer(__func__, vtcm.vscatter32, vscatter32_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned int));
}

/* scatter the 32 bit elements using C */
void scalar_scatter_32_acc(unsigned int *vscatter32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vscatter32[word_offsets[i] / 4] += word_values_acc[i];
    }
}

void check_scatter_32_acc()
{
    memset(vscatter32_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned int));
    scalar_scatter_32(vscatter32_ref);
    scalar_scatter_32_acc(vscatter32_ref);
    check_buffer(__func__, vtcm.vscatter32, vscatter32_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned int));
}

/* scatter the 32 bit elements using C */
void scalar_scatter_32_masked(unsigned int *vscatter32)
{
    for (int i = 0; i < MATRIX_SIZE; i++) {
        if (word_predicates[i]) {
            vscatter32[word_offsets[i] / 4] = word_values_masked[i];
        }
    }
}

void check_scatter_32_masked()
{
    memset(vscatter32_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned int));
    scalar_scatter_32(vscatter32_ref);
    scalar_scatter_32_acc(vscatter32_ref);
    scalar_scatter_32_masked(vscatter32_ref);
    check_buffer(__func__, vtcm.vscatter32, vscatter32_ref,
                  SCATTER_BUFFER_SIZE * sizeof(unsigned int));
}

/* scatter the 32 bit elements using C */
void scalar_scatter_16_32(unsigned short *vscatter16_32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vscatter16_32[word_offsets[i] / 2] = half_values[i];
    }
}

void check_scatter_16_32()
{
    memset(vscatter16_32_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned short));
    scalar_scatter_16_32(vscatter16_32_ref);
    check_buffer(__func__, vtcm.vscatter16_32, vscatter16_32_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned short));
}

/* scatter the 32 bit elements using C */
void scalar_scatter_16_32_acc(unsigned short *vscatter16_32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vscatter16_32[word_offsets[i] / 2] += half_values_acc[i];
    }
}

void check_scatter_16_32_acc()
{
    memset(vscatter16_32_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned short));
    scalar_scatter_16_32(vscatter16_32_ref);
    scalar_scatter_16_32_acc(vscatter16_32_ref);
    check_buffer(__func__, vtcm.vscatter16_32, vscatter16_32_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned short));
}

void scalar_scatter_16_32_masked(unsigned short *vscatter16_32)
{
    for (int i = 0; i < MATRIX_SIZE; i++) {
        if (half_predicates[i]) {
            vscatter16_32[word_offsets[i] / 2] = half_values_masked[i];
        }
    }
}

void check_scatter_16_32_masked()
{
    memset(vscatter16_32_ref, FILL_CHAR,
           SCATTER_BUFFER_SIZE * sizeof(unsigned short));
    scalar_scatter_16_32(vscatter16_32_ref);
    scalar_scatter_16_32_acc(vscatter16_32_ref);
    scalar_scatter_16_32_masked(vscatter16_32_ref);
    check_buffer(__func__, vtcm.vscatter16_32, vscatter16_32_ref,
                 SCATTER_BUFFER_SIZE * sizeof(unsigned short));
}

/* gather the elements from the scatter buffer using C */
void scalar_gather_16(unsigned short *vgather16)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vgather16[i] = vtcm.vscatter16[half_offsets[i] / 2];
    }
}

void check_gather_16()
{
      memset(vgather16_ref, 0, MATRIX_SIZE * sizeof(unsigned short));
      scalar_gather_16(vgather16_ref);
      check_buffer(__func__, vtcm.vgather16, vgather16_ref,
                   MATRIX_SIZE * sizeof(unsigned short));
}

void scalar_gather_16_masked(unsigned short *vgather16)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        if (half_predicates[i]) {
            vgather16[i] = vtcm.vscatter16[half_offsets[i] / 2];
        }
    }
}

void check_gather_16_masked()
{
    memset(vgather16_ref, gather_16_masked_init(),
           MATRIX_SIZE * sizeof(unsigned short));
    scalar_gather_16_masked(vgather16_ref);
    check_buffer(__func__, vtcm.vgather16, vgather16_ref,
                 MATRIX_SIZE * sizeof(unsigned short));
}

/* gather the elements from the scatter buffer using C */
void scalar_gather_32(unsigned int *vgather32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vgather32[i] = vtcm.vscatter32[word_offsets[i] / 4];
    }
}

void check_gather_32(void)
{
    memset(vgather32_ref, 0, MATRIX_SIZE * sizeof(unsigned int));
    scalar_gather_32(vgather32_ref);
    check_buffer(__func__, vtcm.vgather32, vgather32_ref,
                 MATRIX_SIZE * sizeof(unsigned int));
}

void scalar_gather_32_masked(unsigned int *vgather32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        if (word_predicates[i]) {
            vgather32[i] = vtcm.vscatter32[word_offsets[i] / 4];
        }
    }
}


void check_gather_32_masked(void)
{
    memset(vgather32_ref, gather_32_masked_init(),
           MATRIX_SIZE * sizeof(unsigned int));
    scalar_gather_32_masked(vgather32_ref);
    check_buffer(__func__, vtcm.vgather32,
                 vgather32_ref, MATRIX_SIZE * sizeof(unsigned int));
}

/* gather the elements from the scatter buffer using C */
void scalar_gather_16_32(unsigned short *vgather16_32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        vgather16_32[i] = vtcm.vscatter16_32[word_offsets[i] / 2];
    }
}

void check_gather_16_32(void)
{
    memset(vgather16_32_ref, 0, MATRIX_SIZE * sizeof(unsigned short));
    scalar_gather_16_32(vgather16_32_ref);
    check_buffer(__func__, vtcm.vgather16_32, vgather16_32_ref,
                 MATRIX_SIZE * sizeof(unsigned short));
}

void scalar_gather_16_32_masked(unsigned short *vgather16_32)
{
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        if (half_predicates[i]) {
            vgather16_32[i] = vtcm.vscatter16_32[word_offsets[i] / 2];
        }
    }

}

void check_gather_16_32_masked(void)
{
    memset(vgather16_32_ref, gather_16_masked_init(),
           MATRIX_SIZE * sizeof(unsigned short));
    scalar_gather_16_32_masked(vgather16_32_ref);
    check_buffer(__func__, vtcm.vgather16_32, vgather16_32_ref,
                 MATRIX_SIZE * sizeof(unsigned short));
}

/* print scatter16 buffer */
void print_scatter16_buffer(void)
{
    if (PRINT_DATA) {
        printf("\n\nPrinting the 16 bit scatter buffer");

        for (int i = 0; i < SCATTER_BUFFER_SIZE; i++) {
            if ((i % MATRIX_SIZE) == 0) {
                printf("\n");
            }
            for (int j = 0; j < 2; j++) {
                printf("%c", (char)((vtcm.vscatter16[i] >> j * 8) & 0xff));
            }
            printf(" ");
        }
        printf("\n");
    }
}

/* print the gather 16 buffer */
void print_gather_result_16(void)
{
    if (PRINT_DATA) {
        printf("\n\nPrinting the 16 bit gather result\n");

        for (int i = 0; i < MATRIX_SIZE; i++) {
            for (int j = 0; j < 2; j++) {
                printf("%c", (char)((vtcm.vgather16[i] >> j * 8) & 0xff));
            }
            printf(" ");
        }
        printf("\n");
    }
}

/* print the scatter32 buffer */
void print_scatter32_buffer(void)
{
    if (PRINT_DATA) {
        printf("\n\nPrinting the 32 bit scatter buffer");

        for (int i = 0; i < SCATTER_BUFFER_SIZE; i++) {
            if ((i % MATRIX_SIZE) == 0) {
                printf("\n");
            }
            for (int j = 0; j < 4; j++) {
                printf("%c", (char)((vtcm.vscatter32[i] >> j * 8) & 0xff));
            }
            printf(" ");
        }
        printf("\n");
    }
}

/* print the gather 32 buffer */
void print_gather_result_32(void)
{
    if (PRINT_DATA) {
        printf("\n\nPrinting the 32 bit gather result\n");

        for (int i = 0; i < MATRIX_SIZE; i++) {
            for (int j = 0; j < 4; j++) {
                printf("%c", (char)((vtcm.vgather32[i] >> j * 8) & 0xff));
            }
            printf(" ");
        }
        printf("\n");
    }
}

/* print the scatter16_32 buffer */
void print_scatter16_32_buffer(void)
{
    if (PRINT_DATA) {
        printf("\n\nPrinting the 16_32 bit scatter buffer");

        for (int i = 0; i < SCATTER_BUFFER_SIZE; i++) {
            if ((i % MATRIX_SIZE) == 0) {
                printf("\n");
            }
            for (int j = 0; j < 2; j++) {
                printf("%c",
                      (unsigned char)((vtcm.vscatter16_32[i] >> j * 8) & 0xff));
            }
            printf(" ");
        }
        printf("\n");
    }
}

/* print the gather 16_32 buffer */
void print_gather_result_16_32(void)
{
    if (PRINT_DATA) {
        printf("\n\nPrinting the 16_32 bit gather result\n");

        for (int i = 0; i < MATRIX_SIZE; i++) {
            for (int j = 0; j < 2; j++) {
                printf("%c",
                       (unsigned char)((vtcm.vgather16_32[i] >> j * 8) & 0xff));
            }
            printf(" ");
        }
        printf("\n");
    }
}

int main()
{
    prefill_vtcm_scratch();

    /* 16 bit elements with 16 bit offsets */
    create_offsets_values_preds_16();

    vector_scatter_16();
    print_scatter16_buffer();
    check_scatter_16();

    vector_gather_16();
    print_gather_result_16();
    check_gather_16();

    vector_gather_16_masked();
    print_gather_result_16();
    check_gather_16_masked();

    vector_scatter_16_acc();
    print_scatter16_buffer();
    check_scatter_16_acc();

    vector_scatter_16_masked();
    print_scatter16_buffer();
    check_scatter_16_masked();

    /* 32 bit elements with 32 bit offsets */
    create_offsets_values_preds_32();

    vector_scatter_32();
    print_scatter32_buffer();
    check_scatter_32();

    vector_gather_32();
    print_gather_result_32();
    check_gather_32();

    vector_gather_32_masked();
    print_gather_result_32();
    check_gather_32_masked();

    vector_scatter_32_acc();
    print_scatter32_buffer();
    check_scatter_32_acc();

    vector_scatter_32_masked();
    print_scatter32_buffer();
    check_scatter_32_masked();

    /* 16 bit elements with 32 bit offsets */
    create_offsets_values_preds_16_32();

    vector_scatter_16_32();
    print_scatter16_32_buffer();
    check_scatter_16_32();

    vector_gather_16_32();
    print_gather_result_16_32();
    check_gather_16_32();

    vector_gather_16_32_masked();
    print_gather_result_16_32();
    check_gather_16_32_masked();

    vector_scatter_16_32_acc();
    print_scatter16_32_buffer();
    check_scatter_16_32_acc();

    vector_scatter_16_32_masked();
    print_scatter16_32_buffer();
    check_scatter_16_32_masked();

    puts(err ? "FAIL" : "PASS");
    return err;
}
