/*
 * Copyright (c) 2019, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/reset.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/pci-host/gpex.h"
#include "net/net.h"
#include "elf.h"
#include "exec/memory.h"
#include "qemu/error-report.h"
#include "xtensa_memory.h"
#include "xtensa_sim.h"

static void create_pcie(MachineState *ms, CPUXtensaState *env, int irq_base,
                        hwaddr addr_base)
{
    hwaddr base_ecam = addr_base + 0x00100000;
    hwaddr size_ecam =             0x03f00000;
    hwaddr base_pio  = addr_base + 0x00000000;
    hwaddr size_pio  =             0x00010000;
    hwaddr base_mmio = addr_base + 0x04000000;
    hwaddr size_mmio =             0x08000000;

    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    MemoryRegion *pio_alias;
    MemoryRegion *pio_reg;
    MemoryRegion *mmio_alias;
    MemoryRegion *mmio_reg;

    MachineClass *mc = MACHINE_GET_CLASS(ms);
    DeviceState *dev;
    PCIHostState *pci;
    qemu_irq *extints;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Map only the first size_ecam bytes of ECAM space. */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, size_ecam);
    memory_region_add_subregion(get_system_memory(), base_ecam, ecam_alias);

    /*
     * Map the MMIO window into system address space so as to expose
     * the section of PCI MMIO space which starts at the same base address
     * (ie 1:1 mapping for that part of PCI MMIO space visible through
     * the window).
     */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, base_mmio, size_mmio);
    memory_region_add_subregion(get_system_memory(), base_mmio, mmio_alias);

    /* Map IO port space. */
    pio_alias = g_new0(MemoryRegion, 1);
    pio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 2);
    memory_region_init_alias(pio_alias, OBJECT(dev), "pcie-pio",
                             pio_reg, 0, size_pio);
    memory_region_add_subregion(get_system_memory(), base_pio, pio_alias);

    /* Connect IRQ lines. */
    extints = xtensa_get_extints(env);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        void *q = extints[irq_base + i];

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, q);
        gpex_set_irq_num(GPEX_HOST(dev), i, irq_base + i);
    }

    pci = PCI_HOST_BRIDGE(dev);
    if (pci->bus) {
        for (i = 0; i < nb_nics; i++) {
            NICInfo *nd = &nd_table[i];

            if (!nd->model) {
                nd->model = g_strdup(mc->default_nic);
            }

            pci_nic_init_nofail(nd, pci->bus, nd->model, NULL);
        }
    }
}

static void xtensa_virt_init(MachineState *machine)
{
    XtensaCPU *cpu = xtensa_sim_common_init(machine);
    CPUXtensaState *env = &cpu->env;

    create_pcie(machine, env, 0, 0xf0000000);
    xtensa_sim_load_kernel(cpu, machine);
}

static void xtensa_virt_machine_init(MachineClass *mc)
{
    mc->desc = "virt machine (" XTENSA_DEFAULT_CPU_MODEL ")";
    mc->init = xtensa_virt_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
    mc->default_nic = "virtio-net-pci";
}

DEFINE_MACHINE("virt", xtensa_virt_machine_init)
