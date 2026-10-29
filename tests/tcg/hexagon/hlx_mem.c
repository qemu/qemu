/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <setjmp.h>
#include <signal.h>

#include "hlx_test.h"

/* Committed-value seed for the false-predicate load test */
#define SEED_VAL  0x1234

static uint16_t *buf_a;
static uint16_t *buf_out;
static uint16_t expected[HLX_HALF_ELEMS];

static void init_data(void)
{
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        buf_a[i] = (i * 37 + 100) & 0xFFFF;
        buf_out[i] = 0xDEAD;
    }
}


static void test_pred_load_true(void)
{
    init_data();
    /*
     * if (Pv) Ld.tmp = mem(Rs) -- with true predicate.
     *
     * A .tmp load only forwards its data within the same packet, so the
     * consuming assign (L1.h = L0.h) and the .new store must live in the
     * same packet as the load.
     */
    /*
     * { if (p0) L0.tmp = mem(r1)
     *   L1.h = L0.h
     *   mem(r2) = L1.new }
     */
    asm volatile("p0 = cmp.eq(r0, r0)\n"      /* p0 = true */
                 "r1 = %[src]\n"
                 "r2 = %[dst]\n"
                 ".word 0xe5007822\n"
                 ".word 0x98015040\n"
                 ".word 0xa482c001\n"
                 : : [src] "r"(buf_a), [dst] "r"(buf_out)
                 : "r1", "r2", "p0", "memory");
    check_h("LX_L16b_p_tmp (true)", buf_out, buf_a, HLX_HALF_ELEMS);
}

static void test_pred_load_false(void)
{
    uint32_t seed;

    init_data();
    /*
     * if (Pv) Ld.tmp = mem(Rs) -- with false predicate.
     *
     * A .tmp load never writes the register file; it only forwards within
     * its packet.  With a false predicate the load is cancelled, so the
     * same-packet consumer (L1.h = L0.h) reads whatever committed value L0
     * already holds.  Seed L0 with a regular (non-tmp) HLX write first, then
     * confirm the cancelled tmp load does not override it.
     */
    seed = SEED_VAL;
    /*
     * { if (p0) L0.tmp = mem(r1)
     *   L1.h = L0.h
     *   mem(r2) = L1.new }
     */
    asm volatile("r3 = %[seed]\n"
                 "r1 = %[src]\n"
                 "r2 = %[dst]\n"
                 ".word 0xe523e420\n"       /* { L0.h = r3.h } */
                 "p0 = !cmp.eq(r0, r0)\n"   /* p0 = false (always) */
                 ".word 0xe5007822\n"
                 ".word 0x98015040\n"
                 ".word 0xa482c001\n"
                 : : [seed] "r"(seed), [src] "r"(buf_a), [dst] "r"(buf_out)
                 : "r1", "r2", "r3", "p0", "memory");
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = SEED_VAL;
    }
    check_h("LX_L16b_p_tmp (false)", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_pred_store_true(void)
{
    init_data();
    /*
     * if (Pv) mem(Rs) = Ku.new -- with true predicate.
     *
     * The .tmp load, its consumer, and the predicated .new store all live
     * in one packet so the loaded data is forwarded correctly.
     */
    /*
     * { L0.tmp = mem(r1)
     *   L1.h = L0.h
     *   if (p0) mem(r2) = L1.new }
     */
    asm volatile("p0 = cmp.eq(r0, r0)\n"      /* p0 = true */
                 "r1 = %[src]\n"
                 "r2 = %[dst]\n"
                 ".word 0xe5007822\n"
                 ".word 0x98015000\n"
                 ".word 0xa482c041\n"
                 : : [src] "r"(buf_a), [dst] "r"(buf_out)
                 : "r1", "r2", "p0", "memory");
    check_h("LX_S16b_p_new (true)", buf_out, buf_a, HLX_HALF_ELEMS);
}

static void test_pred_store_false(void)
{
    init_data();
    /*
     * if (Pv) mem(Rs) = Ku.new -- with false predicate.
     * Same same-packet producer pattern, but the false predicate cancels
     * the store; buf_out retains 0xDEAD.
     */
    /*
     * { L0.tmp = mem(r1)
     *   L1.h = L0.h
     *   if (p0) mem(r2) = L1.new }
     */
    asm volatile("p0 = !cmp.eq(r0, r0)\n"     /* p0 = false (always) */
                 "r1 = %[src]\n"
                 "r2 = %[dst]\n"
                 ".word 0xe5007822\n"
                 ".word 0x98015000\n"
                 ".word 0xa482c041\n"
                 : : [src] "r"(buf_a), [dst] "r"(buf_out)
                 : "r1", "r2", "p0", "memory");
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = 0xDEAD;
    }
    check_h("LX_S16b_p_new (false)", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_zero_store(void)
{
    uint32_t seed;

    init_data();
    /*
     * A .new store with no producer in the packet stores 2KB of zeros.
     * Give L1 a committed value first, so the test sees if the store
     * uses the register instead of zeros.
     */
    seed = SEED_VAL;
    /*
     * { L1.h = r3.h }
     * { mem(r2) = L1.new }
     */
    asm volatile("r3 = %[seed]\n"
                 "r2 = %[dst]\n"
                 ".word 0xe523e422\n"
                 ".word 0xa482c001\n"
                 : : [seed] "r"(seed), [dst] "r"(buf_out)
                 : "r2", "r3", "memory");
    memset(expected, 0, sizeof(expected));
    check_h("LX_S16b_new (no producer)", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_pred_zero_store(bool pred)
{
    init_data();
    /* { if (p0) mem(r2) = L1.new } with no producer of L1 */
    asm volatile("p0 = cmp.eq(%[pred], #1)\n"
                 "r2 = %[dst]\n"
                 ".word 0xa482c041\n"
                 : : [pred] "r"(pred), [dst] "r"(buf_out)
                 : "r2", "p0", "memory");
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = pred ? 0 : 0xDEAD;
    }
    check_h(pred ? "LX_S16b_p_new (no producer, true)"
                 : "LX_S16b_p_new (no producer, false)",
            buf_out, expected, HLX_HALF_ELEMS);
}


static sigjmp_buf segv_env;

static void handle_segv(int sig)
{
    siglongjmp(segv_env, 1);
}

static void test_store_fault(void)
{
    struct sigaction act;
    /* volatile: the value must survive siglongjmp() */
    volatile bool caught = false;

    init_data();
    memset(&act, 0, sizeof(act));
    act.sa_handler = handle_segv;
    sigaction(SIGSEGV, &act, NULL);
    /*
     * A packet commits all stores or none.  The scalar store faults, so
     * the HLX store in the same packet must not change memory.
     *
     * { memw(r19+#0) = #0
     *   mem(r2) = L1.new }
     */
    if (sigsetjmp(segv_env, 1) == 0) {
        asm volatile("r2 = %[dst]\n"
                     "r19 = #0\n"
                     ".word 0x3c534000\n"
                     ".word 0xa482c001\n"
                     : : [dst] "r"(buf_out) : "r2", "r19", "memory");
    } else {
        caught = true;
    }
    act.sa_handler = SIG_DFL;
    sigaction(SIGSEGV, &act, NULL);

    if (!caught) {
        printf("FAIL LX_S16b_new (fault): no SIGSEGV\n");
        err++;
    }
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = 0xDEAD;
    }
    check_h("LX_S16b_new (fault)", buf_out, expected, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));

    /* Predicated load */
    test_pred_load_true();
    test_pred_load_false();

    /* Predicated store */
    test_pred_store_true();
    test_pred_store_false();

    /* Zero store */
    test_zero_store();
    test_pred_zero_store(true);
    test_pred_zero_store(false);

    /* Store in a faulting packet */
    test_store_fault();

    puts(err ? "FAIL" : "PASS");
    return err;
}
