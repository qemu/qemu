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
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "elf.h"
#include "cpu.h"
#include "exec/hwaddr.h"
#include "monitor/monitor.h"
#include "sysemu/kvm.h"
#include "sysemu/dump.h"
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
    uint64_t len, l;
    size_t total = 0;

    while (size) {
        len = size;

        buf = cpu_physical_memory_map(addr, &len, false);
        if (!buf) {
            error_setg(errp, "win-dump: failed to map physical range"
                             " 0x%016" PRIx64 "-0x%016" PRIx64, addr, addr + size - 1);
            return 0;
        }

        l = qemu_write_full(fd, buf, len);
        cpu_physical_memory_unmap(buf, addr, false, len);
        if (l != len) {
            error_setg(errp, QERR_IO_ERROR);
            return 0;
        }

        addr += l;
        size -= l;
        total += l;
    }

    return total;
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

    /*
     * If BugcheckCode wasn't saved, we consider guest OS as alive.
     */

    if (!h->BugcheckCode) {
        h->BugcheckCode = LIVE_SYSTEM_DUMP;
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
    uint64_t KdDebuggerDataBlock = h->KdDebuggerDataBlock;
    bool try_fallback = true;

try_again:
    if (cpu_memory_rw_debug(first_cpu,
            KdDebuggerDataBlock + KDBG_OWNER_TAG_OFFSET64,
            (uint8_t *)&read_OwnerTag, sizeof(read_OwnerTag), 0)) {
        error_setg(errp, "win-dump: failed to read OwnerTag");
        return;
    }

    if (memcmp(read_OwnerTag, OwnerTag, sizeof(read_OwnerTag))) {
        if (try_fallback) {
            /*
             * If attempt to use original KDBG failed
             * (most likely because of its encryption),
             * we try to use KDBG obtained by guest driver.
             */

            KdDebuggerDataBlock = h->BugcheckParameter1;
            try_fallback = false;
            goto try_again;
        } else {
            error_setg(errp, "win-dump: invalid KDBG OwnerTag,"
                             " expected '%.4s', got '%.4s'",
                             OwnerTag, read_OwnerTag);
            return;
        }
    }

    h->KdDebuggerDataBlock = KdDebuggerDataBlock;
}

struct saved_context {
    WinContext ctx;
    uint64_t addr;
};

static void patch_and_save_context(WinDumpHeader64 *h,
                                   struct saved_context *saved_ctx,
                                   Error **errp)
{
    uint64_t KiProcessorBlock;
    uint16_t OffsetPrcbContext;
    CPUState *cpu;
    int i = 0;

    if (cpu_memory_rw_debug(first_cpu,
            h->KdDebuggerDataBlock + KDBG_KI_PROCESSOR_BLOCK_OFFSET64,
            (uint8_t *)&KiProcessorBlock, sizeof(KiProcessorBlock), 0)) {
        error_setg(errp, "win-dump: failed to read KiProcessorBlock");
        return;
    }

    if (cpu_memory_rw_debug(first_cpu,
            h->KdDebuggerDataBlock + KDBG_OFFSET_PRCB_CONTEXT_OFFSET64,
            (uint8_t *)&OffsetPrcbContext, sizeof(OffsetPrcbContext), 0)) {
        error_setg(errp, "win-dump: failed to read OffsetPrcbContext");
        return;
    }

    CPU_FOREACH(cpu) {
        X86CPU *x86_cpu = X86_CPU(cpu);
        CPUX86State *env = &x86_cpu->env;
        uint64_t Prcb;
        uint64_t Context;
        WinContext ctx;

        if (cpu_memory_rw_debug(first_cpu,
                KiProcessorBlock + i * sizeof(uint64_t),
                (uint8_t *)&Prcb, sizeof(Prcb), 0)) {
            error_setg(errp, "win-dump: failed to read"
                             " CPU #%d PRCB location", i);
            return;
        }

        if (cpu_memory_rw_debug(first_cpu,
                Prcb + OffsetPrcbContext,
                (uint8_t *)&Context, sizeof(Context), 0)) {
            error_setg(errp, "win-dump: failed to read"
                             " CPU #%d ContextFrame location", i);
            return;
        }

        saved_ctx[i].addr = Context;

        ctx = (WinContext){
            .ContextFlags = WIN_CTX_ALL,
            .MxCsr = env->mxcsr,

            .SegEs = env->segs[0].selector,
            .SegCs = env->segs[1].selector,
            .SegSs = env->segs[2].selector,
            .SegDs = env->segs[3].selector,
            .SegFs = env->segs[4].selector,
            .SegGs = env->segs[5].selector,
            .EFlags = cpu_compute_eflags(env),

            .Dr0 = env->dr[0],
            .Dr1 = env->dr[1],
            .Dr2 = env->dr[2],
            .Dr3 = env->dr[3],
            .Dr6 = env->dr[6],
            .Dr7 = env->dr[7],

            .Rax = env->regs[R_EAX],
            .Rbx = env->regs[R_EBX],
            .Rcx = env->regs[R_ECX],
            .Rdx = env->regs[R_EDX],
            .Rsp = env->regs[R_ESP],
            .Rbp = env->regs[R_EBP],
            .Rsi = env->regs[R_ESI],
            .Rdi = env->regs[R_EDI],
            .R8  = env->regs[8],
            .R9  = env->regs[9],
            .R10 = env->regs[10],
            .R11 = env->regs[11],
            .R12 = env->regs[12],
            .R13 = env->regs[13],
            .R14 = env->regs[14],
            .R15 = env->regs[15],

            .Rip = env->eip,
            .FltSave = {
                .MxCsr = env->mxcsr,
            },
        };

        if (cpu_memory_rw_debug(first_cpu, Context,
                (uint8_t *)&saved_ctx[i].ctx, sizeof(WinContext), 0)) {
            error_setg(errp, "win-dump: failed to save CPU #%d context", i);
            return;
        }

        if (cpu_memory_rw_debug(first_cpu, Context,
                (uint8_t *)&ctx, sizeof(WinContext), 1)) {
            error_setg(errp, "win-dump: failed to write CPU #%d context", i);
            return;
        }

        i++;
    }
}

static void restore_context(WinDumpHeader64 *h,
                            struct saved_context *saved_ctx)
{
    int i;
    Error *err = NULL;

    for (i = 0; i < h->NumberProcessors; i++) {
        if (cpu_memory_rw_debug(first_cpu, saved_ctx[i].addr,
                (uint8_t *)&saved_ctx[i].ctx, sizeof(WinContext), 1)) {
            error_setg(&err, "win-dump: failed to restore CPU #%d context", i);
            warn_report_err(err);
        }
    }
}

void create_win_dump(DumpState *s, Error **errp)
{
    WinDumpHeader64 *h = (WinDumpHeader64 *)(s->guest_note +
            VMCOREINFO_ELF_NOTE_HDR_SIZE);
    X86CPU *first_x86_cpu = X86_CPU(first_cpu);
    uint64_t saved_cr3 = first_x86_cpu->env.cr[3];
    struct saved_context *saved_ctx = NULL;
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

    /*
     * Further access to kernel structures by virtual addresses
     * should be made from system context.
     */

    first_x86_cpu->env.cr[3] = h->DirectoryTableBase;

    check_kdbg(h, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_cr3;
    }

    patch_header(h);

    saved_ctx = g_new(struct saved_context, h->NumberProcessors);

    /*
     * Always patch context because there is no way
     * to determine if the system-saved context is valid
     */

    patch_and_save_context(h, saved_ctx, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_free;
    }

    s->total_size = h->RequiredDumpSpace;

    s->written_size = qemu_write_full(s->fd, h, sizeof(*h));
    if (s->written_size != sizeof(*h)) {
        error_setg(errp, QERR_IO_ERROR);
        goto out_restore;
    }

    write_runs(s, h, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_restore;
    }

out_restore:
    restore_context(h, saved_ctx);
out_free:
    g_free(saved_ctx);
out_cr3:
    first_x86_cpu->env.cr[3] = saved_cr3;

    return;
}
