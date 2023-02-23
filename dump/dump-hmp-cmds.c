/*
 * Windows crashdump (Human Monitor Interface commands)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-dump.h"
#include "qapi/qmp/qdict.h"

void hmp_dump_guest_memory(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    bool win_dmp = qdict_get_try_bool(qdict, "windmp", false);
    bool paging = qdict_get_try_bool(qdict, "paging", false);
    bool zlib = qdict_get_try_bool(qdict, "zlib", false);
    bool lzo = qdict_get_try_bool(qdict, "lzo", false);
    bool snappy = qdict_get_try_bool(qdict, "snappy", false);
    const char *file = qdict_get_str(qdict, "filename");
    bool has_begin = qdict_haskey(qdict, "begin");
    bool has_length = qdict_haskey(qdict, "length");
    bool has_detach = qdict_haskey(qdict, "detach");
    int64_t begin = 0;
    int64_t length = 0;
    bool detach = false;
    enum DumpGuestMemoryFormat dump_format = DUMP_GUEST_MEMORY_FORMAT_ELF;
    char *prot;

    if (zlib + lzo + snappy + win_dmp > 1) {
        error_setg(&err, "only one of '-z|-l|-s|-w' can be set");
        hmp_handle_error(mon, err);
        return;
    }

    if (win_dmp) {
        dump_format = DUMP_GUEST_MEMORY_FORMAT_WIN_DMP;
    }

    if (zlib) {
        dump_format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_ZLIB;
    }

    if (lzo) {
        dump_format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO;
    }

    if (snappy) {
        dump_format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY;
    }

    if (has_begin) {
        begin = qdict_get_int(qdict, "begin");
    }
    if (has_length) {
        length = qdict_get_int(qdict, "length");
    }
    if (has_detach) {
        detach = qdict_get_bool(qdict, "detach");
    }

    prot = g_strconcat("file:", file, NULL);

    qmp_dump_guest_memory(paging, prot, true, detach, has_begin, begin,
                          has_length, length, true, dump_format, &err);
    hmp_handle_error(mon, err);
    g_free(prot);
}

void hmp_info_dump(Monitor *mon, const QDict *qdict)
{
    DumpQueryResult *result = qmp_query_dump(NULL);

    assert(result && result->status < DUMP_STATUS__MAX);
    monitor_printf(mon, "Status: %s\n", DumpStatus_str(result->status));

    if (result->status == DUMP_STATUS_ACTIVE) {
        float percent = 0;
        assert(result->total != 0);
        percent = 100.0 * result->completed / result->total;
        monitor_printf(mon, "Finished: %.2f %%\n", percent);
    }

    qapi_free_DumpQueryResult(result);
}
