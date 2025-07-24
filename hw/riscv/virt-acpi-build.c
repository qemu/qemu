/*
 * Support for generating ACPI tables and passing them to Guests
 *
 * RISC-V virt ACPI generation
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO.,LTD.
 * Copyright (C) 2021-2023 Ventana Micro Systems Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/pci.h"
#include "hw/acpi/utils.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/nvram/fw_cfg_acpi.h"
#include "hw/pci-host/gpex.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/numa.h"
#include "hw/virtio/virtio-acpi.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/reset.h"

#define ACPI_BUILD_TABLE_SIZE             0x20000
#define ACPI_BUILD_INTC_ID(socket, index) ((socket << 24) | (index))

typedef struct AcpiBuildState {
    /* Copy of table in RAM (for patching) */
    MemoryRegion *table_mr;
    MemoryRegion *rsdp_mr;
    MemoryRegion *linker_mr;
    /* Is table patched? */
    bool patched;
} AcpiBuildState;

static void acpi_align_size(GArray *blob, unsigned align)
{
    /*
     * Align size to multiple of given size. This reduces the chance
     * we need to change size in the future (breaking cross version migration).
     */
    g_array_set_size(blob, ROUND_UP(acpi_data_len(blob), align));
}

static void riscv_acpi_madt_add_rintc(uint32_t uid,
                                      const CPUArchIdList *arch_ids,
                                      GArray *entry,
                                      RISCVVirtState *s)
{
    uint8_t  guest_index_bits = imsic_num_bits(s->aia_guests + 1);
    uint64_t hart_id = arch_ids->cpus[uid].arch_id;
    uint32_t imsic_size, local_cpu_id, socket_id;
    uint64_t imsic_socket_addr, imsic_addr;
    MachineState *ms = MACHINE(s);

    socket_id = arch_ids->cpus[uid].props.node_id;
    local_cpu_id = (arch_ids->cpus[uid].arch_id -
                    riscv_socket_first_hartid(ms, socket_id)) %
                    riscv_socket_hart_count(ms, socket_id);
    imsic_socket_addr = s->memmap[VIRT_IMSIC_S].base +
                        (socket_id * VIRT_IMSIC_GROUP_MAX_SIZE);
    imsic_size = IMSIC_HART_SIZE(guest_index_bits);
    imsic_addr = imsic_socket_addr + local_cpu_id * imsic_size;
    build_append_int_noprefix(entry, 0x18, 1);       /* Type     */
    build_append_int_noprefix(entry, 36, 1);         /* Length   */
    build_append_int_noprefix(entry, 1, 1);          /* Version  */
    build_append_int_noprefix(entry, 0, 1);          /* Reserved */
    build_append_int_noprefix(entry, 0x1, 4);        /* Flags    */
    build_append_int_noprefix(entry, hart_id, 8);    /* Hart ID  */
    build_append_int_noprefix(entry, uid, 4);        /* ACPI Processor UID */
    /* External Interrupt Controller ID */
    if (s->aia_type == VIRT_AIA_TYPE_APLIC) {
        build_append_int_noprefix(entry,
                                  ACPI_BUILD_INTC_ID(
                                      arch_ids->cpus[uid].props.node_id,
                                      local_cpu_id),
                                  4);
    } else if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        build_append_int_noprefix(entry,
                                  ACPI_BUILD_INTC_ID(
                                      arch_ids->cpus[uid].props.node_id,
                                      2 * local_cpu_id + 1),
                                  4);
    } else {
        build_append_int_noprefix(entry, 0, 4);
    }

    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        /* IMSIC Base address */
        build_append_int_noprefix(entry, imsic_addr, 8);
        /* IMSIC Size */
        build_append_int_noprefix(entry, imsic_size, 4);
    } else {
        build_append_int_noprefix(entry, 0, 8);
        build_append_int_noprefix(entry, 0, 4);
    }
}

static void acpi_dsdt_add_cpus(Aml *scope, RISCVVirtState *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);
    MachineState *ms = MACHINE(s);
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(ms);

    for (int i = 0; i < arch_ids->len; i++) {
            Aml *dev;
            GArray *madt_buf = g_array_new(0, 1, 1);

            dev = aml_device("C%.03X", i);
            aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0007")));
            aml_append(dev, aml_name_decl("_UID",
                       aml_int(arch_ids->cpus[i].arch_id)));

            /* build _MAT object */
            riscv_acpi_madt_add_rintc(i, arch_ids, madt_buf, s);
            aml_append(dev, aml_name_decl("_MAT",
                                          aml_buffer(madt_buf->len,
                                          (uint8_t *)madt_buf->data)));
            g_array_free(madt_buf, true);

            aml_append(scope, dev);
    }
}

static void acpi_dsdt_add_plic_aplic(Aml *scope, uint8_t socket_count,
                                     uint64_t mmio_base, uint64_t mmio_size,
                                     const char *hid)
{
    uint64_t plic_aplic_addr;
    uint32_t gsi_base;
    uint8_t  socket;

    for (socket = 0; socket < socket_count; socket++) {
        plic_aplic_addr = mmio_base + mmio_size * socket;
        gsi_base = VIRT_IRQCHIP_NUM_SOURCES * socket;
        Aml *dev = aml_device("IC%.02X", socket);
        aml_append(dev, aml_name_decl("_HID", aml_string("%s", hid)));
        aml_append(dev, aml_name_decl("_UID", aml_int(socket)));
        aml_append(dev, aml_name_decl("_GSB", aml_int(gsi_base)));

        Aml *crs = aml_resource_template();
        aml_append(crs, aml_memory32_fixed(plic_aplic_addr, mmio_size,
                                           AML_READ_WRITE));
        aml_append(dev, aml_name_decl("_CRS", crs));
        aml_append(scope, dev);
    }
}

static void
acpi_dsdt_add_uart(Aml *scope, const MemMapEntry *uart_memmap,
                    uint32_t uart_irq)
{
    Aml *dev = aml_device("COM0");
    aml_append(dev, aml_name_decl("_HID", aml_string("RSCV0003")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(uart_memmap->base,
                                         uart_memmap->size, AML_READ_WRITE));
    aml_append(crs,
                aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                               AML_EXCLUSIVE, &uart_irq, 1));
    aml_append(dev, aml_name_decl("_CRS", crs));

    Aml *pkg = aml_package(2);
    aml_append(pkg, aml_string("clock-frequency"));
    aml_append(pkg, aml_int(3686400));

    Aml *UUID = aml_touuid("DAFFD814-6EBA-4D8C-8A91-BC9BBF4AA301");

    Aml *pkg1 = aml_package(1);
    aml_append(pkg1, pkg);

    Aml *package = aml_package(2);
    aml_append(package, UUID);
    aml_append(package, pkg1);

    aml_append(dev, aml_name_decl("_DSD", package));
    aml_append(scope, dev);
}

/*
 * Add DSDT entry for the IOMMU platform device.
 * ACPI ID for IOMMU is defined in the section 6.2 of RISC-V BRS spec.
 * https://github.com/riscv-non-isa/riscv-brs/releases/download/v0.8/riscv-brs-spec.pdf
 */
static void acpi_dsdt_add_iommu_sys(Aml *scope, const MemMapEntry *iommu_memmap,
                                    uint32_t iommu_irq)
{
    uint32_t i;

    Aml *dev = aml_device("IMU0");
    aml_append(dev, aml_name_decl("_HID", aml_string("RSCV0004")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));

    Aml *crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(iommu_memmap->base,
                                       iommu_memmap->size, AML_READ_WRITE));
    for (i = iommu_irq; i < iommu_irq + 4; i++) {
        aml_append(crs, aml_interrupt(AML_CONSUMER, AML_EDGE, AML_ACTIVE_LOW,
                                      AML_EXCLUSIVE, &i, 1));
    }

    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

/*
 * Serial Port Console Redirection Table (SPCR)
 * Rev: 1.10
 */

static void
spcr_setup(GArray *table_data, BIOSLinker *linker, RISCVVirtState *s)
{
    const char name[] = ".";
    AcpiSpcrData serial = {
        .interface_type = 0x12,       /* 16550 compatible */
        .base_addr.id = AML_AS_SYSTEM_MEMORY,
        .base_addr.width = 32,
        .base_addr.offset = 0,
        .base_addr.size = 1,
        .base_addr.addr = s->memmap[VIRT_UART0].base,
        .interrupt_type = (1 << 4),/* Bit[4] RISC-V PLIC/APLIC */
        .pc_interrupt = 0,
        .interrupt = UART0_IRQ,
        .baud_rate = 7,            /* 15200 */
        .parity = 0,
        .stop_bits = 1,
        .flow_control = 0,
        .terminal_type = 3,        /* ANSI */
        .language = 0,             /* Language */
        .pci_device_id = 0xffff,   /* not a PCI device*/
        .pci_vendor_id = 0xffff,   /* not a PCI device*/
        .pci_bus = 0,
        .pci_device = 0,
        .pci_function = 0,
        .pci_flags = 0,
        .pci_segment = 0,
        .uart_clk_freq = 0,
        .precise_baudrate = 0,
        .namespace_string_length = sizeof(name),
        .namespace_string_offset = 88,
    };

    build_spcr(table_data, linker, &serial, 4, s->oem_id, s->oem_table_id,
               name);
}

/* RHCT Node[N] starts at offset 56 */
#define RHCT_NODE_ARRAY_OFFSET 56

/*
 * ACPI spec, Revision 6.6
 * 5.2.37 RISC-V Hart Capabilities Table (RHCT)
 */
static void build_rhct(GArray *table_data,
                       BIOSLinker *linker,
                       RISCVVirtState *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);
    MachineState *ms = MACHINE(s);
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(ms);
    size_t len, aligned_len;
    uint32_t isa_offset, num_rhct_nodes, cmo_offset = 0;
    RISCVCPU *cpu = &s->soc[0].harts[0];
    uint32_t mmu_offset = 0;
    bool rv32 = riscv_cpu_is_32bit(cpu);
    g_autofree char *isa = NULL;

    AcpiTable table = { .sig = "RHCT", .rev = 1, .oem_id = s->oem_id,
                        .oem_table_id = s->oem_table_id };

    acpi_table_begin(&table, table_data);

    build_append_int_noprefix(table_data, 0x0, 4);   /* Reserved */

    /* Time Base Frequency */
    build_append_int_noprefix(table_data,
                              RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, 8);

    /* ISA + N hart info */
    num_rhct_nodes = 1 + ms->smp.cpus;
    if (cpu->cfg.ext_zicbom || cpu->cfg.ext_zicboz) {
        num_rhct_nodes++;
    }

    if (!rv32 && cpu->cfg.max_satp_mode >= VM_1_10_SV39) {
        num_rhct_nodes++;
    }

    /* Number of RHCT nodes*/
    build_append_int_noprefix(table_data, num_rhct_nodes, 4);

    /* Offset to the RHCT node array */
    build_append_int_noprefix(table_data, RHCT_NODE_ARRAY_OFFSET, 4);

    /* ISA String Node */
    isa_offset = table_data->len - table.table_offset;
    build_append_int_noprefix(table_data, 0, 2);   /* Type 0 */

    isa = riscv_isa_string(cpu);
    len = 8 + strlen(isa) + 1;
    aligned_len = (len % 2) ? (len + 1) : len;

    build_append_int_noprefix(table_data, aligned_len, 2);   /* Length */
    build_append_int_noprefix(table_data, 0x1, 2);           /* Revision */

    /* ISA string length including NUL */
    build_append_int_noprefix(table_data, strlen(isa) + 1, 2);
    g_array_append_vals(table_data, isa, strlen(isa) + 1);   /* ISA string */

    if (aligned_len != len) {
        build_append_int_noprefix(table_data, 0x0, 1);   /* Optional Padding */
    }

    /* CMO node */
    if (cpu->cfg.ext_zicbom || cpu->cfg.ext_zicboz) {
        cmo_offset = table_data->len - table.table_offset;
        build_append_int_noprefix(table_data, 1, 2);    /* Type */
        build_append_int_noprefix(table_data, 10, 2);   /* Length */
        build_append_int_noprefix(table_data, 0x1, 2);  /* Revision */
        build_append_int_noprefix(table_data, 0, 1);    /* Reserved */

        /* CBOM block size */
        if (cpu->cfg.cbom_blocksize) {
            build_append_int_noprefix(table_data,
                                      __builtin_ctz(cpu->cfg.cbom_blocksize),
                                      1);
        } else {
            build_append_int_noprefix(table_data, 0, 1);
        }

        /* CBOP block size */
        build_append_int_noprefix(table_data, 0, 1);

        /* CBOZ block size */
        if (cpu->cfg.cboz_blocksize) {
            build_append_int_noprefix(table_data,
                                      __builtin_ctz(cpu->cfg.cboz_blocksize),
                                      1);
        } else {
            build_append_int_noprefix(table_data, 0, 1);
        }
    }

    /* MMU node structure */
    if (!rv32 && cpu->cfg.max_satp_mode >= VM_1_10_SV39) {
        mmu_offset = table_data->len - table.table_offset;
        build_append_int_noprefix(table_data, 2, 2);    /* Type */
        build_append_int_noprefix(table_data, 8, 2);    /* Length */
        build_append_int_noprefix(table_data, 0x1, 2);  /* Revision */
        build_append_int_noprefix(table_data, 0, 1);    /* Reserved */
        /* MMU Type */
        if (cpu->cfg.max_satp_mode == VM_1_10_SV57) {
            build_append_int_noprefix(table_data, 2, 1);    /* Sv57 */
        } else if (cpu->cfg.max_satp_mode == VM_1_10_SV48) {
            build_append_int_noprefix(table_data, 1, 1);    /* Sv48 */
        } else if (cpu->cfg.max_satp_mode == VM_1_10_SV39) {
            build_append_int_noprefix(table_data, 0, 1);    /* Sv39 */
        } else {
            g_assert_not_reached();
        }
    }

    /* Hart Info Node */
    for (int i = 0; i < arch_ids->len; i++) {
        len = 16;
        int num_offsets = 1;
        build_append_int_noprefix(table_data, 0xFFFF, 2);  /* Type */

        /* Length */
        if (cmo_offset) {
            len += 4;
            num_offsets++;
        }

        if (mmu_offset) {
            len += 4;
            num_offsets++;
        }

        build_append_int_noprefix(table_data, len, 2);
        build_append_int_noprefix(table_data, 0x1, 2); /* Revision */
        /* Number of offsets */
        build_append_int_noprefix(table_data, num_offsets, 2);
        build_append_int_noprefix(table_data, i, 4);   /* ACPI Processor UID */
        /* Offsets */
        build_append_int_noprefix(table_data, isa_offset, 4);
        if (cmo_offset) {
            build_append_int_noprefix(table_data, cmo_offset, 4);
        }

        if (mmu_offset) {
            build_append_int_noprefix(table_data, mmu_offset, 4);
        }
    }

    acpi_table_end(linker, &table);
}

/*
 * ACPI spec, Revision 6.6
 * 5.2.9 Fixed ACPI Description Table (MADT)
 */
static void build_fadt_rev6(GArray *table_data,
                            BIOSLinker *linker,
                            RISCVVirtState *s,
                            unsigned dsdt_tbl_offset)
{
    AcpiFadtData fadt = {
        .rev = 6,
        .minor_ver = 6,
        .flags = 1 << ACPI_FADT_F_HW_REDUCED_ACPI,
        .xdsdt_tbl_offset = &dsdt_tbl_offset,
    };

    build_fadt(table_data, linker, &fadt, s->oem_id, s->oem_table_id);
}

/* DSDT */
static void build_dsdt(GArray *table_data,
                       BIOSLinker *linker,
                       RISCVVirtState *s)
{
    Aml *scope, *dsdt;
    MachineState *ms = MACHINE(s);
    uint8_t socket_count;
    const MemMapEntry *memmap = s->memmap;
    AcpiTable table = { .sig = "DSDT", .rev = 2, .oem_id = s->oem_id,
                        .oem_table_id = s->oem_table_id };


    acpi_table_begin(&table, table_data);
    dsdt = init_aml_allocator();

    /*
     * When booting the VM with UEFI, UEFI takes ownership of the RTC hardware.
     * While UEFI can use libfdt to disable the RTC device node in the DTB that
     * it passes to the OS, it cannot modify AML. Therefore, we won't generate
     * the RTC ACPI device at all when using UEFI.
     */
    scope = aml_scope("\\_SB");
    acpi_dsdt_add_cpus(scope, s);

    fw_cfg_acpi_dsdt_add(scope, &memmap[VIRT_FW_CFG]);

    socket_count = riscv_socket_count(ms);

    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        acpi_dsdt_add_plic_aplic(scope, socket_count, memmap[VIRT_PLIC].base,
                                 memmap[VIRT_PLIC].size, "RSCV0001");
    } else {
        acpi_dsdt_add_plic_aplic(scope, socket_count, memmap[VIRT_APLIC_S].base,
                                 memmap[VIRT_APLIC_S].size, "RSCV0002");
    }

    acpi_dsdt_add_uart(scope, &memmap[VIRT_UART0], UART0_IRQ);
    if (virt_is_iommu_sys_enabled(s)) {
        acpi_dsdt_add_iommu_sys(scope, &memmap[VIRT_IOMMU_SYS], IOMMU_SYS_IRQ);
    }

    if (socket_count == 1) {
        virtio_acpi_dsdt_add(scope, memmap[VIRT_VIRTIO].base,
                             memmap[VIRT_VIRTIO].size,
                             VIRTIO_IRQ, 0, VIRTIO_COUNT);
        acpi_dsdt_add_gpex_host(scope, PCIE_IRQ);
    } else if (socket_count == 2) {
        virtio_acpi_dsdt_add(scope, memmap[VIRT_VIRTIO].base,
                             memmap[VIRT_VIRTIO].size,
                             VIRTIO_IRQ + VIRT_IRQCHIP_NUM_SOURCES, 0,
                             VIRTIO_COUNT);
        acpi_dsdt_add_gpex_host(scope, PCIE_IRQ + VIRT_IRQCHIP_NUM_SOURCES);
    } else {
        virtio_acpi_dsdt_add(scope, memmap[VIRT_VIRTIO].base,
                             memmap[VIRT_VIRTIO].size,
                             VIRTIO_IRQ + VIRT_IRQCHIP_NUM_SOURCES, 0,
                             VIRTIO_COUNT);
        acpi_dsdt_add_gpex_host(scope, PCIE_IRQ + VIRT_IRQCHIP_NUM_SOURCES * 2);
    }

    aml_append(dsdt, scope);

    /* copy AML table into ACPI tables blob and patch header there */
    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);

    acpi_table_end(linker, &table);
    free_aml_allocator();
}

/*
 * ACPI spec, Revision 6.6
 * 5.2.12 Multiple APIC Description Table (MADT)
 */
static void build_madt(GArray *table_data,
                       BIOSLinker *linker,
                       RISCVVirtState *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);
    MachineState *ms = MACHINE(s);
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(ms);
    uint8_t  group_index_bits = imsic_num_bits(riscv_socket_count(ms));
    uint8_t  guest_index_bits = imsic_num_bits(s->aia_guests + 1);
    uint16_t imsic_max_hart_per_socket = 0;
    uint8_t  hart_index_bits;
    uint64_t aplic_addr;
    uint32_t gsi_base;
    uint8_t  socket;

    for (socket = 0; socket < riscv_socket_count(ms); socket++) {
        if (imsic_max_hart_per_socket < s->soc[socket].num_harts) {
            imsic_max_hart_per_socket = s->soc[socket].num_harts;
        }
    }

    hart_index_bits = imsic_num_bits(imsic_max_hart_per_socket);

    AcpiTable table = { .sig = "APIC", .rev = 7, .oem_id = s->oem_id,
                        .oem_table_id = s->oem_table_id };

    acpi_table_begin(&table, table_data);
    /* Local Interrupt Controller Address */
    build_append_int_noprefix(table_data, 0, 4);
    build_append_int_noprefix(table_data, 0, 4);   /* MADT Flags */

    /* RISC-V Local INTC structures per HART */
    for (int i = 0; i < arch_ids->len; i++) {
        riscv_acpi_madt_add_rintc(i, arch_ids, table_data, s);
    }

    /* IMSIC */
    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        /* IMSIC */
        build_append_int_noprefix(table_data, 0x19, 1);     /* Type */
        build_append_int_noprefix(table_data, 16, 1);       /* Length */
        build_append_int_noprefix(table_data, 1, 1);        /* Version */
        build_append_int_noprefix(table_data, 0, 1);        /* Reserved */
        build_append_int_noprefix(table_data, 0, 4);        /* Flags */
        /* Number of supervisor mode Interrupt Identities */
        build_append_int_noprefix(table_data, VIRT_IRQCHIP_NUM_MSIS, 2);
        /* Number of guest mode Interrupt Identities */
        build_append_int_noprefix(table_data, VIRT_IRQCHIP_NUM_MSIS, 2);
        /* Guest Index Bits */
        build_append_int_noprefix(table_data, guest_index_bits, 1);
        /* Hart Index Bits */
        build_append_int_noprefix(table_data, hart_index_bits, 1);
        /* Group Index Bits */
        build_append_int_noprefix(table_data, group_index_bits, 1);
        /* Group Index Shift */
        build_append_int_noprefix(table_data, IMSIC_MMIO_GROUP_MIN_SHIFT, 1);
    }

    if (s->aia_type != VIRT_AIA_TYPE_NONE) {
        /* APLICs */
        for (socket = 0; socket < riscv_socket_count(ms); socket++) {
            aplic_addr = s->memmap[VIRT_APLIC_S].base +
                             s->memmap[VIRT_APLIC_S].size * socket;
            gsi_base = VIRT_IRQCHIP_NUM_SOURCES * socket;
            build_append_int_noprefix(table_data, 0x1A, 1);    /* Type */
            build_append_int_noprefix(table_data, 36, 1);      /* Length */
            build_append_int_noprefix(table_data, 1, 1);       /* Version */
            build_append_int_noprefix(table_data, socket, 1);  /* APLIC ID */
            build_append_int_noprefix(table_data, 0, 4);       /* Flags */
            build_append_int_noprefix(table_data, 0, 8);       /* Hardware ID */
            /* Number of IDCs */
            if (s->aia_type == VIRT_AIA_TYPE_APLIC) {
                build_append_int_noprefix(table_data,
                                          s->soc[socket].num_harts,
                                          2);
            } else {
                build_append_int_noprefix(table_data, 0, 2);
            }
            /* Total External Interrupt Sources Supported */
            build_append_int_noprefix(table_data, VIRT_IRQCHIP_NUM_SOURCES, 2);
            /* Global System Interrupt Base */
            build_append_int_noprefix(table_data, gsi_base, 4);
            /* APLIC Address */
            build_append_int_noprefix(table_data, aplic_addr, 8);
            /* APLIC size */
            build_append_int_noprefix(table_data,
                                      s->memmap[VIRT_APLIC_S].size, 4);
        }
    } else {
        /* PLICs */
        for (socket = 0; socket < riscv_socket_count(ms); socket++) {
            aplic_addr = s->memmap[VIRT_PLIC].base +
                         s->memmap[VIRT_PLIC].size * socket;
            gsi_base = VIRT_IRQCHIP_NUM_SOURCES * socket;
            build_append_int_noprefix(table_data, 0x1B, 1);   /* Type */
            build_append_int_noprefix(table_data, 36, 1);     /* Length */
            build_append_int_noprefix(table_data, 1, 1);      /* Version */
            build_append_int_noprefix(table_data, socket, 1); /* PLIC ID */
            build_append_int_noprefix(table_data, 0, 8);      /* Hardware ID */
            /* Total External Interrupt Sources Supported */
            build_append_int_noprefix(table_data,
                                      VIRT_IRQCHIP_NUM_SOURCES - 1, 2);
            build_append_int_noprefix(table_data, 0, 2);     /* Max Priority */
            build_append_int_noprefix(table_data, 0, 4);     /* Flags */
            /* PLIC Size */
            build_append_int_noprefix(table_data, s->memmap[VIRT_PLIC].size, 4);
            /* PLIC Address */
            build_append_int_noprefix(table_data, aplic_addr, 8);
            /* Global System Interrupt Vector Base */
            build_append_int_noprefix(table_data, gsi_base, 4);
        }
    }

    acpi_table_end(linker, &table);
}

#define ID_MAPPING_ENTRY_SIZE        20
#define IOMMU_ENTRY_SIZE             40
#define RISCV_INTERRUPT_WIRE_OFFSSET 40
#define ROOT_COMPLEX_ENTRY_SIZE      20
#define RIMT_NODE_OFFSET             48

/*
 * ID Mapping Structure
 */
static void build_rimt_id_mapping(GArray *table_data, uint32_t source_id_base,
                                  uint32_t num_ids, uint32_t dest_id_base)
{
    /* Source ID Base */
    build_append_int_noprefix(table_data, source_id_base, 4);
    /* Number of IDs */
    build_append_int_noprefix(table_data, num_ids, 4);
    /* Destination Device ID Base */
    build_append_int_noprefix(table_data, source_id_base, 4);
    /* Destination IOMMU Offset */
    build_append_int_noprefix(table_data, dest_id_base, 4);
    /* Flags */
    build_append_int_noprefix(table_data, 0, 4);
}

struct AcpiRimtIdMapping {
    uint32_t source_id_base;
    uint32_t num_ids;
};
typedef struct AcpiRimtIdMapping AcpiRimtIdMapping;

/* Build the rimt ID mapping to IOMMU for a given PCI host bridge */
static int rimt_host_bridges(Object *obj, void *opaque)
{
    GArray *idmap_blob = opaque;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            int min_bus, max_bus;

            pci_bus_range(bus, &min_bus, &max_bus);

            AcpiRimtIdMapping idmap = {
                .source_id_base = min_bus << 8,
                .num_ids = (max_bus - min_bus + 1) << 8,
            };
            g_array_append_val(idmap_blob, idmap);
        }
    }

    return 0;
}

static int rimt_idmap_compare(gconstpointer a, gconstpointer b)
{
    AcpiRimtIdMapping *idmap_a = (AcpiRimtIdMapping *)a;
    AcpiRimtIdMapping *idmap_b = (AcpiRimtIdMapping *)b;

    return idmap_a->source_id_base - idmap_b->source_id_base;
}

/*
 * RISC-V IO Mapping Table (RIMT)
 * https://github.com/riscv-non-isa/riscv-acpi-rimt/releases/download/v0.99/rimt-spec.pdf
 */
static void build_rimt(GArray *table_data, BIOSLinker *linker,
                       RISCVVirtState *s)
{
    int i, nb_nodes, rc_mapping_count;
    size_t node_size, iommu_offset = 0;
    uint32_t id = 0;
    g_autoptr(GArray) iommu_idmaps = g_array_new(false, true,
                                                 sizeof(AcpiRimtIdMapping));

    AcpiTable table = { .sig = "RIMT", .rev = 1, .oem_id = s->oem_id,
                        .oem_table_id = s->oem_table_id };

    acpi_table_begin(&table, table_data);

    object_child_foreach_recursive(object_get_root(),
                                   rimt_host_bridges, iommu_idmaps);

    /* Sort the ID mapping  by Source ID Base*/
    g_array_sort(iommu_idmaps, rimt_idmap_compare);

    nb_nodes = 2; /* RC, IOMMU */
    rc_mapping_count = iommu_idmaps->len;
    /* Number of RIMT Nodes */
    build_append_int_noprefix(table_data, nb_nodes, 4);

    /* Offset to Array of RIMT Nodes */
    build_append_int_noprefix(table_data, RIMT_NODE_OFFSET, 4);
    build_append_int_noprefix(table_data, 0, 4); /* Reserved */

    iommu_offset = table_data->len - table.table_offset;
    /*  IOMMU Device Structure */
    build_append_int_noprefix(table_data, 0, 1);         /* Type - IOMMU*/
    build_append_int_noprefix(table_data, 1, 1);         /* Revision */
    node_size =  IOMMU_ENTRY_SIZE;
    build_append_int_noprefix(table_data, node_size, 2); /* Length */
    build_append_int_noprefix(table_data, 0, 2);         /* Reserved */
    build_append_int_noprefix(table_data, id++, 2);      /* ID */
    if (virt_is_iommu_sys_enabled(s)) {
        /* Hardware ID */
        build_append_int_noprefix(table_data, 'R', 1);
        build_append_int_noprefix(table_data, 'S', 1);
        build_append_int_noprefix(table_data, 'C', 1);
        build_append_int_noprefix(table_data, 'V', 1);
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '4', 1);
        /* Base Address */
        build_append_int_noprefix(table_data,
                                  s->memmap[VIRT_IOMMU_SYS].base, 8);
        build_append_int_noprefix(table_data, 0, 4);   /* Flags */
    } else {
        /* Hardware ID */
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '1', 1);
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '0', 1);
        build_append_int_noprefix(table_data, '1', 1);
        build_append_int_noprefix(table_data, '4', 1);

        build_append_int_noprefix(table_data, 0, 8);   /* Base Address */
        build_append_int_noprefix(table_data, 1, 4);   /* Flags */
    }

    build_append_int_noprefix(table_data, 0, 4);       /* Proximity Domain */
    build_append_int_noprefix(table_data, 0, 2);       /* PCI Segment number */
    /* PCIe B/D/F */
    if (virt_is_iommu_sys_enabled(s)) {
        build_append_int_noprefix(table_data, 0, 2);
    } else {
        build_append_int_noprefix(table_data, s->pci_iommu_bdf, 2);
    }
    /* Number of interrupt wires */
    build_append_int_noprefix(table_data, 0, 2);
    /* Interrupt wire array offset */
    build_append_int_noprefix(table_data, RISCV_INTERRUPT_WIRE_OFFSSET, 2);

    /*  PCIe Root Complex Node */
    build_append_int_noprefix(table_data, 1, 1);           /* Type */
    build_append_int_noprefix(table_data, 1, 1);           /* Revision */
    node_size =  ROOT_COMPLEX_ENTRY_SIZE +
                 ID_MAPPING_ENTRY_SIZE * rc_mapping_count;
    build_append_int_noprefix(table_data, node_size, 2);   /* Length */
    build_append_int_noprefix(table_data, 0, 2);           /* Reserved */
    build_append_int_noprefix(table_data, id++, 2);        /* ID */
    build_append_int_noprefix(table_data, 0, 4);           /* Flags */
    build_append_int_noprefix(table_data, 0, 2);           /* Reserved */
    /* PCI Segment number */
    build_append_int_noprefix(table_data, 0, 2);
    /* ID mapping array offset */
    build_append_int_noprefix(table_data, ROOT_COMPLEX_ENTRY_SIZE, 2);
    /* Number of ID mappings */
    build_append_int_noprefix(table_data, rc_mapping_count, 2);

    /* Output Reference */
    AcpiRimtIdMapping *range;

    /* ID mapping array */
    for (i = 0; i < iommu_idmaps->len; i++) {
        range = &g_array_index(iommu_idmaps, AcpiRimtIdMapping, i);
        if (virt_is_iommu_sys_enabled(s)) {
            range->source_id_base = 0;
        } else {
            range->source_id_base = s->pci_iommu_bdf + 1;
        }
        range->num_ids = 0xffff - s->pci_iommu_bdf;
        build_rimt_id_mapping(table_data, range->source_id_base,
                              range->num_ids, iommu_offset);
    }

    acpi_table_end(linker, &table);
}

/*
 * ACPI spec, Revision 6.6
 * 5.2.16 System Resource Affinity Table (SRAT)
 */
static void
build_srat(GArray *table_data, BIOSLinker *linker, RISCVVirtState *vms)
{
    int i;
    uint64_t mem_base;
    MachineClass *mc = MACHINE_GET_CLASS(vms);
    MachineState *ms = MACHINE(vms);
    const CPUArchIdList *cpu_list = mc->possible_cpu_arch_ids(ms);
    AcpiTable table = { .sig = "SRAT", .rev = 3, .oem_id = vms->oem_id,
                        .oem_table_id = vms->oem_table_id };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 1, 4); /* Reserved */
    build_append_int_noprefix(table_data, 0, 8); /* Reserved */

    for (i = 0; i < cpu_list->len; ++i) {
        uint32_t nodeid = cpu_list->cpus[i].props.node_id;
        /*
         * 5.2.16.8 RINTC Affinity Structure
         */
        build_append_int_noprefix(table_data, 7, 1);      /* Type */
        build_append_int_noprefix(table_data, 20, 1);     /* Length */
        build_append_int_noprefix(table_data, 0, 2);        /* Reserved */
        build_append_int_noprefix(table_data, nodeid, 4); /* Proximity Domain */
        build_append_int_noprefix(table_data, i, 4); /* ACPI Processor UID */
        /* Flags, Table 5-70 */
        build_append_int_noprefix(table_data, 1 /* Flags: Enabled */, 4);
        build_append_int_noprefix(table_data, 0, 4); /* Clock Domain */
    }

    mem_base = vms->memmap[VIRT_DRAM].base;
    for (i = 0; i < ms->numa_state->num_nodes; ++i) {
        if (ms->numa_state->nodes[i].node_mem > 0) {
            build_srat_memory(table_data, mem_base,
                              ms->numa_state->nodes[i].node_mem, i,
                              MEM_AFFINITY_ENABLED);
            mem_base += ms->numa_state->nodes[i].node_mem;
        }
    }

    acpi_table_end(linker, &table);
}

static void virt_acpi_build(RISCVVirtState *s, AcpiBuildTables *tables)
{
    GArray *table_offsets;
    unsigned dsdt, xsdt;
    GArray *tables_blob = tables->table_data;
    MachineState *ms = MACHINE(s);

    table_offsets = g_array_new(false, true,
                                sizeof(uint32_t));

    bios_linker_loader_alloc(tables->linker,
                             ACPI_BUILD_TABLE_FILE, tables_blob,
                             64, false);

    /* DSDT is pointed to by FADT */
    dsdt = tables_blob->len;
    build_dsdt(tables_blob, tables->linker, s);

    /* FADT and others pointed to by XSDT */
    acpi_add_table(table_offsets, tables_blob);
    build_fadt_rev6(tables_blob, tables->linker, s, dsdt);

    acpi_add_table(table_offsets, tables_blob);
    build_madt(tables_blob, tables->linker, s);

    acpi_add_table(table_offsets, tables_blob);
    build_rhct(tables_blob, tables->linker, s);

    if (virt_is_iommu_sys_enabled(s) || s->pci_iommu_bdf) {
        acpi_add_table(table_offsets, tables_blob);
        build_rimt(tables_blob, tables->linker, s);
    }

    acpi_add_table(table_offsets, tables_blob);

    if (ms->acpi_spcr_enabled) {
        spcr_setup(tables_blob, tables->linker, s);
    }

    acpi_add_table(table_offsets, tables_blob);
    {
        AcpiMcfgInfo mcfg = {
           .base = s->memmap[VIRT_PCIE_ECAM].base,
           .size = s->memmap[VIRT_PCIE_ECAM].size,
        };
        build_mcfg(tables_blob, tables->linker, &mcfg, s->oem_id,
                   s->oem_table_id);
    }

    if (ms->numa_state->num_nodes > 0) {
        acpi_add_table(table_offsets, tables_blob);
        build_srat(tables_blob, tables->linker, s);
        if (ms->numa_state->have_numa_distance) {
            acpi_add_table(table_offsets, tables_blob);
            build_slit(tables_blob, tables->linker, ms, s->oem_id,
                       s->oem_table_id);
        }
    }

    /* XSDT is pointed to by RSDP */
    xsdt = tables_blob->len;
    build_xsdt(tables_blob, tables->linker, table_offsets, s->oem_id,
                s->oem_table_id);

    /* RSDP is in FSEG memory, so allocate it separately */
    {
        AcpiRsdpData rsdp_data = {
            .revision = 2,
            .oem_id = s->oem_id,
            .xsdt_tbl_offset = &xsdt,
            .rsdt_tbl_offset = NULL,
        };
        build_rsdp(tables->rsdp, tables->linker, &rsdp_data);
    }

    /*
     * The align size is 128, warn if 64k is not enough therefore
     * the align size could be resized.
     */
    if (tables_blob->len > ACPI_BUILD_TABLE_SIZE / 2) {
        warn_report("ACPI table size %u exceeds %d bytes,"
                    " migration may not work",
                    tables_blob->len, ACPI_BUILD_TABLE_SIZE / 2);
        error_printf("Try removing some objects.");
    }

    acpi_align_size(tables_blob, ACPI_BUILD_TABLE_SIZE);

    /* Clean up memory that's no longer used */
    g_array_free(table_offsets, true);
}

static void acpi_ram_update(MemoryRegion *mr, GArray *data)
{
    uint32_t size = acpi_data_len(data);

    /*
     * Make sure RAM size is correct - in case it got changed
     * e.g. by migration
     */
    memory_region_ram_resize(mr, size, &error_abort);

    memcpy(memory_region_get_ram_ptr(mr), data->data, size);
    memory_region_set_dirty(mr, 0, size);
}

static void virt_acpi_build_update(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    AcpiBuildTables tables;

    /* No state to update or already patched? Nothing to do. */
    if (!build_state || build_state->patched) {
        return;
    }

    build_state->patched = true;

    acpi_build_tables_init(&tables);

    virt_acpi_build(RISCV_VIRT_MACHINE(qdev_get_machine()), &tables);

    acpi_ram_update(build_state->table_mr, tables.table_data);
    acpi_ram_update(build_state->rsdp_mr, tables.rsdp);
    acpi_ram_update(build_state->linker_mr, tables.linker->cmd_blob);

    acpi_build_tables_cleanup(&tables, true);
}

static void virt_acpi_build_reset(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    build_state->patched = false;
}

static const VMStateDescription vmstate_virt_acpi_build = {
    .name = "virt_acpi_build",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(patched, AcpiBuildState),
        VMSTATE_END_OF_LIST()
    },
};

void virt_acpi_setup(RISCVVirtState *s)
{
    AcpiBuildTables tables;
    AcpiBuildState *build_state;

    build_state = g_malloc0(sizeof *build_state);

    acpi_build_tables_init(&tables);
    virt_acpi_build(s, &tables);

    /* Now expose it all to Guest */
    build_state->table_mr = acpi_add_rom_blob(virt_acpi_build_update,
                                              build_state, tables.table_data,
                                              ACPI_BUILD_TABLE_FILE);
    assert(build_state->table_mr != NULL);

    build_state->linker_mr = acpi_add_rom_blob(virt_acpi_build_update,
                                               build_state,
                                               tables.linker->cmd_blob,
                                               ACPI_BUILD_LOADER_FILE);

    build_state->rsdp_mr = acpi_add_rom_blob(virt_acpi_build_update,
                                             build_state, tables.rsdp,
                                             ACPI_BUILD_RSDP_FILE);

    qemu_register_reset(virt_acpi_build_reset, build_state);
    virt_acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_virt_acpi_build, build_state);

    /*
     * Clean up tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
