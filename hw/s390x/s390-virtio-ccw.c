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
#include "qemu/config-file.h"
#include "s390-pci-bus.h"

#define TYPE_S390_CCW_MACHINE               "s390-ccw-machine"

#define S390_CCW_MACHINE(obj) \
    OBJECT_CHECK(S390CcwMachineState, (obj), TYPE_S390_CCW_MACHINE)

typedef struct S390CcwMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    bool aes_key_wrap;
    bool dea_key_wrap;
} S390CcwMachineState;

void io_subsystem_reset(void)
{
    DeviceState *css, *sclp, *flic, *diag288;

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
    diag288 = DEVICE(object_resolve_path_type("", "diag288", NULL));
    if (diag288) {
        qdev_reset_all(diag288);
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
    if (queue >= VIRTIO_CCW_QUEUE_MAX) {
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
    sclpMemoryHotplugDev *mhd = init_sclp_memory_hotplug_dev();
    uint8_t *storage_keys;
    int ret;
    VirtualCssBus *css_bus;
    DeviceState *dev;
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("memory"), NULL);
    ram_addr_t pad_size = 0;
    ram_addr_t maxmem = qemu_opt_get_size(opts, "maxmem", my_ram_size);
    ram_addr_t standby_mem_size = maxmem - my_ram_size;
    uint64_t kvm_limit;

    /* The storage increment size is a multiple of 1M and is a power of 2.
     * The number of storage increments must be MAX_STORAGE_INCREMENTS or fewer.
     * The variable 'mhd->increment_size' is an exponent of 2 that can be
     * used to calculate the size (in bytes) of an increment. */
    mhd->increment_size = 20;
    while ((my_ram_size >> mhd->increment_size) > MAX_STORAGE_INCREMENTS) {
        mhd->increment_size++;
    }
    while ((standby_mem_size >> mhd->increment_size) > MAX_STORAGE_INCREMENTS) {
        mhd->increment_size++;
    }

    /* The core and standby memory areas need to be aligned with
     * the increment size.  In effect, this can cause the
     * user-specified memory size to be rounded down to align
     * with the nearest increment boundary. */
    standby_mem_size = standby_mem_size >> mhd->increment_size
                                        << mhd->increment_size;
    my_ram_size = my_ram_size >> mhd->increment_size
                              << mhd->increment_size;

    /* let's propagate the changed ram size into the global variable. */
    ram_size = my_ram_size;
    machine->maxram_size = my_ram_size + standby_mem_size;

    ret = s390_set_memory_limit(machine->maxram_size, &kvm_limit);
    if (ret == -E2BIG) {
        hw_error("qemu: host supports a maximum of %" PRIu64 " GB",
                 kvm_limit >> 30);
    } else if (ret) {
        hw_error("qemu: setting the guest size failed");
    }

    /* get a BUS */
    css_bus = virtual_css_bus_init();
    s390_sclp_init();
    s390_init_ipl_dev(machine->kernel_filename, machine->kernel_cmdline,
                      machine->initrd_filename, "s390-ccw.img", true);
    s390_flic_init();

    dev = qdev_create(NULL, TYPE_S390_PCI_HOST_BRIDGE);
    object_property_add_child(qdev_get_machine(), TYPE_S390_PCI_HOST_BRIDGE,
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);

    /* register hypercalls */
    virtio_ccw_register_hcalls();

    /* allocate RAM for core */
    memory_region_init_ram(ram, NULL, "s390.ram", my_ram_size, &error_abort);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(sysmem, 0, ram);

    /* If the size of ram is not on a MEM_SECTION_SIZE boundary,
       calculate the pad size necessary to force this boundary. */
    if (standby_mem_size) {
        if (my_ram_size % MEM_SECTION_SIZE) {
            pad_size = MEM_SECTION_SIZE - my_ram_size % MEM_SECTION_SIZE;
        }
        my_ram_size += standby_mem_size + pad_size;
        mhd->pad_size = pad_size;
        mhd->standby_mem_size = standby_mem_size;
    }

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

    /* Register savevm handler for guest TOD clock */
    register_savevm(NULL, "todclock", 0, 1,
                    gtod_save, gtod_load, kvm_state);
}

static void ccw_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->init = ccw_init;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->no_floppy = 1;
    mc->no_serial = 1;
    mc->no_parallel = 1;
    mc->no_sdcard = 1;
    mc->use_sclp = 1;
    mc->max_cpus = 255;
    nc->nmi_monitor_handler = s390_nmi;
}

static inline bool machine_get_aes_key_wrap(Object *obj, Error **errp)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(obj);

    return ms->aes_key_wrap;
}

static inline void machine_set_aes_key_wrap(Object *obj, bool value,
                                            Error **errp)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(obj);

    ms->aes_key_wrap = value;
}

static inline bool machine_get_dea_key_wrap(Object *obj, Error **errp)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(obj);

    return ms->dea_key_wrap;
}

static inline void machine_set_dea_key_wrap(Object *obj, bool value,
                                            Error **errp)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(obj);

    ms->dea_key_wrap = value;
}

static inline void s390_machine_initfn(Object *obj)
{
    object_property_add_bool(obj, "aes-key-wrap",
                             machine_get_aes_key_wrap,
                             machine_set_aes_key_wrap, NULL);
    object_property_set_description(obj, "aes-key-wrap",
            "enable/disable AES key wrapping using the CPACF wrapping key",
            NULL);
    object_property_set_bool(obj, true, "aes-key-wrap", NULL);

    object_property_add_bool(obj, "dea-key-wrap",
                             machine_get_dea_key_wrap,
                             machine_set_dea_key_wrap, NULL);
    object_property_set_description(obj, "dea-key-wrap",
            "enable/disable DEA key wrapping using the CPACF wrapping key",
            NULL);
    object_property_set_bool(obj, true, "dea-key-wrap", NULL);
}

static const TypeInfo ccw_machine_info = {
    .name          = TYPE_S390_CCW_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(S390CcwMachineState),
    .instance_init = s390_machine_initfn,
    .class_init    = ccw_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NMI },
        { }
    },
};

static void ccw_machine_2_4_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->name = "s390-ccw-virtio-2.4";
    mc->alias = "s390-ccw-virtio";
    mc->desc = "VirtIO-ccw based S390 machine v2.4";
    mc->is_default = 1;
}

static const TypeInfo ccw_machine_2_4_info = {
    .name          = TYPE_S390_CCW_MACHINE "2.4",
    .parent        = TYPE_S390_CCW_MACHINE,
    .class_init    = ccw_machine_2_4_class_init,
};

static void ccw_machine_register_types(void)
{
    type_register_static(&ccw_machine_info);
    type_register_static(&ccw_machine_2_4_info);
}

type_init(ccw_machine_register_types)
