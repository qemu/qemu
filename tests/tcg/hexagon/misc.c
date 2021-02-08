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
#include <string.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;


static inline void S4_storerhnew_rr(void *p, int index, uint16_t v)
{
  asm volatile("{\n\t"
               "    r0 = %0\n\n"
               "    memh(%1+%2<<#2) = r0.new\n\t"
               "}\n"
               :: "r"(v), "r"(p), "r"(index)
               : "r0", "memory");
}

static uint32_t data;
static inline void *S4_storerbnew_ap(uint8_t v)
{
  void *ret;
  asm volatile("{\n\t"
               "    r0 = %1\n\n"
               "    memb(%0 = ##data) = r0.new\n\t"
               "}\n"
               : "=r"(ret)
               : "r"(v)
               : "r0", "memory");
  return ret;
}

static inline void *S4_storerhnew_ap(uint16_t v)
{
  void *ret;
  asm volatile("{\n\t"
               "    r0 = %1\n\n"
               "    memh(%0 = ##data) = r0.new\n\t"
               "}\n"
               : "=r"(ret)
               : "r"(v)
               : "r0", "memory");
  return ret;
}

static inline void *S4_storerinew_ap(uint32_t v)
{
  void *ret;
  asm volatile("{\n\t"
               "    r0 = %1\n\n"
               "    memw(%0 = ##data) = r0.new\n\t"
               "}\n"
               : "=r"(ret)
               : "r"(v)
               : "r0", "memory");
  return ret;
}

static inline void S4_storeirbt_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (p0) memb(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirbf_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (!p0) memb(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirbtnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (p0.new) memb(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirbfnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (!p0.new) memb(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirht_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (p0) memh(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirhf_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (!p0) memh(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirhtnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (p0.new) memh(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirhfnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (!p0.new) memh(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirit_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (p0) memw(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirif_io(void *p, int pred)
{
  asm volatile("p0 = cmp.eq(%0, #1)\n\t"
               "if (!p0) memw(%1+#4)=#27\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeiritnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (p0.new) memw(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

static inline void S4_storeirifnew_io(void *p, int pred)
{
  asm volatile("{\n\t"
               "    p0 = cmp.eq(%0, #1)\n\t"
               "    if (!p0.new) memw(%1+#4)=#27\n\t"
               "}\n\t"
               :: "r"(pred), "r"(p)
               : "p0", "memory");
}

/*
 * Test that compound-compare-jump is executed in 2 parts
 * First we have to do all the compares in the packet and
 * account for auto-anding.  Then, we can do the predicated
 * jump.
 */
static inline int cmpnd_cmp_jump(void)
{
    int retval;
    asm ("r5 = #7\n\t"
         "r6 = #9\n\t"
         "{\n\t"
         "    p0 = cmp.eq(r5, #7)\n\t"
         "    if (p0.new) jump:nt 1f\n\t"
         "    p0 = cmp.eq(r6, #7)\n\t"
         "}\n\t"
         "%0 = #12\n\t"
         "jump 2f\n\t"
         "1:\n\t"
         "%0 = #13\n\t"
         "2:\n\t"
         : "=r"(retval) :: "r5", "r6", "p0");
    return retval;
}

static inline int test_clrtnew(int arg1, int old_val)
{
  int ret;
  asm volatile("r5 = %2\n\t"
               "{\n\t"
                   "p0 = cmp.eq(%1, #1)\n\t"
                   "if (p0.new) r5=#0\n\t"
               "}\n\t"
               "%0 = r5\n\t"
               : "=r"(ret)
               : "r"(arg1), "r"(old_val)
               : "p0", "r5");
  return ret;
}

int err;

static void check(int val, int expect)
{
    if (val != expect) {
        printf("ERROR: 0x%04x != 0x%04x\n", val, expect);
        err++;
    }
}

uint32_t init[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
uint32_t array[10];

uint32_t early_exit;

/*
 * Write this as a function because we can't guarantee the compiler will
 * allocate a frame with just the SL2_return_tnew packet.
 */
static void SL2_return_tnew(int x);
asm ("SL2_return_tnew:\n\t"
     "   allocframe(#0)\n\t"
     "   r1 = #1\n\t"
     "   memw(##early_exit) = r1\n\t"
     "   {\n\t"
     "       p0 = cmp.eq(r0, #1)\n\t"
     "       if (p0.new) dealloc_return:nt\n\t"    /* SL2_return_tnew */
     "   }\n\t"
     "   r1 = #0\n\t"
     "   memw(##early_exit) = r1\n\t"
     "   dealloc_return\n\t"
    );

static long long creg_pair(int x, int y)
{
    long long retval;
    asm ("m0 = %1\n\t"
         "m1 = %2\n\t"
         "%0 = c7:6\n\t"
         : "=r"(retval) : "r"(x), "r"(y) : "m0", "m1");
    return retval;
}

int main()
{

    memcpy(array, init, sizeof(array));
    S4_storerhnew_rr(array, 4, 0xffff);
    check(array[4], 0xffff);

    data = ~0;
    check((uint32_t)S4_storerbnew_ap(0x12), (uint32_t)&data);
    check(data, 0xffffff12);

    data = ~0;
    check((uint32_t)S4_storerhnew_ap(0x1234), (uint32_t)&data);
    check(data, 0xffff1234);

    data = ~0;
    check((uint32_t)S4_storerinew_ap(0x12345678), (uint32_t)&data);
    check(data, 0x12345678);

    /* Byte */
    memcpy(array, init, sizeof(array));
    S4_storeirbt_io(&array[1], 1);
    check(array[2], 27);
    S4_storeirbt_io(&array[2], 0);
    check(array[3], 3);

    memcpy(array, init, sizeof(array));
    S4_storeirbf_io(&array[3], 0);
    check(array[4], 27);
    S4_storeirbf_io(&array[4], 1);
    check(array[5], 5);

    memcpy(array, init, sizeof(array));
    S4_storeirbtnew_io(&array[5], 1);
    check(array[6], 27);
    S4_storeirbtnew_io(&array[6], 0);
    check(array[7], 7);

    memcpy(array, init, sizeof(array));
    S4_storeirbfnew_io(&array[7], 0);
    check(array[8], 27);
    S4_storeirbfnew_io(&array[8], 1);
    check(array[9], 9);

    /* Half word */
    memcpy(array, init, sizeof(array));
    S4_storeirht_io(&array[1], 1);
    check(array[2], 27);
    S4_storeirht_io(&array[2], 0);
    check(array[3], 3);

    memcpy(array, init, sizeof(array));
    S4_storeirhf_io(&array[3], 0);
    check(array[4], 27);
    S4_storeirhf_io(&array[4], 1);
    check(array[5], 5);

    memcpy(array, init, sizeof(array));
    S4_storeirhtnew_io(&array[5], 1);
    check(array[6], 27);
    S4_storeirhtnew_io(&array[6], 0);
    check(array[7], 7);

    memcpy(array, init, sizeof(array));
    S4_storeirhfnew_io(&array[7], 0);
    check(array[8], 27);
    S4_storeirhfnew_io(&array[8], 1);
    check(array[9], 9);

    /* Word */
    memcpy(array, init, sizeof(array));
    S4_storeirit_io(&array[1], 1);
    check(array[2], 27);
    S4_storeirit_io(&array[2], 0);
    check(array[3], 3);

    memcpy(array, init, sizeof(array));
    S4_storeirif_io(&array[3], 0);
    check(array[4], 27);
    S4_storeirif_io(&array[4], 1);
    check(array[5], 5);

    memcpy(array, init, sizeof(array));
    S4_storeiritnew_io(&array[5], 1);
    check(array[6], 27);
    S4_storeiritnew_io(&array[6], 0);
    check(array[7], 7);

    memcpy(array, init, sizeof(array));
    S4_storeirifnew_io(&array[7], 0);
    check(array[8], 27);
    S4_storeirifnew_io(&array[8], 1);
    check(array[9], 9);

    int x = cmpnd_cmp_jump();
    check(x, 12);

    SL2_return_tnew(0);
    check(early_exit, 0);
    SL2_return_tnew(1);
    check(early_exit, 1);

    long long pair = creg_pair(5, 7);
    check((int)pair, 5);
    check((int)(pair >> 32), 7);

    int res = test_clrtnew(1, 7);
    check(res, 0);
    res = test_clrtnew(2, 7);
    check(res, 7);

    puts(err ? "FAIL" : "PASS");
    return err;
}
