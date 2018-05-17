/*
 * Windows crashdump
 *
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "elf.h"
#include "cpu.h"
#include "exec/hwaddr.h"
#include "monitor/monitor.h"
#include "sysemu/kvm.h"
#include "sysemu/dump.h"
#include "sysemu/sysemu.h"
#include "sysemu/memory_mapping.h"
#include "sysemu/cpus.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "hw/misc/vmcoreinfo.h"
#include "win_dump.h"

static size_t write_run(WinDumpPhyMemRun64 *run, int fd, Error **errp)
{
    void *buf;
    uint64_t addr = run->BasePage << TARGET_PAGE_BITS;
    uint64_t size = run->PageCount << TARGET_PAGE_BITS;
    uint64_t len = size;

    buf = cpu_physical_memory_map(addr, &len, false);
    if (!buf) {
        error_setg(errp, "win-dump: failed to map run");
        return 0;
    }
    if (len != size) {
        error_setg(errp, "win-dump: failed to map entire run");
        len = 0;
        goto out_unmap;
    }

    len = qemu_write_full(fd, buf, len);
    if (len != size) {
        error_setg(errp, QERR_IO_ERROR);
    }

out_unmap:
    cpu_physical_memory_unmap(buf, addr, false, len);

    return len;
}

static void write_runs(DumpState *s, WinDumpHeader64 *h, Error **errp)
{
    WinDumpPhyMemDesc64 *desc = &h->PhysicalMemoryBlock;
    WinDumpPhyMemRun64 *run = desc->Run;
    Error *local_err = NULL;
    int i;

    for (i = 0; i < desc->NumberOfRuns; i++) {
        s->written_size += write_run(run + i, s->fd, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

static void patch_mm_pfn_database(WinDumpHeader64 *h, Error **errp)
{
    if (cpu_memory_rw_debug(first_cpu,
            h->KdDebuggerDataBlock + KDBG_MM_PFN_DATABASE_OFFSET64,
            (uint8_t *)&h->PfnDatabase, sizeof(h->PfnDatabase), 0)) {
        error_setg(errp, "win-dump: failed to read MmPfnDatabase");
        return;
    }
}

static void patch_bugcheck_data(WinDumpHeader64 *h, Error **errp)
{
    uint64_t KiBugcheckData;

    if (cpu_memory_rw_debug(first_cpu,
            h->KdDebuggerDataBlock + KDBG_KI_BUGCHECK_DATA_OFFSET64,
            (uint8_t *)&KiBugcheckData, sizeof(KiBugcheckData), 0)) {
        error_setg(errp, "win-dump: failed to read KiBugcheckData");
        return;
    }

    if (cpu_memory_rw_debug(first_cpu,
            KiBugcheckData,
            h->BugcheckData, sizeof(h->BugcheckData), 0)) {
        error_setg(errp, "win-dump: failed to read bugcheck data");
        return;
    }
}

/*
 * This routine tries to correct mistakes in crashdump header.
 */
static void patch_header(WinDumpHeader64 *h)
{
    Error *local_err = NULL;

    h->RequiredDumpSpace = sizeof(WinDumpHeader64) +
            (h->PhysicalMemoryBlock.NumberOfPages << TARGET_PAGE_BITS);
    h->PhysicalMemoryBlock.unused = 0;
    h->unused1 = 0;

    /*
     * We assume h->DirectoryBase and current CR3 are the same when we access
     * memory by virtual address. In other words, we suppose current context
     * is system context. It is definetely true in case of BSOD.
     */

    patch_mm_pfn_database(h, &local_err);
    if (local_err) {
        warn_report_err(local_err);
        local_err = NULL;
    }
    patch_bugcheck_data(h, &local_err);
    if (local_err) {
        warn_report_err(local_err);
    }
}

static void check_header(WinDumpHeader64 *h, Error **errp)
{
    const char Signature[] = "PAGE";
    const char ValidDump[] = "DU64";

    if (memcmp(h->Signature, Signature, sizeof(h->Signature))) {
        error_setg(errp, "win-dump: invalid header, expected '%.4s',"
                         " got '%.4s'", Signature, h->Signature);
        return;
    }

    if (memcmp(h->ValidDump, ValidDump, sizeof(h->ValidDump))) {
        error_setg(errp, "win-dump: invalid header, expected '%.4s',"
                         " got '%.4s'", ValidDump, h->ValidDump);
        return;
    }
}

static void check_kdbg(WinDumpHeader64 *h, Error **errp)
{
    const char OwnerTag[] = "KDBG";
    char read_OwnerTag[4];

    if (cpu_memory_rw_debug(first_cpu,
            h->KdDebuggerDataBlock + KDBG_OWNER_TAG_OFFSET64,
            (uint8_t *)&read_OwnerTag, sizeof(read_OwnerTag), 0)) {
        error_setg(errp, "win-dump: failed to read OwnerTag");
        return;
    }

    if (memcmp(read_OwnerTag, OwnerTag, sizeof(read_OwnerTag))) {
        error_setg(errp, "win-dump: invalid KDBG OwnerTag,"
                         " expected '%.4s', got '%.4s',"
                         " KdDebuggerDataBlock seems to be encrypted",
                         OwnerTag, read_OwnerTag);
        return;
    }
}

void create_win_dump(DumpState *s, Error **errp)
{
    WinDumpHeader64 *h = (WinDumpHeader64 *)(s->guest_note +
            VMCOREINFO_ELF_NOTE_HDR_SIZE);
    Error *local_err = NULL;

    if (s->guest_note_size != sizeof(WinDumpHeader64) +
            VMCOREINFO_ELF_NOTE_HDR_SIZE) {
        error_setg(errp, "win-dump: invalid vmcoreinfo note size");
        return;
    }

    check_header(h, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    check_kdbg(h, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    patch_header(h);

    s->total_size = h->RequiredDumpSpace;

    s->written_size = qemu_write_full(s->fd, h, sizeof(*h));
    if (s->written_size != sizeof(*h)) {
        error_setg(errp, QERR_IO_ERROR);
        return;
    }

    write_runs(s, h, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}
