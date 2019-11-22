/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef HEXAGON_TARGET_ELF_H
#define HEXAGON_TARGET_ELF_H

static inline const char *cpu_get_model(uint32_t eflags)
{
    /* For now, treat anything newer than v5 as a v67 */
    /* FIXME - Disable instructions that are newer than the specified arch */
    if (eflags == 0x04 ||    /* v5  */
        eflags == 0x05 ||    /* v55 */
        eflags == 0x60 ||    /* v60 */
        eflags == 0x61 ||    /* v61 */
        eflags == 0x62 ||    /* v62 */
        eflags == 0x65 ||    /* v65 */
        eflags == 0x66 ||    /* v66 */
        eflags == 0x67) {    /* v67 */
        return "v67";
    }
    return "unknown";
}

#endif
