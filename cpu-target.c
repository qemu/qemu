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
#include "qapi/error.h"

#include "exec/target_page.h"
#include "exec/page-protection.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "migration/vmstate.h"
#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#include "user/page-protection.h"
#else
#include "hw/core/sysemu-cpu-ops.h"
#include "exec/address-spaces.h"
#include "exec/memory.h"
#endif
#include "system/cpus.h"
#include "system/tcg.h"
#include "exec/tswap.h"
#include "exec/replay-core.h"
#include "exec/cpu-common.h"
#include "exec/exec-all.h"
#include "exec/tb-flush.h"
#include "exec/translation-block.h"
#include "exec/log.h"
#include "hw/core/accel-cpu.h"
#include "trace/trace-root.h"
#include "qemu/accel.h"

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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
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
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(halted, CPUState),
        VMSTATE_UINT32(interrupt_request, CPUState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_cpu_common_exception_index,
        &vmstate_cpu_common_crash_occurred,
        NULL
    }
};
#endif

bool cpu_exec_realizefn(CPUState *cpu, Error **errp)
{
    /* cache the cpu class for the hotpath */
    cpu->cc = CPU_GET_CLASS(cpu);

    if (!accel_cpu_common_realize(cpu, errp)) {
        return false;
    }

    /* Wait until cpu initialization complete before exposing cpu. */
    cpu_list_add(cpu);

#ifdef CONFIG_USER_ONLY
    assert(qdev_get_vmsd(DEVICE(cpu)) == NULL ||
           qdev_get_vmsd(DEVICE(cpu))->unmigratable);
#else
    if (qdev_get_vmsd(DEVICE(cpu)) == NULL) {
        vmstate_register(NULL, cpu->cpu_index, &vmstate_cpu_common, cpu);
    }
    if (cpu->cc->sysemu_ops->legacy_vmsd != NULL) {
        vmstate_register(NULL, cpu->cpu_index, cpu->cc->sysemu_ops->legacy_vmsd, cpu);
    }
#endif /* CONFIG_USER_ONLY */

    return true;
}

void cpu_exec_unrealizefn(CPUState *cpu)
{
#ifndef CONFIG_USER_ONLY
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->sysemu_ops->legacy_vmsd != NULL) {
        vmstate_unregister(NULL, cc->sysemu_ops->legacy_vmsd, cpu);
    }
    if (qdev_get_vmsd(DEVICE(cpu)) == NULL) {
        vmstate_unregister(NULL, &vmstate_cpu_common, cpu);
    }
#endif

    cpu_list_remove(cpu);
    /*
     * Now that the vCPU has been removed from the RCU list, we can call
     * accel_cpu_common_unrealize, which may free fields using call_rcu.
     */
    accel_cpu_common_unrealize(cpu);
}

/*
 * This can't go in hw/core/cpu.c because that file is compiled only
 * once for both user-mode and system builds.
 */
static const Property cpu_common_props[] = {
#ifdef CONFIG_USER_ONLY
    /*
     * Create a property for the user-only object, so users can
     * adjust prctl(PR_SET_UNALIGN) from the command-line.
     * Has no effect if the target does not support the feature.
     */
    DEFINE_PROP_BOOL("prctl-unalign-sigbus", CPUState,
                     prctl_unalign_sigbus, false),
#else
    /*
     * Create a memory property for system CPU object, so users can
     * wire up its memory.  The default if no link is set up is to use
     * the system address space.
     */
    DEFINE_PROP_LINK("memory", CPUState, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
#endif
};

#ifndef CONFIG_USER_ONLY
static bool cpu_get_start_powered_off(Object *obj, Error **errp)
{
    CPUState *cpu = CPU(obj);
    return cpu->start_powered_off;
}

static void cpu_set_start_powered_off(Object *obj, bool value, Error **errp)
{
    CPUState *cpu = CPU(obj);
    cpu->start_powered_off = value;
}
#endif

void cpu_class_init_props(DeviceClass *dc)
{
#ifndef CONFIG_USER_ONLY
    ObjectClass *oc = OBJECT_CLASS(dc);

    /*
     * We can't use DEFINE_PROP_BOOL in the Property array for this
     * property, because we want this to be settable after realize.
     */
    object_class_property_add_bool(oc, "start-powered-off",
                                   cpu_get_start_powered_off,
                                   cpu_set_start_powered_off);
#endif

    device_class_set_props(dc, cpu_common_props);
}

void cpu_exec_initfn(CPUState *cpu)
{
    cpu->as = NULL;
    cpu->num_ases = 0;

#ifndef CONFIG_USER_ONLY
    cpu->memory = get_system_memory();
    object_ref(OBJECT(cpu->memory));
#endif
}

char *cpu_model_from_type(const char *typename)
{
    const char *suffix = "-" CPU_RESOLVING_TYPE;

    if (!object_class_by_name(typename)) {
        return NULL;
    }

    if (g_str_has_suffix(typename, suffix)) {
        return g_strndup(typename, strlen(typename) - strlen(suffix));
    }

    return g_strdup(typename);
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

#ifndef cpu_list
static void cpu_list_entry(gpointer data, gpointer user_data)
{
    CPUClass *cc = CPU_CLASS(OBJECT_CLASS(data));
    const char *typename = object_class_get_name(OBJECT_CLASS(data));
    g_autofree char *model = cpu_model_from_type(typename);

    if (cc->deprecation_note) {
        qemu_printf("  %s (deprecated)\n", model);
    } else {
        qemu_printf("  %s\n", model);
    }
}

static void cpu_list(void)
{
    GSList *list;

    list = object_class_get_list_sorted(TYPE_CPU, false);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, cpu_list_entry, NULL);
    g_slist_free(list);
}
#endif

void list_cpus(void)
{
    cpu_list();
}

/* enable or disable single step mode. EXCP_DEBUG is returned by the
   CPU loop after each instruction */
void cpu_single_step(CPUState *cpu, int enabled)
{
    if (cpu->singlestep_enabled != enabled) {
        cpu->singlestep_enabled = enabled;

#if !defined(CONFIG_USER_ONLY)
        const AccelOpsClass *ops = cpus_get_accel();
        if (ops->update_guest_debug) {
            ops->update_guest_debug(cpu);
        }
#endif

        trace_breakpoint_singlestep(cpu->cpu_index, enabled);
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
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "qemu: fatal: ");
            vfprintf(logfile, fmt, ap2);
            fprintf(logfile, "\n");
            cpu_dump_state(cpu, logfile, CPU_DUMP_FPU | CPU_DUMP_CCOP);
            qemu_log_unlock(logfile);
        }
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
int cpu_memory_rw_debug(CPUState *cpu, vaddr addr,
                        void *ptr, size_t len, bool is_write)
{
    int flags;
    vaddr l, page;
    void * p;
    uint8_t *buf = ptr;
    ssize_t written;
    int ret = -1;
    int fd = -1;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID)) {
            goto out_close;
        }
        if (is_write) {
            if (flags & PAGE_WRITE) {
                /* XXX: this code should not depend on lock_user */
                p = lock_user(VERIFY_WRITE, addr, l, 0);
                if (!p) {
                    goto out_close;
                }
                memcpy(p, buf, l);
                unlock_user(p, addr, l);
            } else {
                /* Bypass the host page protection using ptrace. */
                if (fd == -1) {
                    fd = open("/proc/self/mem", O_WRONLY);
                    if (fd == -1) {
                        goto out;
                    }
                }
                /*
                 * If there is a TranslationBlock and we weren't bypassing the
                 * host page protection, the memcpy() above would SEGV,
                 * ultimately leading to page_unprotect(). So invalidate the
                 * translations manually. Both invalidation and pwrite() must
                 * be under mmap_lock() in order to prevent the creation of
                 * another TranslationBlock in between.
                 */
                mmap_lock();
                tb_invalidate_phys_range(addr, addr + l - 1);
                written = pwrite(fd, buf, l,
                                 (off_t)(uintptr_t)g2h_untagged(addr));
                mmap_unlock();
                if (written != l) {
                    goto out_close;
                }
            }
        } else if (flags & PAGE_READ) {
            /* XXX: this code should not depend on lock_user */
            p = lock_user(VERIFY_READ, addr, l, 1);
            if (!p) {
                goto out_close;
            }
            memcpy(buf, p, l);
            unlock_user(p, addr, 0);
        } else {
            /* Bypass the host page protection using ptrace. */
            if (fd == -1) {
                fd = open("/proc/self/mem", O_RDONLY);
                if (fd == -1) {
                    goto out;
                }
            }
            if (pread(fd, buf, l,
                      (off_t)(uintptr_t)g2h_untagged(addr)) != l) {
                goto out_close;
            }
        }
        len -= l;
        buf += l;
        addr += l;
    }
    ret = 0;
out_close:
    if (fd != -1) {
        close(fd);
    }
out:
    return ret;
}
#endif

bool target_words_bigendian(void)
{
    return TARGET_BIG_ENDIAN;
}

const char *target_name(void)
{
    return TARGET_NAME;
}
