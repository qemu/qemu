/*
 * SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Bootinfo tags from linux bootinfo.h and bootinfo-mac.h:
 * This is an easily parsable and extendable structure containing all
 * information to be passed from the bootstrap to the kernel
 *
 * This structure is copied right after the kernel by the bootstrap
 * routine.
 */

#ifndef HW_M68K_BOOTINFO_H
#define HW_M68K_BOOTINFO_H

#define BOOTINFO0(as, base, id) \
    do { \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, sizeof(struct bi_record)); \
        base += 2; \
    } while (0)

#define BOOTINFO1(as, base, id, value) \
    do { \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, sizeof(struct bi_record) + 4); \
        base += 2; \
        stl_phys(as, base, value); \
        base += 4; \
    } while (0)

#define BOOTINFO2(as, base, id, value1, value2) \
    do { \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, sizeof(struct bi_record) + 8); \
        base += 2; \
        stl_phys(as, base, value1); \
        base += 4; \
        stl_phys(as, base, value2); \
        base += 4; \
    } while (0)

#define BOOTINFOSTR(as, base, id, string) \
    do { \
        int i; \
        stw_phys(as, base, id); \
        base += 2; \
        stw_phys(as, base, \
                 (sizeof(struct bi_record) + strlen(string) + 2) & ~1); \
        base += 2; \
        for (i = 0; string[i]; i++) { \
            stb_phys(as, base++, string[i]); \
        } \
        stb_phys(as, base++, 0); \
        base = (parameters_base + 1) & ~1; \
    } while (0)
#endif
