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
#include "uname.h"

/* return highest utsname machine name for emulated instruction set
 *
 * NB: the default emulated CPU ("any") might not match any existing CPU, e.g.
 * on ARM it has all features turned on, so there is no perfect arch string to
 * return here */
const char *cpu_to_uname_machine(void *cpu_env)
{
#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)

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
#elif defined(TARGET_I386) && !defined(TARGET_X86_64)
    /* see arch/x86/kernel/cpu/bugs.c: check_bugs(), 386, 486, 586, 686 */
    CPUState *cpu = ENV_GET_CPU((CPUX86State *)cpu_env);
    int family = object_property_get_int(OBJECT(cpu), "family", NULL);
    if (family == 4) {
        return "i486";
    }
    if (family == 5) {
        return "i586";
    }
    return "i686";
#else
    /* default is #define-d in each arch/ subdir */
    return UNAME_MACHINE;
#endif
}


#define COPY_UTSNAME_FIELD(dest, src) \
  do { \
      /* __NEW_UTS_LEN doesn't include terminating null */ \
      (void) strncpy((dest), (src), __NEW_UTS_LEN); \
      (dest)[__NEW_UTS_LEN] = '\0'; \
  } while (0)

int sys_uname(struct new_utsname *buf)
{
  struct utsname uts_buf;

  if (uname(&uts_buf) < 0)
      return (-1);

  /*
   * Just in case these have some differences, we
   * translate utsname to new_utsname (which is the
   * struct linux kernel uses).
   */

  memset(buf, 0, sizeof(*buf));
  COPY_UTSNAME_FIELD(buf->sysname, uts_buf.sysname);
  COPY_UTSNAME_FIELD(buf->nodename, uts_buf.nodename);
  COPY_UTSNAME_FIELD(buf->release, uts_buf.release);
  COPY_UTSNAME_FIELD(buf->version, uts_buf.version);
  COPY_UTSNAME_FIELD(buf->machine, uts_buf.machine);
#ifdef _GNU_SOURCE
  COPY_UTSNAME_FIELD(buf->domainname, uts_buf.domainname);
#endif
  return (0);

#undef COPY_UTSNAME_FIELD
}

static int relstr_to_int(const char *s)
{
    /* Convert a uname release string like "2.6.18" to an integer
     * of the form 0x020612. (Beware that 0x020612 is *not* 2.6.12.)
     */
    int i, n, tmp;

    tmp = 0;
    for (i = 0; i < 3; i++) {
        n = 0;
        while (*s >= '0' && *s <= '9') {
            n *= 10;
            n += *s - '0';
            s++;
        }
        tmp = (tmp << 8) + n;
        if (*s == '.') {
            s++;
        }
    }
    return tmp;
}

int get_osversion(void)
{
    static int osversion;
    struct new_utsname buf;
    const char *s;

    if (osversion)
        return osversion;
    if (qemu_uname_release && *qemu_uname_release) {
        s = qemu_uname_release;
    } else {
        if (sys_uname(&buf))
            return 0;
        s = buf.release;
    }
    osversion = relstr_to_int(s);
    return osversion;
}

void init_qemu_uname_release(void)
{
    /* Initialize qemu_uname_release for later use.
     * If the host kernel is too old and the user hasn't asked for
     * a specific fake version number, we might want to fake a minimum
     * target kernel version.
     */
    struct new_utsname buf;

    if (qemu_uname_release && *qemu_uname_release) {
        return;
    }

    if (sys_uname(&buf)) {
        return;
    }

    if (relstr_to_int(buf.release) < relstr_to_int(UNAME_MINIMUM_RELEASE)) {
        qemu_uname_release = UNAME_MINIMUM_RELEASE;
    }
}
