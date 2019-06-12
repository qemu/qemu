/*
 *  Virtual Machine Generation ID Device
 *
 *  Copyright (C) 2017 Skyport Systems.
 *
 *  Author: Ben Warren <ben@skyportsystems.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qemu/module.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/vmgenid.h"
#include "hw/nvram/fw_cfg.h"
#include "sysemu/sysemu.h"

void vmgenid_build_acpi(VmGenIdState *vms, GArray *table_data, GArray *guid,
                        BIOSLinker *linker)
{
    Aml *ssdt, *dev, *scope, *method, *addr, *if_ctx;
    uint32_t vgia_offset;
    QemuUUID guid_le;

    /* Fill in the GUID values.  These need to be converted to little-endian
     * first, since that's what the guest expects
     */
    g_array_set_size(guid, VMGENID_FW_CFG_SIZE - ARRAY_SIZE(guid_le.data));
    guid_le = qemu_uuid_bswap(vms->guid);
    /* The GUID is written at a fixed offset into the fw_cfg file
     * in order to implement the "OVMF SDT Header probe suppressor"
     * see docs/specs/vmgenid.txt for more details
     */
    g_array_insert_vals(guid, VMGENID_GUID_OFFSET, guid_le.data,
                        ARRAY_SIZE(guid_le.data));

    /* Put this in a separate SSDT table */
    ssdt = init_aml_allocator();

    /* Reserve space for header */
    acpi_data_push(ssdt->buf, sizeof(AcpiTableHeader));

    /* Storage for the GUID address */
    vgia_offset = table_data->len +
        build_append_named_dword(ssdt->buf, "VGIA");
    scope = aml_scope("\\_SB");
    dev = aml_device("VGEN");
    aml_append(dev, aml_name_decl("_HID", aml_string("QEMUVGID")));
    aml_append(dev, aml_name_decl("_CID", aml_string("VM_Gen_Counter")));
    aml_append(dev, aml_name_decl("_DDN", aml_string("VM_Gen_Counter")));

    /* Simple status method to check that address is linked and non-zero */
    method = aml_method("_STA", 0, AML_NOTSERIALIZED);
    addr = aml_local(0);
    aml_append(method, aml_store(aml_int(0xf), addr));
    if_ctx = aml_if(aml_equal(aml_name("VGIA"), aml_int(0)));
    aml_append(if_ctx, aml_store(aml_int(0), addr));
    aml_append(method, if_ctx);
    aml_append(method, aml_return(addr));
    aml_append(dev, method);

    /* the ADDR method returns two 32-bit words representing the lower and
     * upper halves * of the physical address of the fw_cfg blob
     * (holding the GUID)
     */
    method = aml_method("ADDR", 0, AML_NOTSERIALIZED);

    addr = aml_local(0);
    aml_append(method, aml_store(aml_package(2), addr));

    aml_append(method, aml_store(aml_add(aml_name("VGIA"),
                                         aml_int(VMGENID_GUID_OFFSET), NULL),
                                 aml_index(addr, aml_int(0))));
    aml_append(method, aml_store(aml_int(0), aml_index(addr, aml_int(1))));
    aml_append(method, aml_return(addr));

    aml_append(dev, method);
    aml_append(scope, dev);
    aml_append(ssdt, scope);

    /* attach an ACPI notify */
    method = aml_method("\\_GPE._E05", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_notify(aml_name("\\_SB.VGEN"), aml_int(0x80)));
    aml_append(ssdt, method);

    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);

    /* Allocate guest memory for the Data fw_cfg blob */
    bios_linker_loader_alloc(linker, VMGENID_GUID_FW_CFG_FILE, guid, 4096,
                             false /* page boundary, high memory */);

    /* Patch address of GUID fw_cfg blob into the ADDR fw_cfg blob
     * so QEMU can write the GUID there.  The address is expected to be
     * < 4GB, but write 64 bits anyway.
     * The address that is patched in is offset in order to implement
     * the "OVMF SDT Header probe suppressor"
     * see docs/specs/vmgenid.txt for more details.
     */
    bios_linker_loader_write_pointer(linker,
        VMGENID_ADDR_FW_CFG_FILE, 0, sizeof(uint64_t),
        VMGENID_GUID_FW_CFG_FILE, VMGENID_GUID_OFFSET);

    /* Patch address of GUID fw_cfg blob into the AML so OSPM can retrieve
     * and read it.  Note that while we provide storage for 64 bits, only
     * the least-signficant 32 get patched into AML.
     */
    bios_linker_loader_add_pointer(linker,
        ACPI_BUILD_TABLE_FILE, vgia_offset, sizeof(uint32_t),
        VMGENID_GUID_FW_CFG_FILE, 0);

    build_header(linker, table_data,
        (void *)(table_data->data + table_data->len - ssdt->buf->len),
        "SSDT", ssdt->buf->len, 1, NULL, "VMGENID");
    free_aml_allocator();
}

void vmgenid_add_fw_cfg(VmGenIdState *vms, FWCfgState *s, GArray *guid)
{
    /* Create a read-only fw_cfg file for GUID */
    fw_cfg_add_file(s, VMGENID_GUID_FW_CFG_FILE, guid->data,
                    VMGENID_FW_CFG_SIZE);
    /* Create a read-write fw_cfg file for Address */
    fw_cfg_add_file_callback(s, VMGENID_ADDR_FW_CFG_FILE, NULL, NULL, NULL,
                             vms->vmgenid_addr_le,
                             ARRAY_SIZE(vms->vmgenid_addr_le), false);
}

static void vmgenid_update_guest(VmGenIdState *vms)
{
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    uint32_t vmgenid_addr;
    QemuUUID guid_le;

    if (obj) {
        /* Write the GUID to guest memory */
        memcpy(&vmgenid_addr, vms->vmgenid_addr_le, sizeof(vmgenid_addr));
        vmgenid_addr = le32_to_cpu(vmgenid_addr);
        /* A zero value in vmgenid_addr means that BIOS has not yet written
         * the address
         */
        if (vmgenid_addr) {
            /* QemuUUID has the first three words as big-endian, and expect
             * that any GUIDs passed in will always be BE.  The guest,
             * however, will expect the fields to be little-endian.
             * Perform a byte swap immediately before writing.
             */
            guid_le = qemu_uuid_bswap(vms->guid);
            /* The GUID is written at a fixed offset into the fw_cfg file
             * in order to implement the "OVMF SDT Header probe suppressor"
             * see docs/specs/vmgenid.txt for more details.
             */
            cpu_physical_memory_write(vmgenid_addr, guid_le.data,
                                      sizeof(guid_le.data));
            /* Send _GPE.E05 event */
            acpi_send_event(DEVICE(obj), ACPI_VMGENID_CHANGE_STATUS);
        }
    }
}

/* After restoring an image, we need to update the guest memory and notify
 * it of a potential change to VM Generation ID
 */
static int vmgenid_post_load(void *opaque, int version_id)
{
    VmGenIdState *vms = opaque;
    vmgenid_update_guest(vms);
    return 0;
}

static const VMStateDescription vmstate_vmgenid = {
    .name = "vmgenid",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vmgenid_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(vmgenid_addr_le, VmGenIdState, sizeof(uint64_t)),
        VMSTATE_END_OF_LIST()
    },
};

static void vmgenid_handle_reset(void *opaque)
{
    VmGenIdState *vms = VMGENID(opaque);
    /* Clear the guest-allocated GUID address when the VM resets */
    memset(vms->vmgenid_addr_le, 0, ARRAY_SIZE(vms->vmgenid_addr_le));
}

static void vmgenid_realize(DeviceState *dev, Error **errp)
{
    VmGenIdState *vms = VMGENID(dev);

    if (!bios_linker_loader_can_write_pointer()) {
        error_setg(errp, "%s requires DMA write support in fw_cfg, "
                   "which this machine type does not provide", VMGENID_DEVICE);
        return;
    }

    /* Given that this function is executing, there is at least one VMGENID
     * device. Check if there are several.
     */
    if (!find_vmgenid_dev()) {
        error_setg(errp, "at most one %s device is permitted", VMGENID_DEVICE);
        return;
    }

    qemu_register_reset(vmgenid_handle_reset, vms);

    vmgenid_update_guest(vms);
}

static Property vmgenid_device_properties[] = {
    DEFINE_PROP_UUID(VMGENID_GUID, VmGenIdState, guid),
    DEFINE_PROP_END_OF_LIST(),
};

static void vmgenid_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vmgenid;
    dc->realize = vmgenid_realize;
    dc->props = vmgenid_device_properties;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo vmgenid_device_info = {
    .name          = VMGENID_DEVICE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(VmGenIdState),
    .class_init    = vmgenid_device_class_init,
};

static void vmgenid_register_types(void)
{
    type_register_static(&vmgenid_device_info);
}

type_init(vmgenid_register_types)

GuidInfo *qmp_query_vm_generation_id(Error **errp)
{
    GuidInfo *info;
    VmGenIdState *vms;
    Object *obj = find_vmgenid_dev();

    if (!obj) {
        error_setg(errp, "VM Generation ID device not found");
        return NULL;
    }
    vms = VMGENID(obj);

    info = g_malloc0(sizeof(*info));
    info->guid = qemu_uuid_unparse_strdup(&vms->guid);
    return info;
}
