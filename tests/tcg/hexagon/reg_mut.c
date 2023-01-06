
/*
 *  Copyright(c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

static int err;

#define check(N, EXPECT) \
    do { \
        uint64_t value = N; \
        uint64_t expect = EXPECT; \
        if (value != EXPECT) { \
            printf("ERROR: \"%s\" 0x%04llx != 0x%04llx at %s:%d\n", #N, value, \
                   expect, __FILE__, __LINE__); \
            err++; \
        } \
    } while (0)

#define check_ne(N, EXPECT) \
    do { \
        uint64_t value = N; \
        uint64_t expect = EXPECT; \
        if (value == EXPECT) { \
            printf("ERROR: \"%s\" 0x%04llx == 0x%04llx at %s:%d\n", #N, value, \
                   expect, __FILE__, __LINE__); \
            err++; \
        } \
    } while (0)

#define WRITE_REG_NOCLOBBER(output, reg_name, input) \
    asm volatile(reg_name " = %1\n\t" \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : );

#define WRITE_REG_ENCODED(output, reg_name, input, encoding) \
    asm volatile("r0 = %1\n\t" \
                 encoding "\n\t" \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : "r0");

#define WRITE_REG_PAIR_ENCODED(output, reg_name, input, encoding) \
    asm volatile("r1:0 = %1\n\t" \
                 encoding "\n\t" \
                 "%0 = " reg_name "\n\t" \
                 : "=r"(output) \
                 : "r"(input) \
                 : "r1:0");

/*
 * Instruction word: { pc = r0 }
 *
 * This instruction is barred by the assembler.
 *
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |    Opc[A2_tfrrcr]   | Src[R0] |P P|                 |  C9/PC  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define PC_EQ_R0        ".word 0x6220c009"
#define C9_8_EQ_R1_0    ".word 0x6320c008"

static inline void write_control_registers(void)
{
    uint32_t result = 0;

    WRITE_REG_NOCLOBBER(result, "usr", 0xffffffff);
    check(result, 0x3ecfff3f);

    WRITE_REG_NOCLOBBER(result, "gp", 0xffffffff);
    check(result, 0xffffffc0);

    WRITE_REG_NOCLOBBER(result, "upcyclelo", 0xffffffff);
    check(result, 0x00000000);

    WRITE_REG_NOCLOBBER(result, "upcyclehi", 0xffffffff);
    check(result, 0x00000000);

    WRITE_REG_NOCLOBBER(result, "utimerlo", 0xffffffff);
    check(result, 0x00000000);

    WRITE_REG_NOCLOBBER(result, "utimerhi", 0xffffffff);
    check(result, 0x00000000);

    /*
     * PC is special.  Setting it to these values
     * should cause a catastrophic failure.
     */
    WRITE_REG_ENCODED(result, "pc", 0x00000000, PC_EQ_R0);
    check_ne(result, 0x00000000);

    WRITE_REG_ENCODED(result, "pc", 0x00000001, PC_EQ_R0);
    check_ne(result, 0x00000001);

    WRITE_REG_ENCODED(result, "pc", 0xffffffff, PC_EQ_R0);
    check_ne(result, 0xffffffff);
}

static inline void write_control_register_pairs(void)
{
    uint64_t result = 0;

    WRITE_REG_NOCLOBBER(result, "c11:10", 0xffffffffffffffff);
    check(result, 0xffffffc0ffffffff);

    WRITE_REG_NOCLOBBER(result, "c15:14", 0xffffffffffffffff);
    check(result, 0x0000000000000000);

    WRITE_REG_NOCLOBBER(result, "c31:30", 0xffffffffffffffff);
    check(result, 0x0000000000000000);

    WRITE_REG_PAIR_ENCODED(result, "c9:8", (uint64_t) 0x0000000000000000,
                           C9_8_EQ_R1_0);
    check_ne(result, 0x000000000000000);

    WRITE_REG_PAIR_ENCODED(result, "c9:8", 0x0000000100000000, C9_8_EQ_R1_0);
    check_ne(result, 0x0000000100000000);

    WRITE_REG_PAIR_ENCODED(result, "c9:8", 0xffffffffffffffff, C9_8_EQ_R1_0);
    check_ne(result, 0xffffffffffffffff);
}

int main()
{
    err = 0;

    write_control_registers();
    write_control_register_pairs();

    puts(err ? "FAIL" : "PASS");
    return err;
}
