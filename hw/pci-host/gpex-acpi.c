#include "qemu/osdep.h"
#include "hw/acpi/aml-build.h"
#include "hw/pci-host/gpex.h"
#include "hw/arm/virt.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_host.h"
#include "hw/acpi/cxl.h"

static void acpi_dsdt_add_pci_route_table(Aml *dev, uint32_t irq)
{
    Aml *method, *crs;
    int i, slot_no;

    /* Declare the PCI Routing Table. */
    Aml *rt_pkg = aml_varpackage(PCI_SLOT_MAX * PCI_NUM_PINS);
    for (slot_no = 0; slot_no < PCI_SLOT_MAX; slot_no++) {
        for (i = 0; i < PCI_NUM_PINS; i++) {
            int gsi = (i + slot_no) % PCI_NUM_PINS;
            Aml *pkg = aml_package(4);
            aml_append(pkg, aml_int((slot_no << 16) | 0xFFFF));
            aml_append(pkg, aml_int(i));
            aml_append(pkg, aml_name("GSI%d", gsi));
            aml_append(pkg, aml_int(0));
            aml_append(rt_pkg, pkg);
        }
    }
    aml_append(dev, aml_name_decl("_PRT", rt_pkg));

    /* Create GSI link device */
    for (i = 0; i < PCI_NUM_PINS; i++) {
        uint32_t irqs = irq + i;
        Aml *dev_gsi = aml_device("GSI%d", i);
        aml_append(dev_gsi, aml_name_decl("_HID", aml_string("PNP0C0F")));
        aml_append(dev_gsi, aml_name_decl("_UID", aml_int(i)));
        crs = aml_resource_template();
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irqs, 1));
        aml_append(dev_gsi, aml_name_decl("_PRS", crs));
        crs = aml_resource_template();
        aml_append(crs,
                   aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                 AML_EXCLUSIVE, &irqs, 1));
        aml_append(dev_gsi, aml_name_decl("_CRS", crs));
        method = aml_method("_SRS", 1, AML_NOTSERIALIZED);
        aml_append(dev_gsi, method);
        aml_append(dev, dev_gsi);
    }
}

static void acpi_dsdt_add_pci_osc(Aml *dev)
{
    Aml *method, *UUID, *ifctx, *ifctx1, *elsectx, *buf;

    /* Declare an _OSC (OS Control Handoff) method */
    aml_append(dev, aml_name_decl("SUPP", aml_int(0)));
    aml_append(dev, aml_name_decl("CTRL", aml_int(0)));
    method = aml_method("_OSC", 4, AML_NOTSERIALIZED);
    aml_append(method,
        aml_create_dword_field(aml_arg(3), aml_int(0), "CDW1"));

    /* PCI Firmware Specification 3.0
     * 4.5.1. _OSC Interface for PCI Host Bridge Devices
     * The _OSC interface for a PCI/PCI-X/PCI Express hierarchy is
     * identified by the Universal Unique IDentifier (UUID)
     * 33DB4D5B-1FF7-401C-9657-7441C03DD766
     */
    UUID = aml_touuid("33DB4D5B-1FF7-401C-9657-7441C03DD766");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    aml_append(ifctx,
        aml_create_dword_field(aml_arg(3), aml_int(4), "CDW2"));
    aml_append(ifctx,
        aml_create_dword_field(aml_arg(3), aml_int(8), "CDW3"));
    aml_append(ifctx, aml_store(aml_name("CDW2"), aml_name("SUPP")));
    aml_append(ifctx, aml_store(aml_name("CDW3"), aml_name("CTRL")));

    /*
     * Allow OS control for all 5 features:
     * PCIeHotplug SHPCHotplug PME AER PCIeCapability.
     */
    aml_append(ifctx, aml_and(aml_name("CTRL"), aml_int(0x1F),
                              aml_name("CTRL")));

    ifctx1 = aml_if(aml_lnot(aml_equal(aml_arg(1), aml_int(0x1))));
    aml_append(ifctx1, aml_or(aml_name("CDW1"), aml_int(0x08),
                              aml_name("CDW1")));
    aml_append(ifctx, ifctx1);

    ifctx1 = aml_if(aml_lnot(aml_equal(aml_name("CDW3"), aml_name("CTRL"))));
    aml_append(ifctx1, aml_or(aml_name("CDW1"), aml_int(0x10),
                              aml_name("CDW1")));
    aml_append(ifctx, ifctx1);

    aml_append(ifctx, aml_store(aml_name("CTRL"), aml_name("CDW3")));
    aml_append(ifctx, aml_return(aml_arg(3)));
    aml_append(method, ifctx);

    elsectx = aml_else();
    aml_append(elsectx, aml_or(aml_name("CDW1"), aml_int(4),
                               aml_name("CDW1")));
    aml_append(elsectx, aml_return(aml_arg(3)));
    aml_append(method, elsectx);
    aml_append(dev, method);

    method = aml_method("_DSM", 4, AML_NOTSERIALIZED);

    /* PCI Firmware Specification 3.0
     * 4.6.1. _DSM for PCI Express Slot Information
     * The UUID in _DSM in this context is
     * {E5C937D0-3553-4D7A-9117-EA4D19C3434D}
     */
    UUID = aml_touuid("E5C937D0-3553-4D7A-9117-EA4D19C3434D");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    ifctx1 = aml_if(aml_equal(aml_arg(2), aml_int(0)));
    uint8_t byte_list[1] = {1};
    buf = aml_buffer(1, byte_list);
    aml_append(ifctx1, aml_return(buf));
    aml_append(ifctx, ifctx1);
    aml_append(method, ifctx);

    byte_list[0] = 0;
    buf = aml_buffer(1, byte_list);
    aml_append(method, aml_return(buf));
    aml_append(dev, method);
}

void acpi_dsdt_add_gpex(Aml *scope, struct GPEXConfig *cfg)
{
    int nr_pcie_buses = cfg->ecam.size / PCIE_MMCFG_SIZE_MIN;
    Aml *method, *crs, *dev, *rbuf;
    PCIBus *bus = cfg->bus;
    CrsRangeSet crs_range_set;
    CrsRangeEntry *entry;
    int i;

    /* start to construct the tables for pxb */
    crs_range_set_init(&crs_range_set);
    if (bus) {
        QLIST_FOREACH(bus, &bus->child, sibling) {
            uint8_t bus_num = pci_bus_num(bus);
            uint8_t numa_node = pci_bus_numa_node(bus);
            bool is_cxl = pci_bus_is_cxl(bus);

            if (!pci_bus_is_root(bus)) {
                continue;
            }

            /*
             * 0 - (nr_pcie_buses - 1) is the bus range for the main
             * host-bridge and it equals the MIN of the
             * busNr defined for pxb-pcie.
             */
            if (bus_num < nr_pcie_buses) {
                nr_pcie_buses = bus_num;
            }

            dev = aml_device("PC%.02X", bus_num);
            if (is_cxl) {
                struct Aml *pkg = aml_package(2);
                aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0016")));
                aml_append(pkg, aml_eisaid("PNP0A08"));
                aml_append(pkg, aml_eisaid("PNP0A03"));
                aml_append(dev, aml_name_decl("_CID", pkg));
            } else {
                aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A08")));
                aml_append(dev, aml_name_decl("_CID", aml_string("PNP0A03")));
            }
            aml_append(dev, aml_name_decl("_BBN", aml_int(bus_num)));
            aml_append(dev, aml_name_decl("_UID", aml_int(bus_num)));
            aml_append(dev, aml_name_decl("_STR", aml_unicode("pxb Device")));
            aml_append(dev, aml_name_decl("_CCA", aml_int(1)));
            if (numa_node != NUMA_NODE_UNASSIGNED) {
                aml_append(dev, aml_name_decl("_PXM", aml_int(numa_node)));
            }

            acpi_dsdt_add_pci_route_table(dev, cfg->irq);

            /*
             * Resources defined for PXBs are composed of the following parts:
             * 1. The resources the pci-brige/pcie-root-port need.
             * 2. The resources the devices behind pxb need.
             */
            crs = build_crs(PCI_HOST_BRIDGE(BUS(bus)->parent), &crs_range_set,
                            cfg->pio.base, 0, 0, 0);
            aml_append(dev, aml_name_decl("_CRS", crs));

            if (is_cxl) {
                build_cxl_osc_method(dev);
            } else {
                acpi_dsdt_add_pci_osc(dev);
            }

            aml_append(scope, dev);
        }
    }

    /* tables for the main */
    dev = aml_device("%s", "PCI0");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A08")));
    aml_append(dev, aml_name_decl("_CID", aml_string("PNP0A03")));
    aml_append(dev, aml_name_decl("_SEG", aml_int(0)));
    aml_append(dev, aml_name_decl("_BBN", aml_int(0)));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));
    aml_append(dev, aml_name_decl("_STR", aml_unicode("PCIe 0 Device")));
    aml_append(dev, aml_name_decl("_CCA", aml_int(1)));

    acpi_dsdt_add_pci_route_table(dev, cfg->irq);

    method = aml_method("_CBA", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_int(cfg->ecam.base)));
    aml_append(dev, method);

    /*
     * At this point crs_range_set has all the ranges used by pci
     * busses *other* than PCI0.  These ranges will be excluded from
     * the PCI0._CRS.
     */
    rbuf = aml_resource_template();
    aml_append(rbuf,
        aml_word_bus_number(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                            0x0000, 0x0000, nr_pcie_buses - 1, 0x0000,
                            nr_pcie_buses));
    if (cfg->mmio32.size) {
        crs_replace_with_free_ranges(crs_range_set.mem_ranges,
                                     cfg->mmio32.base,
                                     cfg->mmio32.base + cfg->mmio32.size - 1);
        for (i = 0; i < crs_range_set.mem_ranges->len; i++) {
            entry = g_ptr_array_index(crs_range_set.mem_ranges, i);
            aml_append(rbuf,
                aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                                 AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000,
                                 entry->base, entry->limit,
                                 0x0000, entry->limit - entry->base + 1));
        }
    }
    if (cfg->pio.size) {
        crs_replace_with_free_ranges(crs_range_set.io_ranges,
                                     0x0000,
                                     cfg->pio.size - 1);
        for (i = 0; i < crs_range_set.io_ranges->len; i++) {
            entry = g_ptr_array_index(crs_range_set.io_ranges, i);
            aml_append(rbuf,
                aml_dword_io(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                             AML_ENTIRE_RANGE, 0x0000, entry->base,
                             entry->limit, cfg->pio.base,
                             entry->limit - entry->base + 1));
        }
    }
    if (cfg->mmio64.size) {
        crs_replace_with_free_ranges(crs_range_set.mem_64bit_ranges,
                                     cfg->mmio64.base,
                                     cfg->mmio64.base + cfg->mmio64.size - 1);
        for (i = 0; i < crs_range_set.mem_64bit_ranges->len; i++) {
            entry = g_ptr_array_index(crs_range_set.mem_64bit_ranges, i);
            aml_append(rbuf,
                aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                                 AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000,
                                 entry->base,
                                 entry->limit, 0x0000,
                                 entry->limit - entry->base + 1));
        }
    }
    aml_append(dev, aml_name_decl("_CRS", rbuf));

    acpi_dsdt_add_pci_osc(dev);

    Aml *dev_res0 = aml_device("%s", "RES0");
    aml_append(dev_res0, aml_name_decl("_HID", aml_string("PNP0C02")));
    crs = aml_resource_template();
    aml_append(crs,
        aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                         AML_NON_CACHEABLE, AML_READ_WRITE, 0x0000,
                         cfg->ecam.base,
                         cfg->ecam.base + cfg->ecam.size - 1,
                         0x0000,
                         cfg->ecam.size));
    aml_append(dev_res0, aml_name_decl("_CRS", crs));
    aml_append(dev, dev_res0);
    aml_append(scope, dev);

    crs_range_set_free(&crs_range_set);
}
