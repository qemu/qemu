/*
 *  Copyright(c) 2020-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
 * This test checks various FP operations performed on Hexagon
 */

#include <stdio.h>
#include <stdint.h>
#include <float.h>

int err;

#include "hex_test.h"

static void check_fpstatus_bit(uint32_t usr, uint32_t expect, uint32_t flag,
                               const char *name)
{
    uint32_t bit = 1 << flag;
    if ((usr & bit) != (expect & bit)) {
        printf("ERROR %s: usr = %d, expect = %d\n", name,
               (usr >> flag) & 1, (expect >> flag) & 1);
        err++;
    }
}

static void check_fpstatus(uint32_t usr, uint32_t expect)
{
    check_fpstatus_bit(usr, expect, USR_FPINVF_BIT, "Invalid");
    check_fpstatus_bit(usr, expect, USR_FPDBZF_BIT, "Div by zero");
    check_fpstatus_bit(usr, expect, USR_FPOVFF_BIT, "Overflow");
    check_fpstatus_bit(usr, expect, USR_FPUNFF_BIT, "Underflow");
    check_fpstatus_bit(usr, expect, USR_FPINPF_BIT, "Inexact");
}

static void check_compare_exception(void)
{
    uint32_t cmp;
    uint32_t usr;

    /* Check that FP compares are quiet (don't raise any exceptions) */
    asm (CLEAR_FPSTATUS
         "p0 = sfcmp.eq(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = sfcmp.gt(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = sfcmp.ge(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = dfcmp.eq(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(DF_QNaN), "r"(DF_any)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = dfcmp.gt(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(DF_QNaN), "r"(DF_any)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = dfcmp.ge(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(DF_QNaN), "r"(DF_any)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);
}

static void check_sfminmax(void)
{
    uint32_t minmax;
    uint32_t usr;

    /*
     * Execute sfmin/sfmax instructions with one operand as NaN
     * Check that
     *     Result is the other operand
     *     Invalid bit in USR is not set
     */
     asm (CLEAR_FPSTATUS
         "%0 = sfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
         : "r2", "usr");
    check32(minmax, SF_any);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0 = sfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
         : "r2", "usr");
    check32(minmax, SF_any);
    check_fpstatus(usr, 0);

    /*
     * Execute sfmin/sfmax instructions with both operands NaN
     * Check that
     *     Result is SF_HEX_NaN
     *     Invalid bit in USR is set
     */
    asm (CLEAR_FPSTATUS
         "%0 = sfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_QNaN), "r"(SF_QNaN)
         : "r2", "usr");
    check32(minmax, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0 = sfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_QNaN), "r"(SF_QNaN)
         : "r2", "usr");
    check32(minmax, SF_HEX_NaN);
    check_fpstatus(usr, 0);
}

static void check_dfminmax(void)
{
    uint64_t minmax;
    uint32_t usr;

    /*
     * Execute dfmin/dfmax instructions with one operand as SNaN
     * Check that
     *     Result is the other operand
     *     Invalid bit in USR is set
     */
     asm (CLEAR_FPSTATUS
         "%0 = dfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_SNaN), "r"(DF_any)
         : "r2", "usr");
    check64(minmax, DF_any);
    check_fpstatus(usr, USR_FPINVF);

    asm (CLEAR_FPSTATUS
         "%0 = dfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_SNaN), "r"(DF_any)
         : "r2", "usr");
    check64(minmax, DF_any);
    check_fpstatus(usr, USR_FPINVF);

    /*
     * Execute dfmin/dfmax instructions with one operand as QNaN
     * Check that
     *     Result is the other operand
     *     No bit in USR is set
     */
     asm (CLEAR_FPSTATUS
         "%0 = dfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_QNaN), "r"(DF_any)
         : "r2", "usr");
    check64(minmax, DF_any);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0 = dfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_QNaN), "r"(DF_any)
         : "r2", "usr");
    check64(minmax, DF_any);
    check_fpstatus(usr, 0);

    /*
     * Execute dfmin/dfmax instructions with both operands SNaN
     * Check that
     *     Result is DF_HEX_NaN
     *     Invalid bit in USR is set
     */
    asm (CLEAR_FPSTATUS
         "%0 = dfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_SNaN), "r"(DF_SNaN)
         : "r2", "usr");
    check64(minmax, DF_HEX_NaN);
    check_fpstatus(usr, USR_FPINVF);

    asm (CLEAR_FPSTATUS
         "%0 = dfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_SNaN), "r"(DF_SNaN)
         : "r2", "usr");
    check64(minmax, DF_HEX_NaN);
    check_fpstatus(usr, USR_FPINVF);

    /*
     * Execute dfmin/dfmax instructions with both operands QNaN
     * Check that
     *     Result is DF_HEX_NaN
     *     No bit in USR is set
     */
    asm (CLEAR_FPSTATUS
         "%0 = dfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_QNaN), "r"(DF_QNaN)
         : "r2", "usr");
    check64(minmax, DF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0 = dfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_QNaN), "r"(DF_QNaN)
         : "r2", "usr");
    check64(minmax, DF_HEX_NaN);
    check_fpstatus(usr, 0);
}

static void check_sfrecipa(void)
{
    uint32_t result;
    uint32_t usr;
    uint32_t pred;

    /*
     * Check that sfrecipa doesn't set status bits when
     * a NaN with bit 22 non-zero is passed
     */
    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_any), "r"(SF_QNaN)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %2)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_QNaN)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    /*
     * Check that sfrecipa doesn't set status bits when
     * a NaN with bit 22 zero is passed
     */
    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_QNaN_special), "r"(SF_any)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NaN);
    check_fpstatus(usr, USR_FPINVF);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_any), "r"(SF_QNaN_special)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NaN);
    check_fpstatus(usr, USR_FPINVF);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %2)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_QNaN_special)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NaN);
    check_fpstatus(usr, USR_FPINVF);

    /*
     * Check that sfrecipa properly sets divid-by-zero
     */
        asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(0x885dc960), "r"(0x80000000)
         : "r2", "p0", "usr");
    check32(result, 0x3f800000);
    check_fpstatus(usr, USR_FPDBZF);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(0x7f800000), "r"(SF_zero)
         : "r2", "p0", "usr");
    check32(result, 0x3f800000);
    check_fpstatus(usr, 0);

    /*
     * Check that sfrecipa properly handles denorm
     */
    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = p0\n\t"
         : "=r"(result), "=r"(pred) : "r"(SF_denorm), "r"(SF_random)
         : "p0", "usr");
    check32(result, 0x6a920001);
    check32(pred, 0x80);
}

static void check_canonical_NaN(void)
{
    uint32_t sf_result;
    uint64_t df_result;
    uint32_t usr;

    /* Check that each FP instruction properly returns SF_HEX_NaN/DF_HEX_NaN */
    asm(CLEAR_FPSTATUS
        "%0 = sfadd(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = sfsub(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = sfmpy(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    sf_result = SF_zero;
    asm(CLEAR_FPSTATUS
        "%0 += sfmpy(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    sf_result = SF_zero;
    asm(CLEAR_FPSTATUS
        "p0 = !cmp.eq(r0, r0)\n\t"
        "%0 += sfmpy(%2, %3, p0):scale\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr", "p0");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    sf_result = SF_zero;
    asm(CLEAR_FPSTATUS
        "%0 -= sfmpy(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    sf_result = SF_zero;
    asm(CLEAR_FPSTATUS
        "%0 += sfmpy(%2, %3):lib\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    sf_result = SF_zero;
    asm(CLEAR_FPSTATUS
        "%0 -= sfmpy(%2, %3):lib\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_QNaN), "r"(SF_any)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2sf(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(DF_QNaN)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = dfadd(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(df_result), "=r"(usr) : "r"(DF_QNaN), "r"(DF_any)
        : "r2", "usr");
    check64(df_result, DF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = dfsub(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(df_result), "=r"(usr) : "r"(DF_QNaN), "r"(DF_any)
        : "r2", "usr");
    check64(df_result, DF_HEX_NaN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2df(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(df_result), "=r"(usr) : "r"(SF_QNaN)
        : "r2", "usr");
    check64(df_result, DF_HEX_NaN);
    check_fpstatus(usr, 0);
}

static void check_invsqrta(void)
{
    uint32_t result;
    uint32_t predval;

    asm volatile("%0,p0 = sfinvsqrta(%2)\n\t"
                 "%1 = p0\n\t"
                 : "+r"(result), "=r"(predval)
                 : "r"(0x7f800000)
                 : "p0");
    check32(result, 0xff800000);
    check32(predval, 0x0);
}

static void check_sffixupn(void)
{
    uint32_t result;

    /* Check that sffixupn properly deals with denorm */
    asm volatile("%0 = sffixupn(%1, %2)\n\t"
                 : "=r"(result)
                 : "r"(SF_random), "r"(SF_denorm));
    check32(result, 0x246001d6);
}

static void check_sffixupd(void)
{
    uint32_t result;

    /* Check that sffixupd properly deals with denorm */
    asm volatile("%0 = sffixupd(%1, %2)\n\t"
                 : "=r"(result)
                 : "r"(SF_denorm), "r"(SF_random));
    check32(result, 0x146001d6);
}

static void check_sffms(void)
{
    uint32_t result;

    /* Check that sffms properly deals with -0 */
    result = SF_zero_neg;
    asm ("%0 -= sfmpy(%1 , %2)\n\t"
        : "+r"(result)
        : "r"(SF_zero), "r"(SF_zero)
        : "r12", "r8");
    check32(result, SF_zero_neg);

    result = SF_zero;
    asm ("%0 -= sfmpy(%1 , %2)\n\t"
        : "+r"(result)
        : "r"(SF_zero_neg), "r"(SF_zero)
        : "r12", "r8");
    check32(result, SF_zero);

    result = SF_zero;
    asm ("%0 -= sfmpy(%1 , %2)\n\t"
        : "+r"(result)
        : "r"(SF_zero), "r"(SF_zero_neg)
        : "r12", "r8");
    check32(result, SF_zero);
}

static void check_float2int_convs()
{
    uint32_t res32;
    uint64_t res64;
    uint32_t usr;

    /*
     * Check that the various forms of float-to-unsigned
     *  check sign before rounding
     */
        asm(CLEAR_FPSTATUS
        "%0 = convert_sf2uw(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(SF_small_neg)
        : "r2", "usr");
    check32(res32, 0);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2uw(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(SF_small_neg)
        : "r2", "usr");
    check32(res32, 0);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2ud(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2ud(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2uw(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check32(res32, 0);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2uw(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check32(res32, 0);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2ud(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2ud(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, USR_FPINVF);

    /*
     * Check that the various forms of float-to-signed return -1 for NaN
     */
    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2w(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(SF_QNaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2w(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(SF_QNaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2d(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_QNaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2d(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_QNaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2w(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_QNaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2w(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_QNaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2d(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_QNaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, USR_FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2d(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_QNaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, USR_FPINVF);
}

static void check_float_consts(void)
{
    uint32_t res32;
    uint64_t res64;

    asm("%0 = sfmake(#%1):neg\n\t" : "=r"(res32) : "i"(0xf));
    check32(res32, 0xbc9e0000);

    asm("%0 = sfmake(#%1):pos\n\t" : "=r"(res32) : "i"(0xf));
    check32(res32, 0x3c9e0000);

    asm("%0 = dfmake(#%1):neg\n\t" : "=r"(res64) : "i"(0xf));
    check64(res64, 0xbf93c00000000000ULL);

    asm("%0 = dfmake(#%1):pos\n\t" : "=r"(res64) : "i"(0xf));
    check64(res64, 0x3f93c00000000000ULL);
}

static inline uint64_t dfmpyll(double x, double y)
{
    uint64_t res64;
    asm("%0 = dfmpyll(%1, %2)" : "=r"(res64) : "r"(x), "r"(y));
    return res64;
}

static inline uint64_t dfmpylh(double acc, double x, double y)
{
    uint64_t res64 = *(uint64_t *)&acc;
    asm("%0 += dfmpylh(%1, %2)" : "+r"(res64) : "r"(x), "r"(y));
    return res64;
}

static void check_dfmpyxx(void)
{
    uint64_t res64;

    res64 = dfmpyll(DBL_MIN, DBL_MIN);
    check64(res64, 0ULL);
    res64 = dfmpyll(-1.0, DBL_MIN);
    check64(res64, 0ULL);
    res64 = dfmpyll(DBL_MAX, DBL_MAX);
    check64(res64, 0x1fffffffdULL);

    res64 = dfmpylh(DBL_MIN, DBL_MIN, DBL_MIN);
    check64(res64, 0x10000000000000ULL);
    res64 = dfmpylh(-1.0, DBL_MAX, DBL_MIN);
    check64(res64, 0xc00fffffffe00000ULL);
    res64 = dfmpylh(DBL_MAX, 0.0, -1.0);
    check64(res64, 0x7fefffffffffffffULL);
}

int main()
{
    check_compare_exception();
    check_sfminmax();
    check_dfminmax();
    check_sfrecipa();
    check_canonical_NaN();
    check_invsqrta();
    check_sffixupn();
    check_sffixupd();
    check_sffms();
    check_float2int_convs();
    check_float_consts();
    check_dfmpyxx();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
