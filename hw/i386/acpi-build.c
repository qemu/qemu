/* Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
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
#include "qapi/error.h"
#include "qapi/qmp/qnum.h"
#include "acpi-build.h"
#include "acpi-common.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"
#include "hw/pci/pci.h"
#include "hw/core/cpu.h"
#include "target/i386/cpu.h"
#include "hw/misc/pvpanic.h"
#include "hw/timer/hpet.h"
#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/cpu.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/isa/isa.h"
#include "hw/input/i8042.h"
#include "hw/block/fdc.h"
#include "hw/acpi/memory_hotplug.h"
#include "sysemu/tpm.h"
#include "hw/acpi/tpm.h"
#include "hw/acpi/vmgenid.h"
#include "hw/acpi/erst.h"
#include "sysemu/tpm_backend.h"
#include "hw/rtc/mc146818rtc_regs.h"
#include "migration/vmstate.h"
#include "hw/mem/memory-device.h"
#include "hw/mem/nvdimm.h"
#include "sysemu/numa.h"
#include "sysemu/reset.h"
#include "hw/hyperv/vmbus-bridge.h"

/* Supported chipsets: */
#include "hw/southbridge/piix.h"
#include "hw/acpi/pcihp.h"
#include "hw/i386/fw_cfg.h"
#include "hw/i386/ich9.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci-host/q35.h"
#include "hw/i386/x86-iommu.h"

#include "hw/acpi/aml-build.h"
#include "hw/acpi/utils.h"
#include "hw/acpi/pci.h"

#include "qom/qom-qobject.h"
#include "hw/i386/amd_iommu.h"
#include "hw/i386/intel_iommu.h"
#include "hw/virtio/virtio-iommu.h"

#include "hw/acpi/ipmi.h"
#include "hw/acpi/hmat.h"
#include "hw/acpi/viot.h"

#include CONFIG_DEVICES

/* These are used to size the ACPI tables for -M pc-i440fx-1.7 and
 * -M pc-i440fx-2.0.  Even if the actual amount of AML generated grows
 * a little bit, there should be plenty of free space since the DSDT
 * shrunk by ~1.5k between QEMU 2.0 and QEMU 2.1.
 */
#define ACPI_BUILD_LEGACY_CPU_AML_SIZE    97
#define ACPI_BUILD_ALIGN_SIZE             0x1000

#define ACPI_BUILD_TABLE_SIZE             0x20000

/* #define DEBUG_ACPI_BUILD */
#ifdef DEBUG_ACPI_BUILD
#define ACPI_BUILD_DPRINTF(fmt, ...)        \
    do {printf("ACPI_BUILD: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ACPI_BUILD_DPRINTF(fmt, ...)
#endif

typedef struct AcpiPmInfo {
    bool s3_disabled;
    bool s4_disabled;
    bool pcihp_bridge_en;
    bool smi_on_cpuhp;
    bool smi_on_cpu_unplug;
    bool pcihp_root_en;
    uint8_t s4_val;
    AcpiFadtData fadt;
    uint16_t cpu_hp_io_base;
    uint16_t pcihp_io_base;
    uint16_t pcihp_io_len;
} AcpiPmInfo;

typedef struct AcpiMiscInfo {
    bool is_piix4;
    bool has_hpet;
#ifdef CONFIG_TPM
    TPMVersion tpm_version;
#endif
    const unsigned char *dsdt_code;
    unsigned dsdt_size;
    uint16_t pvpanic_port;
    uint16_t applesmc_io_base;
} AcpiMiscInfo;

typedef struct AcpiBuildPciBusHotplugState {
    GArray *device_table;
    GArray *notify_table;
    struct AcpiBuildPciBusHotplugState *parent;
    bool pcihp_bridge_en;
} AcpiBuildPciBusHotplugState;

typedef struct FwCfgTPMConfig {
    uint32_t tpmppi_address;
    uint8_t tpm_version;
    uint8_t tpmppi_version;
} QEMU_PACKED FwCfgTPMConfig;

static bool acpi_get_mcfg(AcpiMcfgInfo *mcfg);

const struct AcpiGenericAddress x86_nvdimm_acpi_dsmio = {
    .space_id = AML_AS_SYSTEM_IO,
    .address = NVDIMM_ACPI_IO_BASE,
    .bit_width = NVDIMM_ACPI_IO_LEN << 3
};

static void init_common_fadt_data(MachineState *ms, Object *o,
                                  AcpiFadtData *data)
{
    X86MachineState *x86ms = X86_MACHINE(ms);
    /*
     * "ICH9-LPC" or "PIIX4_PM" has "smm-compat" property to keep the old
     * behavior for compatibility irrelevant to smm_enabled, which doesn't
     * comforms to ACPI spec.
     */
    bool smm_enabled = object_property_get_bool(o, "smm-compat", NULL) ?
        true : x86_machine_is_smm_enabled(x86ms);
    uint32_t io = object_property_get_uint(o, ACPI_PM_PROP_PM_IO_BASE, NULL);
    AmlAddressSpace as = AML_AS_SYSTEM_IO;
    AcpiFadtData fadt = {
        .rev = 3,
        .flags =
            (1 << ACPI_FADT_F_WBINVD) |
            (1 << ACPI_FADT_F_PROC_C1) |
            (1 << ACPI_FADT_F_SLP_BUTTON) |
            (1 << ACPI_FADT_F_RTC_S4) |
            (1 << ACPI_FADT_F_USE_PLATFORM_CLOCK) |
            /* APIC destination mode ("Flat Logical") has an upper limit of 8
             * CPUs for more than 8 CPUs, "Clustered Logical" mode has to be
             * used
             */
            ((ms->smp.max_cpus > 8) ?
                        (1 << ACPI_FADT_F_FORCE_APIC_CLUSTER_MODEL) : 0),
        .int_model = 1 /* Multiple APIC */,
        .rtc_century = RTC_CENTURY,
        .plvl2_lat = 0xfff /* C2 state not supported */,
        .plvl3_lat = 0xfff /* C3 state not supported */,
        .smi_cmd = smm_enabled ? ACPI_PORT_SMI_CMD : 0,
        .sci_int = object_property_get_uint(o, ACPI_PM_PROP_SCI_INT, NULL),
        .acpi_enable_cmd =
            smm_enabled ?
            object_property_get_uint(o, ACPI_PM_PROP_ACPI_ENABLE_CMD, NULL) :
            0,
        .acpi_disable_cmd =
            smm_enabled ?
            object_property_get_uint(o, ACPI_PM_PROP_ACPI_DISABLE_CMD, NULL) :
            0,
        .pm1a_evt = { .space_id = as, .bit_width = 4 * 8, .address = io },
        .pm1a_cnt = { .space_id = as, .bit_width = 2 * 8,
                      .address = io + 0x04 },
        .pm_tmr = { .space_id = as, .bit_width = 4 * 8, .address = io + 0x08 },
        .gpe0_blk = { .space_id = as, .bit_width =
            object_property_get_uint(o, ACPI_PM_PROP_GPE0_BLK_LEN, NULL) * 8,
            .address = object_property_get_uint(o, ACPI_PM_PROP_GPE0_BLK, NULL)
        },
    };

    /*
     * ACPI v2, Table 5-10 - Fixed ACPI Description Table Boot Architecture
     * Flags, bit offset 1 - 8042.
     */
    fadt.iapc_boot_arch = iapc_boot_arch_8042();

    *data = fadt;
}

static Object *object_resolve_type_unambiguous(const char *typename)
{
    bool ambig;
    Object *o = object_resolve_path_type("", typename, &ambig);

    if (ambig || !o) {
        return NULL;
    }
    return o;
}

static void acpi_get_pm_info(MachineState *machine, AcpiPmInfo *pm)
{
    Object *piix = object_resolve_type_unambiguous(TYPE_PIIX4_PM);
    Object *lpc = object_resolve_type_unambiguous(TYPE_ICH9_LPC_DEVICE);
    Object *obj = piix ? piix : lpc;
    QObject *o;
    pm->cpu_hp_io_base = 0;
    pm->pcihp_io_base = 0;
    pm->pcihp_io_len = 0;
    pm->smi_on_cpuhp = false;
    pm->smi_on_cpu_unplug = false;

    assert(obj);
    init_common_fadt_data(machine, obj, &pm->fadt);
    if (piix) {
        /* w2k requires FADT(rev1) or it won't boot, keep PC compatible */
        pm->fadt.rev = 1;
        pm->cpu_hp_io_base = PIIX4_CPU_HOTPLUG_IO_BASE;
    }
    if (lpc) {
        uint64_t smi_features = object_property_get_uint(lpc,
            ICH9_LPC_SMI_NEGOTIATED_FEAT_PROP, NULL);
        struct AcpiGenericAddress r = { .space_id = AML_AS_SYSTEM_IO,
            .bit_width = 8, .address = ICH9_RST_CNT_IOPORT };
        pm->fadt.reset_reg = r;
        pm->fadt.reset_val = 0xf;
        pm->fadt.flags |= 1 << ACPI_FADT_F_RESET_REG_SUP;
        pm->cpu_hp_io_base = ICH9_CPU_HOTPLUG_IO_BASE;
        pm->smi_on_cpuhp =
            !!(smi_features & BIT_ULL(ICH9_LPC_SMI_F_CPU_HOTPLUG_BIT));
        pm->smi_on_cpu_unplug =
            !!(smi_features & BIT_ULL(ICH9_LPC_SMI_F_CPU_HOT_UNPLUG_BIT));
    }
    pm->pcihp_io_base =
        object_property_get_uint(obj, ACPI_PCIHP_IO_BASE_PROP, NULL);
    pm->pcihp_io_len =
        object_property_get_uint(obj, ACPI_PCIHP_IO_LEN_PROP, NULL);

    /* The above need not be conditional on machine type because the reset port
     * happens to be the same on PIIX (pc) and ICH9 (q35). */
    QEMU_BUILD_BUG_ON(ICH9_RST_CNT_IOPORT != PIIX_RCR_IOPORT);

    /* Fill in optional s3/s4 related properties */
    o = object_property_get_qobject(obj, ACPI_PM_PROP_S3_DISABLED, NULL);
    if (o) {
        pm->s3_disabled = qnum_get_uint(qobject_to(QNum, o));
    } else {
        pm->s3_disabled = false;
    }
    qobject_unref(o);
    o = object_property_get_qobject(obj, ACPI_PM_PROP_S4_DISABLED, NULL);
    if (o) {
        pm->s4_disabled = qnum_get_uint(qobject_to(QNum, o));
    } else {
        pm->s4_disabled = false;
    }
    qobject_unref(o);
    o = object_property_get_qobject(obj, ACPI_PM_PROP_S4_VAL, NULL);
    if (o) {
        pm->s4_val = qnum_get_uint(qobject_to(QNum, o));
    } else {
        pm->s4_val = false;
    }
    qobject_unref(o);

    pm->pcihp_bridge_en =
        object_property_get_bool(obj, ACPI_PM_PROP_ACPI_PCIHP_BRIDGE,
                                 NULL);
    pm->pcihp_root_en =
        object_property_get_bool(obj, ACPI_PM_PROP_ACPI_PCI_ROOTHP,
                                 NULL);
}

static void acpi_get_misc_info(AcpiMiscInfo *info)
{
    Object *piix = object_resolve_type_unambiguous(TYPE_PIIX4_PM);
    Object *lpc = object_resolve_type_unambiguous(TYPE_ICH9_LPC_DEVICE);
    assert(!!piix != !!lpc);

    if (piix) {
        info->is_piix4 = true;
    }
    if (lpc) {
        info->is_piix4 = false;
    }

    info->has_hpet = hpet_find();
#ifdef CONFIG_TPM
    info->tpm_version = tpm_get_version(tpm_find());
#endif
    info->pvpanic_port = pvpanic_port();
    info->applesmc_io_base = applesmc_port();
}

/*
 * Because of the PXB hosts we cannot simply query TYPE_PCI_HOST_BRIDGE.
 * On i386 arch we only have two pci hosts, so we can look only for them.
 */
Object *acpi_get_i386_pci_host(void)
{
    PCIHostState *host;

    host = PCI_HOST_BRIDGE(object_resolve_path("/machine/i440fx", NULL));
    if (!host) {
        host = PCI_HOST_BRIDGE(object_resolve_path("/machine/q35", NULL));
    }

    return OBJECT(host);
}

static void acpi_get_pci_holes(Range *hole, Range *hole64)
{
    Object *pci_host;

    pci_host = acpi_get_i386_pci_host();

    if (!pci_host) {
        return;
    }

    range_set_bounds1(hole,
                      object_property_get_uint(pci_host,
                                               PCI_HOST_PROP_PCI_HOLE_START,
                                               NULL),
                      object_property_get_uint(pci_host,
                                               PCI_HOST_PROP_PCI_HOLE_END,
                                               NULL));
    range_set_bounds1(hole64,
                      object_property_get_uint(pci_host,
                                               PCI_HOST_PROP_PCI_HOLE64_START,
                                               NULL),
                      object_property_get_uint(pci_host,
                                               PCI_HOST_PROP_PCI_HOLE64_END,
                                               NULL));
}

static void acpi_align_size(GArray *blob, unsigned align)
{
    /* Align size to multiple of given size. This reduces the chance
     * we need to change size in the future (breaking cross version migration).
     */
    g_array_set_size(blob, ROUND_UP(acpi_data_len(blob), align));
}

/*
 * ACPI spec 1.0b,
 * 5.2.6 Firmware ACPI Control Structure
 */
static void
build_facs(GArray *table_data)
{
    const char *sig = "FACS";
    const uint8_t reserved[40] = {};

    g_array_append_vals(table_data, sig, 4); /* Signature */
    build_append_int_noprefix(table_data, 64, 4); /* Length */
    build_append_int_noprefix(table_data, 0, 4); /* Hardware Signature */
    build_append_int_noprefix(table_data, 0, 4); /* Firmware Waking Vector */
    build_append_int_noprefix(table_data, 0, 4); /* Global Lock */
    build_append_int_noprefix(table_data, 0, 4); /* Flags */
    g_array_append_vals(table_data, reserved, 40); /* Reserved */
}

static void build_append_pcihp_notify_entry(Aml *method, int slot)
{
    Aml *if_ctx;
    int32_t devfn = PCI_DEVFN(slot, 0);

    if_ctx = aml_if(aml_and(aml_arg(0), aml_int(0x1U << slot), NULL));
    aml_append(if_ctx, aml_notify(aml_name("S%.02X", devfn), aml_arg(1)));
    aml_append(method, if_ctx);
}

static void build_append_pci_bus_devices(Aml *parent_scope, PCIBus *bus,
                                         bool pcihp_bridge_en)
{
    Aml *dev, *notify_method = NULL, *method;
    QObject *bsel;
    PCIBus *sec;
    int devfn;

    bsel = object_property_get_qobject(OBJECT(bus), ACPI_PCIHP_PROP_BSEL, NULL);
    if (bsel) {
        uint64_t bsel_val = qnum_get_uint(qobject_to(QNum, bsel));

        aml_append(parent_scope, aml_name_decl("BSEL", aml_int(bsel_val)));
        notify_method = aml_method("DVNT", 2, AML_NOTSERIALIZED);
    }

    for (devfn = 0; devfn < ARRAY_SIZE(bus->devices); devfn++) {
        DeviceClass *dc;
        PCIDeviceClass *pc;
        PCIDevice *pdev = bus->devices[devfn];
        int slot = PCI_SLOT(devfn);
        int func = PCI_FUNC(devfn);
        /* ACPI spec: 1.0b: Table 6-2 _ADR Object Bus Types, PCI type */
        int adr = slot << 16 | func;
        bool hotplug_enabled_dev;
        bool bridge_in_acpi;
        bool cold_plugged_bridge;

        if (!pdev) {
            /*
             * add hotplug slots for non present devices.
             * hotplug is supported only for non-multifunction device
             * so generate device description only for function 0
             */
            if (bsel && !func) {
                if (pci_bus_is_express(bus) && slot > 0) {
                    break;
                }
                dev = aml_device("S%.02X", devfn);
                aml_append(dev, aml_name_decl("_SUN", aml_int(slot)));
                aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));
                method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
                aml_append(method,
                    aml_call2("PCEJ", aml_name("BSEL"), aml_name("_SUN"))
                );
                aml_append(dev, method);
                method = aml_method("_DSM", 4, AML_SERIALIZED);
                aml_append(method,
                    aml_return(aml_call6("PDSM", aml_arg(0), aml_arg(1),
                                         aml_arg(2), aml_arg(3),
                                         aml_name("BSEL"), aml_name("_SUN")))
                );
                aml_append(dev, method);
                aml_append(parent_scope, dev);

                build_append_pcihp_notify_entry(notify_method, slot);
            }
            continue;
        }

        pc = PCI_DEVICE_GET_CLASS(pdev);
        dc = DEVICE_GET_CLASS(pdev);

        /*
         * Cold plugged bridges aren't themselves hot-pluggable.
         * Hotplugged bridges *are* hot-pluggable.
         */
        cold_plugged_bridge = pc->is_bridge && !DEVICE(pdev)->hotplugged;
        bridge_in_acpi =  cold_plugged_bridge && pcihp_bridge_en;

        hotplug_enabled_dev = bsel && dc->hotpluggable && !cold_plugged_bridge;

        if (pc->class_id == PCI_CLASS_BRIDGE_ISA) {
            continue;
        }

        /*
         * allow describing coldplugged bridges in ACPI even if they are not
         * on function 0, as they are not unpluggable, for all other devices
         * generate description only for function 0 per slot
         */
        if (func && !bridge_in_acpi) {
            continue;
        }

        /* start to compose PCI device descriptor */
        dev = aml_device("S%.02X", devfn);
        aml_append(dev, aml_name_decl("_ADR", aml_int(adr)));

        if (bsel) {
            /*
             * Can't declare _SUN here for every device as it changes 'slot'
             * enumeration order in linux kernel, so use another variable for it
             */
            aml_append(dev, aml_name_decl("ASUN", aml_int(slot)));
            method = aml_method("_DSM", 4, AML_SERIALIZED);
            aml_append(method, aml_return(
                aml_call6("PDSM", aml_arg(0), aml_arg(1), aml_arg(2),
                          aml_arg(3), aml_name("BSEL"), aml_name("ASUN"))
            ));
            aml_append(dev, method);
        }

        if (pc->class_id == PCI_CLASS_DISPLAY_VGA) {
            /* add VGA specific AML methods */
            int s3d;

            if (object_dynamic_cast(OBJECT(pdev), "qxl-vga")) {
                s3d = 3;
            } else {
                s3d = 0;
            }

            method = aml_method("_S1D", 0, AML_NOTSERIALIZED);
            aml_append(method, aml_return(aml_int(0)));
            aml_append(dev, method);

            method = aml_method("_S2D", 0, AML_NOTSERIALIZED);
            aml_append(method, aml_return(aml_int(0)));
            aml_append(dev, method);

            method = aml_method("_S3D", 0, AML_NOTSERIALIZED);
            aml_append(method, aml_return(aml_int(s3d)));
            aml_append(dev, method);
        } else if (hotplug_enabled_dev) {
            aml_append(dev, aml_name_decl("_SUN", aml_int(slot)));
            /* add _EJ0 to make slot hotpluggable  */
            method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
            aml_append(method,
                aml_call2("PCEJ", aml_name("BSEL"), aml_name("_SUN"))
            );
            aml_append(dev, method);

            if (bsel) {
                build_append_pcihp_notify_entry(notify_method, slot);
            }
        } else if (bridge_in_acpi) {
            /*
             * device is coldplugged bridge,
             * add child device descriptions into its scope
             */
            PCIBus *sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));

            build_append_pci_bus_devices(dev, sec_bus, pcihp_bridge_en);
        }
        /* device descriptor has been composed, add it into parent context */
        aml_append(parent_scope, dev);
    }

    if (bsel) {
        aml_append(parent_scope, notify_method);
    }

    /* Append PCNT method to notify about events on local and child buses.
     * Add this method for root bus only when hotplug is enabled since DSDT
     * expects it.
     */
    if (bsel || pcihp_bridge_en) {
        method = aml_method("PCNT", 0, AML_NOTSERIALIZED);

        /* If bus supports hotplug select it and notify about local events */
        if (bsel) {
            uint64_t bsel_val = qnum_get_uint(qobject_to(QNum, bsel));

            aml_append(method, aml_store(aml_int(bsel_val), aml_name("BNUM")));
            aml_append(method, aml_call2("DVNT", aml_name("PCIU"),
                                         aml_int(1))); /* Device Check */
            aml_append(method, aml_call2("DVNT", aml_name("PCID"),
                                         aml_int(3))); /* Eject Request */
        }

        /* Notify about child bus events in any case */
        if (pcihp_bridge_en) {
            QLIST_FOREACH(sec, &bus->child, sibling) {
                if (pci_bus_is_root(sec)) {
                    continue;
                }

                aml_append(method, aml_name("^S%.02X.PCNT",
                                            sec->parent_dev->devfn));
            }
        }

        aml_append(parent_scope, method);
    }
    qobject_unref(bsel);
}

Aml *aml_pci_device_dsm(void)
{
    Aml *method, *UUID, *ifctx, *ifctx1, *ifctx2, *ifctx3, *elsectx;
    Aml *acpi_index = aml_local(0);
    Aml *zero = aml_int(0);
    Aml *bnum = aml_arg(4);
    Aml *func = aml_arg(2);
    Aml *rev = aml_arg(1);
    Aml *sunum = aml_arg(5);

    method = aml_method("PDSM", 6, AML_SERIALIZED);

    /*
     * PCI Firmware Specification 3.1
     * 4.6.  _DSM Definitions for PCI
     */
    UUID = aml_touuid("E5C937D0-3553-4D7A-9117-EA4D19C3434D");
    ifctx = aml_if(aml_equal(aml_arg(0), UUID));
    {
        aml_append(ifctx, aml_store(aml_call2("AIDX", bnum, sunum), acpi_index));
        ifctx1 = aml_if(aml_equal(func, zero));
        {
            uint8_t byte_list[1];

            ifctx2 = aml_if(aml_equal(rev, aml_int(2)));
            {
                /*
                 * advertise function 7 if device has acpi-index
                 * acpi_index values:
                 *            0: not present (default value)
                 *     FFFFFFFF: not supported (old QEMU without PIDX reg)
                 *        other: device's acpi-index
                 */
                ifctx3 = aml_if(aml_lnot(
                    aml_or(aml_equal(acpi_index, zero),
                           aml_equal(acpi_index, aml_int(0xFFFFFFFF)), NULL)
                ));
                {
                    byte_list[0] =
                        1 /* have supported functions */ |
                        1 << 7 /* support for function 7 */
                    ;
                    aml_append(ifctx3, aml_return(aml_buffer(1, byte_list)));
                }
                aml_append(ifctx2, ifctx3);
             }
             aml_append(ifctx1, ifctx2);

             byte_list[0] = 0; /* nothing supported */
             aml_append(ifctx1, aml_return(aml_buffer(1, byte_list)));
         }
         aml_append(ifctx, ifctx1);
         elsectx = aml_else();
         /*
          * PCI Firmware Specification 3.1
          * 4.6.7. _DSM for Naming a PCI or PCI Express Device Under
          *        Operating Systems
          */
         ifctx1 = aml_if(aml_equal(func, aml_int(7)));
         {
             Aml *pkg = aml_package(2);
             Aml *ret = aml_local(1);

             aml_append(pkg, zero);
             /*
              * optional, if not impl. should return null string
              */
             aml_append(pkg, aml_string("%s", ""));
             aml_append(ifctx1, aml_store(pkg, ret));
             /*
              * update acpi-index to actual value
              */
             aml_append(ifctx1, aml_store(acpi_index, aml_index(ret, zero)));
             aml_append(ifctx1, aml_return(ret));
         }
         aml_append(elsectx, ifctx1);
         aml_append(ifctx, elsectx);
    }
    aml_append(method, ifctx);
    return method;
}

/**
 * build_prt_entry:
 * @link_name: link name for PCI route entry
 *
 * build AML package containing a PCI route entry for @link_name
 */
static Aml *build_prt_entry(const char *link_name)
{
    Aml *a_zero = aml_int(0);
    Aml *pkg = aml_package(4);
    aml_append(pkg, a_zero);
    aml_append(pkg, a_zero);
    aml_append(pkg, aml_name("%s", link_name));
    aml_append(pkg, a_zero);
    return pkg;
}

/*
 * initialize_route - Initialize the interrupt routing rule
 * through a specific LINK:
 *  if (lnk_idx == idx)
 *      route using link 'link_name'
 */
static Aml *initialize_route(Aml *route, const char *link_name,
                             Aml *lnk_idx, int idx)
{
    Aml *if_ctx = aml_if(aml_equal(lnk_idx, aml_int(idx)));
    Aml *pkg = build_prt_entry(link_name);

    aml_append(if_ctx, aml_store(pkg, route));

    return if_ctx;
}

/*
 * build_prt - Define interrupt rounting rules
 *
 * Returns an array of 128 routes, one for each device,
 * based on device location.
 * The main goal is to equaly distribute the interrupts
 * over the 4 existing ACPI links (works only for i440fx).
 * The hash function is  (slot + pin) & 3 -> "LNK[D|A|B|C]".
 *
 */
static Aml *build_prt(bool is_pci0_prt)
{
    Aml *method, *while_ctx, *pin, *res;

    method = aml_method("_PRT", 0, AML_NOTSERIALIZED);
    res = aml_local(0);
    pin = aml_local(1);
    aml_append(method, aml_store(aml_package(128), res));
    aml_append(method, aml_store(aml_int(0), pin));

    /* while (pin < 128) */
    while_ctx = aml_while(aml_lless(pin, aml_int(128)));
    {
        Aml *slot = aml_local(2);
        Aml *lnk_idx = aml_local(3);
        Aml *route = aml_local(4);

        /* slot = pin >> 2 */
        aml_append(while_ctx,
                   aml_store(aml_shiftright(pin, aml_int(2), NULL), slot));
        /* lnk_idx = (slot + pin) & 3 */
        aml_append(while_ctx,
            aml_store(aml_and(aml_add(pin, slot, NULL), aml_int(3), NULL),
                      lnk_idx));

        /* route[2] = "LNK[D|A|B|C]", selection based on pin % 3  */
        aml_append(while_ctx, initialize_route(route, "LNKD", lnk_idx, 0));
        if (is_pci0_prt) {
            Aml *if_device_1, *if_pin_4, *else_pin_4;

            /* device 1 is the power-management device, needs SCI */
            if_device_1 = aml_if(aml_equal(lnk_idx, aml_int(1)));
            {
                if_pin_4 = aml_if(aml_equal(pin, aml_int(4)));
                {
                    aml_append(if_pin_4,
                        aml_store(build_prt_entry("LNKS"), route));
                }
                aml_append(if_device_1, if_pin_4);
                else_pin_4 = aml_else();
                {
                    aml_append(else_pin_4,
                        aml_store(build_prt_entry("LNKA"), route));
                }
                aml_append(if_device_1, else_pin_4);
            }
            aml_append(while_ctx, if_device_1);
        } else {
            aml_append(while_ctx, initialize_route(route, "LNKA", lnk_idx, 1));
        }
        aml_append(while_ctx, initialize_route(route, "LNKB", lnk_idx, 2));
        aml_append(while_ctx, initialize_route(route, "LNKC", lnk_idx, 3));

        /* route[0] = 0x[slot]FFFF */
        aml_append(while_ctx,
            aml_store(aml_or(aml_shiftleft(slot, aml_int(16)), aml_int(0xFFFF),
                             NULL),
                      aml_index(route, aml_int(0))));
        /* route[1] = pin & 3 */
        aml_append(while_ctx,
            aml_store(aml_and(pin, aml_int(3), NULL),
                      aml_index(route, aml_int(1))));
        /* res[pin] = route */
        aml_append(while_ctx, aml_store(route, aml_index(res, pin)));
        /* pin++ */
        aml_append(while_ctx, aml_increment(pin));
    }
    aml_append(method, while_ctx);
    /* return res*/
    aml_append(method, aml_return(res));

    return method;
}

static void build_hpet_aml(Aml *table)
{
    Aml *crs;
    Aml *field;
    Aml *method;
    Aml *if_ctx;
    Aml *scope = aml_scope("_SB");
    Aml *dev = aml_device("HPET");
    Aml *zero = aml_int(0);
    Aml *id = aml_local(0);
    Aml *period = aml_local(1);

    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0103")));
    aml_append(dev, aml_name_decl("_UID", zero));

    aml_append(dev,
        aml_operation_region("HPTM", AML_SYSTEM_MEMORY, aml_int(HPET_BASE),
                             HPET_LEN));
    field = aml_field("HPTM", AML_DWORD_ACC, AML_LOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("VEND", 32));
    aml_append(field, aml_named_field("PRD", 32));
    aml_append(dev, field);

    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_name("VEND"), id));
    aml_append(method, aml_store(aml_name("PRD"), period));
    aml_append(method, aml_shiftright(id, aml_int(16), id));
    if_ctx = aml_if(aml_lor(aml_equal(id, zero),
                            aml_equal(id, aml_int(0xffff))));
    {
        aml_append(if_ctx, aml_return(zero));
    }
    aml_append(method, if_ctx);

    if_ctx = aml_if(aml_lor(aml_equal(period, zero),
                            aml_lgreater(period, aml_int(100000000))));
    {
        aml_append(if_ctx, aml_return(zero));
    }
    aml_append(method, if_ctx);

    aml_append(method, aml_return(aml_int(0x0F)));
    aml_append(dev, method);

    crs = aml_resource_template();
    aml_append(crs, aml_memory32_fixed(HPET_BASE, HPET_LEN, AML_READ_ONLY));
    aml_append(dev, aml_name_decl("_CRS", crs));

    aml_append(scope, dev);
    aml_append(table, scope);
}

static Aml *build_vmbus_device_aml(VMBusBridge *vmbus_bridge)
{
    Aml *dev;
    Aml *method;
    Aml *crs;

    dev = aml_device("VMBS");
    aml_append(dev, aml_name_decl("STA", aml_int(0xF)));
    aml_append(dev, aml_name_decl("_HID", aml_string("VMBus")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0x0)));
    aml_append(dev, aml_name_decl("_DDN", aml_string("VMBUS")));

    method = aml_method("_DIS", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_and(aml_name("STA"), aml_int(0xD), NULL),
                                     aml_name("STA")));
    aml_append(dev, method);

    method = aml_method("_PS0", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_store(aml_or(aml_name("STA"), aml_int(0xF), NULL),
                                     aml_name("STA")));
    aml_append(dev, method);

    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_name("STA")));
    aml_append(dev, method);

    aml_append(dev, aml_name_decl("_PS3", aml_int(0x0)));

    crs = aml_resource_template();
    aml_append(crs, aml_irq_no_flags(vmbus_bridge->irq));
    aml_append(dev, aml_name_decl("_CRS", crs));

    return dev;
}

static void build_isa_devices_aml(Aml *table)
{
    bool ambiguous;
    Object *obj = object_resolve_path_type("", TYPE_ISA_BUS, &ambiguous);
    Aml *scope;

    assert(obj && !ambiguous);

    scope = aml_scope("_SB.PCI0.ISA");
    build_acpi_ipmi_devices(scope, BUS(obj), "\\_SB.PCI0.ISA");
    isa_build_aml(ISA_BUS(obj), scope);

    aml_append(table, scope);
}

static void build_dbg_aml(Aml *table)
{
    Aml *field;
    Aml *method;
    Aml *while_ctx;
    Aml *scope = aml_scope("\\");
    Aml *buf = aml_local(0);
    Aml *len = aml_local(1);
    Aml *idx = aml_local(2);

    aml_append(scope,
       aml_operation_region("DBG", AML_SYSTEM_IO, aml_int(0x0402), 0x01));
    field = aml_field("DBG", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("DBGB", 8));
    aml_append(scope, field);

    method = aml_method("DBUG", 1, AML_NOTSERIALIZED);

    aml_append(method, aml_to_hexstring(aml_arg(0), buf));
    aml_append(method, aml_to_buffer(buf, buf));
    aml_append(method, aml_subtract(aml_sizeof(buf), aml_int(1), len));
    aml_append(method, aml_store(aml_int(0), idx));

    while_ctx = aml_while(aml_lless(idx, len));
    aml_append(while_ctx,
        aml_store(aml_derefof(aml_index(buf, idx)), aml_name("DBGB")));
    aml_append(while_ctx, aml_increment(idx));
    aml_append(method, while_ctx);

    aml_append(method, aml_store(aml_int(0x0A), aml_name("DBGB")));
    aml_append(scope, method);

    aml_append(table, scope);
}

static Aml *build_link_dev(const char *name, uint8_t uid, Aml *reg)
{
    Aml *dev;
    Aml *crs;
    Aml *method;
    uint32_t irqs[] = {5, 10, 11};

    dev = aml_device("%s", name);
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C0F")));
    aml_append(dev, aml_name_decl("_UID", aml_int(uid)));

    crs = aml_resource_template();
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                  AML_SHARED, irqs, ARRAY_SIZE(irqs)));
    aml_append(dev, aml_name_decl("_PRS", crs));

    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_call1("IQST", reg)));
    aml_append(dev, method);

    method = aml_method("_DIS", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_or(reg, aml_int(0x80), reg));
    aml_append(dev, method);

    method = aml_method("_CRS", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_call1("IQCR", reg)));
    aml_append(dev, method);

    method = aml_method("_SRS", 1, AML_NOTSERIALIZED);
    aml_append(method, aml_create_dword_field(aml_arg(0), aml_int(5), "PRRI"));
    aml_append(method, aml_store(aml_name("PRRI"), reg));
    aml_append(dev, method);

    return dev;
 }

static Aml *build_gsi_link_dev(const char *name, uint8_t uid, uint8_t gsi)
{
    Aml *dev;
    Aml *crs;
    Aml *method;
    uint32_t irqs;

    dev = aml_device("%s", name);
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C0F")));
    aml_append(dev, aml_name_decl("_UID", aml_int(uid)));

    crs = aml_resource_template();
    irqs = gsi;
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                  AML_SHARED, &irqs, 1));
    aml_append(dev, aml_name_decl("_PRS", crs));

    aml_append(dev, aml_name_decl("_CRS", crs));

    /*
     * _DIS can be no-op because the interrupt cannot be disabled.
     */
    method = aml_method("_DIS", 0, AML_NOTSERIALIZED);
    aml_append(dev, method);

    method = aml_method("_SRS", 1, AML_NOTSERIALIZED);
    aml_append(dev, method);

    return dev;
}

/* _CRS method - get current settings */
static Aml *build_iqcr_method(bool is_piix4)
{
    Aml *if_ctx;
    uint32_t irqs;
    Aml *method = aml_method("IQCR", 1, AML_SERIALIZED);
    Aml *crs = aml_resource_template();

    irqs = 0;
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_LEVEL,
                                  AML_ACTIVE_HIGH, AML_SHARED, &irqs, 1));
    aml_append(method, aml_name_decl("PRR0", crs));

    aml_append(method,
        aml_create_dword_field(aml_name("PRR0"), aml_int(5), "PRRI"));

    if (is_piix4) {
        if_ctx = aml_if(aml_lless(aml_arg(0), aml_int(0x80)));
        aml_append(if_ctx, aml_store(aml_arg(0), aml_name("PRRI")));
        aml_append(method, if_ctx);
    } else {
        aml_append(method,
            aml_store(aml_and(aml_arg(0), aml_int(0xF), NULL),
                      aml_name("PRRI")));
    }

    aml_append(method, aml_return(aml_name("PRR0")));
    return method;
}

/* _STA method - get status */
static Aml *build_irq_status_method(void)
{
    Aml *if_ctx;
    Aml *method = aml_method("IQST", 1, AML_NOTSERIALIZED);

    if_ctx = aml_if(aml_and(aml_int(0x80), aml_arg(0), NULL));
    aml_append(if_ctx, aml_return(aml_int(0x09)));
    aml_append(method, if_ctx);
    aml_append(method, aml_return(aml_int(0x0B)));
    return method;
}

static void build_piix4_pci0_int(Aml *table)
{
    Aml *dev;
    Aml *crs;
    Aml *field;
    Aml *method;
    uint32_t irqs;
    Aml *sb_scope = aml_scope("_SB");
    Aml *pci0_scope = aml_scope("PCI0");

    aml_append(pci0_scope, build_prt(true));
    aml_append(sb_scope, pci0_scope);

    field = aml_field("PCI0.ISA.P40C", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PRQ0", 8));
    aml_append(field, aml_named_field("PRQ1", 8));
    aml_append(field, aml_named_field("PRQ2", 8));
    aml_append(field, aml_named_field("PRQ3", 8));
    aml_append(sb_scope, field);

    aml_append(sb_scope, build_irq_status_method());
    aml_append(sb_scope, build_iqcr_method(true));

    aml_append(sb_scope, build_link_dev("LNKA", 0, aml_name("PRQ0")));
    aml_append(sb_scope, build_link_dev("LNKB", 1, aml_name("PRQ1")));
    aml_append(sb_scope, build_link_dev("LNKC", 2, aml_name("PRQ2")));
    aml_append(sb_scope, build_link_dev("LNKD", 3, aml_name("PRQ3")));

    dev = aml_device("LNKS");
    {
        aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C0F")));
        aml_append(dev, aml_name_decl("_UID", aml_int(4)));

        crs = aml_resource_template();
        irqs = 9;
        aml_append(crs, aml_interrupt(AML_CONSUMER, AML_LEVEL,
                                      AML_ACTIVE_HIGH, AML_SHARED,
                                      &irqs, 1));
        aml_append(dev, aml_name_decl("_PRS", crs));

        /* The SCI cannot be disabled and is always attached to GSI 9,
         * so these are no-ops.  We only need this link to override the
         * polarity to active high and match the content of the MADT.
         */
        method = aml_method("_STA", 0, AML_NOTSERIALIZED);
        aml_append(method, aml_return(aml_int(0x0b)));
        aml_append(dev, method);

        method = aml_method("_DIS", 0, AML_NOTSERIALIZED);
        aml_append(dev, method);

        method = aml_method("_CRS", 0, AML_NOTSERIALIZED);
        aml_append(method, aml_return(aml_name("_PRS")));
        aml_append(dev, method);

        method = aml_method("_SRS", 1, AML_NOTSERIALIZED);
        aml_append(dev, method);
    }
    aml_append(sb_scope, dev);

    aml_append(table, sb_scope);
}

static void append_q35_prt_entry(Aml *ctx, uint32_t nr, const char *name)
{
    int i;
    int head;
    Aml *pkg;
    char base = name[3] < 'E' ? 'A' : 'E';
    char *s = g_strdup(name);
    Aml *a_nr = aml_int((nr << 16) | 0xffff);

    assert(strlen(s) == 4);

    head = name[3] - base;
    for (i = 0; i < 4; i++) {
        if (head + i > 3) {
            head = i * -1;
        }
        s[3] = base + head + i;
        pkg = aml_package(4);
        aml_append(pkg, a_nr);
        aml_append(pkg, aml_int(i));
        aml_append(pkg, aml_name("%s", s));
        aml_append(pkg, aml_int(0));
        aml_append(ctx, pkg);
    }
    g_free(s);
}

static Aml *build_q35_routing_table(const char *str)
{
    int i;
    Aml *pkg;
    char *name = g_strdup_printf("%s ", str);

    pkg = aml_package(128);
    for (i = 0; i < 0x18; i++) {
            name[3] = 'E' + (i & 0x3);
            append_q35_prt_entry(pkg, i, name);
    }

    name[3] = 'E';
    append_q35_prt_entry(pkg, 0x18, name);

    /* INTA -> PIRQA for slot 25 - 31, see the default value of D<N>IR */
    for (i = 0x0019; i < 0x1e; i++) {
        name[3] = 'A';
        append_q35_prt_entry(pkg, i, name);
    }

    /* PCIe->PCI bridge. use PIRQ[E-H] */
    name[3] = 'E';
    append_q35_prt_entry(pkg, 0x1e, name);
    name[3] = 'A';
    append_q35_prt_entry(pkg, 0x1f, name);

    g_free(name);
    return pkg;
}

static void build_q35_pci0_int(Aml *table)
{
    Aml *field;
    Aml *method;
    Aml *sb_scope = aml_scope("_SB");
    Aml *pci0_scope = aml_scope("PCI0");

    /* Zero => PIC mode, One => APIC Mode */
    aml_append(table, aml_name_decl("PICF", aml_int(0)));
    method = aml_method("_PIC", 1, AML_NOTSERIALIZED);
    {
        aml_append(method, aml_store(aml_arg(0), aml_name("PICF")));
    }
    aml_append(table, method);

    aml_append(pci0_scope,
        aml_name_decl("PRTP", build_q35_routing_table("LNK")));
    aml_append(pci0_scope,
        aml_name_decl("PRTA", build_q35_routing_table("GSI")));

    method = aml_method("_PRT", 0, AML_NOTSERIALIZED);
    {
        Aml *if_ctx;
        Aml *else_ctx;

        /* PCI IRQ routing table, example from ACPI 2.0a specification,
           section 6.2.8.1 */
        /* Note: we provide the same info as the PCI routing
           table of the Bochs BIOS */
        if_ctx = aml_if(aml_equal(aml_name("PICF"), aml_int(0)));
        aml_append(if_ctx, aml_return(aml_name("PRTP")));
        aml_append(method, if_ctx);
        else_ctx = aml_else();
        aml_append(else_ctx, aml_return(aml_name("PRTA")));
        aml_append(method, else_ctx);
    }
    aml_append(pci0_scope, method);
    aml_append(sb_scope, pci0_scope);

    field = aml_field("PCI0.ISA.PIRQ", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PRQA", 8));
    aml_append(field, aml_named_field("PRQB", 8));
    aml_append(field, aml_named_field("PRQC", 8));
    aml_append(field, aml_named_field("PRQD", 8));
    aml_append(field, aml_reserved_field(0x20));
    aml_append(field, aml_named_field("PRQE", 8));
    aml_append(field, aml_named_field("PRQF", 8));
    aml_append(field, aml_named_field("PRQG", 8));
    aml_append(field, aml_named_field("PRQH", 8));
    aml_append(sb_scope, field);

    aml_append(sb_scope, build_irq_status_method());
    aml_append(sb_scope, build_iqcr_method(false));

    aml_append(sb_scope, build_link_dev("LNKA", 0, aml_name("PRQA")));
    aml_append(sb_scope, build_link_dev("LNKB", 1, aml_name("PRQB")));
    aml_append(sb_scope, build_link_dev("LNKC", 2, aml_name("PRQC")));
    aml_append(sb_scope, build_link_dev("LNKD", 3, aml_name("PRQD")));
    aml_append(sb_scope, build_link_dev("LNKE", 4, aml_name("PRQE")));
    aml_append(sb_scope, build_link_dev("LNKF", 5, aml_name("PRQF")));
    aml_append(sb_scope, build_link_dev("LNKG", 6, aml_name("PRQG")));
    aml_append(sb_scope, build_link_dev("LNKH", 7, aml_name("PRQH")));

    aml_append(sb_scope, build_gsi_link_dev("GSIA", 0x10, 0x10));
    aml_append(sb_scope, build_gsi_link_dev("GSIB", 0x11, 0x11));
    aml_append(sb_scope, build_gsi_link_dev("GSIC", 0x12, 0x12));
    aml_append(sb_scope, build_gsi_link_dev("GSID", 0x13, 0x13));
    aml_append(sb_scope, build_gsi_link_dev("GSIE", 0x14, 0x14));
    aml_append(sb_scope, build_gsi_link_dev("GSIF", 0x15, 0x15));
    aml_append(sb_scope, build_gsi_link_dev("GSIG", 0x16, 0x16));
    aml_append(sb_scope, build_gsi_link_dev("GSIH", 0x17, 0x17));

    aml_append(table, sb_scope);
}

static Aml *build_q35_dram_controller(const AcpiMcfgInfo *mcfg)
{
    Aml *dev;
    Aml *resource_template;

    /* DRAM controller */
    dev = aml_device("DRAC");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0C01")));

    resource_template = aml_resource_template();
    if (mcfg->base + mcfg->size - 1 >= (1ULL << 32)) {
        aml_append(resource_template,
                   aml_qword_memory(AML_POS_DECODE,
                                    AML_MIN_FIXED,
                                    AML_MAX_FIXED,
                                    AML_NON_CACHEABLE,
                                    AML_READ_WRITE,
                                    0x0000000000000000,
                                    mcfg->base,
                                    mcfg->base + mcfg->size - 1,
                                    0x0000000000000000,
                                    mcfg->size));
    } else {
        aml_append(resource_template,
                   aml_dword_memory(AML_POS_DECODE,
                                    AML_MIN_FIXED,
                                    AML_MAX_FIXED,
                                    AML_NON_CACHEABLE,
                                    AML_READ_WRITE,
                                    0x0000000000000000,
                                    mcfg->base,
                                    mcfg->base + mcfg->size - 1,
                                    0x0000000000000000,
                                    mcfg->size));
    }
    aml_append(dev, aml_name_decl("_CRS", resource_template));

    return dev;
}

static void build_q35_isa_bridge(Aml *table)
{
    Aml *dev;
    Aml *scope;

    scope =  aml_scope("_SB.PCI0");
    dev = aml_device("ISA");
    aml_append(dev, aml_name_decl("_ADR", aml_int(0x001F0000)));

    /* ICH9 PCI to ISA irq remapping */
    aml_append(dev, aml_operation_region("PIRQ", AML_PCI_CONFIG,
                                         aml_int(0x60), 0x0C));

    aml_append(scope, dev);
    aml_append(table, scope);
}

static void build_piix4_isa_bridge(Aml *table)
{
    Aml *dev;
    Aml *scope;

    scope =  aml_scope("_SB.PCI0");
    dev = aml_device("ISA");
    aml_append(dev, aml_name_decl("_ADR", aml_int(0x00010000)));

    /* PIIX PCI to ISA irq remapping */
    aml_append(dev, aml_operation_region("P40C", AML_PCI_CONFIG,
                                         aml_int(0x60), 0x04));

    aml_append(scope, dev);
    aml_append(table, scope);
}

static void build_x86_acpi_pci_hotplug(Aml *table, uint64_t pcihp_addr)
{
    Aml *scope;
    Aml *field;
    Aml *method;

    scope =  aml_scope("_SB.PCI0");

    aml_append(scope,
        aml_operation_region("PCST", AML_SYSTEM_IO, aml_int(pcihp_addr), 0x08));
    field = aml_field("PCST", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("PCIU", 32));
    aml_append(field, aml_named_field("PCID", 32));
    aml_append(scope, field);

    aml_append(scope,
        aml_operation_region("SEJ", AML_SYSTEM_IO,
                             aml_int(pcihp_addr + ACPI_PCIHP_SEJ_BASE), 0x04));
    field = aml_field("SEJ", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("B0EJ", 32));
    aml_append(scope, field);

    aml_append(scope,
        aml_operation_region("BNMR", AML_SYSTEM_IO,
                             aml_int(pcihp_addr + ACPI_PCIHP_BNMR_BASE), 0x08));
    field = aml_field("BNMR", AML_DWORD_ACC, AML_NOLOCK, AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field("BNUM", 32));
    aml_append(field, aml_named_field("PIDX", 32));
    aml_append(scope, field);

    aml_append(scope, aml_mutex("BLCK", 0));

    method = aml_method("PCEJ", 2, AML_NOTSERIALIZED);
    aml_append(method, aml_acquire(aml_name("BLCK"), 0xFFFF));
    aml_append(method, aml_store(aml_arg(0), aml_name("BNUM")));
    aml_append(method,
        aml_store(aml_shiftleft(aml_int(1), aml_arg(1)), aml_name("B0EJ")));
    aml_append(method, aml_release(aml_name("BLCK")));
    aml_append(method, aml_return(aml_int(0)));
    aml_append(scope, method);

    method = aml_method("AIDX", 2, AML_NOTSERIALIZED);
    aml_append(method, aml_acquire(aml_name("BLCK"), 0xFFFF));
    aml_append(method, aml_store(aml_arg(0), aml_name("BNUM")));
    aml_append(method,
        aml_store(aml_shiftleft(aml_int(1), aml_arg(1)), aml_name("PIDX")));
    aml_append(method, aml_store(aml_name("PIDX"), aml_local(0)));
    aml_append(method, aml_release(aml_name("BLCK")));
    aml_append(method, aml_return(aml_local(0)));
    aml_append(scope, method);

    aml_append(scope, aml_pci_device_dsm());

    aml_append(table, scope);
}

static Aml *build_q35_osc_method(bool enable_native_pcie_hotplug)
{
    Aml *if_ctx;
    Aml *if_ctx2;
    Aml *else_ctx;
    Aml *method;
    Aml *a_cwd1 = aml_name("CDW1");
    Aml *a_ctrl = aml_local(0);

    method = aml_method("_OSC", 4, AML_NOTSERIALIZED);
    aml_append(method, aml_create_dword_field(aml_arg(3), aml_int(0), "CDW1"));

    if_ctx = aml_if(aml_equal(
        aml_arg(0), aml_touuid("33DB4D5B-1FF7-401C-9657-7441C03DD766")));
    aml_append(if_ctx, aml_create_dword_field(aml_arg(3), aml_int(4), "CDW2"));
    aml_append(if_ctx, aml_create_dword_field(aml_arg(3), aml_int(8), "CDW3"));

    aml_append(if_ctx, aml_store(aml_name("CDW3"), a_ctrl));

    /*
     * Always allow native PME, AER (no dependencies)
     * Allow SHPC (PCI bridges can have SHPC controller)
     * Disable PCIe Native Hot-plug if ACPI PCI Hot-plug is enabled.
     */
    aml_append(if_ctx, aml_and(a_ctrl,
        aml_int(0x1E | (enable_native_pcie_hotplug ? 0x1 : 0x0)), a_ctrl));

    if_ctx2 = aml_if(aml_lnot(aml_equal(aml_arg(1), aml_int(1))));
    /* Unknown revision */
    aml_append(if_ctx2, aml_or(a_cwd1, aml_int(0x08), a_cwd1));
    aml_append(if_ctx, if_ctx2);

    if_ctx2 = aml_if(aml_lnot(aml_equal(aml_name("CDW3"), a_ctrl)));
    /* Capabilities bits were masked */
    aml_append(if_ctx2, aml_or(a_cwd1, aml_int(0x10), a_cwd1));
    aml_append(if_ctx, if_ctx2);

    /* Update DWORD3 in the buffer */
    aml_append(if_ctx, aml_store(a_ctrl, aml_name("CDW3")));
    aml_append(method, if_ctx);

    else_ctx = aml_else();
    /* Unrecognized UUID */
    aml_append(else_ctx, aml_or(a_cwd1, aml_int(4), a_cwd1));
    aml_append(method, else_ctx);

    aml_append(method, aml_return(aml_arg(3)));
    return method;
}

static void build_smb0(Aml *table, I2CBus *smbus, int devnr, int func)
{
    Aml *scope = aml_scope("_SB.PCI0");
    Aml *dev = aml_device("SMB0");

    aml_append(dev, aml_name_decl("_ADR", aml_int(devnr << 16 | func)));
    build_acpi_ipmi_devices(dev, BUS(smbus), "\\_SB.PCI0.SMB0");
    aml_append(scope, dev);
    aml_append(table, scope);
}

static void
build_dsdt(GArray *table_data, BIOSLinker *linker,
           AcpiPmInfo *pm, AcpiMiscInfo *misc,
           Range *pci_hole, Range *pci_hole64, MachineState *machine)
{
    CrsRangeEntry *entry;
    Aml *dsdt, *sb_scope, *scope, *dev, *method, *field, *pkg, *crs;
    CrsRangeSet crs_range_set;
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(machine);
    X86MachineState *x86ms = X86_MACHINE(machine);
    AcpiMcfgInfo mcfg;
    bool mcfg_valid = !!acpi_get_mcfg(&mcfg);
    uint32_t nr_mem = machine->ram_slots;
    int root_bus_limit = 0xFF;
    PCIBus *bus = NULL;
#ifdef CONFIG_TPM
    TPMIf *tpm = tpm_find();
#endif
    int i;
    VMBusBridge *vmbus_bridge = vmbus_bridge_find();
    AcpiTable table = { .sig = "DSDT", .rev = 1, .oem_id = x86ms->oem_id,
                        .oem_table_id = x86ms->oem_table_id };

    acpi_table_begin(&table, table_data);
    dsdt = init_aml_allocator();

    build_dbg_aml(dsdt);
    if (misc->is_piix4) {
        sb_scope = aml_scope("_SB");
        dev = aml_device("PCI0");
        aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A03")));
        aml_append(dev, aml_name_decl("_ADR", aml_int(0)));
        aml_append(dev, aml_name_decl("_UID", aml_int(pcmc->pci_root_uid)));
        aml_append(sb_scope, dev);
        aml_append(dsdt, sb_scope);

        if (misc->has_hpet) {
            build_hpet_aml(dsdt);
        }
        build_piix4_isa_bridge(dsdt);
        build_isa_devices_aml(dsdt);
        if (pm->pcihp_bridge_en || pm->pcihp_root_en) {
            build_x86_acpi_pci_hotplug(dsdt, pm->pcihp_io_base);
        }
        build_piix4_pci0_int(dsdt);
    } else {
        sb_scope = aml_scope("_SB");
        dev = aml_device("PCI0");
        aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A08")));
        aml_append(dev, aml_name_decl("_CID", aml_eisaid("PNP0A03")));
        aml_append(dev, aml_name_decl("_ADR", aml_int(0)));
        aml_append(dev, aml_name_decl("_UID", aml_int(pcmc->pci_root_uid)));
        aml_append(dev, build_q35_osc_method(!pm->pcihp_bridge_en));
        aml_append(sb_scope, dev);
        if (mcfg_valid) {
            aml_append(sb_scope, build_q35_dram_controller(&mcfg));
        }

        if (pm->smi_on_cpuhp) {
            /* reserve SMI block resources, IO ports 0xB2, 0xB3 */
            dev = aml_device("PCI0.SMI0");
            aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A06")));
            aml_append(dev, aml_name_decl("_UID", aml_string("SMI resources")));
            crs = aml_resource_template();
            aml_append(crs,
                aml_io(
                       AML_DECODE16,
                       ACPI_PORT_SMI_CMD,
                       ACPI_PORT_SMI_CMD,
                       1,
                       2)
            );
            aml_append(dev, aml_name_decl("_CRS", crs));
            aml_append(dev, aml_operation_region("SMIR", AML_SYSTEM_IO,
                aml_int(ACPI_PORT_SMI_CMD), 2));
            field = aml_field("SMIR", AML_BYTE_ACC, AML_NOLOCK,
                              AML_WRITE_AS_ZEROS);
            aml_append(field, aml_named_field("SMIC", 8));
            aml_append(field, aml_reserved_field(8));
            aml_append(dev, field);
            aml_append(sb_scope, dev);
        }

        aml_append(dsdt, sb_scope);

        if (misc->has_hpet) {
            build_hpet_aml(dsdt);
        }
        build_q35_isa_bridge(dsdt);
        build_isa_devices_aml(dsdt);
        if (pm->pcihp_bridge_en) {
            build_x86_acpi_pci_hotplug(dsdt, pm->pcihp_io_base);
        }
        build_q35_pci0_int(dsdt);
        if (pcms->smbus && !pcmc->do_not_add_smb_acpi) {
            build_smb0(dsdt, pcms->smbus, ICH9_SMB_DEV, ICH9_SMB_FUNC);
        }
    }

    if (vmbus_bridge) {
        sb_scope = aml_scope("_SB");
        aml_append(sb_scope, build_vmbus_device_aml(vmbus_bridge));
        aml_append(dsdt, sb_scope);
    }

    if (pcmc->legacy_cpu_hotplug) {
        build_legacy_cpu_hotplug_aml(dsdt, machine, pm->cpu_hp_io_base);
    } else {
        CPUHotplugFeatures opts = {
            .acpi_1_compatible = true, .has_legacy_cphp = true,
            .smi_path = pm->smi_on_cpuhp ? "\\_SB.PCI0.SMI0.SMIC" : NULL,
            .fw_unplugs_cpu = pm->smi_on_cpu_unplug,
        };
        build_cpus_aml(dsdt, machine, opts, pm->cpu_hp_io_base,
                       "\\_SB.PCI0", "\\_GPE._E02");
    }

    if (pcms->memhp_io_base && nr_mem) {
        build_memory_hotplug_aml(dsdt, nr_mem, "\\_SB.PCI0",
                                 "\\_GPE._E03", AML_SYSTEM_IO,
                                 pcms->memhp_io_base);
    }

    scope =  aml_scope("_GPE");
    {
        aml_append(scope, aml_name_decl("_HID", aml_string("ACPI0006")));

        if (pm->pcihp_bridge_en || pm->pcihp_root_en) {
            method = aml_method("_E01", 0, AML_NOTSERIALIZED);
            aml_append(method,
                aml_acquire(aml_name("\\_SB.PCI0.BLCK"), 0xFFFF));
            aml_append(method, aml_call0("\\_SB.PCI0.PCNT"));
            aml_append(method, aml_release(aml_name("\\_SB.PCI0.BLCK")));
            aml_append(scope, method);
        }

        if (machine->nvdimms_state->is_enabled) {
            method = aml_method("_E04", 0, AML_NOTSERIALIZED);
            aml_append(method, aml_notify(aml_name("\\_SB.NVDR"),
                                          aml_int(0x80)));
            aml_append(scope, method);
        }
    }
    aml_append(dsdt, scope);

    crs_range_set_init(&crs_range_set);
    bus = PC_MACHINE(machine)->bus;
    if (bus) {
        QLIST_FOREACH(bus, &bus->child, sibling) {
            uint8_t bus_num = pci_bus_num(bus);
            uint8_t numa_node = pci_bus_numa_node(bus);

            /* look only for expander root buses */
            if (!pci_bus_is_root(bus)) {
                continue;
            }

            if (bus_num < root_bus_limit) {
                root_bus_limit = bus_num - 1;
            }

            scope = aml_scope("\\_SB");
            dev = aml_device("PC%.02X", bus_num);
            aml_append(dev, aml_name_decl("_UID", aml_int(bus_num)));
            aml_append(dev, aml_name_decl("_BBN", aml_int(bus_num)));
            if (pci_bus_is_express(bus)) {
                aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A08")));
                aml_append(dev, aml_name_decl("_CID", aml_eisaid("PNP0A03")));

                /* Expander bridges do not have ACPI PCI Hot-plug enabled */
                aml_append(dev, build_q35_osc_method(true));
            } else {
                aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A03")));
            }

            if (numa_node != NUMA_NODE_UNASSIGNED) {
                aml_append(dev, aml_name_decl("_PXM", aml_int(numa_node)));
            }

            aml_append(dev, build_prt(false));
            crs = build_crs(PCI_HOST_BRIDGE(BUS(bus)->parent), &crs_range_set,
                            0, 0, 0, 0);
            aml_append(dev, aml_name_decl("_CRS", crs));
            aml_append(scope, dev);
            aml_append(dsdt, scope);
        }
    }

    /*
     * At this point crs_range_set has all the ranges used by pci
     * busses *other* than PCI0.  These ranges will be excluded from
     * the PCI0._CRS.  Add mmconfig to the set so it will be excluded
     * too.
     */
    if (mcfg_valid) {
        crs_range_insert(crs_range_set.mem_ranges,
                         mcfg.base, mcfg.base + mcfg.size - 1);
    }

    scope = aml_scope("\\_SB.PCI0");
    /* build PCI0._CRS */
    crs = aml_resource_template();
    aml_append(crs,
        aml_word_bus_number(AML_MIN_FIXED, AML_MAX_FIXED, AML_POS_DECODE,
                            0x0000, 0x0, root_bus_limit,
                            0x0000, root_bus_limit + 1));
    aml_append(crs, aml_io(AML_DECODE16, 0x0CF8, 0x0CF8, 0x01, 0x08));

    aml_append(crs,
        aml_word_io(AML_MIN_FIXED, AML_MAX_FIXED,
                    AML_POS_DECODE, AML_ENTIRE_RANGE,
                    0x0000, 0x0000, 0x0CF7, 0x0000, 0x0CF8));

    crs_replace_with_free_ranges(crs_range_set.io_ranges, 0x0D00, 0xFFFF);
    for (i = 0; i < crs_range_set.io_ranges->len; i++) {
        entry = g_ptr_array_index(crs_range_set.io_ranges, i);
        aml_append(crs,
            aml_word_io(AML_MIN_FIXED, AML_MAX_FIXED,
                        AML_POS_DECODE, AML_ENTIRE_RANGE,
                        0x0000, entry->base, entry->limit,
                        0x0000, entry->limit - entry->base + 1));
    }

    aml_append(crs,
        aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                         AML_CACHEABLE, AML_READ_WRITE,
                         0, 0x000A0000, 0x000BFFFF, 0, 0x00020000));

    crs_replace_with_free_ranges(crs_range_set.mem_ranges,
                                 range_lob(pci_hole),
                                 range_upb(pci_hole));
    for (i = 0; i < crs_range_set.mem_ranges->len; i++) {
        entry = g_ptr_array_index(crs_range_set.mem_ranges, i);
        aml_append(crs,
            aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                             AML_NON_CACHEABLE, AML_READ_WRITE,
                             0, entry->base, entry->limit,
                             0, entry->limit - entry->base + 1));
    }

    if (!range_is_empty(pci_hole64)) {
        crs_replace_with_free_ranges(crs_range_set.mem_64bit_ranges,
                                     range_lob(pci_hole64),
                                     range_upb(pci_hole64));
        for (i = 0; i < crs_range_set.mem_64bit_ranges->len; i++) {
            entry = g_ptr_array_index(crs_range_set.mem_64bit_ranges, i);
            aml_append(crs,
                       aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                        AML_MAX_FIXED,
                                        AML_CACHEABLE, AML_READ_WRITE,
                                        0, entry->base, entry->limit,
                                        0, entry->limit - entry->base + 1));
        }
    }

#ifdef CONFIG_TPM
    if (TPM_IS_TIS_ISA(tpm_find())) {
        aml_append(crs, aml_memory32_fixed(TPM_TIS_ADDR_BASE,
                   TPM_TIS_ADDR_SIZE, AML_READ_WRITE));
    }
#endif
    aml_append(scope, aml_name_decl("_CRS", crs));

    /* reserve GPE0 block resources */
    dev = aml_device("GPE0");
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A06")));
    aml_append(dev, aml_name_decl("_UID", aml_string("GPE0 resources")));
    /* device present, functioning, decoding, not shown in UI */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));
    crs = aml_resource_template();
    aml_append(crs,
        aml_io(
               AML_DECODE16,
               pm->fadt.gpe0_blk.address,
               pm->fadt.gpe0_blk.address,
               1,
               pm->fadt.gpe0_blk.bit_width / 8)
    );
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);

    crs_range_set_free(&crs_range_set);

    /* reserve PCIHP resources */
    if (pm->pcihp_io_len && (pm->pcihp_bridge_en || pm->pcihp_root_en)) {
        dev = aml_device("PHPR");
        aml_append(dev, aml_name_decl("_HID", aml_string("PNP0A06")));
        aml_append(dev,
            aml_name_decl("_UID", aml_string("PCI Hotplug resources")));
        /* device present, functioning, decoding, not shown in UI */
        aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));
        crs = aml_resource_template();
        aml_append(crs,
            aml_io(AML_DECODE16, pm->pcihp_io_base, pm->pcihp_io_base, 1,
                   pm->pcihp_io_len)
        );
        aml_append(dev, aml_name_decl("_CRS", crs));
        aml_append(scope, dev);
    }
    aml_append(dsdt, scope);

    /*  create S3_ / S4_ / S5_ packages if necessary */
    scope = aml_scope("\\");
    if (!pm->s3_disabled) {
        pkg = aml_package(4);
        aml_append(pkg, aml_int(1)); /* PM1a_CNT.SLP_TYP */
        aml_append(pkg, aml_int(1)); /* PM1b_CNT.SLP_TYP, FIXME: not impl. */
        aml_append(pkg, aml_int(0)); /* reserved */
        aml_append(pkg, aml_int(0)); /* reserved */
        aml_append(scope, aml_name_decl("_S3", pkg));
    }

    if (!pm->s4_disabled) {
        pkg = aml_package(4);
        aml_append(pkg, aml_int(pm->s4_val)); /* PM1a_CNT.SLP_TYP */
        /* PM1b_CNT.SLP_TYP, FIXME: not impl. */
        aml_append(pkg, aml_int(pm->s4_val));
        aml_append(pkg, aml_int(0)); /* reserved */
        aml_append(pkg, aml_int(0)); /* reserved */
        aml_append(scope, aml_name_decl("_S4", pkg));
    }

    pkg = aml_package(4);
    aml_append(pkg, aml_int(0)); /* PM1a_CNT.SLP_TYP */
    aml_append(pkg, aml_int(0)); /* PM1b_CNT.SLP_TYP not impl. */
    aml_append(pkg, aml_int(0)); /* reserved */
    aml_append(pkg, aml_int(0)); /* reserved */
    aml_append(scope, aml_name_decl("_S5", pkg));
    aml_append(dsdt, scope);

    /* create fw_cfg node, unconditionally */
    {
        scope = aml_scope("\\_SB.PCI0");
        fw_cfg_add_acpi_dsdt(scope, x86ms->fw_cfg);
        aml_append(dsdt, scope);
    }

    if (misc->applesmc_io_base) {
        scope = aml_scope("\\_SB.PCI0.ISA");
        dev = aml_device("SMC");

        aml_append(dev, aml_name_decl("_HID", aml_eisaid("APP0001")));
        /* device present, functioning, decoding, not shown in UI */
        aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));

        crs = aml_resource_template();
        aml_append(crs,
            aml_io(AML_DECODE16, misc->applesmc_io_base, misc->applesmc_io_base,
                   0x01, APPLESMC_MAX_DATA_LENGTH)
        );
        aml_append(crs, aml_irq_no_flags(6));
        aml_append(dev, aml_name_decl("_CRS", crs));

        aml_append(scope, dev);
        aml_append(dsdt, scope);
    }

    if (misc->pvpanic_port) {
        scope = aml_scope("\\_SB.PCI0.ISA");

        dev = aml_device("PEVT");
        aml_append(dev, aml_name_decl("_HID", aml_string("QEMU0001")));

        crs = aml_resource_template();
        aml_append(crs,
            aml_io(AML_DECODE16, misc->pvpanic_port, misc->pvpanic_port, 1, 1)
        );
        aml_append(dev, aml_name_decl("_CRS", crs));

        aml_append(dev, aml_operation_region("PEOR", AML_SYSTEM_IO,
                                              aml_int(misc->pvpanic_port), 1));
        field = aml_field("PEOR", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
        aml_append(field, aml_named_field("PEPT", 8));
        aml_append(dev, field);

        /* device present, functioning, decoding, shown in UI */
        aml_append(dev, aml_name_decl("_STA", aml_int(0xF)));

        method = aml_method("RDPT", 0, AML_NOTSERIALIZED);
        aml_append(method, aml_store(aml_name("PEPT"), aml_local(0)));
        aml_append(method, aml_return(aml_local(0)));
        aml_append(dev, method);

        method = aml_method("WRPT", 1, AML_NOTSERIALIZED);
        aml_append(method, aml_store(aml_arg(0), aml_name("PEPT")));
        aml_append(dev, method);

        aml_append(scope, dev);
        aml_append(dsdt, scope);
    }

    sb_scope = aml_scope("\\_SB");
    {
        Object *pci_host;
        PCIBus *bus = NULL;

        pci_host = acpi_get_i386_pci_host();

        if (pci_host) {
            bus = PCI_HOST_BRIDGE(pci_host)->bus;
        }

        if (bus) {
            Aml *scope = aml_scope("PCI0");
            /* Scan all PCI buses. Generate tables to support hotplug. */
            build_append_pci_bus_devices(scope, bus, pm->pcihp_bridge_en);

#ifdef CONFIG_TPM
            if (TPM_IS_TIS_ISA(tpm)) {
                if (misc->tpm_version == TPM_VERSION_2_0) {
                    dev = aml_device("TPM");
                    aml_append(dev, aml_name_decl("_HID",
                                                  aml_string("MSFT0101")));
                    aml_append(dev,
                               aml_name_decl("_STR",
                                             aml_string("TPM 2.0 Device")));
                } else {
                    dev = aml_device("ISA.TPM");
                    aml_append(dev, aml_name_decl("_HID",
                                                  aml_eisaid("PNP0C31")));
                }
                aml_append(dev, aml_name_decl("_UID", aml_int(1)));

                aml_append(dev, aml_name_decl("_STA", aml_int(0xF)));
                crs = aml_resource_template();
                aml_append(crs, aml_memory32_fixed(TPM_TIS_ADDR_BASE,
                           TPM_TIS_ADDR_SIZE, AML_READ_WRITE));
                /*
                    FIXME: TPM_TIS_IRQ=5 conflicts with PNP0C0F irqs,
                    Rewrite to take IRQ from TPM device model and
                    fix default IRQ value there to use some unused IRQ
                 */
                /* aml_append(crs, aml_irq_no_flags(TPM_TIS_IRQ)); */
                aml_append(dev, aml_name_decl("_CRS", crs));

                tpm_build_ppi_acpi(tpm, dev);

                aml_append(scope, dev);
            }
#endif

            aml_append(sb_scope, scope);
        }
    }

#ifdef CONFIG_TPM
    if (TPM_IS_CRB(tpm)) {
        dev = aml_device("TPM");
        aml_append(dev, aml_name_decl("_HID", aml_string("MSFT0101")));
        aml_append(dev, aml_name_decl("_STR",
                                      aml_string("TPM 2.0 Device")));
        crs = aml_resource_template();
        aml_append(crs, aml_memory32_fixed(TPM_CRB_ADDR_BASE,
                                           TPM_CRB_ADDR_SIZE, AML_READ_WRITE));
        aml_append(dev, aml_name_decl("_CRS", crs));

        aml_append(dev, aml_name_decl("_STA", aml_int(0xf)));
        aml_append(dev, aml_name_decl("_UID", aml_int(1)));

        tpm_build_ppi_acpi(tpm, dev);

        aml_append(sb_scope, dev);
    }
#endif

    if (pcms->sgx_epc.size != 0) {
        uint64_t epc_base = pcms->sgx_epc.base;
        uint64_t epc_size = pcms->sgx_epc.size;

        dev = aml_device("EPC");
        aml_append(dev, aml_name_decl("_HID", aml_eisaid("INT0E0C")));
        aml_append(dev, aml_name_decl("_STR",
                                      aml_unicode("Enclave Page Cache 1.0")));
        crs = aml_resource_template();
        aml_append(crs,
                   aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                    AML_MAX_FIXED, AML_NON_CACHEABLE,
                                    AML_READ_WRITE, 0, epc_base,
                                    epc_base + epc_size - 1, 0, epc_size));
        aml_append(dev, aml_name_decl("_CRS", crs));

        method = aml_method("_STA", 0, AML_NOTSERIALIZED);
        aml_append(method, aml_return(aml_int(0x0f)));
        aml_append(dev, method);

        aml_append(sb_scope, dev);
    }
    aml_append(dsdt, sb_scope);

    /* copy AML table into ACPI tables blob and patch header there */
    g_array_append_vals(table_data, dsdt->buf->data, dsdt->buf->len);
    acpi_table_end(linker, &table);
    free_aml_allocator();
}

/*
 * IA-PC HPET (High Precision Event Timers) Specification (Revision: 1.0a)
 * 3.2.4The ACPI 2.0 HPET Description Table (HPET)
 */
static void
build_hpet(GArray *table_data, BIOSLinker *linker, const char *oem_id,
           const char *oem_table_id)
{
    AcpiTable table = { .sig = "HPET", .rev = 1,
                        .oem_id = oem_id, .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);
    /* Note timer_block_id value must be kept in sync with value advertised by
     * emulated hpet
     */
    /* Event Timer Block ID */
    build_append_int_noprefix(table_data, 0x8086a201, 4);
    /* BASE_ADDRESS */
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0, 0, 0, HPET_BASE);
    /* HPET Number */
    build_append_int_noprefix(table_data, 0, 1);
    /* Main Counter Minimum Clock_tick in Periodic Mode */
    build_append_int_noprefix(table_data, 0, 2);
    /* Page Protection And OEM Attribute */
    build_append_int_noprefix(table_data, 0, 1);
    acpi_table_end(linker, &table);
}

#ifdef CONFIG_TPM
/*
 * TCPA Description Table
 *
 * Following Level 00, Rev 00.37 of specs:
 * http://www.trustedcomputinggroup.org/resources/tcg_acpi_specification
 * 7.1.2 ACPI Table Layout
 */
static void
build_tpm_tcpa(GArray *table_data, BIOSLinker *linker, GArray *tcpalog,
               const char *oem_id, const char *oem_table_id)
{
    unsigned log_addr_offset;
    AcpiTable table = { .sig = "TCPA", .rev = 2,
                        .oem_id = oem_id, .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);
    /* Platform Class */
    build_append_int_noprefix(table_data, TPM_TCPA_ACPI_CLASS_CLIENT, 2);
    /* Log Area Minimum Length (LAML) */
    build_append_int_noprefix(table_data, TPM_LOG_AREA_MINIMUM_SIZE, 4);
    /* Log Area Start Address (LASA) */
    log_addr_offset = table_data->len;
    build_append_int_noprefix(table_data, 0, 8);

    /* allocate/reserve space for TPM log area */
    acpi_data_push(tcpalog, TPM_LOG_AREA_MINIMUM_SIZE);
    bios_linker_loader_alloc(linker, ACPI_BUILD_TPMLOG_FILE, tcpalog, 1,
                             false /* high memory */);
    /* log area start address to be filled by Guest linker */
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
        log_addr_offset, 8, ACPI_BUILD_TPMLOG_FILE, 0);

    acpi_table_end(linker, &table);
}
#endif

#define HOLE_640K_START  (640 * KiB)
#define HOLE_640K_END   (1 * MiB)

/*
 * ACPI spec, Revision 3.0
 * 5.2.15 System Resource Affinity Table (SRAT)
 */
static void
build_srat(GArray *table_data, BIOSLinker *linker, MachineState *machine)
{
    int i;
    int numa_mem_start, slots;
    uint64_t mem_len, mem_base, next_base;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    X86MachineState *x86ms = X86_MACHINE(machine);
    const CPUArchIdList *apic_ids = mc->possible_cpu_arch_ids(machine);
    PCMachineState *pcms = PC_MACHINE(machine);
    int nb_numa_nodes = machine->numa_state->num_nodes;
    NodeInfo *numa_info = machine->numa_state->nodes;
    ram_addr_t hotpluggable_address_space_size =
        object_property_get_int(OBJECT(pcms), PC_MACHINE_DEVMEM_REGION_SIZE,
                                NULL);
    AcpiTable table = { .sig = "SRAT", .rev = 1, .oem_id = x86ms->oem_id,
                        .oem_table_id = x86ms->oem_table_id };

    acpi_table_begin(&table, table_data);
    build_append_int_noprefix(table_data, 1, 4); /* Reserved */
    build_append_int_noprefix(table_data, 0, 8); /* Reserved */

    for (i = 0; i < apic_ids->len; i++) {
        int node_id = apic_ids->cpus[i].props.node_id;
        uint32_t apic_id = apic_ids->cpus[i].arch_id;

        if (apic_id < 255) {
            /* 5.2.15.1 Processor Local APIC/SAPIC Affinity Structure */
            build_append_int_noprefix(table_data, 0, 1);  /* Type  */
            build_append_int_noprefix(table_data, 16, 1); /* Length */
            /* Proximity Domain [7:0] */
            build_append_int_noprefix(table_data, node_id, 1);
            build_append_int_noprefix(table_data, apic_id, 1); /* APIC ID */
            /* Flags, Table 5-36 */
            build_append_int_noprefix(table_data, 1, 4);
            build_append_int_noprefix(table_data, 0, 1); /* Local SAPIC EID */
            /* Proximity Domain [31:8] */
            build_append_int_noprefix(table_data, 0, 3);
            build_append_int_noprefix(table_data, 0, 4); /* Reserved */
        } else {
            /*
             * ACPI spec, Revision 4.0
             * 5.2.16.3 Processor Local x2APIC Affinity Structure
             */
            build_append_int_noprefix(table_data, 2, 1);  /* Type  */
            build_append_int_noprefix(table_data, 24, 1); /* Length */
            build_append_int_noprefix(table_data, 0, 2); /* Reserved */
            /* Proximity Domain */
            build_append_int_noprefix(table_data, node_id, 4);
            build_append_int_noprefix(table_data, apic_id, 4); /* X2APIC ID */
            /* Flags, Table 5-39 */
            build_append_int_noprefix(table_data, 1 /* Enabled */, 4);
            build_append_int_noprefix(table_data, 0, 4); /* Clock Domain */
            build_append_int_noprefix(table_data, 0, 4); /* Reserved */
        }
    }

    /* the memory map is a bit tricky, it contains at least one hole
     * from 640k-1M and possibly another one from 3.5G-4G.
     */
    next_base = 0;
    numa_mem_start = table_data->len;

    for (i = 1; i < nb_numa_nodes + 1; ++i) {
        mem_base = next_base;
        mem_len = numa_info[i - 1].node_mem;
        next_base = mem_base + mem_len;

        /* Cut out the 640K hole */
        if (mem_base <= HOLE_640K_START &&
            next_base > HOLE_640K_START) {
            mem_len -= next_base - HOLE_640K_START;
            if (mem_len > 0) {
                build_srat_memory(table_data, mem_base, mem_len, i - 1,
                                  MEM_AFFINITY_ENABLED);
            }

            /* Check for the rare case: 640K < RAM < 1M */
            if (next_base <= HOLE_640K_END) {
                next_base = HOLE_640K_END;
                continue;
            }
            mem_base = HOLE_640K_END;
            mem_len = next_base - HOLE_640K_END;
        }

        /* Cut out the ACPI_PCI hole */
        if (mem_base <= x86ms->below_4g_mem_size &&
            next_base > x86ms->below_4g_mem_size) {
            mem_len -= next_base - x86ms->below_4g_mem_size;
            if (mem_len > 0) {
                build_srat_memory(table_data, mem_base, mem_len, i - 1,
                                  MEM_AFFINITY_ENABLED);
            }
            mem_base = 1ULL << 32;
            mem_len = next_base - x86ms->below_4g_mem_size;
            next_base = mem_base + mem_len;
        }

        if (mem_len > 0) {
            build_srat_memory(table_data, mem_base, mem_len, i - 1,
                              MEM_AFFINITY_ENABLED);
        }
    }

    if (machine->nvdimms_state->is_enabled) {
        nvdimm_build_srat(table_data);
    }

    sgx_epc_build_srat(table_data);

    /*
     * TODO: this part is not in ACPI spec and current linux kernel boots fine
     * without these entries. But I recall there were issues the last time I
     * tried to remove it with some ancient guest OS, however I can't remember
     * what that was so keep this around for now
     */
    slots = (table_data->len - numa_mem_start) / 40 /* mem affinity len */;
    for (; slots < nb_numa_nodes + 2; slots++) {
        build_srat_memory(table_data, 0, 0, 0, MEM_AFFINITY_NOFLAGS);
    }

    /*
     * Entry is required for Windows to enable memory hotplug in OS
     * and for Linux to enable SWIOTLB when booted with less than
     * 4G of RAM. Windows works better if the entry sets proximity
     * to the highest NUMA node in the machine.
     * Memory devices may override proximity set by this entry,
     * providing _PXM method if necessary.
     */
    if (hotpluggable_address_space_size) {
        build_srat_memory(table_data, machine->device_memory->base,
                          hotpluggable_address_space_size, nb_numa_nodes - 1,
                          MEM_AFFINITY_HOTPLUGGABLE | MEM_AFFINITY_ENABLED);
    }

    acpi_table_end(linker, &table);
}

/*
 * Insert DMAR scope for PCI bridges and endpoint devcie
 */
static void
insert_scope(PCIBus *bus, PCIDevice *dev, void *opaque)
{
    const size_t device_scope_size = 6 /* device scope structure */ +
                                     2 /* 1 path entry */;
    GArray *scope_blob = opaque;

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
        /* Dmar Scope Type: 0x02 for PCI Bridge */
        build_append_int_noprefix(scope_blob, 0x02, 1);
    } else {
        /* Dmar Scope Type: 0x01 for PCI Endpoint Device */
        build_append_int_noprefix(scope_blob, 0x01, 1);
    }

    /* length */
    build_append_int_noprefix(scope_blob, device_scope_size, 1);
    /* reserved */
    build_append_int_noprefix(scope_blob, 0, 2);
    /* enumeration_id */
    build_append_int_noprefix(scope_blob, 0, 1);
    /* bus */
    build_append_int_noprefix(scope_blob, pci_bus_num(bus), 1);
    /* device */
    build_append_int_noprefix(scope_blob, PCI_SLOT(dev->devfn), 1);
    /* function */
    build_append_int_noprefix(scope_blob, PCI_FUNC(dev->devfn), 1);
}

/* For a given PCI host bridge, walk and insert DMAR scope */
static int
dmar_host_bridges(Object *obj, void *opaque)
{
    GArray *scope_blob = opaque;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            pci_for_each_device_under_bus(bus, insert_scope, scope_blob);
        }
    }

    return 0;
}

/*
 * Intel ® Virtualization Technology for Directed I/O
 * Architecture Specification. Revision 3.3
 * 8.1 DMA Remapping Reporting Structure
 */
static void
build_dmar_q35(GArray *table_data, BIOSLinker *linker, const char *oem_id,
               const char *oem_table_id)
{
    uint8_t dmar_flags = 0;
    uint8_t rsvd10[10] = {};
    /* Root complex IOAPIC uses one path only */
    const size_t ioapic_scope_size = 6 /* device scope structure */ +
                                     2 /* 1 path entry */;
    X86IOMMUState *iommu = x86_iommu_get_default();
    IntelIOMMUState *intel_iommu = INTEL_IOMMU_DEVICE(iommu);
    GArray *scope_blob = g_array_new(false, true, 1);

    AcpiTable table = { .sig = "DMAR", .rev = 1, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };

    /*
     * A PCI bus walk, for each PCI host bridge.
     * Insert scope for each PCI bridge and endpoint device which
     * is attached to a bus with iommu enabled.
     */
    object_child_foreach_recursive(object_get_root(),
                                   dmar_host_bridges, scope_blob);

    assert(iommu);
    if (x86_iommu_ir_supported(iommu)) {
        dmar_flags |= 0x1;      /* Flags: 0x1: INT_REMAP */
    }

    acpi_table_begin(&table, table_data);
    /* Host Address Width */
    build_append_int_noprefix(table_data, intel_iommu->aw_bits - 1, 1);
    build_append_int_noprefix(table_data, dmar_flags, 1); /* Flags */
    g_array_append_vals(table_data, rsvd10, sizeof(rsvd10)); /* Reserved */

    /* 8.3 DMAR Remapping Hardware Unit Definition structure */
    build_append_int_noprefix(table_data, 0, 2); /* Type */
    /* Length */
    build_append_int_noprefix(table_data,
                              16 + ioapic_scope_size + scope_blob->len, 2);
    /* Flags */
    build_append_int_noprefix(table_data, 0 /* Don't include all pci device */ ,
                              1);
    build_append_int_noprefix(table_data, 0 , 1); /* Reserved */
    build_append_int_noprefix(table_data, 0 , 2); /* Segment Number */
    /* Register Base Address */
    build_append_int_noprefix(table_data, Q35_HOST_BRIDGE_IOMMU_ADDR , 8);

    /* Scope definition for the root-complex IOAPIC. See VT-d spec
     * 8.3.1 (version Oct. 2014 or later). */
    build_append_int_noprefix(table_data, 0x03 /* IOAPIC */, 1); /* Type */
    build_append_int_noprefix(table_data, ioapic_scope_size, 1); /* Length */
    build_append_int_noprefix(table_data, 0, 2); /* Reserved */
    /* Enumeration ID */
    build_append_int_noprefix(table_data, ACPI_BUILD_IOAPIC_ID, 1);
    /* Start Bus Number */
    build_append_int_noprefix(table_data, Q35_PSEUDO_BUS_PLATFORM, 1);
    /* Path, {Device, Function} pair */
    build_append_int_noprefix(table_data, PCI_SLOT(Q35_PSEUDO_DEVFN_IOAPIC), 1);
    build_append_int_noprefix(table_data, PCI_FUNC(Q35_PSEUDO_DEVFN_IOAPIC), 1);

    /* Add scope found above */
    g_array_append_vals(table_data, scope_blob->data, scope_blob->len);
    g_array_free(scope_blob, true);

    if (iommu->dt_supported) {
        /* 8.5 Root Port ATS Capability Reporting Structure */
        build_append_int_noprefix(table_data, 2, 2); /* Type */
        build_append_int_noprefix(table_data, 8, 2); /* Length */
        build_append_int_noprefix(table_data, 1 /* ALL_PORTS */, 1); /* Flags */
        build_append_int_noprefix(table_data, 0, 1); /* Reserved */
        build_append_int_noprefix(table_data, 0, 2); /* Segment Number */
    }

    acpi_table_end(linker, &table);
}

/*
 * Windows ACPI Emulated Devices Table
 * (Version 1.0 - April 6, 2009)
 * Spec: http://download.microsoft.com/download/7/E/7/7E7662CF-CBEA-470B-A97E-CE7CE0D98DC2/WAET.docx
 *
 * Helpful to speedup Windows guests and ignored by others.
 */
static void
build_waet(GArray *table_data, BIOSLinker *linker, const char *oem_id,
           const char *oem_table_id)
{
    AcpiTable table = { .sig = "WAET", .rev = 1, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);
    /*
     * Set "ACPI PM timer good" flag.
     *
     * Tells Windows guests that our ACPI PM timer is reliable in the
     * sense that guest can read it only once to obtain a reliable value.
     * Which avoids costly VMExits caused by guest re-reading it unnecessarily.
     */
    build_append_int_noprefix(table_data, 1 << 1 /* ACPI PM timer good */, 4);
    acpi_table_end(linker, &table);
}

/*
 *   IVRS table as specified in AMD IOMMU Specification v2.62, Section 5.2
 *   accessible here http://support.amd.com/TechDocs/48882_IOMMU.pdf
 */
#define IOAPIC_SB_DEVID   (uint64_t)PCI_BUILD_BDF(0, PCI_DEVFN(0x14, 0))

/*
 * Insert IVHD entry for device and recurse, insert alias, or insert range as
 * necessary for the PCI topology.
 */
static void
insert_ivhd(PCIBus *bus, PCIDevice *dev, void *opaque)
{
    GArray *table_data = opaque;
    uint32_t entry;

    /* "Select" IVHD entry, type 0x2 */
    entry = PCI_BUILD_BDF(pci_bus_num(bus), dev->devfn) << 8 | 0x2;
    build_append_int_noprefix(table_data, entry, 4);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_BRIDGE)) {
        PCIBus *sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(dev));
        uint8_t sec = pci_bus_num(sec_bus);
        uint8_t sub = dev->config[PCI_SUBORDINATE_BUS];

        if (pci_bus_is_express(sec_bus)) {
            /*
             * Walk the bus if there are subordinates, otherwise use a range
             * to cover an entire leaf bus.  We could potentially also use a
             * range for traversed buses, but we'd need to take care not to
             * create both Select and Range entries covering the same device.
             * This is easier and potentially more compact.
             *
             * An example bare metal system seems to use Select entries for
             * root ports without a slot (ie. built-ins) and Range entries
             * when there is a slot.  The same system also only hard-codes
             * the alias range for an onboard PCIe-to-PCI bridge, apparently
             * making no effort to support nested bridges.  We attempt to
             * be more thorough here.
             */
            if (sec == sub) { /* leaf bus */
                /* "Start of Range" IVHD entry, type 0x3 */
                entry = PCI_BUILD_BDF(sec, PCI_DEVFN(0, 0)) << 8 | 0x3;
                build_append_int_noprefix(table_data, entry, 4);
                /* "End of Range" IVHD entry, type 0x4 */
                entry = PCI_BUILD_BDF(sub, PCI_DEVFN(31, 7)) << 8 | 0x4;
                build_append_int_noprefix(table_data, entry, 4);
            } else {
                pci_for_each_device(sec_bus, sec, insert_ivhd, table_data);
            }
        } else {
            /*
             * If the secondary bus is conventional, then we need to create an
             * Alias range for everything downstream.  The range covers the
             * first devfn on the secondary bus to the last devfn on the
             * subordinate bus.  The alias target depends on legacy versus
             * express bridges, just as in pci_device_iommu_address_space().
             * DeviceIDa vs DeviceIDb as per the AMD IOMMU spec.
             */
            uint16_t dev_id_a, dev_id_b;

            dev_id_a = PCI_BUILD_BDF(sec, PCI_DEVFN(0, 0));

            if (pci_is_express(dev) &&
                pcie_cap_get_type(dev) == PCI_EXP_TYPE_PCI_BRIDGE) {
                dev_id_b = dev_id_a;
            } else {
                dev_id_b = PCI_BUILD_BDF(pci_bus_num(bus), dev->devfn);
            }

            /* "Alias Start of Range" IVHD entry, type 0x43, 8 bytes */
            build_append_int_noprefix(table_data, dev_id_a << 8 | 0x43, 4);
            build_append_int_noprefix(table_data, dev_id_b << 8 | 0x0, 4);

            /* "End of Range" IVHD entry, type 0x4 */
            entry = PCI_BUILD_BDF(sub, PCI_DEVFN(31, 7)) << 8 | 0x4;
            build_append_int_noprefix(table_data, entry, 4);
        }
    }
}

/* For all PCI host bridges, walk and insert IVHD entries */
static int
ivrs_host_bridges(Object *obj, void *opaque)
{
    GArray *ivhd_blob = opaque;

    if (object_dynamic_cast(obj, TYPE_PCI_HOST_BRIDGE)) {
        PCIBus *bus = PCI_HOST_BRIDGE(obj)->bus;

        if (bus && !pci_bus_bypass_iommu(bus)) {
            pci_for_each_device_under_bus(bus, insert_ivhd, ivhd_blob);
        }
    }

    return 0;
}

static void
build_amd_iommu(GArray *table_data, BIOSLinker *linker, const char *oem_id,
                const char *oem_table_id)
{
    int ivhd_table_len = 24;
    AMDVIState *s = AMD_IOMMU_DEVICE(x86_iommu_get_default());
    GArray *ivhd_blob = g_array_new(false, true, 1);
    AcpiTable table = { .sig = "IVRS", .rev = 1, .oem_id = oem_id,
                        .oem_table_id = oem_table_id };

    acpi_table_begin(&table, table_data);
    /* IVinfo - IO virtualization information common to all
     * IOMMU units in a system
     */
    build_append_int_noprefix(table_data, 40UL << 8/* PASize */, 4);
    /* reserved */
    build_append_int_noprefix(table_data, 0, 8);

    /* IVHD definition - type 10h */
    build_append_int_noprefix(table_data, 0x10, 1);
    /* virtualization flags */
    build_append_int_noprefix(table_data,
                             (1UL << 0) | /* HtTunEn      */
                             (1UL << 4) | /* iotblSup     */
                             (1UL << 6) | /* PrefSup      */
                             (1UL << 7),  /* PPRSup       */
                             1);

    /*
     * A PCI bus walk, for each PCI host bridge, is necessary to create a
     * complete set of IVHD entries.  Do this into a separate blob so that we
     * can calculate the total IVRS table length here and then append the new
     * blob further below.  Fall back to an entry covering all devices, which
     * is sufficient when no aliases are present.
     */
    object_child_foreach_recursive(object_get_root(),
                                   ivrs_host_bridges, ivhd_blob);

    if (!ivhd_blob->len) {
        /*
         *   Type 1 device entry reporting all devices
         *   These are 4-byte device entries currently reporting the range of
         *   Refer to Spec - Table 95:IVHD Device Entry Type Codes(4-byte)
         */
        build_append_int_noprefix(ivhd_blob, 0x0000001, 4);
    }

    ivhd_table_len += ivhd_blob->len;

    /*
     * When interrupt remapping is supported, we add a special IVHD device
     * for type IO-APIC.
     */
    if (x86_iommu_ir_supported(x86_iommu_get_default())) {
        ivhd_table_len += 8;
    }

    /* IVHD length */
    build_append_int_noprefix(table_data, ivhd_table_len, 2);
    /* DeviceID */
    build_append_int_noprefix(table_data, s->devid, 2);
    /* Capability offset */
    build_append_int_noprefix(table_data, s->capab_offset, 2);
    /* IOMMU base address */
    build_append_int_noprefix(table_data, s->mmio.addr, 8);
    /* PCI Segment Group */
    build_append_int_noprefix(table_data, 0, 2);
    /* IOMMU info */
    build_append_int_noprefix(table_data, 0, 2);
    /* IOMMU Feature Reporting */
    build_append_int_noprefix(table_data,
                             (48UL << 30) | /* HATS   */
                             (48UL << 28) | /* GATS   */
                             (1UL << 2)   | /* GTSup  */
                             (1UL << 6),    /* GASup  */
                             4);

    /* IVHD entries as found above */
    g_array_append_vals(table_data, ivhd_blob->data, ivhd_blob->len);
    g_array_free(ivhd_blob, TRUE);

    /*
     * Add a special IVHD device type.
     * Refer to spec - Table 95: IVHD device entry type codes
     *
     * Linux IOMMU driver checks for the special IVHD device (type IO-APIC).
     * See Linux kernel commit 'c2ff5cf5294bcbd7fa50f7d860e90a66db7e5059'
     */
    if (x86_iommu_ir_supported(x86_iommu_get_default())) {
        build_append_int_noprefix(table_data,
                                 (0x1ull << 56) |           /* type IOAPIC */
                                 (IOAPIC_SB_DEVID << 40) |  /* IOAPIC devid */
                                 0x48,                      /* special device */
                                 8);
    }
    acpi_table_end(linker, &table);
}

typedef
struct AcpiBuildState {
    /* Copy of table in RAM (for patching). */
    MemoryRegion *table_mr;
    /* Is table patched? */
    uint8_t patched;
    void *rsdp;
    MemoryRegion *rsdp_mr;
    MemoryRegion *linker_mr;
} AcpiBuildState;

static bool acpi_get_mcfg(AcpiMcfgInfo *mcfg)
{
    Object *pci_host;
    QObject *o;

    pci_host = acpi_get_i386_pci_host();
    if (!pci_host) {
        return false;
    }

    o = object_property_get_qobject(pci_host, PCIE_HOST_MCFG_BASE, NULL);
    if (!o) {
        return false;
    }
    mcfg->base = qnum_get_uint(qobject_to(QNum, o));
    qobject_unref(o);
    if (mcfg->base == PCIE_BASE_ADDR_UNMAPPED) {
        return false;
    }

    o = object_property_get_qobject(pci_host, PCIE_HOST_MCFG_SIZE, NULL);
    assert(o);
    mcfg->size = qnum_get_uint(qobject_to(QNum, o));
    qobject_unref(o);
    return true;
}

static
void acpi_build(AcpiBuildTables *tables, MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(machine);
    DeviceState *iommu = pcms->iommu;
    GArray *table_offsets;
    unsigned facs, dsdt, rsdt, fadt;
    AcpiPmInfo pm;
    AcpiMiscInfo misc;
    AcpiMcfgInfo mcfg;
    Range pci_hole = {}, pci_hole64 = {};
    uint8_t *u;
    size_t aml_len = 0;
    GArray *tables_blob = tables->table_data;
    AcpiSlicOem slic_oem = { .id = NULL, .table_id = NULL };
    Object *vmgenid_dev;
    char *oem_id;
    char *oem_table_id;

    acpi_get_pm_info(machine, &pm);
    acpi_get_misc_info(&misc);
    acpi_get_pci_holes(&pci_hole, &pci_hole64);
    acpi_get_slic_oem(&slic_oem);

    if (slic_oem.id) {
        oem_id = slic_oem.id;
    } else {
        oem_id = x86ms->oem_id;
    }

    if (slic_oem.table_id) {
        oem_table_id = slic_oem.table_id;
    } else {
        oem_table_id = x86ms->oem_table_id;
    }

    table_offsets = g_array_new(false, true /* clear */,
                                        sizeof(uint32_t));
    ACPI_BUILD_DPRINTF("init ACPI tables\n");

    bios_linker_loader_alloc(tables->linker,
                             ACPI_BUILD_TABLE_FILE, tables_blob,
                             64 /* Ensure FACS is aligned */,
                             false /* high memory */);

    /*
     * FACS is pointed to by FADT.
     * We place it first since it's the only table that has alignment
     * requirements.
     */
    facs = tables_blob->len;
    build_facs(tables_blob);

    /* DSDT is pointed to by FADT */
    dsdt = tables_blob->len;
    build_dsdt(tables_blob, tables->linker, &pm, &misc,
               &pci_hole, &pci_hole64, machine);

    /* Count the size of the DSDT and SSDT, we will need it for legacy
     * sizing of ACPI tables.
     */
    aml_len += tables_blob->len - dsdt;

    /* ACPI tables pointed to by RSDT */
    fadt = tables_blob->len;
    acpi_add_table(table_offsets, tables_blob);
    pm.fadt.facs_tbl_offset = &facs;
    pm.fadt.dsdt_tbl_offset = &dsdt;
    pm.fadt.xdsdt_tbl_offset = &dsdt;
    build_fadt(tables_blob, tables->linker, &pm.fadt, oem_id, oem_table_id);
    aml_len += tables_blob->len - fadt;

    acpi_add_table(table_offsets, tables_blob);
    acpi_build_madt(tables_blob, tables->linker, x86ms,
                    ACPI_DEVICE_IF(x86ms->acpi_dev), x86ms->oem_id,
                    x86ms->oem_table_id);

#ifdef CONFIG_ACPI_ERST
    {
        Object *erst_dev;
        erst_dev = find_erst_dev();
        if (erst_dev) {
            acpi_add_table(table_offsets, tables_blob);
            build_erst(tables_blob, tables->linker, erst_dev,
                       x86ms->oem_id, x86ms->oem_table_id);
        }
    }
#endif

    vmgenid_dev = find_vmgenid_dev();
    if (vmgenid_dev) {
        acpi_add_table(table_offsets, tables_blob);
        vmgenid_build_acpi(VMGENID(vmgenid_dev), tables_blob,
                           tables->vmgenid, tables->linker, x86ms->oem_id);
    }

    if (misc.has_hpet) {
        acpi_add_table(table_offsets, tables_blob);
        build_hpet(tables_blob, tables->linker, x86ms->oem_id,
                   x86ms->oem_table_id);
    }
#ifdef CONFIG_TPM
    if (misc.tpm_version != TPM_VERSION_UNSPEC) {
        if (misc.tpm_version == TPM_VERSION_1_2) {
            acpi_add_table(table_offsets, tables_blob);
            build_tpm_tcpa(tables_blob, tables->linker, tables->tcpalog,
                           x86ms->oem_id, x86ms->oem_table_id);
        } else { /* TPM_VERSION_2_0 */
            acpi_add_table(table_offsets, tables_blob);
            build_tpm2(tables_blob, tables->linker, tables->tcpalog,
                       x86ms->oem_id, x86ms->oem_table_id);
        }
    }
#endif
    if (machine->numa_state->num_nodes) {
        acpi_add_table(table_offsets, tables_blob);
        build_srat(tables_blob, tables->linker, machine);
        if (machine->numa_state->have_numa_distance) {
            acpi_add_table(table_offsets, tables_blob);
            build_slit(tables_blob, tables->linker, machine, x86ms->oem_id,
                       x86ms->oem_table_id);
        }
        if (machine->numa_state->hmat_enabled) {
            acpi_add_table(table_offsets, tables_blob);
            build_hmat(tables_blob, tables->linker, machine->numa_state,
                       x86ms->oem_id, x86ms->oem_table_id);
        }
    }
    if (acpi_get_mcfg(&mcfg)) {
        acpi_add_table(table_offsets, tables_blob);
        build_mcfg(tables_blob, tables->linker, &mcfg, x86ms->oem_id,
                   x86ms->oem_table_id);
    }
    if (object_dynamic_cast(OBJECT(iommu), TYPE_AMD_IOMMU_DEVICE)) {
        acpi_add_table(table_offsets, tables_blob);
        build_amd_iommu(tables_blob, tables->linker, x86ms->oem_id,
                        x86ms->oem_table_id);
    } else if (object_dynamic_cast(OBJECT(iommu), TYPE_INTEL_IOMMU_DEVICE)) {
        acpi_add_table(table_offsets, tables_blob);
        build_dmar_q35(tables_blob, tables->linker, x86ms->oem_id,
                       x86ms->oem_table_id);
    } else if (object_dynamic_cast(OBJECT(iommu), TYPE_VIRTIO_IOMMU_PCI)) {
        PCIDevice *pdev = PCI_DEVICE(iommu);

        acpi_add_table(table_offsets, tables_blob);
        build_viot(machine, tables_blob, tables->linker, pci_get_bdf(pdev),
                   x86ms->oem_id, x86ms->oem_table_id);
    }
    if (machine->nvdimms_state->is_enabled) {
        nvdimm_build_acpi(table_offsets, tables_blob, tables->linker,
                          machine->nvdimms_state, machine->ram_slots,
                          x86ms->oem_id, x86ms->oem_table_id);
    }

    acpi_add_table(table_offsets, tables_blob);
    build_waet(tables_blob, tables->linker, x86ms->oem_id, x86ms->oem_table_id);

    /* Add tables supplied by user (if any) */
    for (u = acpi_table_first(); u; u = acpi_table_next(u)) {
        unsigned len = acpi_table_len(u);

        acpi_add_table(table_offsets, tables_blob);
        g_array_append_vals(tables_blob, u, len);
    }

    /* RSDT is pointed to by RSDP */
    rsdt = tables_blob->len;
    build_rsdt(tables_blob, tables->linker, table_offsets,
               oem_id, oem_table_id);

    /* RSDP is in FSEG memory, so allocate it separately */
    {
        AcpiRsdpData rsdp_data = {
            .revision = 0,
            .oem_id = x86ms->oem_id,
            .xsdt_tbl_offset = NULL,
            .rsdt_tbl_offset = &rsdt,
        };
        build_rsdp(tables->rsdp, tables->linker, &rsdp_data);
        if (!pcmc->rsdp_in_ram) {
            /* We used to allocate some extra space for RSDP revision 2 but
             * only used the RSDP revision 0 space. The extra bytes were
             * zeroed out and not used.
             * Here we continue wasting those extra 16 bytes to make sure we
             * don't break migration for machine types 2.2 and older due to
             * RSDP blob size mismatch.
             */
            build_append_int_noprefix(tables->rsdp, 0, 16);
        }
    }

    /* We'll expose it all to Guest so we want to reduce
     * chance of size changes.
     *
     * We used to align the tables to 4k, but of course this would
     * too simple to be enough.  4k turned out to be too small an
     * alignment very soon, and in fact it is almost impossible to
     * keep the table size stable for all (max_cpus, max_memory_slots)
     * combinations.  So the table size is always 64k for pc-i440fx-2.1
     * and we give an error if the table grows beyond that limit.
     *
     * We still have the problem of migrating from "-M pc-i440fx-2.0".  For
     * that, we exploit the fact that QEMU 2.1 generates _smaller_ tables
     * than 2.0 and we can always pad the smaller tables with zeros.  We can
     * then use the exact size of the 2.0 tables.
     *
     * All this is for PIIX4, since QEMU 2.0 didn't support Q35 migration.
     */
    if (pcmc->legacy_acpi_table_size) {
        /* Subtracting aml_len gives the size of fixed tables.  Then add the
         * size of the PIIX4 DSDT/SSDT in QEMU 2.0.
         */
        int legacy_aml_len =
            pcmc->legacy_acpi_table_size +
            ACPI_BUILD_LEGACY_CPU_AML_SIZE * x86ms->apic_id_limit;
        int legacy_table_size =
            ROUND_UP(tables_blob->len - aml_len + legacy_aml_len,
                     ACPI_BUILD_ALIGN_SIZE);
        if (tables_blob->len > legacy_table_size) {
            /* Should happen only with PCI bridges and -M pc-i440fx-2.0.  */
            warn_report("ACPI table size %u exceeds %d bytes,"
                        " migration may not work",
                        tables_blob->len, legacy_table_size);
            error_printf("Try removing CPUs, NUMA nodes, memory slots"
                         " or PCI bridges.");
        }
        g_array_set_size(tables_blob, legacy_table_size);
    } else {
        /* Make sure we have a buffer in case we need to resize the tables. */
        if (tables_blob->len > ACPI_BUILD_TABLE_SIZE / 2) {
            /* As of QEMU 2.1, this fires with 160 VCPUs and 255 memory slots.  */
            warn_report("ACPI table size %u exceeds %d bytes,"
                        " migration may not work",
                        tables_blob->len, ACPI_BUILD_TABLE_SIZE / 2);
            error_printf("Try removing CPUs, NUMA nodes, memory slots"
                         " or PCI bridges.");
        }
        acpi_align_size(tables_blob, ACPI_BUILD_TABLE_SIZE);
    }

    acpi_align_size(tables->linker->cmd_blob, ACPI_BUILD_ALIGN_SIZE);

    /* Cleanup memory that's no longer used. */
    g_array_free(table_offsets, true);
    g_free(slic_oem.id);
    g_free(slic_oem.table_id);
}

static void acpi_ram_update(MemoryRegion *mr, GArray *data)
{
    uint32_t size = acpi_data_len(data);

    /* Make sure RAM size is correct - in case it got changed e.g. by migration */
    memory_region_ram_resize(mr, size, &error_abort);

    memcpy(memory_region_get_ram_ptr(mr), data->data, size);
    memory_region_set_dirty(mr, 0, size);
}

static void acpi_build_update(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    AcpiBuildTables tables;

    /* No state to update or already patched? Nothing to do. */
    if (!build_state || build_state->patched) {
        return;
    }
    build_state->patched = 1;

    acpi_build_tables_init(&tables);

    acpi_build(&tables, MACHINE(qdev_get_machine()));

    acpi_ram_update(build_state->table_mr, tables.table_data);

    if (build_state->rsdp) {
        memcpy(build_state->rsdp, tables.rsdp->data, acpi_data_len(tables.rsdp));
    } else {
        acpi_ram_update(build_state->rsdp_mr, tables.rsdp);
    }

    acpi_ram_update(build_state->linker_mr, tables.linker->cmd_blob);
    acpi_build_tables_cleanup(&tables, true);
}

static void acpi_build_reset(void *build_opaque)
{
    AcpiBuildState *build_state = build_opaque;
    build_state->patched = 0;
}

static const VMStateDescription vmstate_acpi_build = {
    .name = "acpi_build",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(patched, AcpiBuildState),
        VMSTATE_END_OF_LIST()
    },
};

void acpi_setup(void)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(pcms);
    AcpiBuildTables tables;
    AcpiBuildState *build_state;
    Object *vmgenid_dev;
#ifdef CONFIG_TPM
    TPMIf *tpm;
    static FwCfgTPMConfig tpm_config;
#endif

    if (!x86ms->fw_cfg) {
        ACPI_BUILD_DPRINTF("No fw cfg. Bailing out.\n");
        return;
    }

    if (!pcms->acpi_build_enabled) {
        ACPI_BUILD_DPRINTF("ACPI build disabled. Bailing out.\n");
        return;
    }

    if (!x86_machine_is_acpi_enabled(X86_MACHINE(pcms))) {
        ACPI_BUILD_DPRINTF("ACPI disabled. Bailing out.\n");
        return;
    }

    build_state = g_malloc0(sizeof *build_state);

    acpi_build_tables_init(&tables);
    acpi_build(&tables, MACHINE(pcms));

    /* Now expose it all to Guest */
    build_state->table_mr = acpi_add_rom_blob(acpi_build_update,
                                              build_state, tables.table_data,
                                              ACPI_BUILD_TABLE_FILE);
    assert(build_state->table_mr != NULL);

    build_state->linker_mr =
        acpi_add_rom_blob(acpi_build_update, build_state,
                          tables.linker->cmd_blob, ACPI_BUILD_LOADER_FILE);

#ifdef CONFIG_TPM
    fw_cfg_add_file(x86ms->fw_cfg, ACPI_BUILD_TPMLOG_FILE,
                    tables.tcpalog->data, acpi_data_len(tables.tcpalog));

    tpm = tpm_find();
    if (tpm && object_property_get_bool(OBJECT(tpm), "ppi", &error_abort)) {
        tpm_config = (FwCfgTPMConfig) {
            .tpmppi_address = cpu_to_le32(TPM_PPI_ADDR_BASE),
            .tpm_version = tpm_get_version(tpm),
            .tpmppi_version = TPM_PPI_VERSION_1_30
        };
        fw_cfg_add_file(x86ms->fw_cfg, "etc/tpm/config",
                        &tpm_config, sizeof tpm_config);
    }
#endif

    vmgenid_dev = find_vmgenid_dev();
    if (vmgenid_dev) {
        vmgenid_add_fw_cfg(VMGENID(vmgenid_dev), x86ms->fw_cfg,
                           tables.vmgenid);
    }

    if (!pcmc->rsdp_in_ram) {
        /*
         * Keep for compatibility with old machine types.
         * Though RSDP is small, its contents isn't immutable, so
         * we'll update it along with the rest of tables on guest access.
         */
        uint32_t rsdp_size = acpi_data_len(tables.rsdp);

        build_state->rsdp = g_memdup(tables.rsdp->data, rsdp_size);
        fw_cfg_add_file_callback(x86ms->fw_cfg, ACPI_BUILD_RSDP_FILE,
                                 acpi_build_update, NULL, build_state,
                                 build_state->rsdp, rsdp_size, true);
        build_state->rsdp_mr = NULL;
    } else {
        build_state->rsdp = NULL;
        build_state->rsdp_mr = acpi_add_rom_blob(acpi_build_update,
                                                 build_state, tables.rsdp,
                                                 ACPI_BUILD_RSDP_FILE);
    }

    qemu_register_reset(acpi_build_reset, build_state);
    acpi_build_reset(build_state);
    vmstate_register(NULL, 0, &vmstate_acpi_build, build_state);

    /* Cleanup tables but don't free the memory: we track it
     * in build_state.
     */
    acpi_build_tables_cleanup(&tables, false);
}
