/*
 * AWS nitro-enclave machine
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"

#include "chardev/char.h"
#include "hw/sysbus.h"
#include "hw/core/eif.h"
#include "hw/i386/x86.h"
#include "hw/i386/microvm.h"
#include "hw/i386/nitro_enclave.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/virtio/virtio-nsm.h"
#include "hw/virtio/vhost-user-vsock.h"
#include "system/hostmem.h"

static BusState *find_free_virtio_mmio_bus(void)
{
    BusChild *kid;
    BusState *bus = sysbus_get_default();

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_MMIO)) {
            VirtIOMMIOProxy *mmio = VIRTIO_MMIO(OBJECT(dev));
            VirtioBusState *mmio_virtio_bus = &mmio->bus;
            BusState *mmio_bus = &mmio_virtio_bus->parent_obj;
            if (QTAILQ_EMPTY(&mmio_bus->children)) {
                return mmio_bus;
            }
        }
    }

    return NULL;
}

static void vhost_user_vsock_init(NitroEnclaveMachineState *nems)
{
    DeviceState *dev = qdev_new(TYPE_VHOST_USER_VSOCK);
    VHostUserVSock *vsock = VHOST_USER_VSOCK(dev);
    BusState *bus;

    if (!nems->vsock) {
        error_report("A valid chardev id for vhost-user-vsock device must be "
                     "provided using the 'vsock' machine option");
        exit(1);
    }

    bus = find_free_virtio_mmio_bus();
    if (!bus) {
        error_report("Failed to find bus for vhost-user-vsock device");
        exit(1);
    }

    Chardev *chardev = qemu_chr_find(nems->vsock);
    if (!chardev) {
        error_report("Failed to find chardev with id %s", nems->vsock);
        exit(1);
    }

    vsock->conf.chardev.chr = chardev;

    qdev_realize_and_unref(dev, bus, &error_fatal);
}

static void virtio_nsm_init(NitroEnclaveMachineState *nems)
{
    DeviceState *dev = qdev_new(TYPE_VIRTIO_NSM);
    VirtIONSM *vnsm = VIRTIO_NSM(dev);
    BusState *bus = find_free_virtio_mmio_bus();

    if (!bus) {
        error_report("Failed to find bus for virtio-nsm device.");
        exit(1);
    }

    qdev_prop_set_string(dev, "module-id", nems->id);

    qdev_realize_and_unref(dev, bus, &error_fatal);
    nems->vnsm = vnsm;
}

static void nitro_enclave_devices_init(NitroEnclaveMachineState *nems)
{
    vhost_user_vsock_init(nems);
    virtio_nsm_init(nems);
}

static void nitro_enclave_machine_state_init(MachineState *machine)
{
    NitroEnclaveMachineClass *ne_class =
        NITRO_ENCLAVE_MACHINE_GET_CLASS(machine);
    NitroEnclaveMachineState *ne_state = NITRO_ENCLAVE_MACHINE(machine);

    ne_class->parent_init(machine);
    nitro_enclave_devices_init(ne_state);
}

static void nitro_enclave_machine_reset(MachineState *machine, ResetType type)
{
    NitroEnclaveMachineClass *ne_class =
        NITRO_ENCLAVE_MACHINE_GET_CLASS(machine);
    NitroEnclaveMachineState *ne_state = NITRO_ENCLAVE_MACHINE(machine);

    ne_class->parent_reset(machine, type);

    memset(ne_state->vnsm->pcrs, 0, sizeof(ne_state->vnsm->pcrs));

    /* PCR0 */
    ne_state->vnsm->extend_pcr(ne_state->vnsm, 0, ne_state->image_hash,
                               QCRYPTO_HASH_DIGEST_LEN_SHA384);
    /* PCR1 */
    ne_state->vnsm->extend_pcr(ne_state->vnsm, 1, ne_state->bootstrap_hash,
                               QCRYPTO_HASH_DIGEST_LEN_SHA384);
    /* PCR2 */
    ne_state->vnsm->extend_pcr(ne_state->vnsm, 2, ne_state->app_hash,
                               QCRYPTO_HASH_DIGEST_LEN_SHA384);
    /* PCR3 */
    if (ne_state->parent_role) {
        ne_state->vnsm->extend_pcr(ne_state->vnsm, 3,
                                   (uint8_t *) ne_state->parent_role,
                                   strlen(ne_state->parent_role));
    }
    /* PCR4 */
    if (ne_state->parent_id) {
        ne_state->vnsm->extend_pcr(ne_state->vnsm, 4,
                                   (uint8_t *) ne_state->parent_id,
                                   strlen(ne_state->parent_id));
    }
    /* PCR8 */
    if (ne_state->signature_found) {
        ne_state->vnsm->extend_pcr(ne_state->vnsm, 8,
                                   ne_state->fingerprint_hash,
                                   QCRYPTO_HASH_DIGEST_LEN_SHA384);
    }

    /* First 16 PCRs are locked from boot and reserved for nitro enclave */
    for (int i = 0; i < 16; ++i) {
        ne_state->vnsm->lock_pcr(ne_state->vnsm, i);
    }
}

static void nitro_enclave_machine_initfn(Object *obj)
{
    MicrovmMachineState *mms = MICROVM_MACHINE(obj);
    X86MachineState *x86ms = X86_MACHINE(obj);
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    nems->id = g_strdup("i-234-enc5678");

    /* AWS nitro enclaves have PCIE and ACPI disabled */
    mms->pcie = ON_OFF_AUTO_OFF;
    x86ms->acpi = ON_OFF_AUTO_OFF;
}

static void x86_load_eif(X86MachineState *x86ms, FWCfgState *fw_cfg,
                         int acpi_data_size, bool pvh_enabled)
{
    Error *err = NULL;
    char *eif_kernel, *eif_initrd, *eif_cmdline;
    MachineState *machine = MACHINE(x86ms);
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(x86ms);

    if (!read_eif_file(machine->kernel_filename, machine->initrd_filename,
                       &eif_kernel, &eif_initrd, &eif_cmdline,
                       nems->image_hash, nems->bootstrap_hash,
                       nems->app_hash, nems->fingerprint_hash,
                       &(nems->signature_found), &err)) {
        error_report_err(err);
        exit(1);
    }

    g_free(machine->kernel_filename);
    machine->kernel_filename = eif_kernel;
    g_free(machine->initrd_filename);
    machine->initrd_filename = eif_initrd;

    /*
     * If kernel cmdline argument was provided, let's concatenate it to the
     * extracted EIF kernel cmdline.
     */
    if (machine->kernel_cmdline != NULL) {
        char *cmd = g_strdup_printf("%s %s", eif_cmdline,
                                    machine->kernel_cmdline);
        g_free(eif_cmdline);
        g_free(machine->kernel_cmdline);
        machine->kernel_cmdline = cmd;
    } else {
        machine->kernel_cmdline = eif_cmdline;
    }

    x86_load_linux(x86ms, fw_cfg, 0, true);

    unlink(machine->kernel_filename);
    unlink(machine->initrd_filename);
}

static bool create_memfd_backend(MachineState *ms, const char *path,
                                 Error **errp)
{
    Object *obj;
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    bool r = false;

    obj = object_new(TYPE_MEMORY_BACKEND_MEMFD);
    if (!object_property_set_int(obj, "size", ms->ram_size, errp)) {
        goto out;
    }
    object_property_add_child(object_get_objects_root(), mc->default_ram_id,
                              obj);

    if (!user_creatable_complete(USER_CREATABLE(obj), errp)) {
        goto out;
    }
    r = object_property_set_link(OBJECT(ms), "memory-backend", obj, errp);

out:
    object_unref(obj);
    return r;
}

static char *nitro_enclave_get_vsock_chardev_id(Object *obj, Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    return g_strdup(nems->vsock);
}

static void nitro_enclave_set_vsock_chardev_id(Object *obj, const char *value,
                                               Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    g_free(nems->vsock);
    nems->vsock = g_strdup(value);
}

static char *nitro_enclave_get_id(Object *obj, Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    return g_strdup(nems->id);
}

static void nitro_enclave_set_id(Object *obj, const char *value,
                                            Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    g_free(nems->id);
    nems->id = g_strdup(value);
}

static char *nitro_enclave_get_parent_role(Object *obj, Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    return g_strdup(nems->parent_role);
}

static void nitro_enclave_set_parent_role(Object *obj, const char *value,
                                          Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    g_free(nems->parent_role);
    nems->parent_role = g_strdup(value);
}

static char *nitro_enclave_get_parent_id(Object *obj, Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    return g_strdup(nems->parent_id);
}

static void nitro_enclave_set_parent_id(Object *obj, const char *value,
                                        Error **errp)
{
    NitroEnclaveMachineState *nems = NITRO_ENCLAVE_MACHINE(obj);

    g_free(nems->parent_id);
    nems->parent_id = g_strdup(value);
}

static void nitro_enclave_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MicrovmMachineClass *mmc = MICROVM_MACHINE_CLASS(oc);
    NitroEnclaveMachineClass *nemc = NITRO_ENCLAVE_MACHINE_CLASS(oc);

    mmc->x86_load_linux = x86_load_eif;

    mc->family = "nitro_enclave_i386";
    mc->desc = "AWS Nitro Enclave";

    nemc->parent_init = mc->init;
    mc->init = nitro_enclave_machine_state_init;

    nemc->parent_reset = mc->reset;
    mc->reset = nitro_enclave_machine_reset;

    mc->create_default_memdev = create_memfd_backend;

    object_class_property_add_str(oc, NITRO_ENCLAVE_VSOCK_CHARDEV_ID,
                                  nitro_enclave_get_vsock_chardev_id,
                                  nitro_enclave_set_vsock_chardev_id);
    object_class_property_set_description(oc, NITRO_ENCLAVE_VSOCK_CHARDEV_ID,
                                          "Set chardev id for vhost-user-vsock "
                                          "device");

    object_class_property_add_str(oc, NITRO_ENCLAVE_ID, nitro_enclave_get_id,
                                  nitro_enclave_set_id);
    object_class_property_set_description(oc, NITRO_ENCLAVE_ID,
                                          "Set enclave identifier");

    object_class_property_add_str(oc, NITRO_ENCLAVE_PARENT_ROLE,
                                  nitro_enclave_get_parent_role,
                                  nitro_enclave_set_parent_role);
    object_class_property_set_description(oc, NITRO_ENCLAVE_PARENT_ROLE,
                                          "Set parent instance IAM role ARN");

    object_class_property_add_str(oc, NITRO_ENCLAVE_PARENT_ID,
                                  nitro_enclave_get_parent_id,
                                  nitro_enclave_set_parent_id);
    object_class_property_set_description(oc, NITRO_ENCLAVE_PARENT_ID,
                                          "Set parent instance identifier");
}

static const TypeInfo nitro_enclave_machine_info = {
    .name          = TYPE_NITRO_ENCLAVE_MACHINE,
    .parent        = TYPE_MICROVM_MACHINE,
    .instance_size = sizeof(NitroEnclaveMachineState),
    .instance_init = nitro_enclave_machine_initfn,
    .class_size    = sizeof(NitroEnclaveMachineClass),
    .class_init    = nitro_enclave_class_init,
};

static void nitro_enclave_machine_init(void)
{
    type_register_static(&nitro_enclave_machine_info);
}
type_init(nitro_enclave_machine_init);
