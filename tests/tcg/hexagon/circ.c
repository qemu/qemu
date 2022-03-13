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

#define DEBUG          0
#define DEBUG_PRINTF(...) \
    do { \
        if (DEBUG) { \
            printf(__VA_ARGS__); \
        } \
    } while (0)


#define NBYTES         (1 << 8)
#define NHALFS         (NBYTES / sizeof(short))
#define NWORDS         (NBYTES / sizeof(int))
#define NDOBLS         (NBYTES / sizeof(long long))

long long     dbuf[NDOBLS] __attribute__((aligned(1 << 12))) = {0};
int           wbuf[NWORDS] __attribute__((aligned(1 << 12))) = {0};
short         hbuf[NHALFS] __attribute__((aligned(1 << 12))) = {0};
unsigned char bbuf[NBYTES] __attribute__((aligned(1 << 12))) = {0};

/*
 * We use the C preporcessor to deal with the combinations of types
 */

#define INIT(BUF, N) \
    void init_##BUF(void) \
    { \
        int i; \
        for (i = 0; i < N; i++) { \
            BUF[i] = i; \
        } \
    } \

INIT(bbuf, NBYTES)
INIT(hbuf, NHALFS)
INIT(wbuf, NWORDS)
INIT(dbuf, NDOBLS)

/*
 * Macros for performing circular load
 *     RES         result
 *     ADDR        address
 *     START       start address of buffer
 *     LEN         length of buffer (in bytes)
 *     INC         address increment (in bytes for IMM, elements for REG)
 */
#define CIRC_LOAD_IMM(SIZE, RES, ADDR, START, LEN, INC) \
    __asm__( \
        "r4 = %3\n\t" \
        "m0 = r4\n\t" \
        "cs0 = %2\n\t" \
        "%0 = mem" #SIZE "(%1++#" #INC ":circ(M0))\n\t" \
        : "=r"(RES), "+r"(ADDR) \
        : "r"(START), "r"(LEN) \
        : "r4", "m0", "cs0")
#define CIRC_LOAD_IMM_b(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_IMM(b, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_IMM_ub(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_IMM(ub, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_IMM_h(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_IMM(h, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_IMM_uh(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_IMM(uh, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_IMM_w(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_IMM(w, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_IMM_d(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_IMM(d, RES, ADDR, START, LEN, INC)

/*
 * The mreg has the following pieces
 *     mreg[31:28]              increment[10:7]
 *     mreg[27:24]              K value (used Hexagon v3 and earlier)
 *     mreg[23:17]              increment[6:0]
 *     mreg[16:0]               circular buffer length
 */
static int build_mreg(int inc, int K, int len)
{
    return ((inc & 0x780) << 21) |
           ((K & 0xf) << 24) |
           ((inc & 0x7f) << 17) |
           (len & 0x1ffff);
}

#define CIRC_LOAD_REG(SIZE, RES, ADDR, START, LEN, INC) \
    __asm__( \
        "r4 = %2\n\t" \
        "m1 = r4\n\t" \
        "cs1 = %3\n\t" \
        "%0 = mem" #SIZE "(%1++I:circ(M1))\n\t" \
        : "=r"(RES), "+r"(ADDR) \
        : "r"(build_mreg((INC), 0, (LEN))), \
          "r"(START) \
        : "r4", "m1", "cs1")
#define CIRC_LOAD_REG_b(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_REG(b, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_REG_ub(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_REG(ub, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_REG_h(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_REG(h, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_REG_uh(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_REG(uh, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_REG_w(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_REG(w, RES, ADDR, START, LEN, INC)
#define CIRC_LOAD_REG_d(RES, ADDR, START, LEN, INC) \
    CIRC_LOAD_REG(d, RES, ADDR, START, LEN, INC)

/*
 * Macros for performing circular store
 *     VAL         value to store
 *     ADDR        address
 *     START       start address of buffer
 *     LEN         length of buffer (in bytes)
 *     INC         address increment (in bytes for IMM, elements for REG)
 */
#define CIRC_STORE_IMM(SIZE, PART, VAL, ADDR, START, LEN, INC) \
    __asm__( \
        "r4 = %3\n\t" \
        "m0 = r4\n\t" \
        "cs0 = %1\n\t" \
        "mem" #SIZE "(%0++#" #INC ":circ(M0)) = %2" PART "\n\t" \
        : "+r"(ADDR) \
        : "r"(START), "r"(VAL), "r"(LEN) \
        : "r4", "m0", "cs0", "memory")
#define CIRC_STORE_IMM_b(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_IMM(b, "", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_IMM_h(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_IMM(h, "", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_IMM_f(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_IMM(h, ".H", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_IMM_w(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_IMM(w, "", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_IMM_d(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_IMM(d, "", VAL, ADDR, START, LEN, INC)

#define CIRC_STORE_NEW_IMM(SIZE, VAL, ADDR, START, LEN, INC) \
    __asm__( \
        "r4 = %3\n\t" \
        "m0 = r4\n\t" \
        "cs0 = %1\n\t" \
        "{\n\t" \
        "    r5 = %2\n\t" \
        "    mem" #SIZE "(%0++#" #INC ":circ(M0)) = r5.new\n\t" \
        "}\n\t" \
        : "+r"(ADDR) \
        : "r"(START), "r"(VAL), "r"(LEN) \
        : "r4", "r5", "m0", "cs0", "memory")
#define CIRC_STORE_IMM_bnew(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_NEW_IMM(b, VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_IMM_hnew(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_NEW_IMM(h, VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_IMM_wnew(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_NEW_IMM(w, VAL, ADDR, START, LEN, INC)

#define CIRC_STORE_REG(SIZE, PART, VAL, ADDR, START, LEN, INC) \
    __asm__( \
        "r4 = %1\n\t" \
        "m1 = r4\n\t" \
        "cs1 = %2\n\t" \
        "mem" #SIZE "(%0++I:circ(M1)) = %3" PART "\n\t" \
        : "+r"(ADDR) \
        : "r"(build_mreg((INC), 0, (LEN))), \
          "r"(START), \
          "r"(VAL) \
        : "r4", "m1", "cs1", "memory")
#define CIRC_STORE_REG_b(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_REG(b, "", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_REG_h(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_REG(h, "", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_REG_f(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_REG(h, ".H", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_REG_w(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_REG(w, "", VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_REG_d(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_REG(d, "", VAL, ADDR, START, LEN, INC)

#define CIRC_STORE_NEW_REG(SIZE, VAL, ADDR, START, LEN, INC) \
    __asm__( \
        "r4 = %1\n\t" \
        "m1 = r4\n\t" \
        "cs1 = %2\n\t" \
        "{\n\t" \
        "    r5 = %3\n\t" \
        "    mem" #SIZE "(%0++I:circ(M1)) = r5.new\n\t" \
        "}\n\t" \
        : "+r"(ADDR) \
        : "r"(build_mreg((INC), 0, (LEN))), \
          "r"(START), \
          "r"(VAL) \
        : "r4", "r5", "m1", "cs1", "memory")
#define CIRC_STORE_REG_bnew(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_NEW_REG(b, VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_REG_hnew(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_NEW_REG(h, VAL, ADDR, START, LEN, INC)
#define CIRC_STORE_REG_wnew(VAL, ADDR, START, LEN, INC) \
    CIRC_STORE_NEW_REG(w, VAL, ADDR, START, LEN, INC)


int err;

/* We'll test increments +1 and -1 */
void check_load(int i, long long result, int inc, int size)
{
    int expect = (i * inc);
    while (expect >= size) {
        expect -= size;
    }
    while (expect < 0) {
        expect += size;
    }
    if (result != expect) {
        printf("ERROR(%d): %lld != %d\n", i, result, expect);
        err++;
    }
}

#define TEST_LOAD_IMM(SZ, TYPE, BUF, BUFSIZE, INC, FMT) \
void circ_test_load_imm_##SZ(void) \
{ \
    TYPE *p = (TYPE *)BUF; \
    int size = 10; \
    int i; \
    for (i = 0; i < BUFSIZE; i++) { \
        TYPE element; \
        CIRC_LOAD_IMM_##SZ(element, p, BUF, size * sizeof(TYPE), (INC)); \
        DEBUG_PRINTF("i = %2d, p = 0x%p, element = %2" #FMT "\n", \
                     i, p, element); \
        check_load(i, element, ((INC) / (int)sizeof(TYPE)), size); \
    } \
    p = (TYPE *)BUF; \
    for (i = 0; i < BUFSIZE; i++) { \
        TYPE element; \
        CIRC_LOAD_IMM_##SZ(element, p, BUF, size * sizeof(TYPE), -(INC)); \
        DEBUG_PRINTF("i = %2d, p = 0x%p, element = %2" #FMT "\n", \
                     i, p, element); \
        check_load(i, element, (-(INC) / (int)sizeof(TYPE)), size); \
    } \
}

TEST_LOAD_IMM(b,  char,           bbuf, NBYTES, 1, d)
TEST_LOAD_IMM(ub, unsigned char,  bbuf, NBYTES, 1, d)
TEST_LOAD_IMM(h,  short,          hbuf, NHALFS, 2, d)
TEST_LOAD_IMM(uh, unsigned short, hbuf, NHALFS, 2, d)
TEST_LOAD_IMM(w,  int,            wbuf, NWORDS, 4, d)
TEST_LOAD_IMM(d,  long long,      dbuf, NDOBLS, 8, lld)

#define TEST_LOAD_REG(SZ, TYPE, BUF, BUFSIZE, FMT) \
void circ_test_load_reg_##SZ(void) \
{ \
    TYPE *p = (TYPE *)BUF; \
    int size = 13; \
    int i; \
    for (i = 0; i < BUFSIZE; i++) { \
        TYPE element; \
        CIRC_LOAD_REG_##SZ(element, p, BUF, size * sizeof(TYPE), 1); \
        DEBUG_PRINTF("i = %2d, p = 0x%p, element = %2" #FMT "\n", \
                     i, p, element); \
        check_load(i, element, 1, size); \
    } \
    p = (TYPE *)BUF; \
    for (i = 0; i < BUFSIZE; i++) { \
        TYPE element; \
        CIRC_LOAD_REG_##SZ(element, p, BUF, size * sizeof(TYPE), -1); \
        DEBUG_PRINTF("i = %2d, p = 0x%p, element = %2" #FMT "\n", \
                     i, p, element); \
        check_load(i, element, -1, size); \
    } \
}

TEST_LOAD_REG(b,  char,           bbuf, NBYTES, d)
TEST_LOAD_REG(ub, unsigned char,  bbuf, NBYTES, d)
TEST_LOAD_REG(h,  short,          hbuf, NHALFS, d)
TEST_LOAD_REG(uh, unsigned short, hbuf, NHALFS, d)
TEST_LOAD_REG(w,  int,            wbuf, NWORDS, d)
TEST_LOAD_REG(d,  long long,      dbuf, NDOBLS, lld)

/* The circular stores will wrap around somewhere inside the buffer */
#define CIRC_VAL(SZ, TYPE, BUFSIZE) \
TYPE circ_val_##SZ(int i, int inc, int size) \
{ \
    int mod = BUFSIZE % size; \
    int elem = i * inc; \
    if (elem < 0) { \
        if (-elem <= size - mod) { \
            return (elem + BUFSIZE - mod); \
        } else { \
            return (elem + BUFSIZE + size - mod); \
        } \
    } else if (elem < mod) {\
        return (elem + BUFSIZE - mod); \
    } else { \
        return (elem + BUFSIZE - size - mod); \
    } \
}

CIRC_VAL(b, unsigned char, NBYTES)
CIRC_VAL(h, short,         NHALFS)
CIRC_VAL(w, int,           NWORDS)
CIRC_VAL(d, long long,     NDOBLS)

/*
 * Circular stores should only write to the first "size" elements of the buffer
 * the remainder of the elements should have BUF[i] == i
 */
#define CHECK_STORE(SZ, BUF, BUFSIZE, FMT) \
void check_store_##SZ(int inc, int size) \
{ \
    int i; \
    for (i = 0; i < size; i++) { \
        DEBUG_PRINTF(#BUF "[%3d] = 0x%02" #FMT ", guess = 0x%02" #FMT "\n", \
                     i, BUF[i], circ_val_##SZ(i, inc, size)); \
        if (BUF[i] != circ_val_##SZ(i, inc, size)) { \
            printf("ERROR(%3d): 0x%02" #FMT " != 0x%02" #FMT "\n", \
                   i, BUF[i], circ_val_##SZ(i, inc, size)); \
            err++; \
        } \
    } \
    for (i = size; i < BUFSIZE; i++) { \
        if (BUF[i] != i) { \
            printf("ERROR(%3d): 0x%02" #FMT " != 0x%02x\n", i, BUF[i], i); \
            err++; \
        } \
    } \
}

CHECK_STORE(b, bbuf, NBYTES, x)
CHECK_STORE(h, hbuf, NHALFS, x)
CHECK_STORE(w, wbuf, NWORDS, x)
CHECK_STORE(d, dbuf, NDOBLS, llx)

#define CIRC_TEST_STORE_IMM(SZ, CHK, TYPE, BUF, BUFSIZE, SHIFT, INC) \
void circ_test_store_imm_##SZ(void) \
{ \
    unsigned int size = 27; \
    TYPE *p = BUF; \
    TYPE val = 0; \
    int i; \
    init_##BUF(); \
    for (i = 0; i < BUFSIZE; i++) { \
        CIRC_STORE_IMM_##SZ(val << SHIFT, p, BUF, size * sizeof(TYPE), INC); \
        val++; \
    } \
    check_store_##CHK(((INC) / (int)sizeof(TYPE)), size); \
    p = BUF; \
    val = 0; \
    init_##BUF(); \
    for (i = 0; i < BUFSIZE; i++) { \
        CIRC_STORE_IMM_##SZ(val << SHIFT, p, BUF, size * sizeof(TYPE), \
                            -(INC)); \
        val++; \
    } \
    check_store_##CHK((-(INC) / (int)sizeof(TYPE)), size); \
}

CIRC_TEST_STORE_IMM(b,    b, unsigned char, bbuf, NBYTES, 0,  1)
CIRC_TEST_STORE_IMM(h,    h, short,         hbuf, NHALFS, 0,  2)
CIRC_TEST_STORE_IMM(f,    h, short,         hbuf, NHALFS, 16, 2)
CIRC_TEST_STORE_IMM(w,    w, int,           wbuf, NWORDS, 0,  4)
CIRC_TEST_STORE_IMM(d,    d, long long,     dbuf, NDOBLS, 0,  8)
CIRC_TEST_STORE_IMM(bnew, b, unsigned char, bbuf, NBYTES, 0,  1)
CIRC_TEST_STORE_IMM(hnew, h, short,         hbuf, NHALFS, 0,  2)
CIRC_TEST_STORE_IMM(wnew, w, int,           wbuf, NWORDS, 0,  4)

#define CIRC_TEST_STORE_REG(SZ, CHK, TYPE, BUF, BUFSIZE, SHIFT) \
void circ_test_store_reg_##SZ(void) \
{ \
    TYPE *p = BUF; \
    unsigned int size = 19; \
    TYPE val = 0; \
    int i; \
    init_##BUF(); \
    for (i = 0; i < BUFSIZE; i++) { \
        CIRC_STORE_REG_##SZ(val << SHIFT, p, BUF, size * sizeof(TYPE), 1); \
        val++; \
    } \
    check_store_##CHK(1, size); \
    p = BUF; \
    val = 0; \
    init_##BUF(); \
    for (i = 0; i < BUFSIZE; i++) { \
        CIRC_STORE_REG_##SZ(val << SHIFT, p, BUF, size * sizeof(TYPE), -1); \
        val++; \
    } \
    check_store_##CHK(-1, size); \
}

CIRC_TEST_STORE_REG(b,    b, unsigned char, bbuf, NBYTES, 0)
CIRC_TEST_STORE_REG(h,    h, short,         hbuf, NHALFS, 0)
CIRC_TEST_STORE_REG(f,    h, short,         hbuf, NHALFS, 16)
CIRC_TEST_STORE_REG(w,    w, int,           wbuf, NWORDS, 0)
CIRC_TEST_STORE_REG(d,    d, long long,     dbuf, NDOBLS, 0)
CIRC_TEST_STORE_REG(bnew, b, unsigned char, bbuf, NBYTES, 0)
CIRC_TEST_STORE_REG(hnew, h, short,         hbuf, NHALFS, 0)
CIRC_TEST_STORE_REG(wnew, w, int,           wbuf, NWORDS, 0)

/* Test the old scheme used in Hexagon V3 */
static void circ_test_v3(void)
{
    int *p = wbuf;
    int size = 15;
    /* set high bit in K to test unsigned extract in fcirc */
    int K = 8;      /* 1024 bytes */
    int element;
    int i;

    init_wbuf();

    for (i = 0; i < NWORDS; i++) {
        __asm__(
            "r4 = %2\n\t"
            "m1 = r4\n\t"
            "%0 = memw(%1++I:circ(M1))\n\t"
            : "=r"(element), "+r"(p)
            : "r"(build_mreg(1, K, size * sizeof(int)))
            : "r4", "m1");
        DEBUG_PRINTF("i = %2d, p = 0x%p, element = %2d\n", i, p, element);
        check_load(i, element, 1, size);
    }
}

int main()
{
    init_bbuf();
    init_hbuf();
    init_wbuf();
    init_dbuf();

    DEBUG_PRINTF("NBYTES = %d\n", NBYTES);
    DEBUG_PRINTF("Address of dbuf = 0x%p\n", dbuf);
    DEBUG_PRINTF("Address of wbuf = 0x%p\n", wbuf);
    DEBUG_PRINTF("Address of hbuf = 0x%p\n", hbuf);
    DEBUG_PRINTF("Address of bbuf = 0x%p\n", bbuf);

    circ_test_load_imm_b();
    circ_test_load_imm_ub();
    circ_test_load_imm_h();
    circ_test_load_imm_uh();
    circ_test_load_imm_w();
    circ_test_load_imm_d();

    circ_test_load_reg_b();
    circ_test_load_reg_ub();
    circ_test_load_reg_h();
    circ_test_load_reg_uh();
    circ_test_load_reg_w();
    circ_test_load_reg_d();

    circ_test_store_imm_b();
    circ_test_store_imm_h();
    circ_test_store_imm_f();
    circ_test_store_imm_w();
    circ_test_store_imm_d();
    circ_test_store_imm_bnew();
    circ_test_store_imm_hnew();
    circ_test_store_imm_wnew();

    circ_test_store_reg_b();
    circ_test_store_reg_h();
    circ_test_store_reg_f();
    circ_test_store_reg_w();
    circ_test_store_reg_d();
    circ_test_store_reg_bnew();
    circ_test_store_reg_hnew();
    circ_test_store_reg_wnew();

    circ_test_v3();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
