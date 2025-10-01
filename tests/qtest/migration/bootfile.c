/*
 * Guest code setup for migration tests
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

/*
 * The boot file modifies memory area in [start_address, end_address)
 * repeatedly. It outputs a 'B' at a fixed rate while it's still running.
 */
#include "bootfile.h"
#include "i386/a-b-bootblock.h"
#include "aarch64/a-b-kernel.h"
#include "ppc64/a-b-kernel.h"
#include "s390x/a-b-bios.h"

static char *bootpath;

void bootfile_delete(void)
{
    if (!bootpath) {
        return;
    }
    unlink(bootpath);
    g_free(bootpath);
    bootpath = NULL;
}

char *bootfile_create(const char *arch, const char *dir, bool suspend_me)
{
    unsigned char *content;
    size_t len;

    bootfile_delete();
    bootpath = g_strdup_printf("%s/bootsect", dir);
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        /* the assembled x86 boot sector should be exactly one sector large */
        g_assert(sizeof(x86_bootsect) == 512);
        x86_bootsect[SYM_suspend_me - SYM_start] = suspend_me;
        content = x86_bootsect;
        len = sizeof(x86_bootsect);
    } else if (g_str_equal(arch, "s390x")) {
        content = s390x_elf;
        len = sizeof(s390x_elf);
    } else if (strcmp(arch, "ppc64") == 0) {
        content = ppc64_kernel;
        len = sizeof(ppc64_kernel);
    } else if (strcmp(arch, "aarch64") == 0) {
        content = aarch64_kernel;
        len = sizeof(aarch64_kernel);
        g_assert(sizeof(aarch64_kernel) <= ARM_TEST_MAX_KERNEL_SIZE);
    } else {
        g_assert_not_reached();
    }

    FILE *bootfile = fopen(bootpath, "wb");

    g_assert_cmpint(fwrite(content, len, 1, bootfile), ==, 1);
    fclose(bootfile);

    return bootpath;
}

char *bootfile_get(void)
{
    return bootpath;
}
