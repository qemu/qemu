/*
 *  cpu to uname machine name map
 *
 *  Copyright (c) 2009 Loïc Minier
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

#include "qemu.h"
//#include "qemu-common.h"
#include "cpu-uname.h"

/* return highest utsname machine name for emulated instruction set
 *
 * NB: the default emulated CPU ("any") might not match any existing CPU, e.g.
 * on ARM it has all features turned on, so there is no perfect arch string to
 * return here */
const char *cpu_to_uname_machine(void *cpu_env)
{
#ifdef TARGET_ARM
    /* utsname machine name on linux arm is CPU arch name + endianness, e.g.
     * armv7l; to get a list of CPU arch names from the linux source, use:
     *     grep arch_name: -A1 linux/arch/arm/mm/proc-*.S
     * see arch/arm/kernel/setup.c: setup_processor()
     */

    /* in theory, endianness is configurable on some ARM CPUs, but this isn't
     * used in user mode emulation */
#ifdef TARGET_WORDS_BIGENDIAN
#define utsname_suffix "b"
#else
#define utsname_suffix "l"
#endif
    if (arm_feature(cpu_env, ARM_FEATURE_V7))
        return "armv7" utsname_suffix;
    if (arm_feature(cpu_env, ARM_FEATURE_V6))
        return "armv6" utsname_suffix;
    /* earliest emulated CPU is ARMv5TE; qemu can emulate the 1026, but not its
     * Jazelle support */
    return "armv5te" utsname_suffix;
#elif defined(TARGET_X86_64)
    return "x86-64";
#elif defined(TARGET_I386)
    /* see arch/x86/kernel/cpu/bugs.c: check_bugs(), 386, 486, 586, 686 */
    uint32_t cpuid_version = ((CPUX86State *)cpu_env)->cpuid_version;
    int family = ((cpuid_version >> 8) & 0x0f) + ((cpuid_version >> 20) & 0xff);
    if (family == 4)
        return "i486";
    if (family == 5)
        return "i586";
    return "i686";
#else
    /* default is #define-d in each arch/ subdir */
    return UNAME_MACHINE;
#endif
}
