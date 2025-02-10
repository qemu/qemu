/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * Hypercall based emulated RTAS
 *
 * Copyright (c) 2010-2011 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "system/system.h"
#include "system/device_tree.h"
#include "system/cpus.h"
#include "system/hw_accel.h"
#include "system/runstate.h"
#include "system/qtest.h"
#include "kvm_ppc.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/ppc.h"

#include <libfdt.h>
#include "hw/ppc/spapr_drc.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "hw/ppc/fdt.h"
#include "target/ppc/mmu-hash64.h"
#include "target/ppc/mmu-book3s-v3.h"
#include "migration/blocker.h"
#include "helper_regs.h"

static void rtas_display_character(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                   uint32_t token, uint32_t nargs,
                                   target_ulong args,
                                   uint32_t nret, target_ulong rets)
{
    uint8_t c = rtas_ld(args, 0);
    SpaprVioDevice *sdev = vty_lookup(spapr, 0);

    if (!sdev) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
    } else {
        vty_putchars(sdev, &c, sizeof(c));
        rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    }
}

static void rtas_power_off(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           uint32_t token, uint32_t nargs, target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    if (nargs != 2 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    cpu_stop_current();
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_system_reboot(PowerPCCPU *cpu, SpaprMachineState *spapr,
                               uint32_t token, uint32_t nargs,
                               target_ulong args,
                               uint32_t nret, target_ulong rets)
{
    if (nargs != 0 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_query_cpu_stopped_state(PowerPCCPU *cpu_,
                                         SpaprMachineState *spapr,
                                         uint32_t token, uint32_t nargs,
                                         target_ulong args,
                                         uint32_t nret, target_ulong rets)
{
    target_ulong id;
    PowerPCCPU *cpu;

    if (nargs != 1 || nret != 2) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    id = rtas_ld(args, 0);
    cpu = spapr_find_cpu(id);
    if (cpu != NULL) {
        if (CPU(cpu)->halted) {
            rtas_st(rets, 1, 0);
        } else {
            rtas_st(rets, 1, 2);
        }

        rtas_st(rets, 0, RTAS_OUT_SUCCESS);
        return;
    }

    /* Didn't find a matching cpu */
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

static void rtas_start_cpu(PowerPCCPU *callcpu, SpaprMachineState *spapr,
                           uint32_t token, uint32_t nargs,
                           target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    target_ulong id, start, r3;
    PowerPCCPU *newcpu;
    CPUPPCState *env;
    target_ulong lpcr;
    target_ulong caller_lpcr;

    if (nargs != 3 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    id = rtas_ld(args, 0);
    start = rtas_ld(args, 1);
    r3 = rtas_ld(args, 2);

    newcpu = spapr_find_cpu(id);
    if (!newcpu) {
        /* Didn't find a matching cpu */
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    env = &newcpu->env;

    if (!CPU(newcpu)->halted) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    cpu_synchronize_state(CPU(newcpu));

    env->msr = (1ULL << MSR_SF) | (1ULL << MSR_ME);
    hreg_compute_hflags(env);

    caller_lpcr = callcpu->env.spr[SPR_LPCR];
    lpcr = env->spr[SPR_LPCR];

    /* Set ILE the same way */
    lpcr = (lpcr & ~LPCR_ILE) | (caller_lpcr & LPCR_ILE);

    /* Set AIL the same way */
    lpcr = (lpcr & ~LPCR_AIL) | (caller_lpcr & LPCR_AIL);

    if (env->mmu_model == POWERPC_MMU_3_00) {
        /*
         * New cpus are expected to start in the same radix/hash mode
         * as the existing CPUs
         */
        if (ppc64_v3_radix(callcpu)) {
            lpcr |= LPCR_UPRT | LPCR_GTSE | LPCR_HR;
        } else {
            lpcr &= ~(LPCR_UPRT | LPCR_GTSE | LPCR_HR);
        }
        env->spr[SPR_PSSCR] &= ~PSSCR_EC;
    }
    ppc_store_lpcr(newcpu, lpcr);

    /*
     * Set the timebase offset of the new CPU to that of the invoking
     * CPU.  This helps hotplugged CPU to have the correct timebase
     * offset.
     */
    newcpu->env.tb_env->tb_offset = callcpu->env.tb_env->tb_offset;

    spapr_cpu_set_entry_state(newcpu, start, 0, r3, 0);

    qemu_cpu_kick(CPU(newcpu));

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_stop_self(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           uint32_t token, uint32_t nargs,
                           target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    /* Disable Power-saving mode Exit Cause exceptions for the CPU.
     * This could deliver an interrupt on a dying CPU and crash the
     * guest.
     * For the same reason, set PSSCR_EC.
     */
    env->spr[SPR_PSSCR] |= PSSCR_EC;
    cs->halted = 1;
    ppc_store_lpcr(cpu, env->spr[SPR_LPCR] & ~pcc->lpcr_pm);
    kvmppc_set_reg_ppc_online(cpu, 0);
    qemu_cpu_kick(cs);
}

static void rtas_ibm_suspend_me(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                uint32_t token, uint32_t nargs,
                                target_ulong args,
                                uint32_t nret, target_ulong rets)
{
    CPUState *cs;

    if (nargs != 0 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    CPU_FOREACH(cs) {
        PowerPCCPU *c = POWERPC_CPU(cs);
        CPUPPCState *e = &c->env;
        if (c == cpu) {
            continue;
        }

        /* See h_join */
        if (!cs->halted || (e->msr & (1ULL << MSR_EE))) {
            rtas_st(rets, 0, H_MULTI_THREADS_ACTIVE);
            return;
        }
    }

    qemu_system_suspend_request();
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static inline int sysparm_st(target_ulong addr, target_ulong len,
                             const void *val, uint16_t vallen)
{
    hwaddr phys = ppc64_phys_to_real(addr);

    if (len < 2) {
        return RTAS_OUT_SYSPARM_PARAM_ERROR;
    }
    stw_be_phys(&address_space_memory, phys, vallen);
    cpu_physical_memory_write(phys + 2, val, MIN(len - 2, vallen));
    return RTAS_OUT_SUCCESS;
}

static void rtas_ibm_get_system_parameter(PowerPCCPU *cpu,
                                          SpaprMachineState *spapr,
                                          uint32_t token, uint32_t nargs,
                                          target_ulong args,
                                          uint32_t nret, target_ulong rets)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    MachineState *ms = MACHINE(spapr);
    target_ulong parameter = rtas_ld(args, 0);
    target_ulong buffer = rtas_ld(args, 1);
    target_ulong length = rtas_ld(args, 2);
    target_ulong ret;

    switch (parameter) {
    case RTAS_SYSPARM_SPLPAR_CHARACTERISTICS: {
        g_autofree char *param_val = g_strdup_printf("MaxEntCap=%d,"
                                                     "DesMem=%" PRIu64 ","
                                                     "DesProcs=%d,"
                                                     "MaxPlatProcs=%d",
                                                     ms->smp.max_cpus,
                                                     ms->ram_size / MiB,
                                                     ms->smp.cpus,
                                                     ms->smp.max_cpus);
        if (pcc->n_host_threads > 0) {
            /*
             * Add HostThrs property. This property is not present in PAPR but
             * is expected by some guests to communicate the number of physical
             * host threads per core on the system so that they can scale
             * information which varies based on the thread configuration.
             */
            g_autofree char *hostthr_val = g_strdup_printf(",HostThrs=%d",
                                                           pcc->n_host_threads);
            char *old = param_val;

            param_val = g_strconcat(param_val, hostthr_val, NULL);
            g_free(old);
        }
        ret = sysparm_st(buffer, length, param_val, strlen(param_val) + 1);
        break;
    }
    case RTAS_SYSPARM_DIAGNOSTICS_RUN_MODE: {
        uint8_t param_val = DIAGNOSTICS_RUN_MODE_DISABLED;

        ret = sysparm_st(buffer, length, &param_val, sizeof(param_val));
        break;
    }
    case RTAS_SYSPARM_UUID:
        ret = sysparm_st(buffer, length, (unsigned char *)&qemu_uuid,
                         (qemu_uuid_set ? 16 : 0));
        break;
    default:
        ret = RTAS_OUT_NOT_SUPPORTED;
    }

    rtas_st(rets, 0, ret);
}

static void rtas_ibm_set_system_parameter(PowerPCCPU *cpu,
                                          SpaprMachineState *spapr,
                                          uint32_t token, uint32_t nargs,
                                          target_ulong args,
                                          uint32_t nret, target_ulong rets)
{
    target_ulong parameter = rtas_ld(args, 0);
    target_ulong ret = RTAS_OUT_NOT_SUPPORTED;

    switch (parameter) {
    case RTAS_SYSPARM_SPLPAR_CHARACTERISTICS:
    case RTAS_SYSPARM_DIAGNOSTICS_RUN_MODE:
    case RTAS_SYSPARM_UUID:
        ret = RTAS_OUT_NOT_AUTHORIZED;
        break;
    }

    rtas_st(rets, 0, ret);
}

static void rtas_ibm_os_term(PowerPCCPU *cpu,
                            SpaprMachineState *spapr,
                            uint32_t token, uint32_t nargs,
                            target_ulong args,
                            uint32_t nret, target_ulong rets)
{
    target_ulong msgaddr = rtas_ld(args, 0);
    char msg[512];

    cpu_physical_memory_read(msgaddr, msg, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;

    error_report("OS terminated: %s", msg);
    qemu_system_guest_panicked(NULL);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_set_power_level(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                 uint32_t token, uint32_t nargs,
                                 target_ulong args, uint32_t nret,
                                 target_ulong rets)
{
    int32_t power_domain;

    if (nargs != 2 || nret != 2) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    /* we currently only use a single, "live insert" powerdomain for
     * hotplugged/dlpar'd resources, so the power is always live/full (100)
     */
    power_domain = rtas_ld(args, 0);
    if (power_domain != -1) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, 100);
}

static void rtas_get_power_level(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args, uint32_t nret,
                                  target_ulong rets)
{
    int32_t power_domain;

    if (nargs != 1 || nret != 2) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    /* we currently only use a single, "live insert" powerdomain for
     * hotplugged/dlpar'd resources, so the power is always live/full (100)
     */
    power_domain = rtas_ld(args, 0);
    if (power_domain != -1) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, 100);
}

static void rtas_ibm_nmi_register(PowerPCCPU *cpu,
                                  SpaprMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args,
                                  uint32_t nret, target_ulong rets)
{
    hwaddr rtas_addr;
    target_ulong sreset_addr, mce_addr;

    if (spapr_get_cap(spapr, SPAPR_CAP_FWNMI) == SPAPR_CAP_OFF) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    rtas_addr = spapr_get_rtas_addr();
    if (!rtas_addr) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    sreset_addr = rtas_ld(args, 0);
    mce_addr = rtas_ld(args, 1);

    /* PAPR requires these are in the first 32M of memory and within RMA */
    if (sreset_addr >= 32 * MiB || sreset_addr >= spapr->rma_size ||
           mce_addr >= 32 * MiB ||    mce_addr >= spapr->rma_size) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    if (kvm_enabled()) {
        if (kvmppc_set_fwnmi(cpu) < 0) {
            rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
            return;
        }
    }

    spapr->fwnmi_system_reset_addr = sreset_addr;
    spapr->fwnmi_machine_check_addr = mce_addr;

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_ibm_nmi_interlock(PowerPCCPU *cpu,
                                   SpaprMachineState *spapr,
                                   uint32_t token, uint32_t nargs,
                                   target_ulong args,
                                   uint32_t nret, target_ulong rets)
{
    if (spapr_get_cap(spapr, SPAPR_CAP_FWNMI) == SPAPR_CAP_OFF) {
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    if (spapr->fwnmi_machine_check_addr == -1) {
        qemu_log_mask(LOG_GUEST_ERROR,
"FWNMI: ibm,nmi-interlock RTAS called with FWNMI not registered.\n");

        /* NMI register not called */
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    if (spapr->fwnmi_machine_check_interlock != cpu->vcpu_id) {
        /*
         * The vCPU that hit the NMI should invoke "ibm,nmi-interlock"
         * This should be PARAM_ERROR, but Linux calls "ibm,nmi-interlock"
         * for system reset interrupts, despite them not being interlocked.
         * PowerVM silently ignores this and returns success here. Returning
         * failure causes Linux to print the error "FWNMI: nmi-interlock
         * failed: -3", although no other apparent ill effects, this is a
         * regression for the user when enabling FWNMI. So for now, match
         * PowerVM. When most Linux clients are fixed, this could be
         * changed.
         */
        rtas_st(rets, 0, RTAS_OUT_SUCCESS);
        return;
    }

    /*
     * vCPU issuing "ibm,nmi-interlock" is done with NMI handling,
     * hence unset fwnmi_machine_check_interlock.
     */
    spapr->fwnmi_machine_check_interlock = -1;
    qemu_cond_signal(&spapr->fwnmi_machine_check_interlock_cond);
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    migrate_del_blocker(&spapr->fwnmi_migration_blocker);
}

static struct rtas_call {
    const char *name;
    spapr_rtas_fn fn;
} rtas_table[RTAS_TOKEN_MAX - RTAS_TOKEN_BASE];

target_ulong spapr_rtas_call(PowerPCCPU *cpu, SpaprMachineState *spapr,
                             uint32_t token, uint32_t nargs, target_ulong args,
                             uint32_t nret, target_ulong rets)
{
    if ((token >= RTAS_TOKEN_BASE) && (token < RTAS_TOKEN_MAX)) {
        struct rtas_call *call = rtas_table + (token - RTAS_TOKEN_BASE);

        if (call->fn) {
            call->fn(cpu, spapr, token, nargs, args, nret, rets);
            return H_SUCCESS;
        }
    }

    /* HACK: Some Linux early debug code uses RTAS display-character,
     * but assumes the token value is 0xa (which it is on some real
     * machines) without looking it up in the device tree.  This
     * special case makes this work */
    if (token == 0xa) {
        rtas_display_character(cpu, spapr, 0xa, nargs, args, nret, rets);
        return H_SUCCESS;
    }

    hcall_dprintf("Unknown RTAS token 0x%x\n", token);
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
    return H_PARAMETER;
}

static uint64_t qtest_rtas_call(char *cmd, uint32_t nargs, uint64_t args,
                                uint32_t nret, uint64_t rets)
{
    int token;

    for (token = 0; token < RTAS_TOKEN_MAX - RTAS_TOKEN_BASE; token++) {
        if (strcmp(cmd, rtas_table[token].name) == 0) {
            SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
            PowerPCCPU *cpu = POWERPC_CPU(first_cpu);

            rtas_table[token].fn(cpu, spapr, token + RTAS_TOKEN_BASE,
                                 nargs, args, nret, rets);
            return H_SUCCESS;
        }
    }
    return H_PARAMETER;
}

static bool spapr_qtest_callback(CharBackend *chr, gchar **words)
{
    if (strcmp(words[0], "rtas") == 0) {
        uint64_t res, args, ret;
        unsigned long nargs, nret;
        int rc;

        rc = qemu_strtoul(words[2], NULL, 0, &nargs);
        g_assert(rc == 0);
        rc = qemu_strtou64(words[3], NULL, 0, &args);
        g_assert(rc == 0);
        rc = qemu_strtoul(words[4], NULL, 0, &nret);
        g_assert(rc == 0);
        rc = qemu_strtou64(words[5], NULL, 0, &ret);
        g_assert(rc == 0);
        res = qtest_rtas_call(words[1], nargs, args, nret, ret);

        qtest_send_prefix(chr);
        qtest_sendf(chr, "OK %"PRIu64"\n", res);

        return true;
    }

    return false;
}

void spapr_rtas_register(int token, const char *name, spapr_rtas_fn fn)
{
    assert((token >= RTAS_TOKEN_BASE) && (token < RTAS_TOKEN_MAX));

    token -= RTAS_TOKEN_BASE;

    assert(!name || !rtas_table[token].name);

    rtas_table[token].name = name;
    rtas_table[token].fn = fn;
}

void spapr_dt_rtas_tokens(void *fdt, int rtas)
{
    int i;

    for (i = 0; i < RTAS_TOKEN_MAX - RTAS_TOKEN_BASE; i++) {
        struct rtas_call *call = &rtas_table[i];

        if (!call->name) {
            continue;
        }

        _FDT(fdt_setprop_cell(fdt, rtas, call->name, i + RTAS_TOKEN_BASE));
    }
}

hwaddr spapr_get_rtas_addr(void)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    int rtas_node;
    const fdt32_t *rtas_data;
    void *fdt = spapr->fdt_blob;

    /* fetch rtas addr from fdt */
    rtas_node = fdt_path_offset(fdt, "/rtas");
    if (rtas_node < 0) {
        return 0;
    }

    rtas_data = fdt_getprop(fdt, rtas_node, "linux,rtas-base", NULL);
    if (!rtas_data) {
        return 0;
    }

    /*
     * We assume that the OS called RTAS instantiate-rtas, but some other
     * OS might call RTAS instantiate-rtas-64 instead. This fine as of now
     * as SLOF only supports 32-bit variant.
     */
    return (hwaddr)fdt32_to_cpu(*rtas_data);
}

static void core_rtas_register_types(void)
{
    spapr_rtas_register(RTAS_DISPLAY_CHARACTER, "display-character",
                        rtas_display_character);
    spapr_rtas_register(RTAS_POWER_OFF, "power-off", rtas_power_off);
    spapr_rtas_register(RTAS_SYSTEM_REBOOT, "system-reboot",
                        rtas_system_reboot);
    spapr_rtas_register(RTAS_QUERY_CPU_STOPPED_STATE, "query-cpu-stopped-state",
                        rtas_query_cpu_stopped_state);
    spapr_rtas_register(RTAS_START_CPU, "start-cpu", rtas_start_cpu);
    spapr_rtas_register(RTAS_STOP_SELF, "stop-self", rtas_stop_self);
    spapr_rtas_register(RTAS_IBM_SUSPEND_ME, "ibm,suspend-me",
                        rtas_ibm_suspend_me);
    spapr_rtas_register(RTAS_IBM_GET_SYSTEM_PARAMETER,
                        "ibm,get-system-parameter",
                        rtas_ibm_get_system_parameter);
    spapr_rtas_register(RTAS_IBM_SET_SYSTEM_PARAMETER,
                        "ibm,set-system-parameter",
                        rtas_ibm_set_system_parameter);
    spapr_rtas_register(RTAS_IBM_OS_TERM, "ibm,os-term",
                        rtas_ibm_os_term);
    spapr_rtas_register(RTAS_SET_POWER_LEVEL, "set-power-level",
                        rtas_set_power_level);
    spapr_rtas_register(RTAS_GET_POWER_LEVEL, "get-power-level",
                        rtas_get_power_level);
    spapr_rtas_register(RTAS_IBM_NMI_REGISTER, "ibm,nmi-register",
                        rtas_ibm_nmi_register);
    spapr_rtas_register(RTAS_IBM_NMI_INTERLOCK, "ibm,nmi-interlock",
                        rtas_ibm_nmi_interlock);

    qtest_set_command_cb(spapr_qtest_callback);
}

type_init(core_rtas_register_types)
