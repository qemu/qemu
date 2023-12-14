/*
 * Semihosting Stubs for system emulation
 *
 * Copyright (c) 2019 Linaro Ltd
 *
 * Stubs for system targets that don't actually do semihosting.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "semihosting/semihost.h"

/* Empty config */
QemuOptsList qemu_semihosting_config_opts = {
    .name = "",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_semihosting_config_opts.head),
    .desc = {
        { /* end of list */ }
    },
};

/* Queries to config status default to off */
bool semihosting_enabled(bool is_user)
{
    return false;
}

/*
 * All the rest are empty subs. We could g_assert_not_reached() but
 * that adds extra weight to the final binary. Waste not want not.
 */
void qemu_semihosting_enable(void)
{
}

int qemu_semihosting_config_options(const char *optstr)
{
    return 1;
}

const char *semihosting_get_arg(int i)
{
    return NULL;
}

int semihosting_get_argc(void)
{
    return 0;
}

const char *semihosting_get_cmdline(void)
{
    return NULL;
}

void semihosting_arg_fallback(const char *file, const char *cmd)
{
}

void qemu_semihosting_chardev_init(void)
{
}
