/*
 * Virtio 9p
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/module.h"

static QemuOptsList qemu_fsdev_opts = {
    .name = "fsdev",
    .implied_opt_name = "fsdriver",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_fsdev_opts.head),
    .desc = {
        {
            .name = "fsdriver",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "path",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "security_model",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "writeout",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "readonly",
            .type = QEMU_OPT_BOOL,

        }, {
            .name = "socket",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "sock_fd",
            .type = QEMU_OPT_NUMBER,
        },

        { /*End of list */ }
    },
};

static QemuOptsList qemu_virtfs_opts = {
    .name = "virtfs",
    .implied_opt_name = "fsdriver",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_virtfs_opts.head),
    .desc = {
        {
            .name = "fsdriver",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "path",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "mount_tag",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "security_model",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "writeout",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "readonly",
            .type = QEMU_OPT_BOOL,
        }, {
            .name = "socket",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "sock_fd",
            .type = QEMU_OPT_NUMBER,
        },

        { /*End of list */ }
    },
};

static void fsdev_register_config(void)
{
    qemu_add_opts(&qemu_fsdev_opts);
    qemu_add_opts(&qemu_virtfs_opts);
}
machine_init(fsdev_register_config);
