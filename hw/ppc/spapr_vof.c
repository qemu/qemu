/*
 * SPAPR machine hooks to Virtual Open Firmware,
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/ppc/spapr_cpu_core.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/vof.h"
#include "sysemu/sysemu.h"
#include "qom/qom-qobject.h"
#include "trace.h"

target_ulong spapr_h_vof_client(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                target_ulong opcode, target_ulong *_args)
{
    int ret = vof_client_call(MACHINE(spapr), spapr->vof, spapr->fdt_blob,
                              ppc64_phys_to_real(_args[0]));

    if (ret) {
        return H_PARAMETER;
    }
    return H_SUCCESS;
}

void spapr_vof_client_dt_finalize(SpaprMachineState *spapr, void *fdt)
{
    char *stdout_path = spapr_vio_stdout_path(spapr->vio_bus);

    vof_build_dt(fdt, spapr->vof);

    if (spapr->vof->bootargs) {
        int chosen;

        _FDT(chosen = fdt_path_offset(fdt, "/chosen"));
        /*
         * If the client did not change "bootargs", spapr_dt_chosen() must have
         * stored machine->kernel_cmdline in it before getting here.
         */
        _FDT(fdt_setprop_string(fdt, chosen, "bootargs", spapr->vof->bootargs));
    }

    /*
     * SLOF-less setup requires an open instance of stdout for early
     * kernel printk. By now all phandles are settled so we can open
     * the default serial console.
     */
    if (stdout_path) {
        _FDT(vof_client_open_store(fdt, spapr->vof, "/chosen", "stdout",
                                   stdout_path));
    }
}

void spapr_vof_reset(SpaprMachineState *spapr, void *fdt, Error **errp)
{
    target_ulong stack_ptr;
    Vof *vof = spapr->vof;
    PowerPCCPU *first_ppc_cpu = POWERPC_CPU(first_cpu);

    vof_init(vof, spapr->rma_size, errp);

    stack_ptr = vof_claim(vof, 0, VOF_STACK_SIZE, VOF_STACK_SIZE);
    if (stack_ptr == -1) {
        error_setg(errp, "Memory allocation for stack failed");
        return;
    }
    /* Stack grows downwards plus reserve space for the minimum stack frame */
    stack_ptr += VOF_STACK_SIZE - 0x20;

    if (spapr->kernel_size &&
        vof_claim(vof, spapr->kernel_addr, spapr->kernel_size, 0) == -1) {
        error_setg(errp, "Memory for kernel is in use");
        return;
    }

    if (spapr->initrd_size &&
        vof_claim(vof, spapr->initrd_base, spapr->initrd_size, 0) == -1) {
        error_setg(errp, "Memory for initramdisk is in use");
        return;
    }

    spapr_vof_client_dt_finalize(spapr, fdt);

    spapr_cpu_set_entry_state(first_ppc_cpu, SPAPR_ENTRY_POINT,
                              stack_ptr, spapr->initrd_base,
                              spapr->initrd_size);
    /* VOF is 32bit BE so enforce MSR here */
    first_ppc_cpu->env.msr &= ~((1ULL << MSR_SF) | (1ULL << MSR_LE));

    /*
     * At this point the expected allocation map is:
     *
     * 0..c38 - the initial firmware
     * 8000..10000 - stack
     * 400000.. - kernel
     * 3ea0000.. - initramdisk
     *
     * We skip writing FDT as nothing expects it; OF client interface is
     * going to be used for reading the device tree.
     */
}

void spapr_vof_quiesce(MachineState *ms)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(ms);

    spapr->fdt_size = fdt_totalsize(spapr->fdt_blob);
    spapr->fdt_initial_size = spapr->fdt_size;
}

bool spapr_vof_setprop(MachineState *ms, const char *path, const char *propname,
                       void *val, int vallen)
{
    SpaprMachineState *spapr = SPAPR_MACHINE(ms);

    /*
     * We only allow changing properties which we know how to update in QEMU
     * OR
     * the ones which we know that they need to survive during "quiesce".
     */

    if (strcmp(path, "/rtas") == 0) {
        if (strcmp(propname, "linux,rtas-base") == 0 ||
            strcmp(propname, "linux,rtas-entry") == 0) {
            /* These need to survive quiesce so let them store in the FDT */
            return true;
        }
    }

    if (strcmp(path, "/chosen") == 0) {
        if (strcmp(propname, "bootargs") == 0) {
            Vof *vof = spapr->vof;

            g_free(vof->bootargs);
            vof->bootargs = g_strndup(val, vallen);
            return true;
        }
        if (strcmp(propname, "linux,initrd-start") == 0) {
            if (vallen == sizeof(uint32_t)) {
                spapr->initrd_base = ldl_be_p(val);
                return true;
            }
            if (vallen == sizeof(uint64_t)) {
                spapr->initrd_base = ldq_be_p(val);
                return true;
            }
            return false;
        }
        if (strcmp(propname, "linux,initrd-end") == 0) {
            if (vallen == sizeof(uint32_t)) {
                spapr->initrd_size = ldl_be_p(val) - spapr->initrd_base;
                return true;
            }
            if (vallen == sizeof(uint64_t)) {
                spapr->initrd_size = ldq_be_p(val) - spapr->initrd_base;
                return true;
            }
            return false;
        }
    }

    return true;
}
