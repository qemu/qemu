/*
 * Generic PCI Express Root Port emulation
 *
 * Copyright (C) 2017 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie_port.h"

#define TYPE_GEN_PCIE_ROOT_PORT                "pcie-root-port"
#define GEN_PCIE_ROOT_PORT(obj) \
        OBJECT_CHECK(GenPCIERootPort, (obj), TYPE_GEN_PCIE_ROOT_PORT)

#define GEN_PCIE_ROOT_PORT_AER_OFFSET           0x100
#define GEN_PCIE_ROOT_PORT_MSIX_NR_VECTOR       1

typedef struct GenPCIERootPort {
    /*< private >*/
    PCIESlot parent_obj;
    /*< public >*/

    bool migrate_msix;

    /* additional resources to reserve on firmware init */
    uint32_t bus_reserve;
    uint64_t io_reserve;
    uint64_t mem_reserve;
    uint64_t pref32_reserve;
    uint64_t pref64_reserve;
} GenPCIERootPort;

static uint8_t gen_rp_aer_vector(const PCIDevice *d)
{
    return 0;
}

static int gen_rp_interrupts_init(PCIDevice *d, Error **errp)
{
    int rc;

    rc = msix_init_exclusive_bar(d, GEN_PCIE_ROOT_PORT_MSIX_NR_VECTOR, 0, errp);

    if (rc < 0) {
        assert(rc == -ENOTSUP);
    } else {
        msix_vector_use(d, 0);
    }

    return rc;
}

static void gen_rp_interrupts_uninit(PCIDevice *d)
{
    msix_uninit_exclusive_bar(d);
}

static bool gen_rp_test_migrate_msix(void *opaque, int version_id)
{
    GenPCIERootPort *rp = opaque;

    return rp->migrate_msix;
}

static void gen_rp_realize(DeviceState *dev, Error **errp)
{
    PCIDevice *d = PCI_DEVICE(dev);
    GenPCIERootPort *grp = GEN_PCIE_ROOT_PORT(d);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(d);
    Error *local_err = NULL;

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    int rc = pci_bridge_qemu_reserve_cap_init(d, 0, grp->bus_reserve,
            grp->io_reserve, grp->mem_reserve, grp->pref32_reserve,
            grp->pref64_reserve, errp);

    if (rc < 0) {
        rpc->parent_class.exit(d);
        return;
    }

    if (!grp->io_reserve) {
        pci_word_test_and_clear_mask(d->wmask + PCI_COMMAND,
                                     PCI_COMMAND_IO);
        d->wmask[PCI_IO_BASE] = 0;
        d->wmask[PCI_IO_LIMIT] = 0;
    }
}

static const VMStateDescription vmstate_rp_dev = {
    .name = "pcie-root-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_MSIX_TEST(parent_obj.parent_obj.parent_obj.parent_obj,
                          GenPCIERootPort,
                          gen_rp_test_migrate_msix),
        VMSTATE_END_OF_LIST()
    }
};

static Property gen_rp_props[] = {
    DEFINE_PROP_BOOL("x-migrate-msix", GenPCIERootPort, migrate_msix, true),
    DEFINE_PROP_UINT32("bus-reserve", GenPCIERootPort, bus_reserve, -1),
    DEFINE_PROP_SIZE("io-reserve", GenPCIERootPort, io_reserve, -1),
    DEFINE_PROP_SIZE("mem-reserve", GenPCIERootPort, mem_reserve, -1),
    DEFINE_PROP_SIZE("pref32-reserve", GenPCIERootPort, pref32_reserve, -1),
    DEFINE_PROP_SIZE("pref64-reserve", GenPCIERootPort, pref64_reserve, -1),
    DEFINE_PROP_END_OF_LIST()
};

static void gen_rp_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_RP;
    dc->desc = "PCI Express Root Port";
    dc->vmsd = &vmstate_rp_dev;
    dc->props = gen_rp_props;

    rpc->parent_realize = dc->realize;
    dc->realize = gen_rp_realize;

    rpc->aer_vector = gen_rp_aer_vector;
    rpc->interrupts_init = gen_rp_interrupts_init;
    rpc->interrupts_uninit = gen_rp_interrupts_uninit;
    rpc->aer_offset = GEN_PCIE_ROOT_PORT_AER_OFFSET;
}

static const TypeInfo gen_rp_dev_info = {
    .name          = TYPE_GEN_PCIE_ROOT_PORT,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(GenPCIERootPort),
    .class_init    = gen_rp_dev_class_init,
};

 static void gen_rp_register_types(void)
 {
    type_register_static(&gen_rp_dev_info);
 }
 type_init(gen_rp_register_types)
