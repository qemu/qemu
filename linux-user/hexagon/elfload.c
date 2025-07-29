/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    static char buf[32];
    int err;

    /* For now, treat anything newer than v5 as a v73 */
    /* FIXME - Disable instructions that are newer than the specified arch */
    if (eflags == 0x04 ||    /* v5  */
        eflags == 0x05 ||    /* v55 */
        eflags == 0x60 ||    /* v60 */
        eflags == 0x61 ||    /* v61 */
        eflags == 0x62 ||    /* v62 */
        eflags == 0x65 ||    /* v65 */
        eflags == 0x66 ||    /* v66 */
        eflags == 0x67 ||    /* v67 */
        eflags == 0x8067 ||  /* v67t */
        eflags == 0x68 ||    /* v68 */
        eflags == 0x69 ||    /* v69 */
        eflags == 0x71 ||    /* v71 */
        eflags == 0x8071 ||  /* v71t */
        eflags == 0x73       /* v73 */
       ) {
        return "v73";
    }

    err = snprintf(buf, sizeof(buf), "unknown (0x%x)", eflags);
    return err >= 0 && err < sizeof(buf) ? buf : "unknown";
}
