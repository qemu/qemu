/*
 * LEFI (a UEFI-like interface for BIOS-Kernel boot parameters) helpers
 *
 * Copyright (c) 2018-2020 Huacai Chen (chenhc@lemote.com)
 * Copyright (c) 2018-2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/bswap.h"
#include "exec/hwaddr.h"
#include "hw/mips/loongson3_bootp.h"

static void init_cpu_info(void *g_cpuinfo, uint32_t cpu_count,
                          uint32_t processor_id, uint64_t cpu_freq)
{
    struct efi_cpuinfo_loongson *c = g_cpuinfo;

    c->cputype = cpu_to_le32(Loongson_3A);
    c->processor_id = cpu_to_le32(processor_id);
    if (cpu_freq > UINT_MAX) {
        c->cpu_clock_freq = cpu_to_le32(UINT_MAX);
    } else {
        c->cpu_clock_freq = cpu_to_le32(cpu_freq);
    }

    c->cpu_startup_core_id = cpu_to_le16(0);
    c->nr_cpus = cpu_to_le32(cpu_count);
    c->total_node = cpu_to_le32(DIV_ROUND_UP(cpu_count,
                                             LOONGSON3_CORE_PER_NODE));
}

static void init_memory_map(void *g_map, uint64_t ram_size)
{
    struct efi_memory_map_loongson *emap = g_map;

    emap->nr_map = cpu_to_le32(2);
    emap->mem_freq = cpu_to_le32(300000000);

    emap->map[0].node_id = cpu_to_le32(0);
    emap->map[0].mem_type = cpu_to_le32(1);
    emap->map[0].mem_start = cpu_to_le64(0x0);
    emap->map[0].mem_size = cpu_to_le32(240);

    emap->map[1].node_id = cpu_to_le32(0);
    emap->map[1].mem_type = cpu_to_le32(2);
    emap->map[1].mem_start = cpu_to_le64(0x90000000);
    emap->map[1].mem_size = cpu_to_le32((ram_size / MiB) - 256);
}

static void init_system_loongson(void *g_system)
{
    struct system_loongson *s = g_system;

    s->ccnuma_smp = cpu_to_le32(0);
    s->sing_double_channel = cpu_to_le32(1);
    s->nr_uarts = cpu_to_le32(1);
    s->uarts[0].iotype = cpu_to_le32(2);
    s->uarts[0].int_offset = cpu_to_le32(2);
    s->uarts[0].uartclk = cpu_to_le32(25000000); /* Random value */
    s->uarts[0].uart_base = cpu_to_le64(virt_memmap[VIRT_UART].base);
}

static void init_irq_source(void *g_irq_source)
{
    struct irq_source_routing_table *irq_info = g_irq_source;

    irq_info->node_id = cpu_to_le32(0);
    irq_info->PIC_type = cpu_to_le32(0);
    irq_info->dma_mask_bits = cpu_to_le16(64);
    irq_info->pci_mem_start_addr = cpu_to_le64(virt_memmap[VIRT_PCIE_MMIO].base);
    irq_info->pci_mem_end_addr = cpu_to_le64(virt_memmap[VIRT_PCIE_MMIO].base +
                                             virt_memmap[VIRT_PCIE_MMIO].size - 1);
    irq_info->pci_io_start_addr = cpu_to_le64(virt_memmap[VIRT_PCIE_PIO].base);
}

static void init_interface_info(void *g_interface)
{
    struct interface_info *interface = g_interface;

    interface->vers = cpu_to_le16(0x01);
    strpadcpy(interface->description, 64, "UEFI_Version_v1.0", '\0');
}

static void board_devices_info(void *g_board)
{
    struct board_devices *bd = g_board;

    strpadcpy(bd->name, 64, "Loongson-3A-VIRT-1w-V1.00-demo", '\0');
}

static void init_special_info(void *g_special)
{
    struct loongson_special_attribute *special = g_special;

    strpadcpy(special->special_name, 64, "2018-05-01", '\0');
}

void init_loongson_params(struct loongson_params *lp, void *p,
                          uint32_t cpu_count, uint32_t processor_id,
                          uint64_t cpu_freq, uint64_t ram_size)
{
    init_cpu_info(p, cpu_count, processor_id, cpu_freq);
    lp->cpu_offset = cpu_to_le64((uintptr_t)p - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct efi_cpuinfo_loongson), 64);

    init_memory_map(p, ram_size);
    lp->memory_offset = cpu_to_le64((uintptr_t)p - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct efi_memory_map_loongson), 64);

    init_system_loongson(p);
    lp->system_offset = cpu_to_le64((uintptr_t)p - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct system_loongson), 64);

    init_irq_source(p);
    lp->irq_offset = cpu_to_le64((uintptr_t)p - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct irq_source_routing_table), 64);

    init_interface_info(p);
    lp->interface_offset = cpu_to_le64((uintptr_t)p - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct interface_info), 64);

    board_devices_info(p);
    lp->boarddev_table_offset = cpu_to_le64((uintptr_t)p - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct board_devices), 64);

    init_special_info(p);
    lp->special_offset = cpu_to_le64((uintptr_t)p - (uintptr_t)lp);
    p += ROUND_UP(sizeof(struct loongson_special_attribute), 64);
}

void init_reset_system(struct efi_reset_system_t *reset)
{
    reset->Shutdown = cpu_to_le64(0xffffffffbfc000a8);
    reset->ResetCold = cpu_to_le64(0xffffffffbfc00080);
    reset->ResetWarm = cpu_to_le64(0xffffffffbfc00080);
    reset->DoSuspend = cpu_to_le64(0xffffffffbfc000d0);
}
