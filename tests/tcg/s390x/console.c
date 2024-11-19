/*
 * Console code for multiarch tests.
 * Reuses the pc-bios/s390-ccw implementation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../../../pc-bios/s390-ccw/sclp.c"
#include "../../../roms/SLOF/lib/libc/string/memset.c"
#include "../../../roms/SLOF/lib/libc/string/memcpy.c"

void __sys_outc(char c)
{
    write(1, &c, sizeof(c));
}
