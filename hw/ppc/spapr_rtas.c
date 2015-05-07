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
#include "cpu.h"
#include "sysemu/sysemu.h"
#include "sysemu/char.h"
#include "hw/qdev.h"
#include "sysemu/device_tree.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "qapi-event.h"

#include <libfdt.h>
#include "hw/ppc/spapr_drc.h"

/* #define DEBUG_SPAPR */

#ifdef DEBUG_SPAPR
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif


static void rtas_display_character(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                                   uint32_t token, uint32_t nargs,
                                   target_ulong args,
                                   uint32_t nret, target_ulong rets)
{
    uint8_t c = rtas_ld(args, 0);
    VIOsPAPRDevice *sdev = vty_lookup(spapr, 0);

    if (!sdev) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
    } else {
        vty_putchars(sdev, &c, sizeof(c));
        rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    }
}

static void rtas_power_off(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                           uint32_t token, uint32_t nargs, target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    if (nargs != 2 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    qemu_system_shutdown_request();
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_system_reboot(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                               uint32_t token, uint32_t nargs,
                               target_ulong args,
                               uint32_t nret, target_ulong rets)
{
    if (nargs != 0 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    qemu_system_reset_request();
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_query_cpu_stopped_state(PowerPCCPU *cpu_,
                                         sPAPREnvironment *spapr,
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
    cpu = ppc_get_vcpu_by_dt_id(id);
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

static void rtas_start_cpu(PowerPCCPU *cpu_, sPAPREnvironment *spapr,
                           uint32_t token, uint32_t nargs,
                           target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    target_ulong id, start, r3;
    PowerPCCPU *cpu;

    if (nargs != 3 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    id = rtas_ld(args, 0);
    start = rtas_ld(args, 1);
    r3 = rtas_ld(args, 2);

    cpu = ppc_get_vcpu_by_dt_id(id);
    if (cpu != NULL) {
        CPUState *cs = CPU(cpu);
        CPUPPCState *env = &cpu->env;

        if (!cs->halted) {
            rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
            return;
        }

        /* This will make sure qemu state is up to date with kvm, and
         * mark it dirty so our changes get flushed back before the
         * new cpu enters */
        kvm_cpu_synchronize_state(cs);

        env->msr = (1ULL << MSR_SF) | (1ULL << MSR_ME);
        env->nip = start;
        env->gpr[3] = r3;
        cs->halted = 0;

        qemu_cpu_kick(cs);

        rtas_st(rets, 0, RTAS_OUT_SUCCESS);
        return;
    }

    /* Didn't find a matching cpu */
    rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
}

static void rtas_stop_self(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                           uint32_t token, uint32_t nargs,
                           target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cs->halted = 1;
    cpu_exit(cs);
    /*
     * While stopping a CPU, the guest calls H_CPPR which
     * effectively disables interrupts on XICS level.
     * However decrementer interrupts in TCG can still
     * wake the CPU up so here we disable interrupts in MSR
     * as well.
     * As rtas_start_cpu() resets the whole MSR anyway, there is
     * no need to bother with specific bits, we just clear it.
     */
    env->msr = 0;
}

static void rtas_ibm_get_system_parameter(PowerPCCPU *cpu,
                                          sPAPREnvironment *spapr,
                                          uint32_t token, uint32_t nargs,
                                          target_ulong args,
                                          uint32_t nret, target_ulong rets)
{
    target_ulong parameter = rtas_ld(args, 0);
    target_ulong buffer = rtas_ld(args, 1);
    target_ulong length = rtas_ld(args, 2);
    target_ulong ret = RTAS_OUT_SUCCESS;

    switch (parameter) {
    case RTAS_SYSPARM_SPLPAR_CHARACTERISTICS: {
        char *param_val = g_strdup_printf("MaxEntCap=%d,MaxPlatProcs=%d",
                                          max_cpus, smp_cpus);
        rtas_st_buffer(buffer, length, (uint8_t *)param_val, strlen(param_val));
        g_free(param_val);
        break;
    }
    case RTAS_SYSPARM_DIAGNOSTICS_RUN_MODE: {
        uint8_t param_val = DIAGNOSTICS_RUN_MODE_DISABLED;

        rtas_st_buffer(buffer, length, &param_val, sizeof(param_val));
        break;
    }
    case RTAS_SYSPARM_UUID:
        rtas_st_buffer(buffer, length, qemu_uuid, (qemu_uuid_set ? 16 : 0));
        break;
    default:
        ret = RTAS_OUT_NOT_SUPPORTED;
    }

    rtas_st(rets, 0, ret);
}

static void rtas_ibm_set_system_parameter(PowerPCCPU *cpu,
                                          sPAPREnvironment *spapr,
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
                            sPAPREnvironment *spapr,
                            uint32_t token, uint32_t nargs,
                            target_ulong args,
                            uint32_t nret, target_ulong rets)
{
    target_ulong ret = 0;

    qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_PAUSE, &error_abort);

    rtas_st(rets, 0, ret);
}

static void rtas_set_power_level(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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

static void rtas_get_power_level(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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

static bool sensor_type_is_dr(uint32_t sensor_type)
{
    switch (sensor_type) {
    case RTAS_SENSOR_TYPE_ISOLATION_STATE:
    case RTAS_SENSOR_TYPE_DR:
    case RTAS_SENSOR_TYPE_ALLOCATION_STATE:
        return true;
    }

    return false;
}

static void rtas_set_indicator(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                               uint32_t token, uint32_t nargs,
                               target_ulong args, uint32_t nret,
                               target_ulong rets)
{
    uint32_t sensor_type;
    uint32_t sensor_index;
    uint32_t sensor_state;
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;

    if (nargs != 3 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    sensor_type = rtas_ld(args, 0);
    sensor_index = rtas_ld(args, 1);
    sensor_state = rtas_ld(args, 2);

    if (!sensor_type_is_dr(sensor_type)) {
        goto out_unimplemented;
    }

    /* if this is a DR sensor we can assume sensor_index == drc_index */
    drc = spapr_dr_connector_by_index(sensor_index);
    if (!drc) {
        DPRINTF("rtas_set_indicator: invalid sensor/DRC index: %xh\n",
                sensor_index);
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    switch (sensor_type) {
    case RTAS_SENSOR_TYPE_ISOLATION_STATE:
        drck->set_isolation_state(drc, sensor_state);
        break;
    case RTAS_SENSOR_TYPE_DR:
        drck->set_indicator_state(drc, sensor_state);
        break;
    case RTAS_SENSOR_TYPE_ALLOCATION_STATE:
        drck->set_allocation_state(drc, sensor_state);
        break;
    default:
        goto out_unimplemented;
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    return;

out_unimplemented:
    /* currently only DR-related sensors are implemented */
    DPRINTF("rtas_set_indicator: sensor/indicator not implemented: %d\n",
            sensor_type);
    rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
}

static void rtas_get_sensor_state(PowerPCCPU *cpu, sPAPREnvironment *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args, uint32_t nret,
                                  target_ulong rets)
{
    uint32_t sensor_type;
    uint32_t sensor_index;
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;
    uint32_t entity_sense;

    if (nargs != 2 || nret != 2) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    sensor_type = rtas_ld(args, 0);
    sensor_index = rtas_ld(args, 1);

    if (sensor_type != RTAS_SENSOR_TYPE_ENTITY_SENSE) {
        /* currently only DR-related sensors are implemented */
        DPRINTF("rtas_get_sensor_state: sensor/indicator not implemented: %d\n",
                sensor_type);
        rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
        return;
    }

    drc = spapr_dr_connector_by_index(sensor_index);
    if (!drc) {
        DPRINTF("rtas_get_sensor_state: invalid sensor/DRC index: %xh\n",
                sensor_index);
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    entity_sense = drck->entity_sense(drc);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    rtas_st(rets, 1, entity_sense);
}

static struct rtas_call {
    const char *name;
    spapr_rtas_fn fn;
} rtas_table[RTAS_TOKEN_MAX - RTAS_TOKEN_BASE];

target_ulong spapr_rtas_call(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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

void spapr_rtas_register(int token, const char *name, spapr_rtas_fn fn)
{
    if (!((token >= RTAS_TOKEN_BASE) && (token < RTAS_TOKEN_MAX))) {
        fprintf(stderr, "RTAS invalid token 0x%x\n", token);
        exit(1);
    }

    token -= RTAS_TOKEN_BASE;
    if (rtas_table[token].name) {
        fprintf(stderr, "RTAS call \"%s\" is registered already as 0x%x\n",
                rtas_table[token].name, token);
        exit(1);
    }

    rtas_table[token].name = name;
    rtas_table[token].fn = fn;
}

int spapr_rtas_device_tree_setup(void *fdt, hwaddr rtas_addr,
                                 hwaddr rtas_size)
{
    int ret;
    int i;

    ret = fdt_add_mem_rsv(fdt, rtas_addr, rtas_size);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add RTAS reserve entry: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    ret = qemu_fdt_setprop_cell(fdt, "/rtas", "linux,rtas-base",
                                rtas_addr);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add linux,rtas-base property: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    ret = qemu_fdt_setprop_cell(fdt, "/rtas", "linux,rtas-entry",
                                rtas_addr);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add linux,rtas-entry property: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    ret = qemu_fdt_setprop_cell(fdt, "/rtas", "rtas-size",
                                rtas_size);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add rtas-size property: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    for (i = 0; i < RTAS_TOKEN_MAX - RTAS_TOKEN_BASE; i++) {
        struct rtas_call *call = &rtas_table[i];

        if (!call->name) {
            continue;
        }

        ret = qemu_fdt_setprop_cell(fdt, "/rtas", call->name,
                                    i + RTAS_TOKEN_BASE);
        if (ret < 0) {
            fprintf(stderr, "Couldn't add rtas token for %s: %s\n",
                    call->name, fdt_strerror(ret));
            return ret;
        }

    }
    return 0;
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
    spapr_rtas_register(RTAS_SET_INDICATOR, "set-indicator",
                        rtas_set_indicator);
    spapr_rtas_register(RTAS_GET_SENSOR_STATE, "get-sensor-state",
                        rtas_get_sensor_state);
}

type_init(core_rtas_register_types)
