/*
 *  Copyright(c) 2020-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

const int FPINVF_BIT = 1;                 /* Invalid */
const int FPINVF = 1 << FPINVF_BIT;
const int FPDBZF_BIT = 2;                 /* Divide by zero */
const int FPDBZF = 1 << FPDBZF_BIT;
const int FPOVFF_BIT = 3;                 /* Overflow */
const int FPOVFF = 1 << FPOVFF_BIT;
const int FPUNFF_BIT = 4;                 /* Underflow */
const int FPUNFF = 1 << FPUNFF_BIT;
const int FPINPF_BIT = 5;                 /* Inexact */
const int FPINPF = 1 << FPINPF_BIT;

const int SF_ZERO =                       0x00000000;
const int SF_NaN =                        0x7fc00000;
const int SF_NaN_special =                0x7f800001;
const int SF_ANY =                        0x3f800000;
const int SF_HEX_NAN =                    0xffffffff;
const int SF_small_neg =                  0xab98fba8;

const long long DF_NaN =                  0x7ff8000000000000ULL;
const long long DF_ANY =                  0x3f80000000000000ULL;
const long long DF_HEX_NAN =              0xffffffffffffffffULL;
const long long DF_small_neg =            0xbd731f7500000000ULL;

int err;

#define CLEAR_FPSTATUS \
    "r2 = usr\n\t" \
    "r2 = clrbit(r2, #1)\n\t" \
    "r2 = clrbit(r2, #2)\n\t" \
    "r2 = clrbit(r2, #3)\n\t" \
    "r2 = clrbit(r2, #4)\n\t" \
    "r2 = clrbit(r2, #5)\n\t" \
    "usr = r2\n\t"

static void check_fpstatus_bit(int usr, int expect, int flag, const char *n)
{
    int bit = 1 << flag;
    if ((usr & bit) != (expect & bit)) {
        printf("ERROR %s: usr = %d, expect = %d\n", n,
               (usr >> flag) & 1, (expect >> flag) & 1);
        err++;
    }
}

static void check_fpstatus(int usr, int expect)
{
    check_fpstatus_bit(usr, expect, FPINVF_BIT, "Invalid");
    check_fpstatus_bit(usr, expect, FPDBZF_BIT, "Div by zero");
    check_fpstatus_bit(usr, expect, FPOVFF_BIT, "Overflow");
    check_fpstatus_bit(usr, expect, FPUNFF_BIT, "Underflow");
    check_fpstatus_bit(usr, expect, FPINPF_BIT, "Inexact");
}

static void check32(int val, int expect)
{
    if (val != expect) {
        printf("ERROR: 0x%x != 0x%x\n", val, expect);
        err++;
    }
}
static void check64(unsigned long long val, unsigned long long expect)
{
    if (val != expect) {
        printf("ERROR: 0x%llx != 0x%llx\n", val, expect);
        err++;
    }
}

static void check_compare_exception(void)
{
    int cmp;
    int usr;

    /* Check that FP compares are quiet (don't raise any execptions) */
    asm (CLEAR_FPSTATUS
         "p0 = sfcmp.eq(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = sfcmp.gt(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = sfcmp.ge(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = dfcmp.eq(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(DF_NaN), "r"(DF_ANY)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = dfcmp.gt(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(DF_NaN), "r"(DF_ANY)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "p0 = dfcmp.ge(%2, %3)\n\t"
         "%0 = p0\n\t"
         "%1 = usr\n\t"
         : "=r"(cmp), "=r"(usr) : "r"(DF_NaN), "r"(DF_ANY)
         : "r2", "p0", "usr");
    check32(cmp, 0);
    check_fpstatus(usr, 0);
}

static void check_sfminmax(void)
{
    int minmax;
    int usr;

    /*
     * Execute sfmin/sfmax instructions with one operand as NaN
     * Check that
     *     Result is the other operand
     *     Invalid bit in USR is not set
     */
     asm (CLEAR_FPSTATUS
         "%0 = sfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
         : "r2", "usr");
    check64(minmax, SF_ANY);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0 = sfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
         : "r2", "usr");
    check64(minmax, SF_ANY);
    check_fpstatus(usr, 0);

    /*
     * Execute sfmin/sfmax instructions with both operands NaN
     * Check that
     *     Result is SF_HEX_NAN
     *     Invalid bit in USR is set
     */
    asm (CLEAR_FPSTATUS
         "%0 = sfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_NaN), "r"(SF_NaN)
         : "r2", "usr");
    check64(minmax, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0 = sfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(SF_NaN), "r"(SF_NaN)
         : "r2", "usr");
    check64(minmax, SF_HEX_NAN);
    check_fpstatus(usr, 0);
}

static void check_dfminmax(void)
{
    unsigned long long minmax;
    int usr;

    /*
     * Execute dfmin/dfmax instructions with one operand as NaN
     * Check that
     *     Result is the other operand
     *     Invalid bit in USR is set
     */
     asm (CLEAR_FPSTATUS
         "%0 = dfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_NaN), "r"(DF_ANY)
         : "r2", "usr");
    check64(minmax, DF_ANY);
    check_fpstatus(usr, FPINVF);

    asm (CLEAR_FPSTATUS
         "%0 = dfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_NaN), "r"(DF_ANY)
         : "r2", "usr");
    check64(minmax, DF_ANY);
    check_fpstatus(usr, FPINVF);

    /*
     * Execute dfmin/dfmax instructions with both operands NaN
     * Check that
     *     Result is DF_HEX_NAN
     *     Invalid bit in USR is set
     */
    asm (CLEAR_FPSTATUS
         "%0 = dfmin(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_NaN), "r"(DF_NaN)
         : "r2", "usr");
    check64(minmax, DF_HEX_NAN);
    check_fpstatus(usr, FPINVF);

    asm (CLEAR_FPSTATUS
         "%0 = dfmax(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(minmax), "=r"(usr) : "r"(DF_NaN), "r"(DF_NaN)
         : "r2", "usr");
    check64(minmax, DF_HEX_NAN);
    check_fpstatus(usr, FPINVF);
}

static void check_recip_exception(void)
{
    int result;
    int usr;

    /*
     * Check that sfrecipa doesn't set status bits when
     * a NaN with bit 22 non-zero is passed
     */
    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_ANY), "r"(SF_NaN)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %2)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_NaN)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    /*
     * Check that sfrecipa doesn't set status bits when
     * a NaN with bit 22 zero is passed
     */
    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_NaN_special), "r"(SF_ANY)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NAN);
    check_fpstatus(usr, FPINVF);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_ANY), "r"(SF_NaN_special)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NAN);
    check_fpstatus(usr, FPINVF);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %2)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(SF_NaN_special)
         : "r2", "p0", "usr");
    check32(result, SF_HEX_NAN);
    check_fpstatus(usr, FPINVF);

    /*
     * Check that sfrecipa properly sets divid-by-zero
     */
        asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(0x885dc960), "r"(0x80000000)
         : "r2", "p0", "usr");
    check32(result, 0x3f800000);
    check_fpstatus(usr, FPDBZF);

    asm (CLEAR_FPSTATUS
         "%0,p0 = sfrecipa(%2, %3)\n\t"
         "%1 = usr\n\t"
         : "=r"(result), "=r"(usr) : "r"(0x7f800000), "r"(SF_ZERO)
         : "r2", "p0", "usr");
    check32(result, 0x3f800000);
    check_fpstatus(usr, 0);
}

static void check_canonical_NaN(void)
{
    int sf_result;
    unsigned long long df_result;
    int usr;

    /* Check that each FP instruction properly returns SF_HEX_NAN/DF_HEX_NAN */
    asm(CLEAR_FPSTATUS
        "%0 = sfadd(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = sfsub(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = sfmpy(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    sf_result = SF_ZERO;
    asm(CLEAR_FPSTATUS
        "%0 += sfmpy(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    sf_result = SF_ZERO;
    asm(CLEAR_FPSTATUS
        "p0 = !cmp.eq(r0, r0)\n\t"
        "%0 += sfmpy(%2, %3, p0):scale\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr", "p0");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    sf_result = SF_ZERO;
    asm(CLEAR_FPSTATUS
        "%0 -= sfmpy(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    sf_result = SF_ZERO;
    asm(CLEAR_FPSTATUS
        "%0 += sfmpy(%2, %3):lib\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    sf_result = SF_ZERO;
    asm(CLEAR_FPSTATUS
        "%0 -= sfmpy(%2, %3):lib\n\t"
        "%1 = usr\n\t"
        : "+r"(sf_result), "=r"(usr) : "r"(SF_NaN), "r"(SF_ANY)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2sf(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(sf_result), "=r"(usr) : "r"(DF_NaN)
        : "r2", "usr");
    check32(sf_result, SF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = dfadd(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(df_result), "=r"(usr) : "r"(DF_NaN), "r"(DF_ANY)
        : "r2", "usr");
    check64(df_result, DF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = dfsub(%2, %3)\n\t"
        "%1 = usr\n\t"
        : "=r"(df_result), "=r"(usr) : "r"(DF_NaN), "r"(DF_ANY)
        : "r2", "usr");
    check64(df_result, DF_HEX_NAN);
    check_fpstatus(usr, 0);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2df(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(df_result), "=r"(usr) : "r"(SF_NaN)
        : "r2", "usr");
    check64(df_result, DF_HEX_NAN);
    check_fpstatus(usr, 0);
}

static void check_invsqrta(void)
{
    int result;
    int predval;

    asm volatile("%0,p0 = sfinvsqrta(%2)\n\t"
                 "%1 = p0\n\t"
                 : "+r"(result), "=r"(predval)
                 : "r"(0x7f800000)
                 : "p0");
    check32(result, 0xff800000);
    check32(predval, 0x0);
}

static void check_float2int_convs()
{
    int res32;
    long long res64;
    int usr;

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
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2uw(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(SF_small_neg)
        : "r2", "usr");
    check32(res32, 0);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2ud(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2ud(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2uw(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check32(res32, 0);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2uw(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check32(res32, 0);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2ud(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2ud(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_small_neg)
        : "r2", "usr");
    check64(res64, 0);
    check_fpstatus(usr, FPINVF);

    /*
     * Check that the various forms of float-to-signed return -1 for NaN
     */
    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2w(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(SF_NaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2w(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(SF_NaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2d(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_NaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_sf2d(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(SF_NaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2w(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_NaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2w(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res32), "=r"(usr) : "r"(DF_NaN)
        : "r2", "usr");
    check32(res32, -1);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2d(%2)\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_NaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, FPINVF);

    asm(CLEAR_FPSTATUS
        "%0 = convert_df2d(%2):chop\n\t"
        "%1 = usr\n\t"
        : "=r"(res64), "=r"(usr) : "r"(DF_NaN)
        : "r2", "usr");
    check64(res64, -1);
    check_fpstatus(usr, FPINVF);
}

int main()
{
    check_compare_exception();
    check_sfminmax();
    check_dfminmax();
    check_recip_exception();
    check_canonical_NaN();
    check_invsqrta();
    check_float2int_convs();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
