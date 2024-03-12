/*
 * QEMU eBPF binary declaration routine.
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 *  Andrew Melnychenko <andrew@daynix.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ebpf.h"
#include "ebpf/ebpf.h"

typedef struct ElfBinaryDataEntry {
    int id;
    const void *data;
    size_t datalen;

    QSLIST_ENTRY(ElfBinaryDataEntry) node;
} ElfBinaryDataEntry;

static QSLIST_HEAD(, ElfBinaryDataEntry) ebpf_elf_obj_list =
                                            QSLIST_HEAD_INITIALIZER();

void ebpf_register_binary_data(int id, const void *data, size_t datalen)
{
    struct ElfBinaryDataEntry *dataentry = NULL;

    dataentry = g_new0(struct ElfBinaryDataEntry, 1);
    dataentry->data = data;
    dataentry->datalen = datalen;
    dataentry->id = id;

    QSLIST_INSERT_HEAD(&ebpf_elf_obj_list, dataentry, node);
}

const void *ebpf_find_binary_by_id(int id, size_t *sz, Error **errp)
{
    struct ElfBinaryDataEntry *it = NULL;
    QSLIST_FOREACH(it, &ebpf_elf_obj_list, node) {
        if (id == it->id) {
            *sz = it->datalen;
            return it->data;
        }
    }

    error_setg(errp, "can't find eBPF object with id: %d", id);

    return NULL;
}

EbpfObject *qmp_request_ebpf(EbpfProgramID id, Error **errp)
{
    EbpfObject *ret = NULL;
    size_t size = 0;
    const void *data = ebpf_find_binary_by_id(id, &size, errp);
    if (!data) {
        return NULL;
    }

    ret = g_new0(EbpfObject, 1);
    ret->object = g_base64_encode(data, size);

    return ret;
}
