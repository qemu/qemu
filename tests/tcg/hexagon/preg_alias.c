/*
 *  Copyright(c) 2019-2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

static inline int preg_alias(int v0, int v1, int v2, int v3)
{
  int ret;
  asm volatile("p0 = %1\n\t"
               "p1 = %2\n\t"
               "p2 = %3\n\t"
               "p3 = %4\n\t"
               "%0 = C4\n"
               : "=r"(ret)
               : "r"(v0), "r"(v1), "r"(v2), "r"(v3)
               : "p0", "p1", "p2", "p3");
  return ret;
}

static inline int preg_alias_pair(int v0, int v1, int v2, int v3)
{
  long long c54;
  asm volatile("p0 = %1\n\t"
               "p1 = %2\n\t"
               "p2 = %3\n\t"
               "p3 = %4\n\t"
               "%0 = C5:4\n"
               : "=r"(c54)
               : "r"(v0), "r"(v1), "r"(v2), "r"(v3)
               : "p0", "p1", "p2", "p3");
  return (int)c54;
}

typedef union {
    int creg;
    struct {
      unsigned char p0;
      unsigned char p1;
      unsigned char p2;
      unsigned char p3;
    } pregs;
} PRegs;

static inline void creg_alias(int cval, PRegs *pregs)
{
  asm("c4 = %4\n\t"
      "%0 = p0\n\t"
      "%1 = p1\n\t"
      "%2 = p2\n\t"
      "%3 = p3\n\t"
      : "=r"(pregs->pregs.p0), "=r"(pregs->pregs.p1),
        "=r"(pregs->pregs.p2), "=r"(pregs->pregs.p3)
      : "r"(cval)
      : "p0", "p1", "p2", "p3");
}

int err;

static void check(int val, int expect)
{
    if (val != expect) {
        printf("ERROR: 0x%08x != 0x%08x\n", val, expect);
        err++;
    }
}

static inline void creg_alias_pair(unsigned int cval, PRegs *pregs)
{
  unsigned long long cval_pair = (0xdeadbeefULL << 32) | cval;
  int c5;

  asm ("c5:4 = %5\n\t"
       "%0 = p0\n\t"
       "%1 = p1\n\t"
       "%2 = p2\n\t"
       "%3 = p3\n\t"
       "%4 = c5\n\t"
       : "=r"(pregs->pregs.p0), "=r"(pregs->pregs.p1),
         "=r"(pregs->pregs.p2), "=r"(pregs->pregs.p3), "=r"(c5)
       : "r"(cval_pair)
       : "p0", "p1", "p2", "p3");

  check(c5, 0xdeadbeef);
}

static void test_packet(void)
{
    /*
     * Test that setting c4 inside a packet doesn't impact the predicates
     * that are read during the packet.
     */

    int result;
    int old_val = 0x0000001c;

    /* Test a predicated register transfer */
    result = old_val;
    asm (
         "c4 = %1\n\t"
         "{\n\t"
         "    c4 = %2\n\t"
         "    if (!p2) %0 = %3\n\t"
         "}\n\t"
         : "+r"(result)
         : "r"(0xffffffff), "r"(0xff00ffff), "r"(0x837ed653)
         : "p0", "p1", "p2", "p3");
    check(result, old_val);

    /* Test a predicated store */
    result = 0xffffffff;
    asm ("c4 = %0\n\t"
         "{\n\t"
         "    c4 = %1\n\t"
         "    if (!p2) memw(%2) = #0\n\t"
         "}\n\t"
         :
         : "r"(0), "r"(0xffffffff), "r"(&result)
         : "p0", "p1", "p2", "p3", "memory");
    check(result, 0x0);
}

int main()
{
    int c4;
    PRegs pregs;

    c4 = preg_alias(0xff, 0x00, 0xff, 0x00);
    check(c4, 0x00ff00ff);
    c4 = preg_alias(0xff, 0x00, 0x00, 0x00);
    check(c4, 0x000000ff);
    c4 = preg_alias(0x00, 0xff, 0x00, 0x00);
    check(c4, 0x0000ff00);
    c4 = preg_alias(0x00, 0x00, 0xff, 0x00);
    check(c4, 0x00ff0000);
    c4 = preg_alias(0x00, 0x00, 0x00, 0xff);
    check(c4, 0xff000000);
    c4 = preg_alias(0xff, 0xff, 0xff, 0xff);
    check(c4, 0xffffffff);

    c4 = preg_alias_pair(0xff, 0x00, 0xff, 0x00);
    check(c4, 0x00ff00ff);
      c4 = preg_alias_pair(0xff, 0x00, 0x00, 0x00);
    check(c4, 0x000000ff);
    c4 = preg_alias_pair(0x00, 0xff, 0x00, 0x00);
    check(c4, 0x0000ff00);
    c4 = preg_alias_pair(0x00, 0x00, 0xff, 0x00);
    check(c4, 0x00ff0000);
    c4 = preg_alias_pair(0x00, 0x00, 0x00, 0xff);
    check(c4, 0xff000000);
    c4 = preg_alias_pair(0xff, 0xff, 0xff, 0xff);
    check(c4, 0xffffffff);

    creg_alias(0x00ff00ff, &pregs);
    check(pregs.creg, 0x00ff00ff);
    creg_alias(0x00ffff00, &pregs);
    check(pregs.creg, 0x00ffff00);
    creg_alias(0x00000000, &pregs);
    check(pregs.creg, 0x00000000);
    creg_alias(0xff000000, &pregs);
    check(pregs.creg, 0xff000000);
    creg_alias(0x00ff0000, &pregs);
    check(pregs.creg, 0x00ff0000);
    creg_alias(0x0000ff00, &pregs);
    check(pregs.creg, 0x0000ff00);
    creg_alias(0x000000ff, &pregs);
    check(pregs.creg, 0x000000ff);
    creg_alias(0xffffffff, &pregs);
    check(pregs.creg, 0xffffffff);

    creg_alias_pair(0x00ff00ff, &pregs);
    check(pregs.creg, 0x00ff00ff);
    creg_alias_pair(0x00ffff00, &pregs);
    check(pregs.creg, 0x00ffff00);
    creg_alias_pair(0x00000000, &pregs);
    check(pregs.creg, 0x00000000);
    creg_alias_pair(0xff000000, &pregs);
    check(pregs.creg, 0xff000000);
    creg_alias_pair(0x00ff0000, &pregs);
    check(pregs.creg, 0x00ff0000);
    creg_alias_pair(0x0000ff00, &pregs);
    check(pregs.creg, 0x0000ff00);
    creg_alias_pair(0x000000ff, &pregs);
    check(pregs.creg, 0x000000ff);
    creg_alias_pair(0xffffffff, &pregs);
    check(pregs.creg, 0xffffffff);

    test_packet();

    puts(err ? "FAIL" : "PASS");
    return err;
}
