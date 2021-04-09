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

int err;

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

int main()
{
    test_sfrecipa();

    puts(err ? "FAIL" : "PASS");
    return err;
}
