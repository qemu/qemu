/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test attempts to execute a magic syscall. The syscall test plugin
 * should intercept this and return an expected value.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
#ifdef SKIP
    return EXIT_SUCCESS;
#endif
    /*
     * We cannot use a very large magic syscall number, because on some ISAs,
     * QEMU will treat it as an illegal instruction and trigger a critical
     * exception. For instance, on arm32, the syscall number cannot exceed
     * ARM_NR_BASE (0xf0000), as can be seen in
     * "linux-user/arm/cpu_loop.c:cpu_loop".
     * As well, some arch expect a minimum, like 4000 for mips 32 bits.
     *
     * Therefore, we pick 4096 because, as of now, no ISA in Linux uses this
     * number. This is just a test case; replace this number as needed in the
     * future.
     *
     * The corresponding syscall filter is implemented in
     * "tests/tcg/plugins/syscall.c".
     */
    long ret = syscall(4096, 0x66CCFF);
    if (ret != 0xFFCC66) {
        fprintf(stderr, "Error: unexpected syscall return value %ld\n", ret);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
