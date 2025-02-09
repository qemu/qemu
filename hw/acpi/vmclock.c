/*
 * Virtual Machine Clock Device
 *
 * Copyright © 2024 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/i386/e820_memory_layout.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/vmclock.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "system/reset.h"

#include "standard-headers/linux/vmclock-abi.h"

void vmclock_build_acpi(VmclockState *vms, GArray *table_data,
                        BIOSLinker *linker, const char *oem_id)
{
    Aml *ssdt, *dev, *scope, *crs;
    AcpiTable table = { .sig = "SSDT", .rev = 1,
                        .oem_id = oem_id, .oem_table_id = "VMCLOCK" };

    /* Put VMCLOCK into a separate SSDT table */
    acpi_table_begin(&table, table_data);
    ssdt = init_aml_allocator();

    scope = aml_scope("\\_SB");
    dev = aml_device("VCLK");
    aml_append(dev, aml_name_decl("_HID", aml_string("AMZNC10C")));
    aml_append(dev, aml_name_decl("_CID", aml_string("VMCLOCK")));
    aml_append(dev, aml_name_decl("_DDN", aml_string("VMCLOCK")));

    /* Simple status method */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xf)));

    crs = aml_resource_template();
    aml_append(crs, aml_qword_memory(AML_POS_DECODE,
                                     AML_MIN_FIXED, AML_MAX_FIXED,
                                     AML_CACHEABLE, AML_READ_ONLY,
                                     0xffffffffffffffffULL,
                                     vms->physaddr,
                                     vms->physaddr + VMCLOCK_SIZE - 1,
                                     0, VMCLOCK_SIZE));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
    aml_append(ssdt, scope);

    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);
    acpi_table_end(linker, &table);
    free_aml_allocator();
}

static void vmclock_update_guest(VmclockState *vms)
{
    uint64_t disruption_marker;
    uint32_t seq_count;

    if (!vms->clk) {
        return;
    }

    seq_count = le32_to_cpu(vms->clk->seq_count) | 1;
    vms->clk->seq_count = cpu_to_le32(seq_count);
    /* These barriers pair with read barriers in the guest */
    smp_wmb();

    disruption_marker = le64_to_cpu(vms->clk->disruption_marker);
    disruption_marker++;
    vms->clk->disruption_marker = cpu_to_le64(disruption_marker);

    /* These barriers pair with read barriers in the guest */
    smp_wmb();
    vms->clk->seq_count = cpu_to_le32(seq_count + 1);
}

/*
 * After restoring an image, we need to update the guest memory to notify
 * it of clock disruption.
 */
static int vmclock_post_load(void *opaque, int version_id)
{
    VmclockState *vms = opaque;

    vmclock_update_guest(vms);
    return 0;
}

static const VMStateDescription vmstate_vmclock = {
    .name = "vmclock",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vmclock_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(physaddr, VmclockState),
        VMSTATE_END_OF_LIST()
    },
};

static void vmclock_handle_reset(void *opaque)
{
    VmclockState *vms = VMCLOCK(opaque);

    if (!memory_region_is_mapped(&vms->clk_page)) {
        memory_region_add_subregion_overlap(get_system_memory(),
                                            vms->physaddr,
                                            &vms->clk_page, 0);
    }
}

static void vmclock_realize(DeviceState *dev, Error **errp)
{
    VmclockState *vms = VMCLOCK(dev);

    /*
     * Given that this function is executing, there is at least one VMCLOCK
     * device. Check if there are several.
     */
    if (!find_vmclock_dev()) {
        error_setg(errp, "at most one %s device is permitted", TYPE_VMCLOCK);
        return;
    }

    vms->physaddr = VMCLOCK_ADDR;

    e820_add_entry(vms->physaddr, VMCLOCK_SIZE, E820_RESERVED);

    memory_region_init_ram(&vms->clk_page, OBJECT(dev), "vmclock_page",
                           VMCLOCK_SIZE, &error_abort);
    memory_region_set_enabled(&vms->clk_page, true);
    vms->clk = memory_region_get_ram_ptr(&vms->clk_page);
    memset(vms->clk, 0, VMCLOCK_SIZE);

    vms->clk->magic = cpu_to_le32(VMCLOCK_MAGIC);
    vms->clk->size = cpu_to_le16(VMCLOCK_SIZE);
    vms->clk->version = cpu_to_le16(1);

    /* These are all zero and thus default, but be explicit */
    vms->clk->clock_status = VMCLOCK_STATUS_UNKNOWN;
    vms->clk->counter_id = VMCLOCK_COUNTER_INVALID;

    qemu_register_reset(vmclock_handle_reset, vms);

    vmclock_update_guest(vms);
}

static void vmclock_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vmclock;
    dc->realize = vmclock_realize;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo vmclock_device_info = {
    .name          = TYPE_VMCLOCK,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(VmclockState),
    .class_init    = vmclock_device_class_init,
};

static void vmclock_register_types(void)
{
    type_register_static(&vmclock_device_info);
}

type_init(vmclock_register_types)
