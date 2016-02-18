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
#include "cpu.h"
#include "sysemu/sysemu.h"
#include "sysemu/char.h"
#include "hw/qdev.h"
#include "sysemu/device_tree.h"
#include "sysemu/cpus.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "qapi-event.h"
#include "hw/boards.h"

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

static sPAPRConfigureConnectorState *spapr_ccs_find(sPAPRMachineState *spapr,
                                                    uint32_t drc_index)
{
    sPAPRConfigureConnectorState *ccs = NULL;

    QTAILQ_FOREACH(ccs, &spapr->ccs_list, next) {
        if (ccs->drc_index == drc_index) {
            break;
        }
    }

    return ccs;
}

static void spapr_ccs_add(sPAPRMachineState *spapr,
                          sPAPRConfigureConnectorState *ccs)
{
    g_assert(!spapr_ccs_find(spapr, ccs->drc_index));
    QTAILQ_INSERT_HEAD(&spapr->ccs_list, ccs, next);
}

static void spapr_ccs_remove(sPAPRMachineState *spapr,
                             sPAPRConfigureConnectorState *ccs)
{
    QTAILQ_REMOVE(&spapr->ccs_list, ccs, next);
    g_free(ccs);
}

void spapr_ccs_reset_hook(void *opaque)
{
    sPAPRMachineState *spapr = opaque;
    sPAPRConfigureConnectorState *ccs, *ccs_tmp;

    QTAILQ_FOREACH_SAFE(ccs, &spapr->ccs_list, next, ccs_tmp) {
        spapr_ccs_remove(spapr, ccs);
    }
}

static void rtas_display_character(PowerPCCPU *cpu, sPAPRMachineState *spapr,
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

static void rtas_power_off(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                           uint32_t token, uint32_t nargs, target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    if (nargs != 2 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    qemu_system_shutdown_request();
    cpu_stop_current();
    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_system_reboot(PowerPCCPU *cpu, sPAPRMachineState *spapr,
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
                                         sPAPRMachineState *spapr,
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

static void rtas_start_cpu(PowerPCCPU *cpu_, sPAPRMachineState *spapr,
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

static void rtas_stop_self(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                           uint32_t token, uint32_t nargs,
                           target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    cs->halted = 1;
    qemu_cpu_kick(cs);
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
                                          sPAPRMachineState *spapr,
                                          uint32_t token, uint32_t nargs,
                                          target_ulong args,
                                          uint32_t nret, target_ulong rets)
{
    target_ulong parameter = rtas_ld(args, 0);
    target_ulong buffer = rtas_ld(args, 1);
    target_ulong length = rtas_ld(args, 2);
    target_ulong ret;

    switch (parameter) {
    case RTAS_SYSPARM_SPLPAR_CHARACTERISTICS: {
        char *param_val = g_strdup_printf("MaxEntCap=%d,"
                                          "DesMem=%llu,"
                                          "DesProcs=%d,"
                                          "MaxPlatProcs=%d",
                                          max_cpus,
                                          current_machine->ram_size / M_BYTE,
                                          smp_cpus,
                                          max_cpus);
        ret = sysparm_st(buffer, length, param_val, strlen(param_val) + 1);
        g_free(param_val);
        break;
    }
    case RTAS_SYSPARM_DIAGNOSTICS_RUN_MODE: {
        uint8_t param_val = DIAGNOSTICS_RUN_MODE_DISABLED;

        ret = sysparm_st(buffer, length, &param_val, sizeof(param_val));
        break;
    }
    case RTAS_SYSPARM_UUID:
        ret = sysparm_st(buffer, length, qemu_uuid, (qemu_uuid_set ? 16 : 0));
        break;
    default:
        ret = RTAS_OUT_NOT_SUPPORTED;
    }

    rtas_st(rets, 0, ret);
}

static void rtas_ibm_set_system_parameter(PowerPCCPU *cpu,
                                          sPAPRMachineState *spapr,
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
                            sPAPRMachineState *spapr,
                            uint32_t token, uint32_t nargs,
                            target_ulong args,
                            uint32_t nret, target_ulong rets)
{
    target_ulong ret = 0;

    qapi_event_send_guest_panicked(GUEST_PANIC_ACTION_PAUSE, &error_abort);

    rtas_st(rets, 0, ret);
}

static void rtas_set_power_level(PowerPCCPU *cpu, sPAPRMachineState *spapr,
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

static void rtas_get_power_level(PowerPCCPU *cpu, sPAPRMachineState *spapr,
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

static void rtas_set_indicator(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                               uint32_t token, uint32_t nargs,
                               target_ulong args, uint32_t nret,
                               target_ulong rets)
{
    uint32_t sensor_type;
    uint32_t sensor_index;
    uint32_t sensor_state;
    uint32_t ret = RTAS_OUT_SUCCESS;
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;

    if (nargs != 3 || nret != 1) {
        ret = RTAS_OUT_PARAM_ERROR;
        goto out;
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
        ret = RTAS_OUT_PARAM_ERROR;
        goto out;
    }
    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    switch (sensor_type) {
    case RTAS_SENSOR_TYPE_ISOLATION_STATE:
        /* if the guest is configuring a device attached to this
         * DRC, we should reset the configuration state at this
         * point since it may no longer be reliable (guest released
         * device and needs to start over, or unplug occurred so
         * the FDT is no longer valid)
         */
        if (sensor_state == SPAPR_DR_ISOLATION_STATE_ISOLATED) {
            sPAPRConfigureConnectorState *ccs = spapr_ccs_find(spapr,
                                                               sensor_index);
            if (ccs) {
                spapr_ccs_remove(spapr, ccs);
            }
        }
        ret = drck->set_isolation_state(drc, sensor_state);
        break;
    case RTAS_SENSOR_TYPE_DR:
        ret = drck->set_indicator_state(drc, sensor_state);
        break;
    case RTAS_SENSOR_TYPE_ALLOCATION_STATE:
        ret = drck->set_allocation_state(drc, sensor_state);
        break;
    default:
        goto out_unimplemented;
    }

out:
    rtas_st(rets, 0, ret);
    return;

out_unimplemented:
    /* currently only DR-related sensors are implemented */
    DPRINTF("rtas_set_indicator: sensor/indicator not implemented: %d\n",
            sensor_type);
    rtas_st(rets, 0, RTAS_OUT_NOT_SUPPORTED);
}

static void rtas_get_sensor_state(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args, uint32_t nret,
                                  target_ulong rets)
{
    uint32_t sensor_type;
    uint32_t sensor_index;
    uint32_t sensor_state = 0;
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;
    uint32_t ret = RTAS_OUT_SUCCESS;

    if (nargs != 2 || nret != 2) {
        ret = RTAS_OUT_PARAM_ERROR;
        goto out;
    }

    sensor_type = rtas_ld(args, 0);
    sensor_index = rtas_ld(args, 1);

    if (sensor_type != RTAS_SENSOR_TYPE_ENTITY_SENSE) {
        /* currently only DR-related sensors are implemented */
        DPRINTF("rtas_get_sensor_state: sensor/indicator not implemented: %d\n",
                sensor_type);
        ret = RTAS_OUT_NOT_SUPPORTED;
        goto out;
    }

    drc = spapr_dr_connector_by_index(sensor_index);
    if (!drc) {
        DPRINTF("rtas_get_sensor_state: invalid sensor/DRC index: %xh\n",
                sensor_index);
        ret = RTAS_OUT_PARAM_ERROR;
        goto out;
    }
    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    ret = drck->entity_sense(drc, &sensor_state);

out:
    rtas_st(rets, 0, ret);
    rtas_st(rets, 1, sensor_state);
}

/* configure-connector work area offsets, int32_t units for field
 * indexes, bytes for field offset/len values.
 *
 * as documented by PAPR+ v2.7, 13.5.3.5
 */
#define CC_IDX_NODE_NAME_OFFSET 2
#define CC_IDX_PROP_NAME_OFFSET 2
#define CC_IDX_PROP_LEN 3
#define CC_IDX_PROP_DATA_OFFSET 4
#define CC_VAL_DATA_OFFSET ((CC_IDX_PROP_DATA_OFFSET + 1) * 4)
#define CC_WA_LEN 4096

static void configure_connector_st(target_ulong addr, target_ulong offset,
                                   const void *buf, size_t len)
{
    cpu_physical_memory_write(ppc64_phys_to_real(addr + offset),
                              buf, MIN(len, CC_WA_LEN - offset));
}

static void rtas_ibm_configure_connector(PowerPCCPU *cpu,
                                         sPAPRMachineState *spapr,
                                         uint32_t token, uint32_t nargs,
                                         target_ulong args, uint32_t nret,
                                         target_ulong rets)
{
    uint64_t wa_addr;
    uint64_t wa_offset;
    uint32_t drc_index;
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;
    sPAPRConfigureConnectorState *ccs;
    sPAPRDRCCResponse resp = SPAPR_DR_CC_RESPONSE_CONTINUE;
    int rc;
    const void *fdt;

    if (nargs != 2 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    wa_addr = ((uint64_t)rtas_ld(args, 1) << 32) | rtas_ld(args, 0);

    drc_index = rtas_ld(wa_addr, 0);
    drc = spapr_dr_connector_by_index(drc_index);
    if (!drc) {
        DPRINTF("rtas_ibm_configure_connector: invalid DRC index: %xh\n",
                drc_index);
        rc = RTAS_OUT_PARAM_ERROR;
        goto out;
    }

    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    fdt = drck->get_fdt(drc, NULL);
    if (!fdt) {
        DPRINTF("rtas_ibm_configure_connector: Missing FDT for DRC index: %xh\n",
                drc_index);
        rc = SPAPR_DR_CC_RESPONSE_NOT_CONFIGURABLE;
        goto out;
    }

    ccs = spapr_ccs_find(spapr, drc_index);
    if (!ccs) {
        ccs = g_new0(sPAPRConfigureConnectorState, 1);
        (void)drck->get_fdt(drc, &ccs->fdt_offset);
        ccs->drc_index = drc_index;
        spapr_ccs_add(spapr, ccs);
    }

    do {
        uint32_t tag;
        const char *name;
        const struct fdt_property *prop;
        int fdt_offset_next, prop_len;

        tag = fdt_next_tag(fdt, ccs->fdt_offset, &fdt_offset_next);

        switch (tag) {
        case FDT_BEGIN_NODE:
            ccs->fdt_depth++;
            name = fdt_get_name(fdt, ccs->fdt_offset, NULL);

            /* provide the name of the next OF node */
            wa_offset = CC_VAL_DATA_OFFSET;
            rtas_st(wa_addr, CC_IDX_NODE_NAME_OFFSET, wa_offset);
            configure_connector_st(wa_addr, wa_offset, name, strlen(name) + 1);
            resp = SPAPR_DR_CC_RESPONSE_NEXT_CHILD;
            break;
        case FDT_END_NODE:
            ccs->fdt_depth--;
            if (ccs->fdt_depth == 0) {
                /* done sending the device tree, don't need to track
                 * the state anymore
                 */
                drck->set_configured(drc);
                spapr_ccs_remove(spapr, ccs);
                ccs = NULL;
                resp = SPAPR_DR_CC_RESPONSE_SUCCESS;
            } else {
                resp = SPAPR_DR_CC_RESPONSE_PREV_PARENT;
            }
            break;
        case FDT_PROP:
            prop = fdt_get_property_by_offset(fdt, ccs->fdt_offset,
                                              &prop_len);
            name = fdt_string(fdt, fdt32_to_cpu(prop->nameoff));

            /* provide the name of the next OF property */
            wa_offset = CC_VAL_DATA_OFFSET;
            rtas_st(wa_addr, CC_IDX_PROP_NAME_OFFSET, wa_offset);
            configure_connector_st(wa_addr, wa_offset, name, strlen(name) + 1);

            /* provide the length and value of the OF property. data gets
             * placed immediately after NULL terminator of the OF property's
             * name string
             */
            wa_offset += strlen(name) + 1,
            rtas_st(wa_addr, CC_IDX_PROP_LEN, prop_len);
            rtas_st(wa_addr, CC_IDX_PROP_DATA_OFFSET, wa_offset);
            configure_connector_st(wa_addr, wa_offset, prop->data, prop_len);
            resp = SPAPR_DR_CC_RESPONSE_NEXT_PROPERTY;
            break;
        case FDT_END:
            resp = SPAPR_DR_CC_RESPONSE_ERROR;
        default:
            /* keep seeking for an actionable tag */
            break;
        }
        if (ccs) {
            ccs->fdt_offset = fdt_offset_next;
        }
    } while (resp == SPAPR_DR_CC_RESPONSE_CONTINUE);

    rc = resp;
out:
    rtas_st(rets, 0, rc);
}

static struct rtas_call {
    const char *name;
    spapr_rtas_fn fn;
} rtas_table[RTAS_TOKEN_MAX - RTAS_TOKEN_BASE];

target_ulong spapr_rtas_call(PowerPCCPU *cpu, sPAPRMachineState *spapr,
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
    assert((token >= RTAS_TOKEN_BASE) && (token < RTAS_TOKEN_MAX));

    token -= RTAS_TOKEN_BASE;

    assert(!rtas_table[token].name);

    rtas_table[token].name = name;
    rtas_table[token].fn = fn;
}

int spapr_rtas_device_tree_setup(void *fdt, hwaddr rtas_addr,
                                 hwaddr rtas_size)
{
    int ret;
    int i;
    uint32_t lrdr_capacity[5];
    MachineState *machine = MACHINE(qdev_get_machine());

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

    lrdr_capacity[0] = cpu_to_be32(((uint64_t)machine->maxram_size) >> 32);
    lrdr_capacity[1] = cpu_to_be32(machine->maxram_size & 0xffffffff);
    lrdr_capacity[2] = 0;
    lrdr_capacity[3] = cpu_to_be32(SPAPR_MEMORY_BLOCK_SIZE);
    lrdr_capacity[4] = cpu_to_be32(max_cpus/smp_threads);
    ret = qemu_fdt_setprop(fdt, "/rtas", "ibm,lrdr-capacity", lrdr_capacity,
                     sizeof(lrdr_capacity));
    if (ret < 0) {
        fprintf(stderr, "Couldn't add ibm,lrdr-capacity rtas property\n");
        return ret;
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
    spapr_rtas_register(RTAS_IBM_CONFIGURE_CONNECTOR, "ibm,configure-connector",
                        rtas_ibm_configure_connector);
}

type_init(core_rtas_register_types)
