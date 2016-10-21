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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "s390-virtio.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/ioinst.h"
#include "hw/s390x/css.h"
#include "virtio-ccw.h"
#include "qemu/config-file.h"
#include "s390-pci-bus.h"
#include "hw/s390x/storage-keys.h"
#include "hw/compat.h"
#include "ipl.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/css-bridge.h"

static const char *const reset_dev_types[] = {
    TYPE_VIRTUAL_CSS_BRIDGE,
    "s390-sclp-event-facility",
    "s390-flic",
    "diag288",
};

void subsystem_reset(void)
{
    DeviceState *dev;
    int i;

    for (i = 0; i < ARRAY_SIZE(reset_dev_types); i++) {
        dev = DEVICE(object_resolve_path_type("", reset_dev_types[i], NULL));
        if (dev) {
            qdev_reset_all(dev);
        }
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
    if (queue >= VIRTIO_QUEUE_MAX) {
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

void s390_memory_init(ram_addr_t mem_size)
{
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);

    /* allocate RAM for core */
    memory_region_allocate_system_memory(ram, NULL, "s390.ram", mem_size);
    memory_region_add_subregion(sysmem, 0, ram);

    /* Initialize storage key device */
    s390_skeys_init();
}

static void ccw_init(MachineState *machine)
{
    int ret;
    VirtualCssBus *css_bus;
    DeviceState *dev;

    s390_sclp_init();
    s390_memory_init(machine->ram_size);

    /* get a BUS */
    css_bus = virtual_css_bus_init();
    s390_init_ipl_dev(machine->kernel_filename, machine->kernel_cmdline,
                      machine->initrd_filename, "s390-ccw.img",
                      "s390-netboot.img", true);
    s390_flic_init();

    dev = qdev_create(NULL, TYPE_S390_PCI_HOST_BRIDGE);
    object_property_add_child(qdev_get_machine(), TYPE_S390_PCI_HOST_BRIDGE,
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);

    /* register hypercalls */
    virtio_ccw_register_hcalls();

    /* init CPUs */
    s390_init_cpus(machine);

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

static void s390_cpu_plug(HotplugHandler *hotplug_dev,
                        DeviceState *dev, Error **errp)
{
    gchar *name;
    S390CPU *cpu = S390_CPU(dev);
    CPUState *cs = CPU(dev);

    name = g_strdup_printf("cpu[%i]", cpu->env.cpu_num);
    object_property_set_link(OBJECT(hotplug_dev), OBJECT(cs), name,
                             errp);
    g_free(name);
}

static void s390_machine_device_plug(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        s390_cpu_plug(hotplug_dev, dev, errp);
    }
}

static HotplugHandler *s390_get_hotplug_handler(MachineState *machine,
                                                DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static void s390_hot_add_cpu(const int64_t id, Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    s390x_new_cpu(machine->cpu_model, id, errp);
}

static void ccw_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);
    S390CcwMachineClass *s390mc = S390_MACHINE_CLASS(mc);

    s390mc->ri_allowed = true;
    s390mc->cpu_model_allowed = true;
    mc->init = ccw_init;
    mc->reset = s390_machine_reset;
    mc->hot_add_cpu = s390_hot_add_cpu;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->no_floppy = 1;
    mc->no_serial = 1;
    mc->no_parallel = 1;
    mc->no_sdcard = 1;
    mc->use_sclp = 1;
    mc->max_cpus = 248;
    mc->get_hotplug_handler = s390_get_hotplug_handler;
    hc->plug = s390_machine_device_plug;
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

bool ri_allowed(void)
{
    if (kvm_enabled()) {
        MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());
        if (object_class_dynamic_cast(OBJECT_CLASS(mc),
                                      TYPE_S390_CCW_MACHINE)) {
            S390CcwMachineClass *s390mc = S390_MACHINE_CLASS(mc);

            return s390mc->ri_allowed;
        }
        /*
         * Make sure the "none" machine can have ri, otherwise it won't * be
         * unlocked in KVM and therefore the host CPU model might be wrong.
         */
        return true;
    }
    return 0;
}

bool cpu_model_allowed(void)
{
    MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());
    if (object_class_dynamic_cast(OBJECT_CLASS(mc),
                                  TYPE_S390_CCW_MACHINE)) {
        S390CcwMachineClass *s390mc = S390_MACHINE_CLASS(mc);

        return s390mc->cpu_model_allowed;
    }
    /* allow CPU model qmp queries with the "none" machine */
    return true;
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
    .class_size = sizeof(S390CcwMachineClass),
    .class_init    = ccw_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NMI },
        { TYPE_HOTPLUG_HANDLER},
        { }
    },
};

#define DEFINE_CCW_MACHINE(suffix, verstr, latest)                            \
    static void ccw_machine_##suffix##_class_init(ObjectClass *oc,            \
                                                  void *data)                 \
    {                                                                         \
        MachineClass *mc = MACHINE_CLASS(oc);                                 \
        ccw_machine_##suffix##_class_options(mc);                             \
        mc->desc = "VirtIO-ccw based S390 machine v" verstr;                  \
        if (latest) {                                                         \
            mc->alias = "s390-ccw-virtio";                                    \
            mc->is_default = 1;                                               \
        }                                                                     \
    }                                                                         \
    static void ccw_machine_##suffix##_instance_init(Object *obj)             \
    {                                                                         \
        MachineState *machine = MACHINE(obj);                                 \
        ccw_machine_##suffix##_instance_options(machine);                     \
    }                                                                         \
    static const TypeInfo ccw_machine_##suffix##_info = {                     \
        .name = MACHINE_TYPE_NAME("s390-ccw-virtio-" verstr),                 \
        .parent = TYPE_S390_CCW_MACHINE,                                      \
        .class_init = ccw_machine_##suffix##_class_init,                      \
        .instance_init = ccw_machine_##suffix##_instance_init,                \
    };                                                                        \
    static void ccw_machine_register_##suffix(void)                           \
    {                                                                         \
        type_register_static(&ccw_machine_##suffix##_info);                   \
    }                                                                         \
    type_init(ccw_machine_register_##suffix)

#define CCW_COMPAT_2_8 \
        HW_COMPAT_2_8 \
        {\
            .driver   = TYPE_S390_FLIC_COMMON,\
            .property = "adapter_routes_max_batch",\
            .value    = "64",\
        },

#define CCW_COMPAT_2_7 \
        HW_COMPAT_2_7

#define CCW_COMPAT_2_6 \
        HW_COMPAT_2_6 \
        {\
            .driver   = TYPE_S390_IPL,\
            .property = "iplbext_migration",\
            .value    = "off",\
        }, {\
            .driver   = TYPE_VIRTUAL_CSS_BRIDGE,\
            .property = "css_dev_path",\
            .value    = "off",\
        },

#define CCW_COMPAT_2_5 \
        HW_COMPAT_2_5

#define CCW_COMPAT_2_4 \
        HW_COMPAT_2_4 \
        {\
            .driver   = TYPE_S390_SKEYS,\
            .property = "migration-enabled",\
            .value    = "off",\
        },{\
            .driver   = "virtio-blk-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },{\
            .driver   = "virtio-balloon-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },{\
            .driver   = "virtio-serial-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },{\
            .driver   = "virtio-9p-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },{\
            .driver   = "virtio-rng-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },{\
            .driver   = "virtio-net-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },{\
            .driver   = "virtio-scsi-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },{\
            .driver   = "vhost-scsi-ccw",\
            .property = "max_revision",\
            .value    = "0",\
        },

static void ccw_machine_2_9_instance_options(MachineState *machine)
{
}

static void ccw_machine_2_9_class_options(MachineClass *mc)
{
}
DEFINE_CCW_MACHINE(2_9, "2.9", true);

static void ccw_machine_2_8_instance_options(MachineState *machine)
{
    ccw_machine_2_9_instance_options(machine);
}

static void ccw_machine_2_8_class_options(MachineClass *mc)
{
    ccw_machine_2_9_class_options(mc);
    SET_MACHINE_COMPAT(mc, CCW_COMPAT_2_8);
}
DEFINE_CCW_MACHINE(2_8, "2.8", false);

static void ccw_machine_2_7_instance_options(MachineState *machine)
{
    ccw_machine_2_8_instance_options(machine);
}

static void ccw_machine_2_7_class_options(MachineClass *mc)
{
    S390CcwMachineClass *s390mc = S390_MACHINE_CLASS(mc);

    s390mc->cpu_model_allowed = false;
    ccw_machine_2_8_class_options(mc);
    SET_MACHINE_COMPAT(mc, CCW_COMPAT_2_7);
}
DEFINE_CCW_MACHINE(2_7, "2.7", false);

static void ccw_machine_2_6_instance_options(MachineState *machine)
{
    ccw_machine_2_7_instance_options(machine);
}

static void ccw_machine_2_6_class_options(MachineClass *mc)
{
    S390CcwMachineClass *s390mc = S390_MACHINE_CLASS(mc);

    s390mc->ri_allowed = false;
    ccw_machine_2_7_class_options(mc);
    SET_MACHINE_COMPAT(mc, CCW_COMPAT_2_6);
}
DEFINE_CCW_MACHINE(2_6, "2.6", false);

static void ccw_machine_2_5_instance_options(MachineState *machine)
{
    ccw_machine_2_6_instance_options(machine);
}

static void ccw_machine_2_5_class_options(MachineClass *mc)
{
    ccw_machine_2_6_class_options(mc);
    SET_MACHINE_COMPAT(mc, CCW_COMPAT_2_5);
}
DEFINE_CCW_MACHINE(2_5, "2.5", false);

static void ccw_machine_2_4_instance_options(MachineState *machine)
{
    ccw_machine_2_5_instance_options(machine);
}

static void ccw_machine_2_4_class_options(MachineClass *mc)
{
    ccw_machine_2_5_class_options(mc);
    SET_MACHINE_COMPAT(mc, CCW_COMPAT_2_4);
}
DEFINE_CCW_MACHINE(2_4, "2.4", false);

static void ccw_machine_register_types(void)
{
    type_register_static(&ccw_machine_info);
}

type_init(ccw_machine_register_types)
