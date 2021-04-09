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

#include <stdio.h>

static int sfrecipa(int Rs, int Rt, int *pred_result)
{
  int result;
  int predval;

  asm volatile("%0,p0 = sfrecipa(%2, %3)\n\t"
               "%1 = p0\n\t"
               : "+r"(result), "=r"(predval)
               : "r"(Rs), "r"(Rt)
               : "p0");
  *pred_result = predval;
  return result;
}

static int sfinvsqrta(int Rs, int *pred_result)
{
  int result;
  int predval;

  asm volatile("%0,p0 = sfinvsqrta(%2)\n\t"
               "%1 = p0\n\t"
               : "+r"(result), "=r"(predval)
               : "r"(Rs)
               : "p0");
  *pred_result = predval;
  return result;
}

static long long vacsh(long long Rxx, long long Rss, long long Rtt,
                       int *pred_result, int *ovf_result)
{
  long long result = Rxx;
  int predval;
  int usr;

  /*
   * This instruction can set bit 0 (OVF/overflow) in usr
   * Clear the bit first, then return that bit to the caller
   */
  asm volatile("r2 = usr\n\t"
               "r2 = clrbit(r2, #0)\n\t"        /* clear overflow bit */
               "usr = r2\n\t"
               "%0,p0 = vacsh(%3, %4)\n\t"
               "%1 = p0\n\t"
               "%2 = usr\n\t"
               : "+r"(result), "=r"(predval), "=r"(usr)
               : "r"(Rss), "r"(Rtt)
               : "r2", "p0", "usr");
  *pred_result = predval;
  *ovf_result = (usr & 1);
  return result;
}

static long long vminub(long long Rtt, long long Rss,
                        int *pred_result)
{
  long long result;
  int predval;

  asm volatile("%0,p0 = vminub(%2, %3)\n\t"
               "%1 = p0\n\t"
               : "=r"(result), "=r"(predval)
               : "r"(Rtt), "r"(Rss)
               : "p0");
  *pred_result = predval;
  return result;
}

static long long add_carry(long long Rss, long long Rtt,
                           int pred_in, int *pred_result)
{
  long long result;
  int predval = pred_in;

  asm volatile("p0 = %1\n\t"
               "%0 = add(%2, %3, p0):carry\n\t"
               "%1 = p0\n\t"
               : "=r"(result), "+r"(predval)
               : "r"(Rss), "r"(Rtt)
               : "p0");
  *pred_result = predval;
  return result;
}

static long long sub_carry(long long Rss, long long Rtt,
                           int pred_in, int *pred_result)
{
  long long result;
  int predval = pred_in;

  asm volatile("p0 = !cmp.eq(%1, #0)\n\t"
               "%0 = sub(%2, %3, p0):carry\n\t"
               "%1 = p0\n\t"
               : "=r"(result), "+r"(predval)
               : "r"(Rss), "r"(Rtt)
               : "p0");
  *pred_result = predval;
  return result;
}

int err;

static void check_ll(long long val, long long expect)
{
    if (val != expect) {
        printf("ERROR: 0x%016llx != 0x%016llx\n", val, expect);
        err++;
    }
}

static void check(int val, int expect)
{
    if (val != expect) {
        printf("ERROR: 0x%08x != 0x%08x\n", val, expect);
        err++;
    }
}

static void check_p(int val, int expect)
{
    if (val != expect) {
        printf("ERROR: 0x%02x != 0x%02x\n", val, expect);
        err++;
    }
}

static void test_sfrecipa()
{
    int res;
    int pred_result;

    res = sfrecipa(0x04030201, 0x05060708, &pred_result);
    check(res, 0x59f38001);
    check_p(pred_result, 0x00);
}

static void test_sfinvsqrta()
{
    int res;
    int pred_result;

    res = sfinvsqrta(0x04030201, &pred_result);
    check(res, 0x4d330000);
    check_p(pred_result, 0xe0);

    res = sfinvsqrta(0x0, &pred_result);
    check(res, 0x3f800000);
    check_p(pred_result, 0x0);
}

static void test_vacsh()
{
    long long res64;
    int pred_result;
    int ovf_result;

    res64 = vacsh(0x0004000300020001LL,
                  0x0001000200030004LL,
                  0x0000000000000000LL, &pred_result, &ovf_result);
    check_ll(res64, 0x0004000300030004LL);
    check_p(pred_result, 0xf0);
    check(ovf_result, 0);

    res64 = vacsh(0x0004000300020001LL,
                  0x0001000200030004LL,
                  0x000affff000d0000LL, &pred_result, &ovf_result);
    check_ll(res64, 0x000e0003000f0004LL);
    check_p(pred_result, 0xcc);
    check(ovf_result, 0);

    res64 = vacsh(0x00047fff00020001LL,
                  0x00017fff00030004LL,
                  0x000a0fff000d0000LL, &pred_result, &ovf_result);
    check_ll(res64, 0x000e7fff000f0004LL);
    check_p(pred_result, 0xfc);
    check(ovf_result, 1);

    res64 = vacsh(0x0004000300020001LL,
                  0x0001000200030009LL,
                  0x000affff000d0001LL, &pred_result, &ovf_result);
    check_ll(res64, 0x000e0003000f0008LL);
    check_p(pred_result, 0xcc);
    check(ovf_result, 0);
}

static void test_vminub()
{
    long long res64;
    int pred_result;

    res64 = vminub(0x0807060504030201LL,
                   0x0102030405060708LL,
                   &pred_result);
    check_ll(res64, 0x0102030404030201LL);
    check_p(pred_result, 0xf0);

    res64 = vminub(0x0802060405030701LL,
                   0x0107030504060208LL,
                   &pred_result);
    check_ll(res64, 0x0102030404030201LL);
    check_p(pred_result, 0xaa);
}

static void test_add_carry()
{
    long long res64;
    int pred_result;

    res64 = add_carry(0x0000000000000000LL,
                      0xffffffffffffffffLL,
                      1, &pred_result);
    check_ll(res64, 0x0000000000000000LL);
    check_p(pred_result, 0xff);

    res64 = add_carry(0x0000000100000000LL,
                      0xffffffffffffffffLL,
                      0, &pred_result);
    check_ll(res64, 0x00000000ffffffffLL);
    check_p(pred_result, 0xff);

    res64 = add_carry(0x0000000100000000LL,
                      0xffffffffffffffffLL,
                      0, &pred_result);
    check_ll(res64, 0x00000000ffffffffLL);
    check_p(pred_result, 0xff);
}

static void test_sub_carry()
{
    long long res64;
    int pred_result;

    res64 = sub_carry(0x0000000000000000LL,
                      0x0000000000000000LL,
                      1, &pred_result);
    check_ll(res64, 0x0000000000000000LL);
    check_p(pred_result, 0xff);

    res64 = sub_carry(0x0000000100000000LL,
                      0x0000000000000000LL,
                      0, &pred_result);
    check_ll(res64, 0x00000000ffffffffLL);
    check_p(pred_result, 0xff);

    res64 = sub_carry(0x0000000100000000LL,
                      0x0000000000000000LL,
                      0, &pred_result);
    check_ll(res64, 0x00000000ffffffffLL);
    check_p(pred_result, 0xff);
}

int main()
{
    test_sfrecipa();
    test_sfinvsqrta();
    test_vacsh();
    test_vminub();
    test_add_carry();
    test_sub_carry();

    puts(err ? "FAIL" : "PASS");
    return err;
}
