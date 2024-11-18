#include "qemu/osdep.h"
#include "block/qdict.h" /* for qdict_extract_subqdict() */
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/config-file.h"

QemuOptsList *vm_config_groups[48];
QemuOptsList *drive_config_groups[5];

static QemuOptsList *find_list(QemuOptsList **lists, const char *group,
                               Error **errp)
{
    int i;

    qemu_load_module_for_opts(group);
    for (i = 0; lists[i] != NULL; i++) {
        if (strcmp(lists[i]->name, group) == 0)
            break;
    }
    if (lists[i] == NULL) {
        error_setg(errp, "There is no option group '%s'", group);
    }
    return lists[i];
}

QemuOptsList *qemu_find_opts(const char *group)
{
    QemuOptsList *ret;
    Error *local_err = NULL;

    ret = find_list(vm_config_groups, group, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }

    return ret;
}

QemuOpts *qemu_find_opts_singleton(const char *group)
{
    QemuOptsList *list;
    QemuOpts *opts;

    list = qemu_find_opts(group);
    assert(list);
    opts = qemu_opts_find(list, NULL);
    if (!opts) {
        opts = qemu_opts_create(list, NULL, 0, &error_abort);
    }
    return opts;
}

QemuOptsList *qemu_find_opts_err(const char *group, Error **errp)
{
    return find_list(vm_config_groups, group, errp);
}

void qemu_add_drive_opts(QemuOptsList *list)
{
    int entries, i;

    entries = ARRAY_SIZE(drive_config_groups);
    entries--; /* keep list NULL terminated */
    for (i = 0; i < entries; i++) {
        if (drive_config_groups[i] == NULL) {
            drive_config_groups[i] = list;
            return;
        }
    }
    fprintf(stderr, "ran out of space in drive_config_groups");
    abort();
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

/* Returns number of config groups on success, -errno on error */
static int qemu_config_foreach(FILE *fp, QEMUConfigCB *cb, void *opaque,
                               const char *fname, Error **errp)
{
    ERRP_GUARD();
    char line[1024], prev_group[64], group[64], arg[64], value[1024];
    Location loc;
    QDict *qdict = NULL;
    int res = -EINVAL, lno = 0;
    int count = 0;

    loc_push_none(&loc);
    while (fgets(line, sizeof(line), fp) != NULL) {
        ++lno;
        if (line[0] == '\n') {
            /* skip empty lines */
            continue;
        }
        if (line[0] == '#') {
            /* comment */
            continue;
        }
        if (line[0] == '[') {
            QDict *prev = qdict;
            if (sscanf(line, "[%63s \"%63[^\"]\"]", group, value) == 2) {
                qdict = qdict_new();
                qdict_put_str(qdict, "id", value);
                count++;
            } else if (sscanf(line, "[%63[^]]]", group) == 1) {
                qdict = qdict_new();
                count++;
            }
            if (qdict != prev) {
                if (prev) {
                    cb(prev_group, prev, opaque, errp);
                    qobject_unref(prev);
                    if (*errp) {
                        goto out;
                    }
                }
                strcpy(prev_group, group);
                continue;
            }
        }
        loc_set_file(fname, lno);
        value[0] = '\0';
        if (sscanf(line, " %63s = \"%1023[^\"]\"", arg, value) == 2 ||
            sscanf(line, " %63s = \"\"", arg) == 1) {
            /* arg = value */
            if (qdict == NULL) {
                error_setg(errp, "no group defined");
                goto out;
            }
            qdict_put_str(qdict, arg, value);
            continue;
        }
        error_setg(errp, "parse error");
        goto out;
    }
    if (ferror(fp)) {
        loc_pop(&loc);
        error_setg_errno(errp, errno, "Cannot read config file");
        goto out_no_loc;
    }
    res = count;
    if (qdict) {
        cb(group, qdict, opaque, errp);
    }
out:
    loc_pop(&loc);
out_no_loc:
    qobject_unref(qdict);
    return res;
}

void qemu_config_do_parse(const char *group, QDict *qdict, void *opaque, Error **errp)
{
    QemuOptsList **lists = opaque;
    QemuOptsList *list;

    list = find_list(lists, group, errp);
    if (!list) {
        return;
    }

    qemu_opts_from_qdict(list, qdict, errp);
}

int qemu_config_parse(FILE *fp, QemuOptsList **lists, const char *fname, Error **errp)
{
    return qemu_config_foreach(fp, qemu_config_do_parse, lists, fname, errp);
}

int qemu_read_config_file(const char *filename, QEMUConfigCB *cb, Error **errp)
{
    FILE *f = fopen(filename, "r");
    int ret;

    if (f == NULL) {
        error_setg_file_open(errp, errno, filename);
        return -errno;
    }

    ret = qemu_config_foreach(f, cb, vm_config_groups, filename, errp);
    fclose(f);
    return ret;
}

static bool config_parse_qdict_section(QDict *options, QemuOptsList *opts,
                                       Error **errp)
{
    QemuOpts *subopts;
    g_autoptr(QDict) subqdict = NULL;
    g_autoptr(QList) list = NULL;
    size_t orig_size, enum_size;
    char *prefix;

    prefix = g_strdup_printf("%s.", opts->name);
    qdict_extract_subqdict(options, &subqdict, prefix);
    g_free(prefix);
    orig_size = qdict_size(subqdict);
    if (!orig_size) {
        return true;
    }

    subopts = qemu_opts_create(opts, NULL, 0, errp);
    if (!subopts) {
        return false;
    }

    if (!qemu_opts_absorb_qdict(subopts, subqdict, errp)) {
        return false;
    }

    enum_size = qdict_size(subqdict);
    if (enum_size < orig_size && enum_size) {
        error_setg(errp, "Unknown option '%s' for [%s]",
                   qdict_first(subqdict)->key, opts->name);
        return false;
    }

    if (enum_size) {
        /* Multiple, enumerated sections */
        QListEntry *list_entry;
        unsigned i = 0;

        /* Not required anymore */
        qemu_opts_del(subopts);

        qdict_array_split(subqdict, &list);
        if (qdict_size(subqdict)) {
            error_setg(errp, "Unused option '%s' for [%s]",
                       qdict_first(subqdict)->key, opts->name);
            return false;
        }

        QLIST_FOREACH_ENTRY(list, list_entry) {
            QDict *section = qobject_to(QDict, qlist_entry_obj(list_entry));
            char *opt_name;

            if (!section) {
                error_setg(errp, "[%s] section (index %u) does not consist of "
                           "keys", opts->name, i);
                return false;
            }

            opt_name = g_strdup_printf("%s.%u", opts->name, i++);
            subopts = qemu_opts_create(opts, opt_name, 1, errp);
            g_free(opt_name);
            if (!subopts) {
                return false;
            }

            if (!qemu_opts_absorb_qdict(subopts, section, errp)) {
                qemu_opts_del(subopts);
                return false;
            }

            if (qdict_size(section)) {
                error_setg(errp, "[%s] section doesn't support the option '%s'",
                           opts->name, qdict_first(section)->key);
                qemu_opts_del(subopts);
                return false;
            }
        }
    }

    return true;
}

bool qemu_config_parse_qdict(QDict *options, QemuOptsList **lists,
                             Error **errp)
{
    int i;

    for (i = 0; lists[i]; i++) {
        if (!config_parse_qdict_section(options, lists[i], errp)) {
            return false;
        }
    }

    return true;
}
