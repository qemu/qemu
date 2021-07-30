/*
 * QEMU S/390 CPU - System Emulation-only code
 *
 * Copyright (c) 2009 Ulrich Hecht
 * Copyright (c) 2011 Alexander Graf
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * Copyright (c) 2012 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "kvm/kvm_s390x.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"
#include "qemu/timer.h"
#include "trace.h"
#include "qapi/qapi-visit-run-state.h"
#include "sysemu/hw_accel.h"

#include "hw/s390x/pv.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "hw/core/sysemu-cpu-ops.h"

/* S390CPUClass::load_normal() */
static void s390_cpu_load_normal(CPUState *s)
{
    S390CPU *cpu = S390_CPU(s);
    uint64_t spsw;

    if (!s390_is_pv()) {
        spsw = ldq_phys(s->as, 0);
        cpu->env.psw.mask = spsw & PSW_MASK_SHORT_CTRL;
        /*
         * Invert short psw indication, so SIE will report a specification
         * exception if it was not set.
         */
        cpu->env.psw.mask ^= PSW_MASK_SHORTPSW;
        cpu->env.psw.addr = spsw & PSW_MASK_SHORT_ADDR;
    } else {
        /*
         * Firmware requires us to set the load state before we set
         * the cpu to operating on protected guests.
         */
        s390_cpu_set_state(S390_CPU_STATE_LOAD, cpu);
    }
    s390_cpu_set_state(S390_CPU_STATE_OPERATING, cpu);
}

void s390_cpu_machine_reset_cb(void *opaque)
{
    S390CPU *cpu = opaque;

    run_on_cpu(CPU(cpu), s390_do_cpu_full_reset, RUN_ON_CPU_NULL);
}

static GuestPanicInformation *s390_cpu_get_crash_info(CPUState *cs)
{
    GuestPanicInformation *panic_info;
    S390CPU *cpu = S390_CPU(cs);

    cpu_synchronize_state(cs);
    panic_info = g_malloc0(sizeof(GuestPanicInformation));

    panic_info->type = GUEST_PANIC_INFORMATION_TYPE_S390;
    panic_info->u.s390.core = cpu->env.core_id;
    panic_info->u.s390.psw_mask = cpu->env.psw.mask;
    panic_info->u.s390.psw_addr = cpu->env.psw.addr;
    panic_info->u.s390.reason = cpu->env.crash_reason;

    return panic_info;
}

static void s390_cpu_get_crash_info_qom(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    CPUState *cs = CPU(obj);
    GuestPanicInformation *panic_info;

    if (!cs->crash_occurred) {
        error_setg(errp, "No crash occurred");
        return;
    }

    panic_info = s390_cpu_get_crash_info(cs);

    visit_type_GuestPanicInformation(v, "crash-information", &panic_info,
                                     errp);
    qapi_free_GuestPanicInformation(panic_info);
}

void s390_cpu_init_sysemu(Object *obj)
{
    CPUState *cs = CPU(obj);
    S390CPU *cpu = S390_CPU(obj);

    cs->start_powered_off = true;
    object_property_add(obj, "crash-information", "GuestPanicInformation",
                        s390_cpu_get_crash_info_qom, NULL, NULL, NULL);
    cpu->env.tod_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, s390x_tod_timer, cpu);
    cpu->env.cpu_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, s390x_cpu_timer, cpu);
    s390_cpu_set_state(S390_CPU_STATE_STOPPED, cpu);
}

bool s390_cpu_realize_sysemu(DeviceState *dev, Error **errp)
{
    S390CPU *cpu = S390_CPU(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int max_cpus = ms->smp.max_cpus;

    if (cpu->env.core_id >= max_cpus) {
        error_setg(errp, "Unable to add CPU with core-id: %" PRIu32
                   ", maximum core-id: %d", cpu->env.core_id,
                   max_cpus - 1);
        return false;
    }

    if (cpu_exists(cpu->env.core_id)) {
        error_setg(errp, "Unable to add CPU with core-id: %" PRIu32
                   ", it already exists", cpu->env.core_id);
        return false;
    }

    /* sync cs->cpu_index and env->core_id. The latter is needed for TCG. */
    CPU(cpu)->cpu_index = cpu->env.core_id;
    return true;
}

void s390_cpu_finalize(Object *obj)
{
    S390CPU *cpu = S390_CPU(obj);

    timer_free(cpu->env.tod_timer);
    timer_free(cpu->env.cpu_timer);

    qemu_unregister_reset(s390_cpu_machine_reset_cb, cpu);
    g_free(cpu->irqstate);
}

static const struct SysemuCPUOps s390_sysemu_ops = {
    .get_phys_page_debug = s390_cpu_get_phys_page_debug,
    .get_crash_info = s390_cpu_get_crash_info,
    .write_elf64_note = s390_cpu_write_elf64_note,
    .legacy_vmsd = &vmstate_s390_cpu,
};

void s390_cpu_class_init_sysemu(CPUClass *cc)
{
    S390CPUClass *scc = S390_CPU_CLASS(cc);

    scc->load_normal = s390_cpu_load_normal;
    cc->sysemu_ops = &s390_sysemu_ops;
}

static bool disabled_wait(CPUState *cpu)
{
    return cpu->halted && !(S390_CPU(cpu)->env.psw.mask &
                            (PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK));
}

static unsigned s390_count_running_cpus(void)
{
    CPUState *cpu;
    int nr_running = 0;

    CPU_FOREACH(cpu) {
        uint8_t state = S390_CPU(cpu)->env.cpu_state;
        if (state == S390_CPU_STATE_OPERATING ||
            state == S390_CPU_STATE_LOAD) {
            if (!disabled_wait(cpu)) {
                nr_running++;
            }
        }
    }

    return nr_running;
}

unsigned int s390_cpu_halt(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    trace_cpu_halt(cs->cpu_index);

    if (!cs->halted) {
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
    }

    return s390_count_running_cpus();
}

void s390_cpu_unhalt(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    trace_cpu_unhalt(cs->cpu_index);

    if (cs->halted) {
        cs->halted = 0;
        cs->exception_index = -1;
    }
}

unsigned int s390_cpu_set_state(uint8_t cpu_state, S390CPU *cpu)
 {
    trace_cpu_set_state(CPU(cpu)->cpu_index, cpu_state);

    switch (cpu_state) {
    case S390_CPU_STATE_STOPPED:
    case S390_CPU_STATE_CHECK_STOP:
        /* halt the cpu for common infrastructure */
        s390_cpu_halt(cpu);
        break;
    case S390_CPU_STATE_OPERATING:
    case S390_CPU_STATE_LOAD:
        /*
         * Starting a CPU with a PSW WAIT bit set:
         * KVM: handles this internally and triggers another WAIT exit.
         * TCG: will actually try to continue to run. Don't unhalt, will
         *      be done when the CPU actually has work (an interrupt).
         */
        if (!tcg_enabled() || !(cpu->env.psw.mask & PSW_MASK_WAIT)) {
            s390_cpu_unhalt(cpu);
        }
        break;
    default:
        error_report("Requested CPU state is not a valid S390 CPU state: %u",
                     cpu_state);
        exit(1);
    }
    if (kvm_enabled() && cpu->env.cpu_state != cpu_state) {
        kvm_s390_set_cpu_state(cpu, cpu_state);
    }
    cpu->env.cpu_state = cpu_state;

    return s390_count_running_cpus();
}

int s390_set_memory_limit(uint64_t new_limit, uint64_t *hw_limit)
{
    if (kvm_enabled()) {
        return kvm_s390_set_mem_limit(new_limit, hw_limit);
    }
    return 0;
}

void s390_set_max_pagesize(uint64_t pagesize, Error **errp)
{
    if (kvm_enabled()) {
        kvm_s390_set_max_pagesize(pagesize, errp);
    }
}

void s390_cmma_reset(void)
{
    if (kvm_enabled()) {
        kvm_s390_cmma_reset();
    }
}

int s390_assign_subch_ioeventfd(EventNotifier *notifier, uint32_t sch_id,
                                int vq, bool assign)
{
    if (kvm_enabled()) {
        return kvm_s390_assign_subch_ioeventfd(notifier, sch_id, vq, assign);
    } else {
        return 0;
    }
}

void s390_crypto_reset(void)
{
    if (kvm_enabled()) {
        kvm_s390_crypto_reset();
    }
}

void s390_enable_css_support(S390CPU *cpu)
{
    if (kvm_enabled()) {
        kvm_s390_enable_css_support(cpu);
    }
}

void s390_do_cpu_set_diag318(CPUState *cs, run_on_cpu_data arg)
{
    if (kvm_enabled()) {
        kvm_s390_set_diag318(cs, arg.host_ulong);
    }
}
