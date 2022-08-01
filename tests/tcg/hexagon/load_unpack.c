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
 * Test load unpack instructions
 *
 * Example
 *     r0 = memubh(r1+#0)
 * loads a half word from memory and zero-extends the 2 bytes to form a word
 *
 * For each addressing mode, there are 4 tests
 *     bzw2          unsigned     2 elements
 *     bsw2          signed       2 elements
 *     bzw4          unsigned     4 elements
 *     bsw4          signed       4 elements
 * There are 8 addressing modes, for a total of 32 instructions to test
 */

#include <stdio.h>
#include <string.h>

int err;

char buf[16] __attribute__((aligned(1 << 16)));

void init_buf(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        int sign = i % 2 == 0 ? 0x80 : 0;
        buf[i] = sign | (i + 1);
    }
}

void __check(int line, long long result, long long expect)
{
    if (result != expect) {
        printf("ERROR at line %d: 0x%08llx != 0x%08llx\n",
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
#define BxW_LOAD_io(SZ, RES, ADDR, OFF) \
    __asm__( \
        "%0 = mem" #SZ "(%1+#" #OFF ")\n\t" \
        : "=r"(RES) \
        : "r"(ADDR))
#define BxW_LOAD_io_Z(RES, ADDR, OFF) \
    BxW_LOAD_io(ubh, RES, ADDR, OFF)
#define BxW_LOAD_io_S(RES, ADDR, OFF) \
    BxW_LOAD_io(bh, RES, ADDR, OFF)

#define TEST_io(NAME, TYPE, SIGN, SIZE, EXT, EXP1, EXP2, EXP3, EXP4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    init_buf(); \
    BxW_LOAD_io_##SIGN(result, buf, 0 * (SIZE)); \
    check(result, (EXP1) | (EXT)); \
    BxW_LOAD_io_##SIGN(result, buf, 1 * (SIZE)); \
    check(result, (EXP2) | (EXT)); \
    BxW_LOAD_io_##SIGN(result, buf, 2 * (SIZE)); \
    check(result, (EXP3) | (EXT)); \
    BxW_LOAD_io_##SIGN(result, buf, 3 * (SIZE)); \
    check(result, (EXP4) | (EXT)); \
}


TEST_io(loadbzw2_io, int, Z, 2, 0x00000000,
        0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_io(loadbsw2_io, int, S, 2, 0x0000ff00,
        0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_io(loadbzw4_io, long long, Z,  4, 0x0000000000000000LL,
        0x0004008300020081LL, 0x0008008700060085LL,
        0x000c008b000a0089LL, 0x0010008f000e008dLL)
TEST_io(loadbsw4_io, long long, S,  4, 0x0000ff000000ff00LL,
        0x0004008300020081LL, 0x0008008700060085LL,
        0x000c008b000a0089LL, 0x0010008f000e008dLL)

/*
 ****************************************************************************
 * _ur addressing mode (index << offset + base)
 */
#define BxW_LOAD_ur(SZ, RES, SHIFT, IDX) \
    __asm__( \
        "%0 = mem" #SZ "(%1<<#" #SHIFT " + ##buf)\n\t" \
        : "=r"(RES) \
        : "r"(IDX))
#define BxW_LOAD_ur_Z(RES, SHIFT, IDX) \
    BxW_LOAD_ur(ubh, RES, SHIFT, IDX)
#define BxW_LOAD_ur_S(RES, SHIFT, IDX) \
    BxW_LOAD_ur(bh, RES, SHIFT, IDX)

#define TEST_ur(NAME, TYPE, SIGN, SHIFT, EXT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    init_buf(); \
    BxW_LOAD_ur_##SIGN(result, (SHIFT), 0); \
    check(result, (RES1) | (EXT)); \
    BxW_LOAD_ur_##SIGN(result, (SHIFT), 1); \
    check(result, (RES2) | (EXT)); \
    BxW_LOAD_ur_##SIGN(result, (SHIFT), 2); \
    check(result, (RES3) | (EXT)); \
    BxW_LOAD_ur_##SIGN(result, (SHIFT), 3); \
    check(result, (RES4) | (EXT)); \
} \

TEST_ur(loadbzw2_ur, int, Z, 1, 0x00000000,
        0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_ur(loadbsw2_ur, int, S, 1, 0x0000ff00,
        0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_ur(loadbzw4_ur, long long, Z, 2, 0x0000000000000000LL,
        0x0004008300020081LL, 0x0008008700060085LL,
        0x000c008b000a0089LL, 0x0010008f000e008dLL)
TEST_ur(loadbsw4_ur, long long, S, 2, 0x0000ff000000ff00LL,
        0x0004008300020081LL, 0x0008008700060085LL,
        0x000c008b000a0089LL, 0x0010008f000e008dLL)

/*
 ****************************************************************************
 * _ap addressing mode (addr = base)
 */
#define BxW_LOAD_ap(SZ, RES, PTR, ADDR) \
    __asm__( \
        "%0 = mem" #SZ "(%1 = ##" #ADDR ")\n\t" \
        : "=r"(RES), "=r"(PTR))
#define BxW_LOAD_ap_Z(RES, PTR, ADDR) \
    BxW_LOAD_ap(ubh, RES, PTR, ADDR)
#define BxW_LOAD_ap_S(RES, PTR, ADDR) \
    BxW_LOAD_ap(bh, RES, PTR, ADDR)

#define TEST_ap(NAME, TYPE, SIGN, SIZE, EXT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    void *ptr; \
    init_buf(); \
    BxW_LOAD_ap_##SIGN(result, ptr, (buf + 0 * (SIZE))); \
    check(result, (RES1) | (EXT)); \
    checkp(ptr, &buf[0 * (SIZE)]); \
    BxW_LOAD_ap_##SIGN(result, ptr, (buf + 1 * (SIZE))); \
    check(result, (RES2) | (EXT)); \
    checkp(ptr, &buf[1 * (SIZE)]); \
    BxW_LOAD_ap_##SIGN(result, ptr, (buf + 2 * (SIZE))); \
    check(result, (RES3) | (EXT)); \
    checkp(ptr, &buf[2 * (SIZE)]); \
    BxW_LOAD_ap_##SIGN(result, ptr, (buf + 3 * (SIZE))); \
    check(result, (RES4) | (EXT)); \
    checkp(ptr, &buf[3 * (SIZE)]); \
}

TEST_ap(loadbzw2_ap, int, Z, 2, 0x00000000,
        0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_ap(loadbsw2_ap, int, S, 2, 0x0000ff00,
        0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_ap(loadbzw4_ap, long long, Z, 4, 0x0000000000000000LL,
        0x0004008300020081LL, 0x0008008700060085LL,
        0x000c008b000a0089LL, 0x0010008f000e008dLL)
TEST_ap(loadbsw4_ap, long long, S, 4, 0x0000ff000000ff00LL,
        0x0004008300020081LL, 0x0008008700060085LL,
        0x000c008b000a0089LL, 0x0010008f000e008dLL)

/*
 ****************************************************************************
 * _rp addressing mode (addr ++ modifer-reg)
 */
#define BxW_LOAD_pr(SZ, RES, PTR, INC) \
    __asm__( \
        "m0 = %2\n\t" \
        "%0 = mem" #SZ "(%1++m0)\n\t" \
        : "=r"(RES), "+r"(PTR) \
        : "r"(INC) \
        : "m0")
#define BxW_LOAD_pr_Z(RES, PTR, INC) \
    BxW_LOAD_pr(ubh, RES, PTR, INC)
#define BxW_LOAD_pr_S(RES, PTR, INC) \
    BxW_LOAD_pr(bh, RES, PTR, INC)

#define TEST_pr(NAME, TYPE, SIGN, SIZE, EXT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    void *ptr = buf; \
    init_buf(); \
    BxW_LOAD_pr_##SIGN(result, ptr, (SIZE)); \
    check(result, (RES1) | (EXT)); \
    checkp(ptr, &buf[1 * (SIZE)]); \
    BxW_LOAD_pr_##SIGN(result, ptr, (SIZE)); \
    check(result, (RES2) | (EXT)); \
    checkp(ptr, &buf[2 * (SIZE)]); \
    BxW_LOAD_pr_##SIGN(result, ptr, (SIZE)); \
    check(result, (RES3) | (EXT)); \
    checkp(ptr, &buf[3 * (SIZE)]); \
    BxW_LOAD_pr_##SIGN(result, ptr, (SIZE)); \
    check(result, (RES4) | (EXT)); \
    checkp(ptr, &buf[4 * (SIZE)]); \
}

TEST_pr(loadbzw2_pr, int, Z, 2, 0x00000000,
    0x00020081, 0x0040083, 0x00060085, 0x00080087)
TEST_pr(loadbsw2_pr, int, S, 2, 0x0000ff00,
    0x00020081, 0x0040083, 0x00060085, 0x00080087)
TEST_pr(loadbzw4_pr, long long, Z, 4, 0x0000000000000000LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x000c008b000a0089LL, 0x0010008f000e008dLL)
TEST_pr(loadbsw4_pr, long long, S, 4, 0x0000ff000000ff00LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x000c008b000a0089LL, 0x0010008f000e008dLL)

/*
 ****************************************************************************
 * _pbr addressing mode (addr ++ modifer-reg:brev)
 */
#define BxW_LOAD_pbr(SZ, RES, PTR) \
    __asm__( \
        "r4 = #(1 << (16 - 4))\n\t" \
        "m0 = r4\n\t" \
        "%0 = mem" #SZ "(%1++m0:brev)\n\t" \
        : "=r"(RES), "+r"(PTR) \
        : \
        : "r4", "m0")
#define BxW_LOAD_pbr_Z(RES, PTR) \
    BxW_LOAD_pbr(ubh, RES, PTR)
#define BxW_LOAD_pbr_S(RES, PTR) \
    BxW_LOAD_pbr(bh, RES, PTR)

#define TEST_pbr(NAME, TYPE, SIGN, EXT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    void *ptr = buf; \
    init_buf(); \
    BxW_LOAD_pbr_##SIGN(result, ptr); \
    check(result, (RES1) | (EXT)); \
    BxW_LOAD_pbr_##SIGN(result, ptr); \
    check(result, (RES2) | (EXT)); \
    BxW_LOAD_pbr_##SIGN(result, ptr); \
    check(result, (RES3) | (EXT)); \
    BxW_LOAD_pbr_##SIGN(result, ptr); \
    check(result, (RES4) | (EXT)); \
}

TEST_pbr(loadbzw2_pbr, int, Z, 0x00000000,
    0x00020081, 0x000a0089, 0x00060085, 0x000e008d)
TEST_pbr(loadbsw2_pbr, int, S, 0x0000ff00,
    0x00020081, 0x000aff89, 0x0006ff85, 0x000eff8d)
TEST_pbr(loadbzw4_pbr, long long, Z, 0x0000000000000000LL,
    0x0004008300020081LL, 0x000c008b000a0089LL,
    0x0008008700060085LL, 0x0010008f000e008dLL)
TEST_pbr(loadbsw4_pbr, long long, S, 0x0000ff000000ff00LL,
    0x0004008300020081LL, 0x000cff8b000aff89LL,
    0x0008ff870006ff85LL, 0x0010ff8f000eff8dLL)

/*
 ****************************************************************************
 * _pi addressing mode (addr ++ inc)
 */
#define BxW_LOAD_pi(SZ, RES, PTR, INC) \
    __asm__( \
        "%0 = mem" #SZ "(%1++#" #INC ")\n\t" \
        : "=r"(RES), "+r"(PTR))
#define BxW_LOAD_pi_Z(RES, PTR, INC) \
    BxW_LOAD_pi(ubh, RES, PTR, INC)
#define BxW_LOAD_pi_S(RES, PTR, INC) \
    BxW_LOAD_pi(bh, RES, PTR, INC)

#define TEST_pi(NAME, TYPE, SIGN, INC, EXT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    void *ptr = buf; \
    init_buf(); \
    BxW_LOAD_pi_##SIGN(result, ptr, (INC)); \
    check(result, (RES1) | (EXT)); \
    checkp(ptr, &buf[1 * (INC)]); \
    BxW_LOAD_pi_##SIGN(result, ptr, (INC)); \
    check(result, (RES2) | (EXT)); \
    checkp(ptr, &buf[2 * (INC)]); \
    BxW_LOAD_pi_##SIGN(result, ptr, (INC)); \
    check(result, (RES3) | (EXT)); \
    checkp(ptr, &buf[3 * (INC)]); \
    BxW_LOAD_pi_##SIGN(result, ptr, (INC)); \
    check(result, (RES4) | (EXT)); \
    checkp(ptr, &buf[4 * (INC)]); \
}

TEST_pi(loadbzw2_pi, int, Z, 2, 0x00000000,
    0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_pi(loadbsw2_pi, int, S, 2, 0x0000ff00,
    0x00020081, 0x00040083, 0x00060085, 0x00080087)
TEST_pi(loadbzw4_pi, long long, Z, 4, 0x0000000000000000LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x000c008b000a0089LL, 0x0010008f000e008dLL)
TEST_pi(loadbsw4_pi, long long, S, 4, 0x0000ff000000ff00LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x000c008b000a0089LL, 0x0010008f000e008dLL)

/*
 ****************************************************************************
 * _pci addressing mode (addr ++ inc:circ)
 */
#define BxW_LOAD_pci(SZ, RES, PTR, START, LEN, INC) \
    __asm__( \
        "r4 = %3\n\t" \
        "m0 = r4\n\t" \
        "cs0 = %2\n\t" \
        "%0 = mem" #SZ "(%1++#" #INC ":circ(m0))\n\t" \
        : "=r"(RES), "+r"(PTR) \
        : "r"(START), "r"(LEN) \
        : "r4", "m0", "cs0")
#define BxW_LOAD_pci_Z(RES, PTR, START, LEN, INC) \
    BxW_LOAD_pci(ubh, RES, PTR, START, LEN, INC)
#define BxW_LOAD_pci_S(RES, PTR, START, LEN, INC) \
    BxW_LOAD_pci(bh, RES, PTR, START, LEN, INC)

#define TEST_pci(NAME, TYPE, SIGN, LEN, INC, EXT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    void *ptr = buf; \
    init_buf(); \
    BxW_LOAD_pci_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES1) | (EXT)); \
    checkp(ptr, &buf[(1 * (INC)) % (LEN)]); \
    BxW_LOAD_pci_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES2) | (EXT)); \
    checkp(ptr, &buf[(2 * (INC)) % (LEN)]); \
    BxW_LOAD_pci_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES3) | (EXT)); \
    checkp(ptr, &buf[(3 * (INC)) % (LEN)]); \
    BxW_LOAD_pci_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES4) | (EXT)); \
    checkp(ptr, &buf[(4 * (INC)) % (LEN)]); \
}

TEST_pci(loadbzw2_pci, int, Z, 6, 2, 0x00000000,
    0x00020081, 0x00040083, 0x00060085, 0x00020081)
TEST_pci(loadbsw2_pci, int, S, 6, 2, 0x0000ff00,
    0x00020081, 0x00040083, 0x00060085, 0x00020081)
TEST_pci(loadbzw4_pci, long long, Z, 8, 4, 0x0000000000000000LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x0004008300020081LL, 0x0008008700060085LL)
TEST_pci(loadbsw4_pci, long long, S, 8, 4, 0x0000ff000000ff00LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x0004008300020081LL, 0x0008008700060085LL)

/*
 ****************************************************************************
 * _pcr addressing mode (addr ++ I:circ(modifier-reg))
 */
#define BxW_LOAD_pcr(SZ, RES, PTR, START, LEN, INC) \
    __asm__( \
        "r4 = %2\n\t" \
        "m1 = r4\n\t" \
        "cs1 = %3\n\t" \
        "%0 = mem" #SZ "(%1++I:circ(m1))\n\t" \
        : "=r"(RES), "+r"(PTR) \
        : "r"((((INC) & 0x7f) << 17) | ((LEN) & 0x1ffff)), \
          "r"(START) \
        : "r4", "m1", "cs1")
#define BxW_LOAD_pcr_Z(RES, PTR, START, LEN, INC) \
    BxW_LOAD_pcr(ubh, RES, PTR, START, LEN, INC)
#define BxW_LOAD_pcr_S(RES, PTR, START, LEN, INC) \
    BxW_LOAD_pcr(bh, RES, PTR, START, LEN, INC)

#define TEST_pcr(NAME, TYPE, SIGN, SIZE, LEN, INC, \
                 EXT, RES1, RES2, RES3, RES4) \
void test_##NAME(void) \
{ \
    TYPE result; \
    void *ptr = buf; \
    init_buf(); \
    BxW_LOAD_pcr_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES1) | (EXT)); \
    checkp(ptr, &buf[(1 * (INC) * (SIZE)) % (LEN)]); \
    BxW_LOAD_pcr_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES2) | (EXT)); \
    checkp(ptr, &buf[(2 * (INC) * (SIZE)) % (LEN)]); \
    BxW_LOAD_pcr_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES3) | (EXT)); \
    checkp(ptr, &buf[(3 * (INC) * (SIZE)) % (LEN)]); \
    BxW_LOAD_pcr_##SIGN(result, ptr, buf, (LEN), (INC)); \
    check(result, (RES4) | (EXT)); \
    checkp(ptr, &buf[(4 * (INC) * (SIZE)) % (LEN)]); \
}

TEST_pcr(loadbzw2_pcr, int, Z, 2, 8, 2, 0x00000000,
    0x00020081, 0x00060085, 0x00020081, 0x00060085)
TEST_pcr(loadbsw2_pcr, int, S, 2, 8, 2, 0x0000ff00,
    0x00020081, 0x00060085, 0x00020081, 0x00060085)
TEST_pcr(loadbzw4_pcr, long long, Z, 4, 8, 1, 0x0000000000000000LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x0004008300020081LL, 0x0008008700060085LL)
TEST_pcr(loadbsw4_pcr, long long, S, 4, 8, 1, 0x0000ff000000ff00LL,
    0x0004008300020081LL, 0x0008008700060085LL,
    0x0004008300020081LL, 0x0008008700060085LL)

int main()
{
    test_loadbzw2_io();
    test_loadbsw2_io();
    test_loadbzw4_io();
    test_loadbsw4_io();

    test_loadbzw2_ur();
    test_loadbsw2_ur();
    test_loadbzw4_ur();
    test_loadbsw4_ur();

    test_loadbzw2_ap();
    test_loadbsw2_ap();
    test_loadbzw4_ap();
    test_loadbsw4_ap();

    test_loadbzw2_pr();
    test_loadbsw2_pr();
    test_loadbzw4_pr();
    test_loadbsw4_pr();

    test_loadbzw2_pbr();
    test_loadbsw2_pbr();
    test_loadbzw4_pbr();
    test_loadbsw4_pbr();

    test_loadbzw2_pi();
    test_loadbsw2_pi();
    test_loadbzw4_pi();
    test_loadbsw4_pi();

    test_loadbzw2_pci();
    test_loadbsw2_pci();
    test_loadbzw4_pci();
    test_loadbsw4_pci();

    test_loadbzw2_pcr();
    test_loadbsw2_pcr();
    test_loadbzw4_pcr();
    test_loadbsw4_pcr();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
