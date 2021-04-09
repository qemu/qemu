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
 * Test load align instructions
 *
 * Example
 *     r1:0 = memh_fifo(r1+#0)
 * loads a half word from memory, shifts the destination register
 * right by one half word and inserts the loaded value into the high
 * half word of the destination.
 *
 * There are 8 addressing modes and byte and half word variants, for a
 * total of 16 instructions to test
 */

#include <stdio.h>
#include <string.h>

int err;

char buf[16] __attribute__((aligned(1 << 16)));

void init_buf(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        buf[i] = i + 1;
    }
}

void __check(int line, long long result, long long expect)
{
    if (result != expect) {
        printf("ERROR at line %d: 0x%016llx != 0x%016llx\n",
               line, result, expect);
        err++;
    }
}

#define check(RES, EXP) __check(__LINE__, RES, EXP)

void __checkp(int line, void *p, void *expect)
{
    if (p != expect) {
        printf("ERROR at line %d: 0x%p != 0x%p\n", line, p, expect);
        err++;
    }
}

#define checkp(RES, EXP) __checkp(__LINE__, RES, EXP)

/*
 ****************************************************************************
 * _io addressing mode (addr + offset)
 */
#define LOAD_io(SZ, RES, ADDR, OFF) \
    __asm__( \
        "%0 = mem" #SZ "_fifo(%1+#" #OFF ")\n\t" \
        : "+r"(RES) \
        : "r"(ADDR))
#define LOAD_io_b(RES, ADDR, OFF) \
    LOAD_io(b, RES, ADDR, OFF)
#define LOAD_io_h(RES, ADDR, OFF) \
    LOAD_io(h, RES, ADDR, OFF)

#define TEST_io(NAME, SZ, SIZE, EXP1, EXP2, EXP3, EXP4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    LOAD_io_##SZ(result, buf, 0 * (SIZE)); \
    check(result, (EXP1)); \
    LOAD_io_##SZ(result, buf, 1 * (SIZE)); \
    check(result, (EXP2)); \
    LOAD_io_##SZ(result, buf, 2 * (SIZE)); \
    check(result, (EXP3)); \
    LOAD_io_##SZ(result, buf, 3 * (SIZE)); \
    check(result, (EXP4)); \
}

TEST_io(loadalignb_io, b, 1,
        0x01ffffffffffffffLL, 0x0201ffffffffffffLL,
        0x030201ffffffffffLL, 0x04030201ffffffffLL)
TEST_io(loadalignh_io, h, 2,
        0x0201ffffffffffffLL, 0x04030201ffffffffLL,
        0x060504030201ffffLL, 0x0807060504030201LL)

/*
 ****************************************************************************
 * _ur addressing mode (index << offset + base)
 */
#define LOAD_ur(SZ, RES, SHIFT, IDX) \
    __asm__( \
        "%0 = mem" #SZ "_fifo(%1<<#" #SHIFT " + ##buf)\n\t" \
        : "+r"(RES) \
        : "r"(IDX))
#define LOAD_ur_b(RES, SHIFT, IDX) \
    LOAD_ur(b, RES, SHIFT, IDX)
#define LOAD_ur_h(RES, SHIFT, IDX) \
    LOAD_ur(h, RES, SHIFT, IDX)

#define TEST_ur(NAME, SZ, SHIFT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    LOAD_ur_##SZ(result, (SHIFT), 0); \
    check(result, (RES1)); \
    LOAD_ur_##SZ(result, (SHIFT), 1); \
    check(result, (RES2)); \
    LOAD_ur_##SZ(result, (SHIFT), 2); \
    check(result, (RES3)); \
    LOAD_ur_##SZ(result, (SHIFT), 3); \
    check(result, (RES4)); \
}

TEST_ur(loadalignb_ur, b, 1,
        0x01ffffffffffffffLL, 0x0301ffffffffffffLL,
        0x050301ffffffffffLL, 0x07050301ffffffffLL)
TEST_ur(loadalignh_ur, h, 1,
        0x0201ffffffffffffLL, 0x04030201ffffffffLL,
        0x060504030201ffffLL, 0x0807060504030201LL)

/*
 ****************************************************************************
 * _ap addressing mode (addr = base)
 */
#define LOAD_ap(SZ, RES, PTR, ADDR) \
    __asm__(  \
        "%0 = mem" #SZ "_fifo(%1 = ##" #ADDR ")\n\t" \
        : "+r"(RES), "=r"(PTR))
#define LOAD_ap_b(RES, PTR, ADDR) \
    LOAD_ap(b, RES, PTR, ADDR)
#define LOAD_ap_h(RES, PTR, ADDR) \
    LOAD_ap(h, RES, PTR, ADDR)

#define TEST_ap(NAME, SZ, SIZE, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    void *ptr; \
    LOAD_ap_##SZ(result, ptr, (buf + 0 * (SIZE))); \
    check(result, (RES1)); \
    checkp(ptr, &buf[0 * (SIZE)]); \
    LOAD_ap_##SZ(result, ptr, (buf + 1 * (SIZE))); \
    check(result, (RES2)); \
    checkp(ptr, &buf[1 * (SIZE)]); \
    LOAD_ap_##SZ(result, ptr, (buf + 2 * (SIZE))); \
    check(result, (RES3)); \
    checkp(ptr, &buf[2 * (SIZE)]); \
    LOAD_ap_##SZ(result, ptr, (buf + 3 * (SIZE))); \
    check(result, (RES4)); \
    checkp(ptr, &buf[3 * (SIZE)]); \
}

TEST_ap(loadalignb_ap, b, 1,
        0x01ffffffffffffffLL, 0x0201ffffffffffffLL,
        0x030201ffffffffffLL, 0x04030201ffffffffLL)
TEST_ap(loadalignh_ap, h, 2,
        0x0201ffffffffffffLL, 0x04030201ffffffffLL,
        0x060504030201ffffLL, 0x0807060504030201LL)

/*
 ****************************************************************************
 * _rp addressing mode (addr ++ modifer-reg)
 */
#define LOAD_pr(SZ, RES, PTR, INC) \
    __asm__( \
        "m0 = %2\n\t" \
        "%0 = mem" #SZ "_fifo(%1++m0)\n\t" \
        : "+r"(RES), "+r"(PTR) \
        : "r"(INC) \
        : "m0")
#define LOAD_pr_b(RES, PTR, INC) \
    LOAD_pr(b, RES, PTR, INC)
#define LOAD_pr_h(RES, PTR, INC) \
    LOAD_pr(h, RES, PTR, INC)

#define TEST_pr(NAME, SZ, SIZE, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    void *ptr = buf; \
    LOAD_pr_##SZ(result, ptr, (SIZE)); \
    check(result, (RES1)); \
    checkp(ptr, &buf[1 * (SIZE)]); \
    LOAD_pr_##SZ(result, ptr, (SIZE)); \
    check(result, (RES2)); \
    checkp(ptr, &buf[2 * (SIZE)]); \
    LOAD_pr_##SZ(result, ptr, (SIZE)); \
    check(result, (RES3)); \
    checkp(ptr, &buf[3 * (SIZE)]); \
    LOAD_pr_##SZ(result, ptr, (SIZE)); \
    check(result, (RES4)); \
    checkp(ptr, &buf[4 * (SIZE)]); \
}

TEST_pr(loadalignb_pr, b, 1,
        0x01ffffffffffffffLL, 0x0201ffffffffffffLL,
        0x030201ffffffffffLL, 0x04030201ffffffffLL)
TEST_pr(loadalignh_pr, h, 2,
        0x0201ffffffffffffLL, 0x04030201ffffffffLL,
        0x060504030201ffffLL, 0x0807060504030201LL)

/*
 ****************************************************************************
 * _pbr addressing mode (addr ++ modifer-reg:brev)
 */
#define LOAD_pbr(SZ, RES, PTR) \
    __asm__( \
        "r4 = #(1 << (16 - 3))\n\t" \
        "m0 = r4\n\t" \
        "%0 = mem" #SZ "_fifo(%1++m0:brev)\n\t" \
        : "+r"(RES), "+r"(PTR) \
        : \
        : "r4", "m0")
#define LOAD_pbr_b(RES, PTR) \
    LOAD_pbr(b, RES, PTR)
#define LOAD_pbr_h(RES, PTR) \
    LOAD_pbr(h, RES, PTR)

#define TEST_pbr(NAME, SZ, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    void *ptr = buf; \
    LOAD_pbr_##SZ(result, ptr); \
    check(result, (RES1)); \
    LOAD_pbr_##SZ(result, ptr); \
    check(result, (RES2)); \
    LOAD_pbr_##SZ(result, ptr); \
    check(result, (RES3)); \
    LOAD_pbr_##SZ(result, ptr); \
    check(result, (RES4)); \
}

TEST_pbr(loadalignb_pbr, b,
    0x01ffffffffffffffLL, 0x0501ffffffffffffLL,
    0x030501ffffffffffLL, 0x07030501ffffffffLL)
TEST_pbr(loadalignh_pbr, h,
    0x0201ffffffffffffLL, 0x06050201ffffffffLL,
    0x040306050201ffffLL, 0x0807040306050201LL)

/*
 ****************************************************************************
 * _pi addressing mode (addr ++ inc)
 */
#define LOAD_pi(SZ, RES, PTR, INC) \
    __asm__( \
        "%0 = mem" #SZ "_fifo(%1++#" #INC ")\n\t" \
        : "+r"(RES), "+r"(PTR))
#define LOAD_pi_b(RES, PTR, INC) \
    LOAD_pi(b, RES, PTR, INC)
#define LOAD_pi_h(RES, PTR, INC) \
    LOAD_pi(h, RES, PTR, INC)

#define TEST_pi(NAME, SZ, INC, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    void *ptr = buf; \
    LOAD_pi_##SZ(result, ptr, (INC)); \
    check(result, (RES1)); \
    checkp(ptr, &buf[1 * (INC)]); \
    LOAD_pi_##SZ(result, ptr, (INC)); \
    check(result, (RES2)); \
    checkp(ptr, &buf[2 * (INC)]); \
    LOAD_pi_##SZ(result, ptr, (INC)); \
    check(result, (RES3)); \
    checkp(ptr, &buf[3 * (INC)]); \
    LOAD_pi_##SZ(result, ptr, (INC)); \
    check(result, (RES4)); \
    checkp(ptr, &buf[4 * (INC)]); \
}

TEST_pi(loadalignb_pi, b, 1,
        0x01ffffffffffffffLL, 0x0201ffffffffffffLL,
        0x030201ffffffffffLL, 0x04030201ffffffffLL)
TEST_pi(loadalignh_pi, h, 2,
        0x0201ffffffffffffLL, 0x04030201ffffffffLL,
        0x060504030201ffffLL, 0x0807060504030201LL)

/*
 ****************************************************************************
 * _pci addressing mode (addr ++ inc:circ)
 */
#define LOAD_pci(SZ, RES, PTR, START, LEN, INC) \
    __asm__( \
        "r4 = %3\n\t" \
        "m0 = r4\n\t" \
        "cs0 = %2\n\t" \
        "%0 = mem" #SZ "_fifo(%1++#" #INC ":circ(m0))\n\t" \
        : "+r"(RES), "+r"(PTR) \
        : "r"(START), "r"(LEN) \
        : "r4", "m0", "cs0")
#define LOAD_pci_b(RES, PTR, START, LEN, INC) \
    LOAD_pci(b, RES, PTR, START, LEN, INC)
#define LOAD_pci_h(RES, PTR, START, LEN, INC) \
    LOAD_pci(h, RES, PTR, START, LEN, INC)

#define TEST_pci(NAME, SZ, LEN, INC, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    void *ptr = buf; \
    LOAD_pci_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES1)); \
    checkp(ptr, &buf[(1 * (INC)) % (LEN)]); \
    LOAD_pci_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES2)); \
    checkp(ptr, &buf[(2 * (INC)) % (LEN)]); \
    LOAD_pci_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES3)); \
    checkp(ptr, &buf[(3 * (INC)) % (LEN)]); \
    LOAD_pci_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES4)); \
    checkp(ptr, &buf[(4 * (INC)) % (LEN)]); \
}

TEST_pci(loadalignb_pci, b, 2, 1,
    0x01ffffffffffffffLL, 0x0201ffffffffffffLL,
    0x010201ffffffffffLL, 0x02010201ffffffffLL)
TEST_pci(loadalignh_pci, h, 4, 2,
    0x0201ffffffffffffLL, 0x04030201ffffffffLL,
    0x020104030201ffffLL, 0x0403020104030201LL)

/*
 ****************************************************************************
 * _pcr addressing mode (addr ++ I:circ(modifier-reg))
 */
#define LOAD_pcr(SZ, RES, PTR, START, LEN, INC) \
    __asm__( \
        "r4 = %2\n\t" \
        "m1 = r4\n\t" \
        "cs1 = %3\n\t" \
        "%0 = mem" #SZ "_fifo(%1++I:circ(m1))\n\t" \
        : "+r"(RES), "+r"(PTR) \
        : "r"((((INC) & 0x7f) << 17) | ((LEN) & 0x1ffff)), \
          "r"(START) \
        : "r4", "m1", "cs1")
#define LOAD_pcr_b(RES, PTR, START, LEN, INC) \
    LOAD_pcr(b, RES, PTR, START, LEN, INC)
#define LOAD_pcr_h(RES, PTR, START, LEN, INC) \
    LOAD_pcr(h, RES, PTR, START, LEN, INC)

#define TEST_pcr(NAME, SZ, SIZE, LEN, INC, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    long long result = ~0LL; \
    void *ptr = buf; \
    LOAD_pcr_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES1)); \
    checkp(ptr, &buf[(1 * (INC) * (SIZE)) % (LEN)]); \
    LOAD_pcr_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES2)); \
    checkp(ptr, &buf[(2 * (INC) * (SIZE)) % (LEN)]); \
    LOAD_pcr_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES3)); \
    checkp(ptr, &buf[(3 * (INC) * (SIZE)) % (LEN)]); \
    LOAD_pcr_##SZ(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES4)); \
    checkp(ptr, &buf[(4 * (INC) * (SIZE)) % (LEN)]); \
}

TEST_pcr(loadalignb_pcr, b, 1, 2, 1,
    0x01ffffffffffffffLL, 0x0201ffffffffffffLL,
    0x010201ffffffffffLL, 0x02010201ffffffffLL)
TEST_pcr(loadalignh_pcr, h, 2, 4, 1,
    0x0201ffffffffffffLL, 0x04030201ffffffffLL,
    0x020104030201ffffLL, 0x0403020104030201LL)

int main()
{
    init_buf();

    test_loadalignb_io();
    test_loadalignh_io();

    test_loadalignb_ur();
    test_loadalignh_ur();

    test_loadalignb_ap();
    test_loadalignh_ap();

    test_loadalignb_pr();
    test_loadalignh_pr();

    test_loadalignb_pbr();
    test_loadalignh_pbr();

    test_loadalignb_pi();
    test_loadalignh_pi();

    test_loadalignb_pci();
    test_loadalignh_pci();

    test_loadalignb_pcr();
    test_loadalignh_pcr();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
