/*
 * Target-specific parts of the CPU object
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"

#include "exec/target_page.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#else
#include "exec/address-spaces.h"
#endif
#include "sysemu/tcg.h"
#include "sysemu/kvm.h"
#include "sysemu/replay.h"
#include "exec/translate-all.h"
#include "exec/log.h"

uintptr_t qemu_host_page_size;
intptr_t qemu_host_page_mask;

#ifndef CONFIG_USER_ONLY
static int cpu_common_post_load(void *opaque, int version_id)
{
    CPUState *cpu = opaque;

    /* 0x01 was CPU_INTERRUPT_EXIT. This line can be removed when the
       version_id is increased. */
    cpu->interrupt_request &= ~0x01;
    tlb_flush(cpu);

    /* loadvm has just updated the content of RAM, bypassing the
     * usual mechanisms that ensure we flush TBs for writes to
     * memory we've translated code from. So we must flush all TBs,
     * which will now be stale.
     */
    tb_flush(cpu);

    return 0;
}

static int cpu_common_pre_load(void *opaque)
{
    CPUState *cpu = opaque;

    cpu->exception_index = -1;

    return 0;
}

static bool cpu_common_exception_index_needed(void *opaque)
{
    CPUState *cpu = opaque;

    return tcg_enabled() && cpu->exception_index != -1;
}

static const VMStateDescription vmstate_cpu_common_exception_index = {
    .name = "cpu_common/exception_index",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cpu_common_exception_index_needed,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(exception_index, CPUState),
        VMSTATE_END_OF_LIST()
    }
};

static bool cpu_common_crash_occurred_needed(void *opaque)
{
    CPUState *cpu = opaque;

    return cpu->crash_occurred;
}

static const VMStateDescription vmstate_cpu_common_crash_occurred = {
    .name = "cpu_common/crash_occurred",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cpu_common_crash_occurred_needed,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(crash_occurred, CPUState),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_cpu_common = {
    .name = "cpu_common",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_load = cpu_common_pre_load,
    .post_load = cpu_common_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(halted, CPUState),
        VMSTATE_UINT32(interrupt_request, CPUState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_cpu_common_exception_index,
        &vmstate_cpu_common_crash_occurred,
        NULL
    }
};
#endif

void cpu_exec_realizefn(CPUState *cpu, Error **errp)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    cpu_list_add(cpu);

#ifdef CONFIG_TCG
    /* NB: errp parameter is unused currently */
    if (tcg_enabled()) {
        tcg_exec_realizefn(cpu, errp);
    }
#endif /* CONFIG_TCG */

#ifdef CONFIG_USER_ONLY
    assert(cc->vmsd == NULL);
#else
    if (qdev_get_vmsd(DEVICE(cpu)) == NULL) {
        vmstate_register(NULL, cpu->cpu_index, &vmstate_cpu_common, cpu);
    }
    if (cc->vmsd != NULL) {
        vmstate_register(NULL, cpu->cpu_index, cc->vmsd, cpu);
    }
#endif /* CONFIG_USER_ONLY */
}

void cpu_exec_unrealizefn(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

#ifdef CONFIG_USER_ONLY
    assert(cc->vmsd == NULL);
#else
    if (cc->vmsd != NULL) {
        vmstate_unregister(NULL, cc->vmsd, cpu);
    }
    if (qdev_get_vmsd(DEVICE(cpu)) == NULL) {
        vmstate_unregister(NULL, &vmstate_cpu_common, cpu);
    }
#endif
#ifdef CONFIG_TCG
    /* NB: errp parameter is unused currently */
    if (tcg_enabled()) {
        tcg_exec_unrealizefn(cpu);
    }
#endif /* CONFIG_TCG */

    cpu_list_remove(cpu);
}

void cpu_exec_initfn(CPUState *cpu)
{
    cpu->as = NULL;
    cpu->num_ases = 0;

#ifndef CONFIG_USER_ONLY
    cpu->thread_id = qemu_get_thread_id();
    cpu->memory = get_system_memory();
    object_ref(OBJECT(cpu->memory));
#endif
}

const char *parse_cpu_option(const char *cpu_option)
{
    ObjectClass *oc;
    CPUClass *cc;
    gchar **model_pieces;
    const char *cpu_type;

    model_pieces = g_strsplit(cpu_option, ",", 2);
    if (!model_pieces[0]) {
        error_report("-cpu option cannot be empty");
        exit(1);
    }

    oc = cpu_class_by_name(CPU_RESOLVING_TYPE, model_pieces[0]);
    if (oc == NULL) {
        error_report("unable to find CPU model '%s'", model_pieces[0]);
        g_strfreev(model_pieces);
        exit(EXIT_FAILURE);
    }

    cpu_type = object_class_get_name(oc);
    cc = CPU_CLASS(oc);
    cc->parse_features(cpu_type, model_pieces[1], &error_fatal);
    g_strfreev(model_pieces);
    return cpu_type;
}

#if defined(CONFIG_USER_ONLY)
void tb_invalidate_phys_addr(target_ulong addr)
{
    mmap_lock();
    tb_invalidate_phys_page_range(addr, addr + 1);
    mmap_unlock();
}

static void breakpoint_invalidate(CPUState *cpu, target_ulong pc)
{
    tb_invalidate_phys_addr(pc);
}
#else
void tb_invalidate_phys_addr(AddressSpace *as, hwaddr addr, MemTxAttrs attrs)
{
    ram_addr_t ram_addr;
    MemoryRegion *mr;
    hwaddr l = 1;

    if (!tcg_enabled()) {
        return;
    }

    RCU_READ_LOCK_GUARD();
    mr = address_space_translate(as, addr, &addr, &l, false, attrs);
    if (!(memory_region_is_ram(mr)
          || memory_region_is_romd(mr))) {
        return;
    }
    ram_addr = memory_region_get_ram_addr(mr) + addr;
    tb_invalidate_phys_page_range(ram_addr, ram_addr + 1);
}

static void breakpoint_invalidate(CPUState *cpu, target_ulong pc)
{
    /*
     * There may not be a virtual to physical translation for the pc
     * right now, but there may exist cached TB for this pc.
     * Flush the whole TB cache to force re-translation of such TBs.
     * This is heavyweight, but we're debugging anyway.
     */
    tb_flush(cpu);
}
#endif

/* Add a breakpoint.  */
int cpu_breakpoint_insert(CPUState *cpu, vaddr pc, int flags,
                          CPUBreakpoint **breakpoint)
{
    CPUBreakpoint *bp;

    bp = g_malloc(sizeof(*bp));

    bp->pc = pc;
    bp->flags = flags;

    /* keep all GDB-injected breakpoints in front */
    if (flags & BP_GDB) {
        QTAILQ_INSERT_HEAD(&cpu->breakpoints, bp, entry);
    } else {
        QTAILQ_INSERT_TAIL(&cpu->breakpoints, bp, entry);
    }

    breakpoint_invalidate(cpu, pc);

    if (breakpoint) {
        *breakpoint = bp;
    }
    return 0;
}

/* Remove a specific breakpoint.  */
int cpu_breakpoint_remove(CPUState *cpu, vaddr pc, int flags)
{
    CPUBreakpoint *bp;

    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        if (bp->pc == pc && bp->flags == flags) {
            cpu_breakpoint_remove_by_ref(cpu, bp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific breakpoint by reference.  */
void cpu_breakpoint_remove_by_ref(CPUState *cpu, CPUBreakpoint *breakpoint)
{
    QTAILQ_REMOVE(&cpu->breakpoints, breakpoint, entry);

    breakpoint_invalidate(cpu, breakpoint->pc);

    g_free(breakpoint);
}

/* Remove all matching breakpoints. */
void cpu_breakpoint_remove_all(CPUState *cpu, int mask)
{
    CPUBreakpoint *bp, *next;

    QTAILQ_FOREACH_SAFE(bp, &cpu->breakpoints, entry, next) {
        if (bp->flags & mask) {
            cpu_breakpoint_remove_by_ref(cpu, bp);
        }
    }
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *cpu, int enabled)
{
    if (cpu->singlestep_enabled != enabled) {
        cpu->singlestep_enabled = enabled;
        if (kvm_enabled()) {
            kvm_update_guest_debug(cpu, 0);
        } else {
            /* must flush all the translated code to avoid inconsistencies */
            /* XXX: only flush what is necessary */
            tb_flush(cpu);
        }
    }
}

void cpu_abort(CPUState *cpu, const char *fmt, ...)
{
    va_list ap;
    va_list ap2;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    fprintf(stderr, "qemu: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    cpu_dump_state(cpu, stderr, CPU_DUMP_FPU | CPU_DUMP_CCOP);
    if (qemu_log_separate()) {
        FILE *logfile = qemu_log_lock();
        qemu_log("qemu: fatal: ");
        qemu_log_vprintf(fmt, ap2);
        qemu_log("\n");
        log_cpu_state(cpu, CPU_DUMP_FPU | CPU_DUMP_CCOP);
        qemu_log_flush();
        qemu_log_unlock(logfile);
        qemu_log_close();
    }
    va_end(ap2);
    va_end(ap);
    replay_finish();
#if defined(CONFIG_USER_ONLY)
    {
        struct sigaction act;
        sigfillset(&act.sa_mask);
        act.sa_handler = SIG_DFL;
        act.sa_flags = 0;
        sigaction(SIGABRT, &act, NULL);
    }
#endif
    abort();
}

/* physical memory access (slow version, mainly for debug) */
#if defined(CONFIG_USER_ONLY)
int cpu_memory_rw_debug(CPUState *cpu, target_ulong addr,
                        void *ptr, target_ulong len, bool is_write)
{
    int flags;
    target_ulong l, page;
    void * p;
    uint8_t *buf = ptr;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID))
            return -1;
        if (is_write) {
            if (!(flags & PAGE_WRITE))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_WRITE, addr, l, 0)))
                return -1;
            memcpy(p, buf, l);
            unlock_user(p, addr, l);
        } else {
            if (!(flags & PAGE_READ))
                return -1;
            /* XXX: this code should not depend on lock_user */
            if (!(p = lock_user(VERIFY_READ, addr, l, 1)))
                return -1;
            memcpy(buf, p, l);
            unlock_user(p, addr, 0);
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}
#endif

bool target_words_bigendian(void)
{
#if defined(TARGET_WORDS_BIGENDIAN)
    return true;
#else
    return false;
#endif
}

void page_size_init(void)
{
    /* NOTE: we can always suppose that qemu_host_page_size >=
       TARGET_PAGE_SIZE */
    if (qemu_host_page_size == 0) {
        qemu_host_page_size = qemu_real_host_page_size;
    }
    if (qemu_host_page_size < TARGET_PAGE_SIZE) {
        qemu_host_page_size = TARGET_PAGE_SIZE;
    }
    qemu_host_page_mask = -(intptr_t)qemu_host_page_size;
}
