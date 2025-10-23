/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * print ovmf debug log
 *
 * see OvmfPkg/Library/MemDebugLogLib/ in edk2
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/target-info-qapi.h"
#include "hw/boards.h"
#include "hw/i386/x86.h"
#include "hw/arm/virt.h"
#include "system/dma.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-machine.h"
#include "qobject/qdict.h"


/* ----------------------------------------------------------------------- */
/* copy from edk2                                                          */

#define MEM_DEBUG_LOG_MAGIC1  0x3167646d666d766f  /* "ovmfmdg1" */
#define MEM_DEBUG_LOG_MAGIC2  0x3267646d666d766f  /* "ovmfmdg2" */

/*
 * Mem Debug Log buffer header.
 * The Log buffer is circular. Only the most
 * recent messages are retained. Older messages
 * will be discarded if the buffer overflows.
 * The Debug Log starts just after the header.
 */
typedef struct {
    /*
     * Magic values
     * These fields are used by tools to locate the buffer in
     * memory. These MUST be the first two fields of the structure.
     * Use a 128 bit Magic to vastly reduce the possibility of
     * a collision with random data in memory.
     */
    uint64_t             Magic1;
    uint64_t             Magic2;
    /*
     * Header Size
     * This MUST be the third field of the structure
     */
    uint64_t             HeaderSize;
    /*
     * Debug log size (minus header)
     */
    uint64_t             DebugLogSize;
    /*
     * edk2 uses this for locking access.
     */
    uint64_t             MemDebugLogLock;
    /*
     * Debug log head offset
     */
    uint64_t             DebugLogHeadOffset;
    /*
     *  Debug log tail offset
     */
    uint64_t             DebugLogTailOffset;
    /*
     * Flag to indicate if the buffer wrapped and was thus truncated.
     */
    uint64_t             Truncated;
    /*
     * Firmware Build Version (PcdFirmwareVersionString)
     */
    char                 FirmwareVersion[128];
} MEM_DEBUG_LOG_HDR;


/* ----------------------------------------------------------------------- */
/* qemu monitor command                                                    */

typedef struct {
    uint64_t             magic1;
    uint64_t             magic2;
} MemDebugLogMagic;

/* find log buffer in guest memory by searching for the magic cookie */
static dma_addr_t find_ovmf_log_range(dma_addr_t start, dma_addr_t end)
{
    static const MemDebugLogMagic magic = {
        .magic1 = MEM_DEBUG_LOG_MAGIC1,
        .magic2 = MEM_DEBUG_LOG_MAGIC2,
    };
    MemDebugLogMagic check;
    dma_addr_t step = 4 * KiB;
    dma_addr_t offset;

    for (offset = start; offset < end; offset += step) {
        if (dma_memory_read(&address_space_memory, offset,
                            &check, sizeof(check),
                            MEMTXATTRS_UNSPECIFIED)) {
            /* dma error -> stop searching */
            break;
        }
        if (memcmp(&magic, &check, sizeof(check)) == 0) {
            return offset;
        }
    }
    return (dma_addr_t)-1;
}

static dma_addr_t find_ovmf_log(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    dma_addr_t start, end, offset;

    if (target_arch() == SYS_EMU_TARGET_X86_64 &&
        object_dynamic_cast(OBJECT(ms), TYPE_X86_MACHINE)) {
        X86MachineState *x86ms = X86_MACHINE(ms);

        /* early log buffer, static allocation in memfd, sec + early pei */
        offset = find_ovmf_log_range(0x800000, 0x900000);
        if (offset != -1) {
            return offset;
        }

        /*
         * normal log buffer, dynamically allocated close to end of low memory,
         * late pei + dxe phase
         */
        end = x86ms->below_4g_mem_size;
        start = end - MIN(end, 128 * MiB);
        return find_ovmf_log_range(start, end);
    }

    if (target_arch() == SYS_EMU_TARGET_AARCH64 &&
        object_dynamic_cast(OBJECT(ms), TYPE_VIRT_MACHINE)) {
        VirtMachineState *vms = VIRT_MACHINE(ms);

        /* edk2 ArmVirt firmware allocations are in the first 128 MB */
        start = vms->memmap[VIRT_MEM].base;
        end = start + 128 * MiB;
        return find_ovmf_log_range(start, end);
    }

    return (dma_addr_t)-1;
}

static void handle_ovmf_log_range(GString *out,
                                  dma_addr_t start,
                                  dma_addr_t end,
                                  Error **errp)
{
    if (start > end) {
        return;
    }

    size_t len = end - start;
    g_string_set_size(out, out->len + len);
    if (dma_memory_read(&address_space_memory, start,
                        out->str + (out->len - len),
                        len, MEMTXATTRS_UNSPECIFIED)) {
        error_setg(errp, "can not read firmware log buffer contents");
        return;
    }
}

FirmwareLog *qmp_query_firmware_log(bool have_max_size, uint64_t max_size,
                                    Error **errp)
{
    MEM_DEBUG_LOG_HDR header;
    dma_addr_t offset, base;
    FirmwareLog *ret;
    g_autoptr(GString) log = g_string_new("");

    offset = find_ovmf_log();
    if (offset == -1) {
        error_setg(errp, "firmware log buffer not found");
        return NULL;
    }

    if (dma_memory_read(&address_space_memory, offset,
                        &header, sizeof(header),
                        MEMTXATTRS_UNSPECIFIED)) {
        error_setg(errp, "can not read firmware log buffer header");
        return NULL;
    }

    if (header.DebugLogHeadOffset > header.DebugLogSize ||
        header.DebugLogTailOffset > header.DebugLogSize) {
        error_setg(errp, "firmware log buffer header is invalid");
        return NULL;
    }

    if (have_max_size) {
        if (max_size > MiB) {
            error_setg(errp, "parameter 'max-size' exceeds 1MiB");
            return NULL;
        }
    } else {
        max_size = MiB;
    }

    /* adjust header.DebugLogHeadOffset so we return at most maxsize bytes */
    if (header.DebugLogHeadOffset > header.DebugLogTailOffset) {
        /* wrap around */
        if (header.DebugLogTailOffset > max_size) {
            header.DebugLogHeadOffset = header.DebugLogTailOffset - max_size;
        } else {
            uint64_t max_chunk = max_size - header.DebugLogTailOffset;
            if (header.DebugLogSize > max_chunk &&
                header.DebugLogHeadOffset < header.DebugLogSize - max_chunk) {
                header.DebugLogHeadOffset = header.DebugLogSize - max_chunk;
            }
        }
    } else {
        if (header.DebugLogTailOffset > max_size &&
            header.DebugLogHeadOffset < header.DebugLogTailOffset - max_size) {
            header.DebugLogHeadOffset = header.DebugLogTailOffset - max_size;
        }
    }

    base = offset + header.HeaderSize;
    if (header.DebugLogHeadOffset > header.DebugLogTailOffset) {
        /* wrap around */
        handle_ovmf_log_range(log,
                              base + header.DebugLogHeadOffset,
                              base + header.DebugLogSize,
                              errp);
        if (*errp) {
            return NULL;
        }
        handle_ovmf_log_range(log,
                              base + 0,
                              base + header.DebugLogTailOffset,
                              errp);
        if (*errp) {
            return NULL;
        }
    } else {
        handle_ovmf_log_range(log,
                              base + header.DebugLogHeadOffset,
                              base + header.DebugLogTailOffset,
                              errp);
        if (*errp) {
            return NULL;
        }
    }

    ret = g_new0(FirmwareLog, 1);
    if (header.FirmwareVersion[0] != '\0') {
        ret->version = g_strndup(header.FirmwareVersion,
                                 sizeof(header.FirmwareVersion));
    }
    ret->log = g_base64_encode((const guchar *)log->str, log->len);
    return ret;
}

void hmp_info_firmware_log(Monitor *mon, const QDict *qdict)
{
    g_autofree gchar *log_esc = NULL;
    g_autofree guchar *log_out = NULL;
    Error *err = NULL;
    g_autoptr(FirmwareLog) log = NULL;
    gsize log_len;
    int64_t maxsize;

    maxsize = qdict_get_try_int(qdict, "max-size", -1);
    log = qmp_query_firmware_log(maxsize != -1, (uint64_t)maxsize, &err);
    if (err)  {
        hmp_handle_error(mon, err);
        return;
    }

    g_assert(log != NULL);
    g_assert(log->log != NULL);

    if (log->version) {
        g_autofree gchar *esc = g_strescape(log->version, NULL);
        monitor_printf(mon, "[ firmware version: %s ]\n", esc);
    }

    log_out = g_base64_decode(log->log, &log_len);
    log_esc = g_strescape((gchar *)log_out, "\r\n");
    monitor_printf(mon, "%s\n", log_esc);
}
