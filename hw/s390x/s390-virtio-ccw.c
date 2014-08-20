/*
 * virtio ccw machine
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "s390-virtio.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/s390_flic.h"
#include "ioinst.h"
#include "css.h"
#include "virtio-ccw.h"

#define TYPE_S390_CCW_MACHINE               "s390-ccw-machine"

void io_subsystem_reset(void)
{
    DeviceState *css, *sclp, *flic;

    css = DEVICE(object_resolve_path_type("", "virtual-css-bridge", NULL));
    if (css) {
        qdev_reset_all(css);
    }
    sclp = DEVICE(object_resolve_path_type("",
                  "s390-sclp-event-facility", NULL));
    if (sclp) {
        qdev_reset_all(sclp);
    }
    flic = DEVICE(object_resolve_path_type("", "s390-flic", NULL));
    if (flic) {
        qdev_reset_all(flic);
    }
}

static int virtio_ccw_hcall_notify(const uint64_t *args)
{
    uint64_t subch_id = args[0];
    uint64_t queue = args[1];
    SubchDev *sch;
    int cssid, ssid, schid, m;

    if (ioinst_disassemble_sch_ident(subch_id, &m, &cssid, &ssid, &schid)) {
        return -EINVAL;
    }
    sch = css_find_subch(m, cssid, ssid, schid);
    if (!sch || !css_subch_visible(sch)) {
        return -EINVAL;
    }
    if (queue >= VIRTIO_PCI_QUEUE_MAX) {
        return -EINVAL;
    }
    virtio_queue_notify(virtio_ccw_get_vdev(sch), queue);
    return 0;

}

static int virtio_ccw_hcall_early_printk(const uint64_t *args)
{
    uint64_t mem = args[0];

    if (mem < ram_size) {
        /* Early printk */
        return 0;
    }
    return -EINVAL;
}

static void virtio_ccw_register_hcalls(void)
{
    s390_register_virtio_hypercall(KVM_S390_VIRTIO_CCW_NOTIFY,
                                   virtio_ccw_hcall_notify);
    /* Tolerate early printk. */
    s390_register_virtio_hypercall(KVM_S390_VIRTIO_NOTIFY,
                                   virtio_ccw_hcall_early_printk);
}

static void ccw_init(MachineState *machine)
{
    ram_addr_t my_ram_size = machine->ram_size;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    int shift = 0;
    uint8_t *storage_keys;
    int ret;
    VirtualCssBus *css_bus;

    /* s390x ram size detection needs a 16bit multiplier + an increment. So
       guests > 64GB can be specified in 2MB steps etc. */
    while ((my_ram_size >> (20 + shift)) > 65535) {
        shift++;
    }
    my_ram_size = my_ram_size >> (20 + shift) << (20 + shift);

    /* let's propagate the changed ram size into the global variable. */
    ram_size = my_ram_size;

    /* get a BUS */
    css_bus = virtual_css_bus_init();
    s390_sclp_init();
    s390_init_ipl_dev(machine->kernel_filename, machine->kernel_cmdline,
                      machine->initrd_filename, "s390-ccw.img");
    s390_flic_init();

    /* register hypercalls */
    virtio_ccw_register_hcalls();

    /* allocate RAM */
    memory_region_init_ram(ram, NULL, "s390.ram", my_ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(sysmem, 0, ram);

    /* allocate storage keys */
    storage_keys = g_malloc0(my_ram_size / TARGET_PAGE_SIZE);

    /* init CPUs */
    s390_init_cpus(machine->cpu_model, storage_keys);

    if (kvm_enabled()) {
        kvm_s390_enable_css_support(s390_cpu_addr2state(0));
    }
    /*
     * Create virtual css and set it as default so that non mcss-e
     * enabled guests only see virtio devices.
     */
    ret = css_create_css_image(VIRTUAL_CSSID, true);
    assert(ret == 0);

    /* Create VirtIO network adapters */
    s390_create_virtio_net(BUS(css_bus), "virtio-net-ccw");
}

static void ccw_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->name = "s390-ccw-virtio";
    mc->alias = "s390-ccw";
    mc->desc = "VirtIO-ccw based S390 machine";
    mc->init = ccw_init;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->no_floppy = 1;
    mc->no_serial = 1;
    mc->no_parallel = 1;
    mc->no_sdcard = 1;
    mc->use_sclp = 1,
    mc->max_cpus = 255;
}

static const TypeInfo ccw_machine_info = {
    .name          = TYPE_S390_CCW_MACHINE,
    .parent        = TYPE_MACHINE,
    .class_init    = ccw_machine_class_init,
};

static void ccw_machine_register_types(void)
{
    type_register_static(&ccw_machine_info);
}

type_init(ccw_machine_register_types)
