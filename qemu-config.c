#include "qemu-common.h"
#include "qemu-error.h"
#include "qemu-option.h"
#include "qemu-config.h"
#include "hw/qdev.h"

static QemuOptsList qemu_drive_opts = {
    .name = "drive",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_drive_opts.head),
    .desc = {
        {
            .name = "bus",
            .type = QEMU_OPT_NUMBER,
            .help = "bus number",
        },{
            .name = "unit",
            .type = QEMU_OPT_NUMBER,
            .help = "unit number (i.e. lun for scsi)",
        },{
            .name = "if",
            .type = QEMU_OPT_STRING,
            .help = "interface (ide, scsi, sd, mtd, floppy, pflash, virtio)",
        },{
            .name = "index",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "cyls",
            .type = QEMU_OPT_NUMBER,
            .help = "number of cylinders (ide disk geometry)",
        },{
            .name = "heads",
            .type = QEMU_OPT_NUMBER,
            .help = "number of heads (ide disk geometry)",
        },{
            .name = "secs",
            .type = QEMU_OPT_NUMBER,
            .help = "number of sectors (ide disk geometry)",
        },{
            .name = "trans",
            .type = QEMU_OPT_STRING,
            .help = "chs translation (auto, lba. none)",
        },{
            .name = "media",
            .type = QEMU_OPT_STRING,
            .help = "media type (disk, cdrom)",
        },{
            .name = "snapshot",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "file",
            .type = QEMU_OPT_STRING,
            .help = "disk image",
        },{
            .name = "cache",
            .type = QEMU_OPT_STRING,
            .help = "host cache usage (none, writeback, writethrough, unsafe)",
        },{
            .name = "aio",
            .type = QEMU_OPT_STRING,
            .help = "host AIO implementation (threads, native)",
        },{
            .name = "format",
            .type = QEMU_OPT_STRING,
            .help = "disk format (raw, qcow2, ...)",
        },{
            .name = "serial",
            .type = QEMU_OPT_STRING,
        },{
            .name = "rerror",
            .type = QEMU_OPT_STRING,
        },{
            .name = "werror",
            .type = QEMU_OPT_STRING,
        },{
            .name = "addr",
            .type = QEMU_OPT_STRING,
            .help = "pci address (virtio only)",
        },{
            .name = "readonly",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_chardev_opts = {
    .name = "chardev",
    .implied_opt_name = "backend",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_chardev_opts.head),
    .desc = {
        {
            .name = "backend",
            .type = QEMU_OPT_STRING,
        },{
            .name = "path",
            .type = QEMU_OPT_STRING,
        },{
            .name = "host",
            .type = QEMU_OPT_STRING,
        },{
            .name = "port",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localaddr",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localport",
            .type = QEMU_OPT_STRING,
        },{
            .name = "to",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "wait",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "server",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "delay",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "telnet",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "width",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "height",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "cols",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "rows",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "mux",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "signal",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "name",
            .type = QEMU_OPT_STRING,
        },{
            .name = "debug",
            .type = QEMU_OPT_NUMBER,
        },
        { /* end of list */ }
    },
};

QemuOptsList qemu_fsdev_opts = {
    .name = "fsdev",
    .implied_opt_name = "fstype",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_fsdev_opts.head),
    .desc = {
        {
            .name = "fstype",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "path",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "security_model",
            .type = QEMU_OPT_STRING,
        },
        { /*End of list */ }
    },
};

QemuOptsList qemu_virtfs_opts = {
    .name = "virtfs",
    .implied_opt_name = "fstype",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_virtfs_opts.head),
    .desc = {
        {
            .name = "fstype",
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
        },

        { /*End of list */ }
    },
};

static QemuOptsList qemu_device_opts = {
    .name = "device",
    .implied_opt_name = "driver",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_device_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting device properties
         */
        { /* end of list */ }
    },
};

static QemuOptsList qemu_netdev_opts = {
    .name = "netdev",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_netdev_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

static QemuOptsList qemu_net_opts = {
    .name = "net",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_net_opts.head),
    .desc = {
        /*
         * no elements => accept any params
         * validation will happen later
         */
        { /* end of list */ }
    },
};

static QemuOptsList qemu_rtc_opts = {
    .name = "rtc",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rtc_opts.head),
    .desc = {
        {
            .name = "base",
            .type = QEMU_OPT_STRING,
        },{
            .name = "clock",
            .type = QEMU_OPT_STRING,
        },{
            .name = "driftfix",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_global_opts = {
    .name = "global",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_global_opts.head),
    .desc = {
        {
            .name = "driver",
            .type = QEMU_OPT_STRING,
        },{
            .name = "property",
            .type = QEMU_OPT_STRING,
        },{
            .name = "value",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static QemuOptsList qemu_mon_opts = {
    .name = "mon",
    .implied_opt_name = "chardev",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_mon_opts.head),
    .desc = {
        {
            .name = "mode",
            .type = QEMU_OPT_STRING,
        },{
            .name = "chardev",
            .type = QEMU_OPT_STRING,
        },{
            .name = "default",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "pretty",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

#ifdef CONFIG_SIMPLE_TRACE
static QemuOptsList qemu_trace_opts = {
    .name = "trace",
    .implied_opt_name = "trace",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_trace_opts.head),
    .desc = {
        {
            .name = "file",
            .type = QEMU_OPT_STRING,
        },
        { /* end if list */ }
    },
};
#endif

static QemuOptsList qemu_cpudef_opts = {
    .name = "cpudef",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_cpudef_opts.head),
    .desc = {
        {
            .name = "name",
            .type = QEMU_OPT_STRING,
        },{
            .name = "level",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "vendor",
            .type = QEMU_OPT_STRING,
        },{
            .name = "family",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "model",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "stepping",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "feature_edx",      /* cpuid 0000_0001.edx */
            .type = QEMU_OPT_STRING,
        },{
            .name = "feature_ecx",      /* cpuid 0000_0001.ecx */
            .type = QEMU_OPT_STRING,
        },{
            .name = "extfeature_edx",   /* cpuid 8000_0001.edx */
            .type = QEMU_OPT_STRING,
        },{
            .name = "extfeature_ecx",   /* cpuid 8000_0001.ecx */
            .type = QEMU_OPT_STRING,
        },{
            .name = "xlevel",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "model_id",
            .type = QEMU_OPT_STRING,
        },{
            .name = "vendor_override",
            .type = QEMU_OPT_NUMBER,
        },
        { /* end of list */ }
    },
};

QemuOptsList qemu_spice_opts = {
    .name = "spice",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_spice_opts.head),
    .desc = {
        {
            .name = "port",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "tls-port",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "addr",
            .type = QEMU_OPT_STRING,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "password",
            .type = QEMU_OPT_STRING,
        },{
            .name = "disable-ticketing",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "disable-copy-paste",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "x509-dir",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-key-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-key-password",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-cert-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-cacert-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-dh-key-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "tls-ciphers",
            .type = QEMU_OPT_STRING,
        },{
            .name = "tls-channel",
            .type = QEMU_OPT_STRING,
        },{
            .name = "plaintext-channel",
            .type = QEMU_OPT_STRING,
        },{
            .name = "image-compression",
            .type = QEMU_OPT_STRING,
        },{
            .name = "jpeg-wan-compression",
            .type = QEMU_OPT_STRING,
        },{
            .name = "zlib-glz-wan-compression",
            .type = QEMU_OPT_STRING,
        },{
            .name = "streaming-video",
            .type = QEMU_OPT_STRING,
        },{
            .name = "agent-mouse",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "playback-compression",
            .type = QEMU_OPT_BOOL,
        },
        { /* end if list */ }
    },
};

QemuOptsList qemu_option_rom_opts = {
    .name = "option-rom",
    .implied_opt_name = "romfile",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_option_rom_opts.head),
    .desc = {
        {
            .name = "bootindex",
            .type = QEMU_OPT_NUMBER,
        }, {
            .name = "romfile",
            .type = QEMU_OPT_STRING,
        },
        { /* end if list */ }
    },
};

static QemuOptsList qemu_machine_opts = {
    .name = "machine",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_machine_opts.head),
    .desc = {
        {
            .name = "accel",
            .type = QEMU_OPT_STRING,
            .help = "accelerator list",
        },
        { /* End of list */ }
    },
};

static QemuOptsList *vm_config_groups[32] = {
    &qemu_drive_opts,
    &qemu_chardev_opts,
    &qemu_device_opts,
    &qemu_netdev_opts,
    &qemu_net_opts,
    &qemu_rtc_opts,
    &qemu_global_opts,
    &qemu_mon_opts,
    &qemu_cpudef_opts,
#ifdef CONFIG_SIMPLE_TRACE
    &qemu_trace_opts,
#endif
    &qemu_option_rom_opts,
    &qemu_machine_opts,
    NULL,
};

static QemuOptsList *find_list(QemuOptsList **lists, const char *group)
{
    int i;

    for (i = 0; lists[i] != NULL; i++) {
        if (strcmp(lists[i]->name, group) == 0)
            break;
    }
    if (lists[i] == NULL) {
        error_report("there is no option group \"%s\"", group);
    }
    return lists[i];
}

QemuOptsList *qemu_find_opts(const char *group)
{
    return find_list(vm_config_groups, group);
}

void qemu_add_opts(QemuOptsList *list)
{
    int entries, i;

    entries = ARRAY_SIZE(vm_config_groups);
    entries--; /* keep list NULL terminated */
    for (i = 0; i < entries; i++) {
        if (vm_config_groups[i] == NULL) {
            vm_config_groups[i] = list;
            return;
        }
    }
    fprintf(stderr, "ran out of space in vm_config_groups");
    abort();
}

int qemu_set_option(const char *str)
{
    char group[64], id[64], arg[64];
    QemuOptsList *list;
    QemuOpts *opts;
    int rc, offset;

    rc = sscanf(str, "%63[^.].%63[^.].%63[^=]%n", group, id, arg, &offset);
    if (rc < 3 || str[offset] != '=') {
        error_report("can't parse: \"%s\"", str);
        return -1;
    }

    list = qemu_find_opts(group);
    if (list == NULL) {
        return -1;
    }

    opts = qemu_opts_find(list, id);
    if (!opts) {
        error_report("there is no %s \"%s\" defined",
                     list->name, id);
        return -1;
    }

    if (qemu_opt_set(opts, arg, str+offset+1) == -1) {
        return -1;
    }
    return 0;
}

int qemu_global_option(const char *str)
{
    char driver[64], property[64];
    QemuOpts *opts;
    int rc, offset;

    rc = sscanf(str, "%63[^.].%63[^=]%n", driver, property, &offset);
    if (rc < 2 || str[offset] != '=') {
        error_report("can't parse: \"%s\"", str);
        return -1;
    }

    opts = qemu_opts_create(&qemu_global_opts, NULL, 0);
    qemu_opt_set(opts, "driver", driver);
    qemu_opt_set(opts, "property", property);
    qemu_opt_set(opts, "value", str+offset+1);
    return 0;
}

struct ConfigWriteData {
    QemuOptsList *list;
    FILE *fp;
};

static int config_write_opt(const char *name, const char *value, void *opaque)
{
    struct ConfigWriteData *data = opaque;

    fprintf(data->fp, "  %s = \"%s\"\n", name, value);
    return 0;
}

static int config_write_opts(QemuOpts *opts, void *opaque)
{
    struct ConfigWriteData *data = opaque;
    const char *id = qemu_opts_id(opts);

    if (id) {
        fprintf(data->fp, "[%s \"%s\"]\n", data->list->name, id);
    } else {
        fprintf(data->fp, "[%s]\n", data->list->name);
    }
    qemu_opt_foreach(opts, config_write_opt, data, 0);
    fprintf(data->fp, "\n");
    return 0;
}

void qemu_config_write(FILE *fp)
{
    struct ConfigWriteData data = { .fp = fp };
    QemuOptsList **lists = vm_config_groups;
    int i;

    fprintf(fp, "# qemu config file\n\n");
    for (i = 0; lists[i] != NULL; i++) {
        data.list = lists[i];
        qemu_opts_foreach(data.list, config_write_opts, &data, 0);
    }
}

int qemu_config_parse(FILE *fp, QemuOptsList **lists, const char *fname)
{
    char line[1024], group[64], id[64], arg[64], value[1024];
    Location loc;
    QemuOptsList *list = NULL;
    QemuOpts *opts = NULL;
    int res = -1, lno = 0;

    loc_push_none(&loc);
    while (fgets(line, sizeof(line), fp) != NULL) {
        loc_set_file(fname, ++lno);
        if (line[0] == '\n') {
            /* skip empty lines */
            continue;
        }
        if (line[0] == '#') {
            /* comment */
            continue;
        }
        if (sscanf(line, "[%63s \"%63[^\"]\"]", group, id) == 2) {
            /* group with id */
            list = find_list(lists, group);
            if (list == NULL)
                goto out;
            opts = qemu_opts_create(list, id, 1);
            continue;
        }
        if (sscanf(line, "[%63[^]]]", group) == 1) {
            /* group without id */
            list = find_list(lists, group);
            if (list == NULL)
                goto out;
            opts = qemu_opts_create(list, NULL, 0);
            continue;
        }
        if (sscanf(line, " %63s = \"%1023[^\"]\"", arg, value) == 2) {
            /* arg = value */
            if (opts == NULL) {
                error_report("no group defined");
                goto out;
            }
            if (qemu_opt_set(opts, arg, value) != 0) {
                goto out;
            }
            continue;
        }
        error_report("parse error");
        goto out;
    }
    if (ferror(fp)) {
        error_report("error reading file");
        goto out;
    }
    res = 0;
out:
    loc_pop(&loc);
    return res;
}

int qemu_read_config_file(const char *filename)
{
    FILE *f = fopen(filename, "r");
    int ret;

    if (f == NULL) {
        return -errno;
    }

    ret = qemu_config_parse(f, vm_config_groups, filename);
    fclose(f);

    if (ret == 0) {
        return 0;
    } else {
        return -EINVAL;
    }
}
