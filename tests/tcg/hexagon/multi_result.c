/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include <stdint.h>
#include <stdbool.h>

int err;

#include "hex_test.h"

static int32_t sfrecipa(int32_t Rs, int32_t Rt, bool *pred_result)
{
  int32_t result;
  bool predval;

  asm volatile("%0,p0 = sfrecipa(%2, %3)\n\t"
               "%1 = p0\n\t"
               : "+r"(result), "=r"(predval)
               : "r"(Rs), "r"(Rt)
               : "p0");
  *pred_result = predval;
  return result;
}

static int32_t sfinvsqrta(int32_t Rs, int32_t *pred_result)
{
  int32_t result;
  int32_t predval;

  asm volatile("%0,p0 = sfinvsqrta(%2)\n\t"
               "%1 = p0\n\t"
               : "+r"(result), "=r"(predval)
               : "r"(Rs)
               : "p0");
  *pred_result = predval;
  return result;
}

static int64_t vacsh(int64_t Rxx, int64_t Rss, int64_t Rtt,
                     int *pred_result, bool *ovf_result)
{
  int64_t result = Rxx;
  int predval;
  uint32_t usr;

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

static int64_t vminub(int64_t Rtt, int64_t Rss, int32_t *pred_result)
{
  int64_t result;
  int32_t predval;

  asm volatile("%0,p0 = vminub(%2, %3)\n\t"
               "%1 = p0\n\t"
               : "=r"(result), "=r"(predval)
               : "r"(Rtt), "r"(Rss)
               : "p0");
  *pred_result = predval;
  return result;
}

static int64_t add_carry(int64_t Rss, int64_t Rtt,
                         int32_t pred_in, int32_t *pred_result)
{
  int64_t result;
  int32_t predval = pred_in;

  asm volatile("p0 = %1\n\t"
               "%0 = add(%2, %3, p0):carry\n\t"
               "%1 = p0\n\t"
               : "=r"(result), "+r"(predval)
               : "r"(Rss), "r"(Rtt)
               : "p0");
  *pred_result = predval;
  return result;
}

static int64_t sub_carry(int64_t Rss, int64_t Rtt,
                         int32_t pred_in, int32_t *pred_result)
{
  int64_t result;
  int32_t predval = pred_in;

  asm volatile("p0 = !cmp.eq(%1, #0)\n\t"
               "%0 = sub(%2, %3, p0):carry\n\t"
               "%1 = p0\n\t"
               : "=r"(result), "+r"(predval)
               : "r"(Rss), "r"(Rtt)
               : "p0");
  *pred_result = predval;
  return result;
}

static void test_sfrecipa()
{
    int32_t res;
    bool pred_result;

    res = sfrecipa(0x04030201, 0x05060708, &pred_result);
    check32(res, 0x59f38001);
    check32(pred_result, false);
}

static void test_sfinvsqrta()
{
    int32_t res;
    int32_t pred_result;

    res = sfinvsqrta(0x04030201, &pred_result);
    check32(res, 0x4d330000);
    check32(pred_result, 0xe0);

    res = sfinvsqrta(0x0, &pred_result);
    check32(res, 0x3f800000);
    check32(pred_result, 0x0);
}

static void test_vacsh()
{
    int64_t res64;
    int32_t pred_result;
    bool ovf_result;

    res64 = vacsh(0x0004000300020001LL,
                  0x0001000200030004LL,
                  0x0000000000000000LL, &pred_result, &ovf_result);
    check64(res64, 0x0004000300030004LL);
    check32(pred_result, 0xf0);
    check32(ovf_result, false);

    res64 = vacsh(0x0004000300020001LL,
                  0x0001000200030004LL,
                  0x000affff000d0000LL, &pred_result, &ovf_result);
    check64(res64, 0x000e0003000f0004LL);
    check32(pred_result, 0xcc);
    check32(ovf_result, false);

    res64 = vacsh(0x00047fff00020001LL,
                  0x00017fff00030004LL,
                  0x000a0fff000d0000LL, &pred_result, &ovf_result);
    check64(res64, 0x000e7fff000f0004LL);
    check32(pred_result, 0xfc);
    check32(ovf_result, true);

    res64 = vacsh(0x0004000300020001LL,
                  0x0001000200030009LL,
                  0x000affff000d0001LL, &pred_result, &ovf_result);
    check64(res64, 0x000e0003000f0008LL);
    check32(pred_result, 0xcc);
    check32(ovf_result, false);
}

static void test_vminub()
{
    int64_t res64;
    int32_t pred_result;

    res64 = vminub(0x0807060504030201LL,
                   0x0102030405060708LL,
                   &pred_result);
    check64(res64, 0x0102030404030201LL);
    check32(pred_result, 0xf0);

    res64 = vminub(0x0802060405030701LL,
                   0x0107030504060208LL,
                   &pred_result);
    check64(res64, 0x0102030404030201LL);
    check32(pred_result, 0xaa);
}

static void test_add_carry()
{
    int64_t res64;
    int32_t pred_result;

    res64 = add_carry(0x0000000000000000LL,
                      0xffffffffffffffffLL,
                      1, &pred_result);
    check64(res64, 0x0000000000000000LL);
    check32(pred_result, 0xff);

    res64 = add_carry(0x0000000100000000LL,
                      0xffffffffffffffffLL,
                      0, &pred_result);
    check64(res64, 0x00000000ffffffffLL);
    check32(pred_result, 0xff);

    res64 = add_carry(0x0000000100000000LL,
                      0xffffffffffffffffLL,
                      0, &pred_result);
    check64(res64, 0x00000000ffffffffLL);
    check32(pred_result, 0xff);
}

static void test_sub_carry()
{
    int64_t res64;
    int32_t pred_result;

    res64 = sub_carry(0x0000000000000000LL,
                      0x0000000000000000LL,
                      1, &pred_result);
    check64(res64, 0x0000000000000000LL);
    check32(pred_result, 0xff);

    res64 = sub_carry(0x0000000100000000LL,
                      0x0000000000000000LL,
                      0, &pred_result);
    check64(res64, 0x00000000ffffffffLL);
    check32(pred_result, 0xff);

    res64 = sub_carry(0x0000000100000000LL,
                      0x0000000000000000LL,
                      0, &pred_result);
    check64(res64, 0x00000000ffffffffLL);
    check32(pred_result, 0xff);
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
