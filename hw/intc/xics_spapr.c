/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtualized Interrupt System, aka ICS/ICP aka xics
 *
 * Copyright (c) 2010,2011 David Gibson, IBM Corporation.
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
#include "hw/hw.h"
#include "trace.h"
#include "qemu/timer.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/xics_spapr.h"
#include "hw/ppc/fdt.h"
#include "qapi/visitor.h"

/*
 * Guest interfaces
 */

static bool check_emulated_xics(SpaprMachineState *spapr, const char *func)
{
    if (spapr_ovec_test(spapr->ov5_cas, OV5_XIVE_EXPLOIT) ||
        kvm_irqchip_in_kernel()) {
        error_report("pseries: %s must only be called for emulated XICS",
                     func);
        return false;
    }

    return true;
}

#define CHECK_EMULATED_XICS_HCALL(spapr)               \
    do {                                               \
        if (!check_emulated_xics((spapr), __func__)) { \
            return H_HARDWARE;                         \
        }                                              \
    } while (0)

static target_ulong h_cppr(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    target_ulong cppr = args[0];

    CHECK_EMULATED_XICS_HCALL(spapr);

    icp_set_cppr(spapr_cpu_state(cpu)->icp, cppr);
    return H_SUCCESS;
}

static target_ulong h_ipi(PowerPCCPU *cpu, SpaprMachineState *spapr,
                          target_ulong opcode, target_ulong *args)
{
    target_ulong mfrr = args[1];
    ICPState *icp = xics_icp_get(XICS_FABRIC(spapr), args[0]);

    CHECK_EMULATED_XICS_HCALL(spapr);

    if (!icp) {
        return H_PARAMETER;
    }

    icp_set_mfrr(icp, mfrr);
    return H_SUCCESS;
}

static target_ulong h_xirr(PowerPCCPU *cpu, SpaprMachineState *spapr,
                           target_ulong opcode, target_ulong *args)
{
    uint32_t xirr = icp_accept(spapr_cpu_state(cpu)->icp);

    CHECK_EMULATED_XICS_HCALL(spapr);

    args[0] = xirr;
    return H_SUCCESS;
}

static target_ulong h_xirr_x(PowerPCCPU *cpu, SpaprMachineState *spapr,
                             target_ulong opcode, target_ulong *args)
{
    uint32_t xirr = icp_accept(spapr_cpu_state(cpu)->icp);

    CHECK_EMULATED_XICS_HCALL(spapr);

    args[0] = xirr;
    args[1] = cpu_get_host_ticks();
    return H_SUCCESS;
}

static target_ulong h_eoi(PowerPCCPU *cpu, SpaprMachineState *spapr,
                          target_ulong opcode, target_ulong *args)
{
    target_ulong xirr = args[0];

    CHECK_EMULATED_XICS_HCALL(spapr);

    icp_eoi(spapr_cpu_state(cpu)->icp, xirr);
    return H_SUCCESS;
}

static target_ulong h_ipoll(PowerPCCPU *cpu, SpaprMachineState *spapr,
                            target_ulong opcode, target_ulong *args)
{
    ICPState *icp = xics_icp_get(XICS_FABRIC(spapr), args[0]);
    uint32_t mfrr;
    uint32_t xirr;

    CHECK_EMULATED_XICS_HCALL(spapr);

    if (!icp) {
        return H_PARAMETER;
    }

    xirr = icp_ipoll(icp, &mfrr);

    args[0] = xirr;
    args[1] = mfrr;

    return H_SUCCESS;
}

#define CHECK_EMULATED_XICS_RTAS(spapr, rets)          \
    do {                                               \
        if (!check_emulated_xics((spapr), __func__)) { \
            rtas_st((rets), 0, RTAS_OUT_HW_ERROR);     \
            return;                                    \
        }                                              \
    } while (0)

static void rtas_set_xive(PowerPCCPU *cpu, SpaprMachineState *spapr,
                          uint32_t token,
                          uint32_t nargs, target_ulong args,
                          uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->ics;
    uint32_t nr, srcno, server, priority;

    CHECK_EMULATED_XICS_RTAS(spapr, rets);

    if ((nargs != 3) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    if (!ics) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);
    server = rtas_ld(args, 1);
    priority = rtas_ld(args, 2);

    if (!ics_valid_irq(ics, nr) || !xics_icp_get(XICS_FABRIC(spapr), server)
        || (priority > 0xff)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    srcno = nr - ics->offset;
    ics_simple_write_xive(ics, srcno, server, priority, priority);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_get_xive(PowerPCCPU *cpu, SpaprMachineState *spapr,
                          uint32_t token,
                          uint32_t nargs, target_ulong args,
                          uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->ics;
    uint32_t nr, srcno;

    CHECK_EMULATED_XICS_RTAS(spapr, rets);

    if ((nargs != 1) || (nret != 3)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    if (!ics) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
    srcno = nr - ics->offset;
    rtas_st(rets, 1, ics->irqs[srcno].server);
    rtas_st(rets, 2, ics->irqs[srcno].priority);
}

static void rtas_int_off(PowerPCCPU *cpu, SpaprMachineState *spapr,
                         uint32_t token,
                         uint32_t nargs, target_ulong args,
                         uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->ics;
    uint32_t nr, srcno;

    CHECK_EMULATED_XICS_RTAS(spapr, rets);

    if ((nargs != 1) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    if (!ics) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    srcno = nr - ics->offset;
    ics_simple_write_xive(ics, srcno, ics->irqs[srcno].server, 0xff,
                          ics->irqs[srcno].priority);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_int_on(PowerPCCPU *cpu, SpaprMachineState *spapr,
                        uint32_t token,
                        uint32_t nargs, target_ulong args,
                        uint32_t nret, target_ulong rets)
{
    ICSState *ics = spapr->ics;
    uint32_t nr, srcno;

    CHECK_EMULATED_XICS_RTAS(spapr, rets);

    if ((nargs != 1) || (nret != 1)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    if (!ics) {
        rtas_st(rets, 0, RTAS_OUT_HW_ERROR);
        return;
    }

    nr = rtas_ld(args, 0);

    if (!ics_valid_irq(ics, nr)) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    srcno = nr - ics->offset;
    ics_simple_write_xive(ics, srcno, ics->irqs[srcno].server,
                          ics->irqs[srcno].saved_priority,
                          ics->irqs[srcno].saved_priority);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

void xics_spapr_init(SpaprMachineState *spapr)
{
    spapr_rtas_register(RTAS_IBM_SET_XIVE, "ibm,set-xive", rtas_set_xive);
    spapr_rtas_register(RTAS_IBM_GET_XIVE, "ibm,get-xive", rtas_get_xive);
    spapr_rtas_register(RTAS_IBM_INT_OFF, "ibm,int-off", rtas_int_off);
    spapr_rtas_register(RTAS_IBM_INT_ON, "ibm,int-on", rtas_int_on);

    spapr_register_hypercall(H_CPPR, h_cppr);
    spapr_register_hypercall(H_IPI, h_ipi);
    spapr_register_hypercall(H_XIRR, h_xirr);
    spapr_register_hypercall(H_XIRR_X, h_xirr_x);
    spapr_register_hypercall(H_EOI, h_eoi);
    spapr_register_hypercall(H_IPOLL, h_ipoll);
}

void spapr_dt_xics(SpaprMachineState *spapr, uint32_t nr_servers, void *fdt,
                   uint32_t phandle)
{
    uint32_t interrupt_server_ranges_prop[] = {
        0, cpu_to_be32(nr_servers),
    };
    int node;

    _FDT(node = fdt_add_subnode(fdt, 0, XICS_NODENAME));

    _FDT(fdt_setprop_string(fdt, node, "device_type",
                            "PowerPC-External-Interrupt-Presentation"));
    _FDT(fdt_setprop_string(fdt, node, "compatible", "IBM,ppc-xicp"));
    _FDT(fdt_setprop(fdt, node, "interrupt-controller", NULL, 0));
    _FDT(fdt_setprop(fdt, node, "ibm,interrupt-server-ranges",
                     interrupt_server_ranges_prop,
                     sizeof(interrupt_server_ranges_prop)));
    _FDT(fdt_setprop_cell(fdt, node, "#interrupt-cells", 2));
    _FDT(fdt_setprop_cell(fdt, node, "linux,phandle", phandle));
    _FDT(fdt_setprop_cell(fdt, node, "phandle", phandle));
}
