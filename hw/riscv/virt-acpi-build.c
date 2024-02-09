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
#include "sysemu/reset.h"

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

static void
acpi_dsdt_add_uart(Aml *scope, const MemMapEntry *uart_memmap,
                    uint32_t uart_irq)
{
    Aml *dev = aml_device("COM0");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0501")));
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

/* RHCT Node[N] starts at offset 56 */
#define RHCT_NODE_ARRAY_OFFSET 56

/*
 * ACPI spec, Revision 6.5+
 * 5.2.36 RISC-V Hart Capabilities Table (RHCT)
 * REF: https://github.com/riscv-non-isa/riscv-acpi/issues/16
 *      https://drive.google.com/file/d/1nP3nFiH4jkPMp6COOxP6123DCZKR-tia/view
 *      https://drive.google.com/file/d/1sKbOa8m1UZw1JkquZYe3F1zQBN1xXsaf/view
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
    uint8_t satp_mode_max;
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

    if (cpu->cfg.satp_mode.supported != 0) {
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
    if (cpu->cfg.satp_mode.supported != 0) {
        satp_mode_max = satp_mode_max_from_map(cpu->cfg.satp_mode.map);
        mmu_offset = table_data->len - table.table_offset;
        build_append_int_noprefix(table_data, 2, 2);    /* Type */
        build_append_int_noprefix(table_data, 8, 2);    /* Length */
        build_append_int_noprefix(table_data, 0x1, 2);  /* Revision */
        build_append_int_noprefix(table_data, 0, 1);    /* Reserved */
        /* MMU Type */
        if (satp_mode_max == VM_1_10_SV57) {
            build_append_int_noprefix(table_data, 2, 1);    /* Sv57 */
        } else if (satp_mode_max == VM_1_10_SV48) {
            build_append_int_noprefix(table_data, 1, 1);    /* Sv48 */
        } else if (satp_mode_max == VM_1_10_SV39) {
            build_append_int_noprefix(table_data, 0, 1);    /* Sv39 */
        } else {
            assert(1);
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

/* FADT */
static void build_fadt_rev6(GArray *table_data,
                            BIOSLinker *linker,
                            RISCVVirtState *s,
                            unsigned dsdt_tbl_offset)
{
    AcpiFadtData fadt = {
        .rev = 6,
        .minor_ver = 5,
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

    acpi_dsdt_add_uart(scope, &memmap[VIRT_UART0], UART0_IRQ);

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
 * ACPI spec, Revision 6.5+
 * 5.2.12 Multiple APIC Description Table (MADT)
 * REF: https://github.com/riscv-non-isa/riscv-acpi/issues/15
 *      https://drive.google.com/file/d/1R6k4MshhN3WTT-hwqAquu5nX6xSEqK2l/view
 *      https://drive.google.com/file/d/1oMGPyOD58JaPgMl1pKasT-VKsIKia7zR/view
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

    AcpiTable table = { .sig = "APIC", .rev = 6, .oem_id = s->oem_id,
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

static void virt_acpi_build(RISCVVirtState *s, AcpiBuildTables *tables)
{
    GArray *table_offsets;
    unsigned dsdt, xsdt;
    GArray *tables_blob = tables->table_data;

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

    acpi_add_table(table_offsets, tables_blob);
    {
        AcpiMcfgInfo mcfg = {
           .base = s->memmap[VIRT_PCIE_MMIO].base,
           .size = s->memmap[VIRT_PCIE_MMIO].size,
        };
        build_mcfg(tables_blob, tables->linker, &mcfg, s->oem_id,
                   s->oem_table_id);
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
