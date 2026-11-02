/*
 * MIPS32R6 tests: PC-relative AUIPC and ALUIPC instructions
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#include "../include/tap.h"

/*
 * Run INSN with the immediate IMM, and probe the address of INSN with a
 * second AUIPC (which yields its own address, +4 being INSN's size).
 */
#define PCREL(insn, imm, res, addr)                                     \
    __asm__ volatile (                                                  \
        ".set push\n\t"                                                 \
        ".set noreorder\n\t"                                            \
        insn " %0, " #imm "\n\t"                                        \
        "auipc %1, 0\n\t"                                               \
        ".set pop\n\t"                                                  \
        : "=r" (res), "=r" (addr))

static int check(const char *name, uint32_t imm, uint32_t res, uint32_t probe,
                 int aligned)
{
    /* The probe AUIPC is the instruction after the tested one. */
    uint32_t addr = probe - 4;
    uint32_t expected = addr + (imm << 16);
    int ok;

    if (aligned) {
        expected &= ~0xffffu;
    }
    ok = res == expected;
    if (!ok) {
        TAP_INFO("%s imm=0x%04x at 0x%08x: got 0x%08x expected 0x%08x",
                 name, imm, addr, res, expected);
    }
    return !ok;
}

#define CHECK_AUIPC(imm)                                                \
    do {                                                                \
        uint32_t res, probe;                                            \
        PCREL("auipc", imm, res, probe);                                \
        ret |= check("AUIPC", imm, res, probe, 0);                      \
    } while (0)

#define CHECK_ALUIPC(imm)                                               \
    do {                                                                \
        uint32_t res, probe;                                            \
        PCREL("aluipc", imm, res, probe);                               \
        ret |= check("ALUIPC", imm, res, probe, 1);                     \
    } while (0)

static int test_auipc(void)
{
    int ret = 0;

    CHECK_AUIPC(0);
    CHECK_AUIPC(1);
    CHECK_AUIPC(0x7fff);
    CHECK_AUIPC(0x8000);
    CHECK_AUIPC(0xffff);
    return ret;
}

static int test_aluipc(void)
{
    int ret = 0;

    CHECK_ALUIPC(0);
    CHECK_ALUIPC(1);
    CHECK_ALUIPC(0x7fff);
    CHECK_ALUIPC(0x8000);
    CHECK_ALUIPC(0xffff);
    return ret;
}

int main(void)
{
    TAP_START();
    tap_report(!test_auipc(), "MIPS32R6 AUIPC");
    tap_report(!test_aluipc(), "MIPS32R6 ALUIPC");

    return tap_done();
}
