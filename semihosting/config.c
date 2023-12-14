/*
 * Semihosting configuration
 *
 * Copyright (c) 2015 Imagination Technologies
 * Copyright (c) 2019 Linaro Ltd
 *
 * This controls the configuration of semihosting for all guest
 * targets that support it. Architecture specific handling is handled
 * in target/HW/HW-semi.c
 *
 * Semihosting is slightly strange in that it is also supported by some
 * linux-user targets. However in that use case no configuration of
 * the outputs and command lines is supported.
 *
 * The config module is common to all system targets however as vl.c
 * needs to link against the helpers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "semihosting/semihost.h"
#include "chardev/char.h"

QemuOptsList qemu_semihosting_config_opts = {
    .name = "semihosting-config",
    .merge_lists = true,
    .implied_opt_name = "enable",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_semihosting_config_opts.head),
    .desc = {
        {
            .name = "enable",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "userspace",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "target",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "chardev",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "arg",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

typedef struct SemihostingConfig {
    bool enabled;
    bool userspace_enabled;
    SemihostingTarget target;
    char **argv;
    int argc;
    const char *cmdline; /* concatenated argv */
} SemihostingConfig;

static SemihostingConfig semihosting;
static const char *semihost_chardev;

bool semihosting_enabled(bool is_user)
{
    return semihosting.enabled && (!is_user || semihosting.userspace_enabled);
}

SemihostingTarget semihosting_get_target(void)
{
    return semihosting.target;
}

const char *semihosting_get_arg(int i)
{
    if (i >= semihosting.argc) {
        return NULL;
    }
    return semihosting.argv[i];
}

int semihosting_get_argc(void)
{
    return semihosting.argc;
}

const char *semihosting_get_cmdline(void)
{
    if (semihosting.cmdline == NULL && semihosting.argc > 0) {
        semihosting.cmdline = g_strjoinv(" ", (gchar **)semihosting.argv);
    }
    return semihosting.cmdline;
}

static int add_semihosting_arg(void *opaque,
                               const char *name, const char *val,
                               Error **errp)
{
    SemihostingConfig *s = opaque;
    if (strcmp(name, "arg") == 0) {
        s->argc++;
        /* one extra element as g_strjoinv() expects NULL-terminated array */
        s->argv = g_renew(char *, s->argv, s->argc + 1);
        s->argv[s->argc - 1] = g_strdup(val);
        s->argv[s->argc] = NULL;
    }
    return 0;
}

/* Use strings passed via -kernel/-append to initialize semihosting.argv[] */
void semihosting_arg_fallback(const char *file, const char *cmd)
{
    char *cmd_token;
    g_autofree char *cmd_dup = g_strdup(cmd);

    /* argv[0] */
    add_semihosting_arg(&semihosting, "arg", file, NULL);

    /* split -append and initialize argv[1..n] */
    cmd_token = strtok(cmd_dup, " ");
    while (cmd_token) {
        add_semihosting_arg(&semihosting, "arg", cmd_token, NULL);
        cmd_token = strtok(NULL, " ");
    }
}

void qemu_semihosting_enable(void)
{
    semihosting.enabled = true;
    semihosting.target = SEMIHOSTING_TARGET_AUTO;
}

int qemu_semihosting_config_options(const char *optstr)
{
    QemuOptsList *opt_list = qemu_find_opts("semihosting-config");
    QemuOpts *opts = qemu_opts_parse_noisily(opt_list, optstr, false);

    semihosting.enabled = true;

    if (opts != NULL) {
        semihosting.enabled = qemu_opt_get_bool(opts, "enable",
                                                true);
        semihosting.userspace_enabled = qemu_opt_get_bool(opts, "userspace",
                                                          false);
        const char *target = qemu_opt_get(opts, "target");
        /* setup of chardev is deferred until they are initialised */
        semihost_chardev = qemu_opt_get(opts, "chardev");
        if (target != NULL) {
            if (strcmp("native", target) == 0) {
                semihosting.target = SEMIHOSTING_TARGET_NATIVE;
            } else if (strcmp("gdb", target) == 0) {
                semihosting.target = SEMIHOSTING_TARGET_GDB;
            } else  if (strcmp("auto", target) == 0) {
                semihosting.target = SEMIHOSTING_TARGET_AUTO;
            } else {
                error_report("unsupported semihosting-config %s",
                             optstr);
                return 1;
            }
        } else {
            semihosting.target = SEMIHOSTING_TARGET_AUTO;
        }
        /* Set semihosting argument count and vector */
        qemu_opt_foreach(opts, add_semihosting_arg,
                         &semihosting, NULL);
    } else {
        error_report("unsupported semihosting-config %s", optstr);
        return 1;
    }

    return 0;
}

/* We had to defer this until chardevs were created */
void qemu_semihosting_chardev_init(void)
{
    Chardev *chr = NULL;

    if (semihost_chardev) {
        chr = qemu_chr_find(semihost_chardev);
        if (chr == NULL) {
            error_report("semihosting chardev '%s' not found",
                         semihost_chardev);
            exit(1);
        }
    }

    qemu_semihosting_console_init(chr);
}
