/*
 * QTest testcase for the ASPEED AST2500 and AST2600 SCU.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025 Tan Siewert
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

/*
 * SCU base, as well as protection key are
 * the same on AST2500 and 2600.
 */
#define AST_SCU_BASE                    0x1E6E2000
#define AST_SCU_PROT_LOCK_STATE         0x0
#define AST_SCU_PROT_LOCK_VALUE         0x2
#define AST_SCU_PROT_UNLOCK_STATE       0x1
#define AST_SCU_PROT_UNLOCK_VALUE       0x1688A8A8

#define AST2500_MACHINE                 "-machine ast2500-evb"
#define AST2500_SCU_PROT_REG            0x00
#define AST2500_SCU_MISC_2_CONTROL_REG  0x4C

#define AST2600_MACHINE                 "-machine ast2600-evb"
/* AST2600 has two protection registers */
#define AST2600_SCU_PROT_REG            0x000
#define AST2600_SCU_PROT_REG2           0x010
#define AST2600_SCU_MISC_2_CONTROL_REG  0x0C4

#define TEST_LOCK_ARBITRARY_VALUE       0xABCDEFAB

/**
 * Assert that a given register matches an expected value.
 *
 * Reads the register and checks if its value equals the expected value.
 *
 * @param *s - QTest machine state
 * @param reg - Address of the register to be checked
 * @param expected - Expected register value
 */
static inline void assert_register_eq(QTestState *s,
                                      uint32_t reg,
                                      uint32_t expected)
{
    uint32_t value = qtest_readl(s, reg);
    g_assert_cmphex(value, ==, expected);
}

/**
 * Assert that a given register does not match a specific value.
 *
 * Reads the register and checks that its value is not equal to the
 * provided value.
 *
 * @param *s - QTest machine state
 * @param reg - Address of the register to be checked
 * @param not_expected - Value the register must not contain
 */
static inline void assert_register_neq(QTestState *s,
                                       uint32_t reg,
                                       uint32_t not_expected)
{
    uint32_t value = qtest_readl(s, reg);
    g_assert_cmphex(value, !=, not_expected);
}

/**
 * Test whether the SCU can be locked and unlocked correctly.
 *
 * When testing multiple registers, this function assumes that writing
 * to the first register also affects the others. However, writing to
 * any other register only affects itself.
 *
 * @param *machine - input machine configuration, passed directly
 *                   to QTest
 * @param regs[] - List of registers to be checked
 * @param regc - amount of arguments for registers to be checked
 */
static void test_protection_register(const char *machine,
                                     const uint32_t regs[],
                                     const int regc)
{
    QTestState *s = qtest_init(machine);

    for (int i = 0; i < regc; i++) {
        uint32_t reg = regs[i];

        qtest_writel(s, reg, AST_SCU_PROT_UNLOCK_VALUE);
        assert_register_eq(s, reg, AST_SCU_PROT_UNLOCK_STATE);

        /**
         * Check that other registers are unlocked too, if more
         * than one is available.
         */
        if (regc > 1 && i == 0) {
            /* Initialise at 1 instead of 0 to skip first */
            for (int j = 1; j < regc; j++) {
                uint32_t add_reg = regs[j];
                assert_register_eq(s, add_reg, AST_SCU_PROT_UNLOCK_STATE);
            }
        }

        /* Lock the register again */
        qtest_writel(s, reg, AST_SCU_PROT_LOCK_VALUE);
        assert_register_eq(s, reg, AST_SCU_PROT_LOCK_STATE);

        /* And the same for locked state */
        if (regc > 1 && i == 0) {
            /* Initialise at 1 instead of 0 to skip first */
            for (int j = 1; j < regc; j++) {
                uint32_t add_reg = regs[j];
                assert_register_eq(s, add_reg, AST_SCU_PROT_LOCK_STATE);
            }
        }
    }

    qtest_quit(s);
}

static void test_2500_protection_register(void)
{
    uint32_t regs[] = { AST_SCU_BASE + AST2500_SCU_PROT_REG };

    test_protection_register(AST2500_MACHINE,
                             regs,
                             ARRAY_SIZE(regs));
}

static void test_2600_protection_register(void)
{
    /**
     * The AST2600 has two protection registers, both
     * being required to be unlocked to do any operation.
     *
     * Modifying SCU000 also modifies SCU010, but modifying
     * SCU010 only will keep SCU000 untouched.
     */
    uint32_t regs[] = { AST_SCU_BASE + AST2600_SCU_PROT_REG,
                        AST_SCU_BASE + AST2600_SCU_PROT_REG2 };

    test_protection_register(AST2600_MACHINE,
                             regs,
                             ARRAY_SIZE(regs));
}

/**
 * Test if SCU register writes are correctly allowed or blocked
 * depending on the protection register state.
 *
 * The test first locks the protection register and verifies that
 * writes to the target SCU register are rejected. It then unlocks
 * the protection register and confirms that the written value is
 * retained when unlocked.
 *
 * @param *machine - input machine configuration, passed directly
 *                   to QTest
 * @param protection_register - first SCU protection key register
 *                              (only one for keeping it simple)
 * @param test_register - Register to be used for writing arbitrary
 *                        values
 */
static void test_write_permission_lock_state(const char *machine,
                                             const uint32_t protection_register,
                                             const uint32_t test_register)
{
    QTestState *s = qtest_init(machine);

    /* Arbitrary value to lock provided SCU protection register */
    qtest_writel(s, protection_register, AST_SCU_PROT_LOCK_VALUE);

    /* Ensure that the SCU is really locked */
    assert_register_eq(s, protection_register, AST_SCU_PROT_LOCK_STATE);

    /* Write a known arbitrary value to test that the write is blocked */
    qtest_writel(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    /* We do not want to have the written value to be saved */
    assert_register_neq(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    /**
     * Unlock the SCU and verify that it can be written to.
     * Assumes that the first SCU protection register is sufficient to
     * unlock all protection registers, if multiple are present.
     */
    qtest_writel(s, protection_register, AST_SCU_PROT_UNLOCK_VALUE);
    assert_register_eq(s, protection_register, AST_SCU_PROT_UNLOCK_STATE);

    /* Write a known arbitrary value to test that the write works */
    qtest_writel(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    /* Ensure that the written value is retained */
    assert_register_eq(s, test_register, TEST_LOCK_ARBITRARY_VALUE);

    qtest_quit(s);
}

static void test_2500_write_permission_lock_state(void)
{
    test_write_permission_lock_state(
            AST2500_MACHINE,
            AST_SCU_BASE + AST2500_SCU_PROT_REG,
            AST_SCU_BASE + AST2500_SCU_MISC_2_CONTROL_REG
    );
}

static void test_2600_write_permission_lock_state(void)
{
    test_write_permission_lock_state(
            AST2600_MACHINE,
            AST_SCU_BASE + AST2600_SCU_PROT_REG,
            AST_SCU_BASE + AST2600_SCU_MISC_2_CONTROL_REG
    );
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ast2500/scu/protection_register",
                   test_2500_protection_register);
    qtest_add_func("/ast2600/scu/protection_register",
                   test_2600_protection_register);

    qtest_add_func("/ast2500/scu/write_permission_lock_state",
                   test_2500_write_permission_lock_state);
    qtest_add_func("/ast2600/scu/write_permission_lock_state",
                   test_2600_write_permission_lock_state);

    return g_test_run();
}
