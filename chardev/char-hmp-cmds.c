/*
 * HMP commands related to character devices
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "chardev/char.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-char.h"
#include "qapi/qmp/qdict.h"
#include "qemu/config-file.h"
#include "qemu/option.h"

void hmp_info_chardev(Monitor *mon, const QDict *qdict)
{
    ChardevInfoList *char_info, *info;

    char_info = qmp_query_chardev(NULL);
    for (info = char_info; info; info = info->next) {
        monitor_printf(mon, "%s: filename=%s\n", info->value->label,
                                                 info->value->filename);
    }

    qapi_free_ChardevInfoList(char_info);
}

void hmp_ringbuf_write(Monitor *mon, const QDict *qdict)
{
    const char *chardev = qdict_get_str(qdict, "device");
    const char *data = qdict_get_str(qdict, "data");
    Error *err = NULL;

    qmp_ringbuf_write(chardev, data, false, 0, &err);

    hmp_handle_error(mon, err);
}

void hmp_ringbuf_read(Monitor *mon, const QDict *qdict)
{
    uint32_t size = qdict_get_int(qdict, "size");
    const char *chardev = qdict_get_str(qdict, "device");
    char *data;
    Error *err = NULL;
    int i;

    data = qmp_ringbuf_read(chardev, size, false, 0, &err);
    if (hmp_handle_error(mon, err)) {
        return;
    }

    for (i = 0; data[i]; i++) {
        unsigned char ch = data[i];

        if (ch == '\\') {
            monitor_printf(mon, "\\\\");
        } else if ((ch < 0x20 && ch != '\n' && ch != '\t') || ch == 0x7F) {
            monitor_printf(mon, "\\u%04X", ch);
        } else {
            monitor_printf(mon, "%c", ch);
        }

    }
    monitor_printf(mon, "\n");
    g_free(data);
}

void hmp_chardev_add(Monitor *mon, const QDict *qdict)
{
    const char *args = qdict_get_str(qdict, "args");
    Error *err = NULL;
    QemuOpts *opts;

    opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"), args, true);
    if (opts == NULL) {
        error_setg(&err, "Parsing chardev args failed");
    } else {
        qemu_chr_new_from_opts(opts, NULL, &err);
        qemu_opts_del(opts);
    }
    hmp_handle_error(mon, err);
}

void hmp_chardev_change(Monitor *mon, const QDict *qdict)
{
    const char *args = qdict_get_str(qdict, "args");
    const char *id;
    Error *err = NULL;
    ChardevBackend *backend = NULL;
    ChardevReturn *ret = NULL;
    QemuOpts *opts = qemu_opts_parse_noisily(qemu_find_opts("chardev"), args,
                                             true);
    if (!opts) {
        error_setg(&err, "Parsing chardev args failed");
        goto end;
    }

    id = qdict_get_str(qdict, "id");
    if (qemu_opts_id(opts)) {
        error_setg(&err, "Unexpected 'id' parameter");
        goto end;
    }

    backend = qemu_chr_parse_opts(opts, &err);
    if (!backend) {
        goto end;
    }

    ret = qmp_chardev_change(id, backend, &err);

end:
    qapi_free_ChardevReturn(ret);
    qapi_free_ChardevBackend(backend);
    qemu_opts_del(opts);
    hmp_handle_error(mon, err);
}

void hmp_chardev_remove(Monitor *mon, const QDict *qdict)
{
    Error *local_err = NULL;

    qmp_chardev_remove(qdict_get_str(qdict, "id"), &local_err);
    hmp_handle_error(mon, local_err);
}

void hmp_chardev_send_break(Monitor *mon, const QDict *qdict)
{
    Error *local_err = NULL;

    qmp_chardev_send_break(qdict_get_str(qdict, "id"), &local_err);
    hmp_handle_error(mon, local_err);
}

void chardev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevBackendInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev_backends(NULL);
    while (list) {
        const char *chr_name = list->value->name;

        if (!strncmp(chr_name, str, len)) {
            readline_add_completion(rs, chr_name);
        }
        list = list->next;
    }
    qapi_free_ChardevBackendInfoList(start);
}

void chardev_remove_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr = list->value;

        if (!strncmp(chr->label, str, len)) {
            readline_add_completion(rs, chr->label);
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

static void ringbuf_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr_info = list->value;

        if (!strncmp(chr_info->label, str, len)) {
            Chardev *chr = qemu_chr_find(chr_info->label);
            if (chr && CHARDEV_IS_RINGBUF(chr)) {
                readline_add_completion(rs, chr_info->label);
            }
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

void ringbuf_write_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args != 2) {
        return;
    }
    ringbuf_completion(rs, str);
}
