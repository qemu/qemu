/*
 * Windows crashdump (target specific implementations)
 *
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sysemu/dump.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qerror.h"
#include "exec/cpu-defs.h"
#include "hw/core/cpu.h"
#include "qemu/win_dump_defs.h"
#include "win_dump.h"
#include "cpu.h"

#if defined(TARGET_X86_64)

bool win_dump_available(Error **errp)
{
    return true;
}

static size_t win_dump_ptr_size(bool x64)
{
    return x64 ? sizeof(uint64_t) : sizeof(uint32_t);
}

#define _WIN_DUMP_FIELD(f) (x64 ? h->x64.f : h->x32.f)
#define WIN_DUMP_FIELD(field) _WIN_DUMP_FIELD(field)

#define _WIN_DUMP_FIELD_PTR(f) (x64 ? (void *)&h->x64.f : (void *)&h->x32.f)
#define WIN_DUMP_FIELD_PTR(field) _WIN_DUMP_FIELD_PTR(field)

#define _WIN_DUMP_FIELD_SIZE(f) (x64 ? sizeof(h->x64.f) : sizeof(h->x32.f))
#define WIN_DUMP_FIELD_SIZE(field) _WIN_DUMP_FIELD_SIZE(field)

static size_t win_dump_ctx_size(bool x64)
{
    return x64 ? sizeof(WinContext64) : sizeof(WinContext32);
}

static size_t write_run(uint64_t base_page, uint64_t page_count,
        int fd, Error **errp)
{
    void *buf;
    uint64_t addr = base_page << TARGET_PAGE_BITS;
    uint64_t size = page_count << TARGET_PAGE_BITS;
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

static void write_runs(DumpState *s, WinDumpHeader *h, bool x64, Error **errp)
{
    uint64_t BasePage, PageCount;
    Error *local_err = NULL;
    int i;

    for (i = 0; i < WIN_DUMP_FIELD(PhysicalMemoryBlock.NumberOfRuns); i++) {
        BasePage = WIN_DUMP_FIELD(PhysicalMemoryBlock.Run[i].BasePage);
        PageCount = WIN_DUMP_FIELD(PhysicalMemoryBlock.Run[i].PageCount);
        s->written_size += write_run(BasePage, PageCount, s->fd, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

static int cpu_read_ptr(bool x64, CPUState *cpu, uint64_t addr, uint64_t *ptr)
{
    int ret;
    uint32_t ptr32;
    uint64_t ptr64;

    ret = cpu_memory_rw_debug(cpu, addr, x64 ? (void *)&ptr64 : (void *)&ptr32,
            win_dump_ptr_size(x64), 0);

    *ptr = x64 ? ptr64 : ptr32;

    return ret;
}

static void patch_mm_pfn_database(WinDumpHeader *h, bool x64, Error **errp)
{
    if (cpu_memory_rw_debug(first_cpu,
            WIN_DUMP_FIELD(KdDebuggerDataBlock) + KDBG_MM_PFN_DATABASE_OFFSET,
            WIN_DUMP_FIELD_PTR(PfnDatabase),
            WIN_DUMP_FIELD_SIZE(PfnDatabase), 0)) {
        error_setg(errp, "win-dump: failed to read MmPfnDatabase");
        return;
    }
}

static void patch_bugcheck_data(WinDumpHeader *h, bool x64, Error **errp)
{
    uint64_t KiBugcheckData;

    if (cpu_read_ptr(x64, first_cpu,
            WIN_DUMP_FIELD(KdDebuggerDataBlock) + KDBG_KI_BUGCHECK_DATA_OFFSET,
            &KiBugcheckData)) {
        error_setg(errp, "win-dump: failed to read KiBugcheckData");
        return;
    }

    if (cpu_memory_rw_debug(first_cpu, KiBugcheckData,
            WIN_DUMP_FIELD(BugcheckData),
            WIN_DUMP_FIELD_SIZE(BugcheckData), 0)) {
        error_setg(errp, "win-dump: failed to read bugcheck data");
        return;
    }

    /*
     * If BugcheckCode wasn't saved, we consider guest OS as alive.
     */

    if (!WIN_DUMP_FIELD(BugcheckCode)) {
        *(uint32_t *)WIN_DUMP_FIELD_PTR(BugcheckCode) = LIVE_SYSTEM_DUMP;
    }
}

/*
 * This routine tries to correct mistakes in crashdump header.
 */
static void patch_header(WinDumpHeader *h, bool x64)
{
    Error *local_err = NULL;

    if (x64) {
        h->x64.RequiredDumpSpace = sizeof(WinDumpHeader64) +
            (h->x64.PhysicalMemoryBlock.NumberOfPages << TARGET_PAGE_BITS);
        h->x64.PhysicalMemoryBlock.unused = 0;
        h->x64.unused1 = 0;
    } else {
        h->x32.RequiredDumpSpace = sizeof(WinDumpHeader32) +
            (h->x32.PhysicalMemoryBlock.NumberOfPages << TARGET_PAGE_BITS);
    }

    patch_mm_pfn_database(h, x64, &local_err);
    if (local_err) {
        warn_report_err(local_err);
        local_err = NULL;
    }
    patch_bugcheck_data(h, x64, &local_err);
    if (local_err) {
        warn_report_err(local_err);
    }
}

static bool check_header(WinDumpHeader *h, bool *x64, Error **errp)
{
    const char Signature[] = "PAGE";

    if (memcmp(h->Signature, Signature, sizeof(h->Signature))) {
        error_setg(errp, "win-dump: invalid header, expected '%.4s',"
                         " got '%.4s'", Signature, h->Signature);
        return false;
    }

    if (!memcmp(h->ValidDump, "DUMP", sizeof(h->ValidDump))) {
        *x64 = false;
    } else if (!memcmp(h->ValidDump, "DU64", sizeof(h->ValidDump))) {
        *x64 = true;
    } else {
        error_setg(errp, "win-dump: invalid header, expected 'DUMP' or 'DU64',"
                   " got '%.4s'", h->ValidDump);
        return false;
    }

    return true;
}

static void check_kdbg(WinDumpHeader *h, bool x64, Error **errp)
{
    const char OwnerTag[] = "KDBG";
    char read_OwnerTag[4];
    uint64_t KdDebuggerDataBlock = WIN_DUMP_FIELD(KdDebuggerDataBlock);
    bool try_fallback = true;

try_again:
    if (cpu_memory_rw_debug(first_cpu,
            KdDebuggerDataBlock + KDBG_OWNER_TAG_OFFSET,
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

            KdDebuggerDataBlock = WIN_DUMP_FIELD(BugcheckParameter1);
            try_fallback = false;
            goto try_again;
        } else {
            error_setg(errp, "win-dump: invalid KDBG OwnerTag,"
                             " expected '%.4s', got '%.4s'",
                             OwnerTag, read_OwnerTag);
            return;
        }
    }

    if (x64) {
        h->x64.KdDebuggerDataBlock = KdDebuggerDataBlock;
    } else {
        h->x32.KdDebuggerDataBlock = KdDebuggerDataBlock;
    }
}

struct saved_context {
    WinContext ctx;
    uint64_t addr;
};

static void patch_and_save_context(WinDumpHeader *h, bool x64,
                                   struct saved_context *saved_ctx,
                                   Error **errp)
{
    uint64_t KdDebuggerDataBlock = WIN_DUMP_FIELD(KdDebuggerDataBlock);
    uint64_t KiProcessorBlock;
    uint16_t OffsetPrcbContext;
    CPUState *cpu;
    int i = 0;

    if (cpu_read_ptr(x64, first_cpu,
            KdDebuggerDataBlock + KDBG_KI_PROCESSOR_BLOCK_OFFSET,
            &KiProcessorBlock)) {
        error_setg(errp, "win-dump: failed to read KiProcessorBlock");
        return;
    }

    if (cpu_memory_rw_debug(first_cpu,
            KdDebuggerDataBlock + KDBG_OFFSET_PRCB_CONTEXT_OFFSET,
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

        if (i >= WIN_DUMP_FIELD(NumberProcessors)) {
            warn_report("win-dump: number of QEMU CPUs is bigger than"
                        " NumberProcessors (%u) in guest Windows",
                        WIN_DUMP_FIELD(NumberProcessors));
            return;
        }

        if (cpu_read_ptr(x64, first_cpu,
                KiProcessorBlock + i * win_dump_ptr_size(x64),
                &Prcb)) {
            error_setg(errp, "win-dump: failed to read"
                             " CPU #%d PRCB location", i);
            return;
        }

        if (cpu_read_ptr(x64, first_cpu,
                Prcb + OffsetPrcbContext,
                &Context)) {
            error_setg(errp, "win-dump: failed to read"
                             " CPU #%d ContextFrame location", i);
            return;
        }

        saved_ctx[i].addr = Context;

        if (x64) {
            ctx.x64 = (WinContext64){
                .ContextFlags = WIN_CTX64_ALL,
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
        } else {
            ctx.x32 = (WinContext32){
                .ContextFlags = WIN_CTX32_FULL | WIN_CTX_DBG,

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

                .Eax = env->regs[R_EAX],
                .Ebx = env->regs[R_EBX],
                .Ecx = env->regs[R_ECX],
                .Edx = env->regs[R_EDX],
                .Esp = env->regs[R_ESP],
                .Ebp = env->regs[R_EBP],
                .Esi = env->regs[R_ESI],
                .Edi = env->regs[R_EDI],

                .Eip = env->eip,
            };
        }

        if (cpu_memory_rw_debug(first_cpu, Context,
                &saved_ctx[i].ctx, win_dump_ctx_size(x64), 0)) {
            error_setg(errp, "win-dump: failed to save CPU #%d context", i);
            return;
        }

        if (cpu_memory_rw_debug(first_cpu, Context,
                &ctx, win_dump_ctx_size(x64), 1)) {
            error_setg(errp, "win-dump: failed to write CPU #%d context", i);
            return;
        }

        i++;
    }
}

static void restore_context(WinDumpHeader *h, bool x64,
                            struct saved_context *saved_ctx)
{
    int i;

    for (i = 0; i < WIN_DUMP_FIELD(NumberProcessors); i++) {
        if (cpu_memory_rw_debug(first_cpu, saved_ctx[i].addr,
                &saved_ctx[i].ctx, win_dump_ctx_size(x64), 1)) {
            warn_report("win-dump: failed to restore CPU #%d context", i);
        }
    }
}

void create_win_dump(DumpState *s, Error **errp)
{
    WinDumpHeader *h = (void *)(s->guest_note + VMCOREINFO_ELF_NOTE_HDR_SIZE);
    X86CPU *first_x86_cpu = X86_CPU(first_cpu);
    uint64_t saved_cr3 = first_x86_cpu->env.cr[3];
    struct saved_context *saved_ctx = NULL;
    Error *local_err = NULL;
    bool x64 = true;
    size_t hdr_size;

    if (s->guest_note_size != VMCOREINFO_WIN_DUMP_NOTE_SIZE32 &&
            s->guest_note_size != VMCOREINFO_WIN_DUMP_NOTE_SIZE64) {
        error_setg(errp, "win-dump: invalid vmcoreinfo note size");
        return;
    }

    if (!check_header(h, &x64, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }

    hdr_size = x64 ? sizeof(WinDumpHeader64) : sizeof(WinDumpHeader32);

    /*
     * Further access to kernel structures by virtual addresses
     * should be made from system context.
     */

    first_x86_cpu->env.cr[3] = WIN_DUMP_FIELD(DirectoryTableBase);

    check_kdbg(h, x64, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_cr3;
    }

    patch_header(h, x64);

    saved_ctx = g_new(struct saved_context, WIN_DUMP_FIELD(NumberProcessors));

    /*
     * Always patch context because there is no way
     * to determine if the system-saved context is valid
     */

    patch_and_save_context(h, x64, saved_ctx, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_free;
    }

    s->total_size = WIN_DUMP_FIELD(RequiredDumpSpace);

    s->written_size = qemu_write_full(s->fd, h, hdr_size);
    if (s->written_size != hdr_size) {
        error_setg(errp, QERR_IO_ERROR);
        goto out_restore;
    }

    write_runs(s, h, x64, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out_restore;
    }

out_restore:
    restore_context(h, x64, saved_ctx);
out_free:
    g_free(saved_ctx);
out_cr3:
    first_x86_cpu->env.cr[3] = saved_cr3;

    return;
}

#else /* !TARGET_X86_64 */

bool win_dump_available(Error **errp)
{
    error_setg(errp, "Windows dump is only available for x86-64");

    return false;
}

void create_win_dump(DumpState *s, Error **errp)
{
    win_dump_available(errp);
}

#endif
