/*
 * virtio ccw machine
 *
 * Copyright 2012, 2020 IBM Corp.
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *            Janosch Frank <frankja@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/ram_addr.h"
#include "hw/s390x/s390-virtio-hcall.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/ioinst.h"
#include "hw/s390x/css.h"
#include "virtio-ccw.h"
#include "qemu/config-file.h"
#include "qemu/ctype.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qemu/units.h"
#include "hw/s390x/s390-pci-bus.h"
#include "sysemu/reset.h"
#include "hw/s390x/storage-keys.h"
#include "hw/s390x/storage-attributes.h"
#include "hw/s390x/event-facility.h"
#include "ipl.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/css-bridge.h"
#include "hw/s390x/ap-bridge.h"
#include "migration/register.h"
#include "cpu_models.h"
#include "hw/nmi.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/tod.h"
#include "sysemu/sysemu.h"
#include "hw/s390x/pv.h"
#include "migration/blocker.h"
#include "qapi/visitor.h"

static Error *pv_mig_blocker;

S390CPU *s390_cpu_addr2state(uint16_t cpu_addr)
{
    static MachineState *ms;

    if (!ms) {
        ms = MACHINE(qdev_get_machine());
        g_assert(ms->possible_cpus);
    }

    /* CPU address corresponds to the core_id and the index */
    if (cpu_addr >= ms->possible_cpus->len) {
        return NULL;
    }
    return S390_CPU(ms->possible_cpus->cpus[cpu_addr].cpu);
}

static S390CPU *s390x_new_cpu(const char *typename, uint32_t core_id,
                              Error **errp)
{
    S390CPU *cpu = S390_CPU(object_new(typename));
    S390CPU *ret = NULL;

    if (!object_property_set_int(OBJECT(cpu), "core-id", core_id, errp)) {
        goto out;
    }
    if (!qdev_realize(DEVICE(cpu), NULL, errp)) {
        goto out;
    }
    ret = cpu;

out:
    object_unref(OBJECT(cpu));
    return ret;
}

static void s390_init_cpus(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    S390CcwMachineClass *s390mc = S390_CCW_MACHINE_CLASS(mc);
    int i;

    if (machine->smp.threads > s390mc->max_threads) {
        error_report("S390 does not support more than %d threads.",
                     s390mc->max_threads);
        exit(1);
    }

    /* initialize possible_cpus */
    mc->possible_cpu_arch_ids(machine);

    for (i = 0; i < machine->smp.cpus; i++) {
        s390x_new_cpu(machine->cpu_type, i, &error_fatal);
    }
}

static const char *const reset_dev_types[] = {
    TYPE_VIRTUAL_CSS_BRIDGE,
    "s390-sclp-event-facility",
    "s390-flic",
    "diag288",
    TYPE_S390_PCI_HOST_BRIDGE,
};

static void subsystem_reset(void)
{
    DeviceState *dev;
    int i;

    for (i = 0; i < ARRAY_SIZE(reset_dev_types); i++) {
        dev = DEVICE(object_resolve_path_type("", reset_dev_types[i], NULL));
        if (dev) {
            device_cold_reset(dev);
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
    MachineState *ms = MACHINE(qdev_get_machine());

    if (mem < ms->ram_size) {
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

static void s390_memory_init(MemoryRegion *ram)
{
    MemoryRegion *sysmem = get_system_memory();

    /* allocate RAM for core */
    memory_region_add_subregion(sysmem, 0, ram);

    /*
     * Configure the maximum page size. As no memory devices were created
     * yet, this is the page size of initial memory only.
     */
    s390_set_max_pagesize(qemu_maxrampagesize(), &error_fatal);
    /* Initialize storage key device */
    s390_skeys_init();
    /* Initialize storage attributes device */
    s390_stattrib_init();
}

static void s390_init_ipl_dev(const char *kernel_filename,
                              const char *kernel_cmdline,
                              const char *initrd_filename, const char *firmware,
                              const char *netboot_fw, bool enforce_bios)
{
    Object *new = object_new(TYPE_S390_IPL);
    DeviceState *dev = DEVICE(new);
    char *netboot_fw_prop;

    if (kernel_filename) {
        qdev_prop_set_string(dev, "kernel", kernel_filename);
    }
    if (initrd_filename) {
        qdev_prop_set_string(dev, "initrd", initrd_filename);
    }
    qdev_prop_set_string(dev, "cmdline", kernel_cmdline);
    qdev_prop_set_string(dev, "firmware", firmware);
    qdev_prop_set_bit(dev, "enforce_bios", enforce_bios);
    netboot_fw_prop = object_property_get_str(new, "netboot_fw", &error_abort);
    if (!strlen(netboot_fw_prop)) {
        qdev_prop_set_string(dev, "netboot_fw", netboot_fw);
    }
    g_free(netboot_fw_prop);
    object_property_add_child(qdev_get_machine(), TYPE_S390_IPL,
                              new);
    object_unref(new);
    qdev_realize(dev, NULL, &error_fatal);
}

static void s390_create_virtio_net(BusState *bus, const char *name)
{
    int i;

    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        DeviceState *dev;

        if (!nd->model) {
            nd->model = g_strdup("virtio");
        }

        qemu_check_nic_model(nd, "virtio");

        dev = qdev_new(name);
        qdev_set_nic_properties(dev, nd);
        qdev_realize_and_unref(dev, bus, &error_fatal);
    }
}

static void s390_create_sclpconsole(const char *type, Chardev *chardev)
{
    DeviceState *dev;

    dev = qdev_new(type);
    qdev_prop_set_chr(dev, "chardev", chardev);
    qdev_realize_and_unref(dev, sclp_get_event_facility_bus(), &error_fatal);
}

static void ccw_init(MachineState *machine)
{
    int ret;
    VirtualCssBus *css_bus;
    DeviceState *dev;

    s390_sclp_init();
    /* init memory + setup max page size. Required for the CPU model */
    s390_memory_init(machine->ram);

    /* init CPUs (incl. CPU model) early so s390_has_feature() works */
    s390_init_cpus(machine);

    /* Need CPU model to be determined before we can set up PV */
    s390_pv_init(machine->cgs, &error_fatal);

    s390_flic_init();

    /* init the SIGP facility */
    s390_init_sigp();

    /* create AP bridge and bus(es) */
    s390_init_ap();

    /* get a BUS */
    css_bus = virtual_css_bus_init();
    s390_init_ipl_dev(machine->kernel_filename, machine->kernel_cmdline,
                      machine->initrd_filename,
                      machine->firmware ?: "s390-ccw.img",
                      "s390-netboot.img", true);

    dev = qdev_new(TYPE_S390_PCI_HOST_BRIDGE);
    object_property_add_child(qdev_get_machine(), TYPE_S390_PCI_HOST_BRIDGE,
                              OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* register hypercalls */
    virtio_ccw_register_hcalls();

    s390_enable_css_support(s390_cpu_addr2state(0));

    ret = css_create_css_image(VIRTUAL_CSSID, true);

    assert(ret == 0);
    if (css_migration_enabled()) {
        css_register_vmstate();
    }

    /* Create VirtIO network adapters */
    s390_create_virtio_net(BUS(css_bus), "virtio-net-ccw");

    /* init consoles */
    if (serial_hd(0)) {
        s390_create_sclpconsole("sclpconsole", serial_hd(0));
    }
    if (serial_hd(1)) {
        s390_create_sclpconsole("sclplmconsole", serial_hd(1));
    }

    /* init the TOD clock */
    s390_init_tod();
}

static void s390_cpu_plug(HotplugHandler *hotplug_dev,
                        DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(hotplug_dev);
    S390CPU *cpu = S390_CPU(dev);

    g_assert(!ms->possible_cpus->cpus[cpu->env.core_id].cpu);
    ms->possible_cpus->cpus[cpu->env.core_id].cpu = OBJECT(dev);

    if (dev->hotplugged) {
        raise_irq_cpu_hotplug();
    }
}

static inline void s390_do_cpu_ipl(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);

    s390_ipl_prepare_cpu(cpu);
    s390_cpu_set_state(S390_CPU_STATE_OPERATING, cpu);
}

static void s390_machine_unprotect(S390CcwMachineState *ms)
{
    s390_pv_vm_disable();
    ms->pv = false;
    migrate_del_blocker(pv_mig_blocker);
    error_free_or_abort(&pv_mig_blocker);
    ram_block_discard_disable(false);
}

static int s390_machine_protect(S390CcwMachineState *ms)
{
    Error *local_err = NULL;
    int rc;

   /*
    * Discarding of memory in RAM blocks does not work as expected with
    * protected VMs. Sharing and unsharing pages would be required. Disable
    * it for now, until until we have a solution to make at least Linux
    * guests either support it (e.g., virtio-balloon) or fail gracefully.
    */
    rc = ram_block_discard_disable(true);
    if (rc) {
        error_report("protected VMs: cannot disable RAM discard");
        return rc;
    }

    error_setg(&pv_mig_blocker,
               "protected VMs are currently not migratable.");
    rc = migrate_add_blocker(pv_mig_blocker, &local_err);
    if (rc) {
        ram_block_discard_disable(false);
        error_report_err(local_err);
        error_free_or_abort(&pv_mig_blocker);
        return rc;
    }

    /* Create SE VM */
    rc = s390_pv_vm_enable();
    if (rc) {
        ram_block_discard_disable(false);
        migrate_del_blocker(pv_mig_blocker);
        error_free_or_abort(&pv_mig_blocker);
        return rc;
    }

    ms->pv = true;

    /* Will return 0 if API is not available since it's not vital */
    rc = s390_pv_query_info();
    if (rc) {
        goto out_err;
    }

    /* Set SE header and unpack */
    rc = s390_ipl_prepare_pv_header();
    if (rc) {
        goto out_err;
    }

    /* Decrypt image */
    rc = s390_ipl_pv_unpack();
    if (rc) {
        goto out_err;
    }

    /* Verify integrity */
    rc = s390_pv_verify();
    if (rc) {
        goto out_err;
    }
    return rc;

out_err:
    s390_machine_unprotect(ms);
    return rc;
}

static void s390_pv_prepare_reset(S390CcwMachineState *ms)
{
    CPUState *cs;

    if (!s390_is_pv()) {
        return;
    }
    /* Unsharing requires all cpus to be stopped */
    CPU_FOREACH(cs) {
        s390_cpu_set_state(S390_CPU_STATE_STOPPED, S390_CPU(cs));
    }
    s390_pv_unshare();
    s390_pv_prep_reset();
}

static void s390_machine_reset(MachineState *machine, ShutdownCause reason)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(machine);
    enum s390_reset reset_type;
    CPUState *cs, *t;
    S390CPU *cpu;

    /* get the reset parameters, reset them once done */
    s390_ipl_get_reset_request(&cs, &reset_type);

    /* all CPUs are paused and synchronized at this point */
    s390_cmma_reset();

    cpu = S390_CPU(cs);

    switch (reset_type) {
    case S390_RESET_EXTERNAL:
    case S390_RESET_REIPL:
        if (s390_is_pv()) {
            s390_machine_unprotect(ms);
        }

        qemu_devices_reset(reason);
        s390_crypto_reset();

        /* configure and start the ipl CPU only */
        run_on_cpu(cs, s390_do_cpu_ipl, RUN_ON_CPU_NULL);
        break;
    case S390_RESET_MODIFIED_CLEAR:
        /*
         * Subsystem reset needs to be done before we unshare memory
         * and lose access to VIRTIO structures in guest memory.
         */
        subsystem_reset();
        s390_crypto_reset();
        s390_pv_prepare_reset(ms);
        CPU_FOREACH(t) {
            run_on_cpu(t, s390_do_cpu_full_reset, RUN_ON_CPU_NULL);
        }
        run_on_cpu(cs, s390_do_cpu_load_normal, RUN_ON_CPU_NULL);
        break;
    case S390_RESET_LOAD_NORMAL:
        /*
         * Subsystem reset needs to be done before we unshare memory
         * and lose access to VIRTIO structures in guest memory.
         */
        subsystem_reset();
        s390_pv_prepare_reset(ms);
        CPU_FOREACH(t) {
            if (t == cs) {
                continue;
            }
            run_on_cpu(t, s390_do_cpu_reset, RUN_ON_CPU_NULL);
        }
        run_on_cpu(cs, s390_do_cpu_initial_reset, RUN_ON_CPU_NULL);
        run_on_cpu(cs, s390_do_cpu_load_normal, RUN_ON_CPU_NULL);
        break;
    case S390_RESET_PV: /* Subcode 10 */
        subsystem_reset();
        s390_crypto_reset();

        CPU_FOREACH(t) {
            if (t == cs) {
                continue;
            }
            run_on_cpu(t, s390_do_cpu_full_reset, RUN_ON_CPU_NULL);
        }
        run_on_cpu(cs, s390_do_cpu_reset, RUN_ON_CPU_NULL);

        if (s390_machine_protect(ms)) {
            s390_pv_inject_reset_error(cs);
            /*
             * Continue after the diag308 so the guest knows something
             * went wrong.
             */
            s390_cpu_set_state(S390_CPU_STATE_OPERATING, cpu);
            return;
        }

        run_on_cpu(cs, s390_do_cpu_load_normal, RUN_ON_CPU_NULL);
        break;
    default:
        g_assert_not_reached();
    }

    CPU_FOREACH(t) {
        run_on_cpu(t, s390_do_cpu_set_diag318, RUN_ON_CPU_HOST_ULONG(0));
    }
    s390_ipl_clear_reset_request();
}

static void s390_machine_device_plug(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        s390_cpu_plug(hotplug_dev, dev, errp);
    }
}

static void s390_machine_device_unplug_request(HotplugHandler *hotplug_dev,
                                               DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        error_setg(errp, "CPU hot unplug not supported on this machine");
        return;
    }
}

static CpuInstanceProperties s390_cpu_index_to_props(MachineState *ms,
                                                     unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static const CPUArchIdList *s390_possible_cpu_arch_ids(MachineState *ms)
{
    int i;
    unsigned int max_cpus = ms->smp.max_cpus;

    if (ms->possible_cpus) {
        g_assert(ms->possible_cpus && ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (i = 0; i < ms->possible_cpus->len; i++) {
        ms->possible_cpus->cpus[i].type = ms->cpu_type;
        ms->possible_cpus->cpus[i].vcpus_count = 1;
        ms->possible_cpus->cpus[i].arch_id = i;
        ms->possible_cpus->cpus[i].props.has_core_id = true;
        ms->possible_cpus->cpus[i].props.core_id = i;
    }

    return ms->possible_cpus;
}

static HotplugHandler *s390_get_hotplug_handler(MachineState *machine,
                                                DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static void s390_nmi(NMIState *n, int cpu_index, Error **errp)
{
    CPUState *cs = qemu_get_cpu(cpu_index);

    s390_cpu_restart(S390_CPU(cs));
}

static ram_addr_t s390_fixup_ram_size(ram_addr_t sz)
{
    /* same logic as in sclp.c */
    int increment_size = 20;
    ram_addr_t newsz;

    while ((sz >> increment_size) > MAX_STORAGE_INCREMENTS) {
        increment_size++;
    }
    newsz = sz >> increment_size << increment_size;

    if (sz != newsz) {
        qemu_printf("Ram size %" PRIu64 "MB was fixed up to %" PRIu64
                    "MB to match machine restrictions. Consider updating "
                    "the guest definition.\n", (uint64_t) (sz / MiB),
                    (uint64_t) (newsz / MiB));
    }
    return newsz;
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

static S390CcwMachineClass *current_mc;

/*
 * Get the class of the s390-ccw-virtio machine that is currently in use.
 * Note: libvirt is using the "none" machine to probe for the features of the
 * host CPU, so in case this is called with the "none" machine, the function
 * returns the TYPE_S390_CCW_MACHINE base class. In this base class, all the
 * various "*_allowed" variables are enabled, so that the *_allowed() wrappers
 * below return the correct default value for the "none" machine.
 *
 * Attention! Do *not* add additional new wrappers for CPU features (e.g. like
 * the ri_allowed() wrapper) via this mechanism anymore. CPU features should
 * be handled via the CPU models, i.e. checking with cpu_model_allowed() during
 * CPU initialization and s390_has_feat() later should be sufficient.
 */
static S390CcwMachineClass *get_machine_class(void)
{
    if (unlikely(!current_mc)) {
        /*
        * No s390 ccw machine was instantiated, we are likely to
        * be called for the 'none' machine. The properties will
        * have their after-initialization values.
        */
        current_mc = S390_CCW_MACHINE_CLASS(
                     object_class_by_name(TYPE_S390_CCW_MACHINE));
    }
    return current_mc;
}

bool ri_allowed(void)
{
    return get_machine_class()->ri_allowed;
}

bool cpu_model_allowed(void)
{
    return get_machine_class()->cpu_model_allowed;
}

bool hpage_1m_allowed(void)
{
    return get_machine_class()->hpage_1m_allowed;
}

static void machine_get_loadparm(Object *obj, Visitor *v,
                                 const char *name, void *opaque,
                                 Error **errp)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(obj);
    char *str = g_strndup((char *) ms->loadparm, sizeof(ms->loadparm));

    visit_type_str(v, name, &str, errp);
    g_free(str);
}

static void machine_set_loadparm(Object *obj, Visitor *v,
                                 const char *name, void *opaque,
                                 Error **errp)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(obj);
    char *val;
    int i;

    if (!visit_type_str(v, name, &val, errp)) {
        return;
    }

    for (i = 0; i < sizeof(ms->loadparm) && val[i]; i++) {
        uint8_t c = qemu_toupper(val[i]); /* mimic HMC */

        if (('A' <= c && c <= 'Z') || ('0' <= c && c <= '9') || (c == '.') ||
            (c == ' ')) {
            ms->loadparm[i] = c;
        } else {
            error_setg(errp, "LOADPARM: invalid character '%c' (ASCII 0x%02x)",
                       c, c);
            return;
        }
    }

    for (; i < sizeof(ms->loadparm); i++) {
        ms->loadparm[i] = ' '; /* pad right with spaces */
    }
}

static void ccw_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);
    S390CcwMachineClass *s390mc = S390_CCW_MACHINE_CLASS(mc);

    s390mc->ri_allowed = true;
    s390mc->cpu_model_allowed = true;
    s390mc->css_migration_enabled = true;
    s390mc->hpage_1m_allowed = true;
    s390mc->max_threads = 1;
    mc->init = ccw_init;
    mc->reset = s390_machine_reset;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->no_floppy = 1;
    mc->no_parallel = 1;
    mc->no_sdcard = 1;
    mc->max_cpus = S390_MAX_CPUS;
    mc->has_hotpluggable_cpus = true;
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = s390_get_hotplug_handler;
    mc->cpu_index_to_instance_props = s390_cpu_index_to_props;
    mc->possible_cpu_arch_ids = s390_possible_cpu_arch_ids;
    /* it is overridden with 'host' cpu *in kvm_arch_init* */
    mc->default_cpu_type = S390_CPU_TYPE_NAME("qemu");
    hc->plug = s390_machine_device_plug;
    hc->unplug_request = s390_machine_device_unplug_request;
    nc->nmi_monitor_handler = s390_nmi;
    mc->default_ram_id = "s390.ram";

    object_class_property_add_bool(oc, "aes-key-wrap",
                                   machine_get_aes_key_wrap,
                                   machine_set_aes_key_wrap);
    object_class_property_set_description(oc, "aes-key-wrap",
            "enable/disable AES key wrapping using the CPACF wrapping key");

    object_class_property_add_bool(oc, "dea-key-wrap",
                                   machine_get_dea_key_wrap,
                                   machine_set_dea_key_wrap);
    object_class_property_set_description(oc, "dea-key-wrap",
            "enable/disable DEA key wrapping using the CPACF wrapping key");

    object_class_property_add(oc, "loadparm", "loadparm",
                              machine_get_loadparm, machine_set_loadparm,
                              NULL, NULL);
    object_class_property_set_description(oc, "loadparm",
            "Up to 8 chars in set of [A-Za-z0-9. ] (lower case chars converted"
            " to upper case) to pass to machine loader, boot manager,"
            " and guest kernel");
}

static inline void s390_machine_initfn(Object *obj)
{
    S390CcwMachineState *ms = S390_CCW_MACHINE(obj);

    ms->aes_key_wrap = true;
    ms->dea_key_wrap = true;
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

bool css_migration_enabled(void)
{
    return get_machine_class()->css_migration_enabled;
}

#define DEFINE_CCW_MACHINE(suffix, verstr, latest)                            \
    static void ccw_machine_##suffix##_class_init(ObjectClass *oc,            \
                                                  void *data)                 \
    {                                                                         \
        MachineClass *mc = MACHINE_CLASS(oc);                                 \
        ccw_machine_##suffix##_class_options(mc);                             \
        mc->desc = "Virtual s390x machine (version " verstr ")";              \
        if (latest) {                                                         \
            mc->alias = "s390-ccw-virtio";                                    \
            mc->is_default = true;                                            \
        }                                                                     \
    }                                                                         \
    static void ccw_machine_##suffix##_instance_init(Object *obj)             \
    {                                                                         \
        MachineState *machine = MACHINE(obj);                                 \
        current_mc = S390_CCW_MACHINE_CLASS(MACHINE_GET_CLASS(machine));          \
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

static void ccw_machine_8_0_instance_options(MachineState *machine)
{
}

static void ccw_machine_8_0_class_options(MachineClass *mc)
{
}
DEFINE_CCW_MACHINE(8_0, "8.0", true);

static void ccw_machine_7_2_instance_options(MachineState *machine)
{
    ccw_machine_8_0_instance_options(machine);
}

static void ccw_machine_7_2_class_options(MachineClass *mc)
{
    ccw_machine_8_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_2, hw_compat_7_2_len);
}
DEFINE_CCW_MACHINE(7_2, "7.2", false);

static void ccw_machine_7_1_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V7_1 };

    ccw_machine_7_2_instance_options(machine);
    s390_cpudef_featoff_greater(16, 1, S390_FEAT_PAIE);
    s390_set_qemu_cpu_model(0x8561, 15, 1, qemu_cpu_feat);
}

static void ccw_machine_7_1_class_options(MachineClass *mc)
{
    S390CcwMachineClass *s390mc = S390_CCW_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { TYPE_S390_PCI_DEVICE, "interpret", "off", },
        { TYPE_S390_PCI_DEVICE, "forwarding-assist", "off", },
    };

    ccw_machine_7_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_1, hw_compat_7_1_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
    s390mc->max_threads = S390_MAX_CPUS;
}
DEFINE_CCW_MACHINE(7_1, "7.1", false);

static void ccw_machine_7_0_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V7_0 };

    ccw_machine_7_1_instance_options(machine);
    s390_set_qemu_cpu_model(0x8561, 15, 1, qemu_cpu_feat);
}

static void ccw_machine_7_0_class_options(MachineClass *mc)
{
    ccw_machine_7_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_0, hw_compat_7_0_len);
}
DEFINE_CCW_MACHINE(7_0, "7.0", false);

static void ccw_machine_6_2_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V6_2 };

    ccw_machine_7_0_instance_options(machine);
    s390_set_qemu_cpu_model(0x3906, 14, 2, qemu_cpu_feat);
}

static void ccw_machine_6_2_class_options(MachineClass *mc)
{
    ccw_machine_7_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_2, hw_compat_6_2_len);
}
DEFINE_CCW_MACHINE(6_2, "6.2", false);

static void ccw_machine_6_1_instance_options(MachineState *machine)
{
    ccw_machine_6_2_instance_options(machine);
    s390_cpudef_featoff_greater(16, 1, S390_FEAT_NNPA);
    s390_cpudef_featoff_greater(16, 1, S390_FEAT_VECTOR_PACKED_DECIMAL_ENH2);
    s390_cpudef_featoff_greater(16, 1, S390_FEAT_BEAR_ENH);
    s390_cpudef_featoff_greater(16, 1, S390_FEAT_RDP);
    s390_cpudef_featoff_greater(16, 1, S390_FEAT_PAI);
}

static void ccw_machine_6_1_class_options(MachineClass *mc)
{
    ccw_machine_6_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_1, hw_compat_6_1_len);
    mc->smp_props.prefer_sockets = true;
}
DEFINE_CCW_MACHINE(6_1, "6.1", false);

static void ccw_machine_6_0_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V6_0 };

    ccw_machine_6_1_instance_options(machine);
    s390_set_qemu_cpu_model(0x2964, 13, 2, qemu_cpu_feat);
}

static void ccw_machine_6_0_class_options(MachineClass *mc)
{
    ccw_machine_6_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_0, hw_compat_6_0_len);
}
DEFINE_CCW_MACHINE(6_0, "6.0", false);

static void ccw_machine_5_2_instance_options(MachineState *machine)
{
    ccw_machine_6_0_instance_options(machine);
}

static void ccw_machine_5_2_class_options(MachineClass *mc)
{
    ccw_machine_6_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_2, hw_compat_5_2_len);
}
DEFINE_CCW_MACHINE(5_2, "5.2", false);

static void ccw_machine_5_1_instance_options(MachineState *machine)
{
    ccw_machine_5_2_instance_options(machine);
}

static void ccw_machine_5_1_class_options(MachineClass *mc)
{
    ccw_machine_5_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_1, hw_compat_5_1_len);
}
DEFINE_CCW_MACHINE(5_1, "5.1", false);

static void ccw_machine_5_0_instance_options(MachineState *machine)
{
    ccw_machine_5_1_instance_options(machine);
}

static void ccw_machine_5_0_class_options(MachineClass *mc)
{
    ccw_machine_5_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_0, hw_compat_5_0_len);
}
DEFINE_CCW_MACHINE(5_0, "5.0", false);

static void ccw_machine_4_2_instance_options(MachineState *machine)
{
    ccw_machine_5_0_instance_options(machine);
}

static void ccw_machine_4_2_class_options(MachineClass *mc)
{
    ccw_machine_5_0_class_options(mc);
    mc->fixup_ram_size = s390_fixup_ram_size;
    compat_props_add(mc->compat_props, hw_compat_4_2, hw_compat_4_2_len);
}
DEFINE_CCW_MACHINE(4_2, "4.2", false);

static void ccw_machine_4_1_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V4_1 };
    ccw_machine_4_2_instance_options(machine);
    s390_set_qemu_cpu_model(0x2964, 13, 2, qemu_cpu_feat);
}

static void ccw_machine_4_1_class_options(MachineClass *mc)
{
    ccw_machine_4_2_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_4_1, hw_compat_4_1_len);
}
DEFINE_CCW_MACHINE(4_1, "4.1", false);

static void ccw_machine_4_0_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V4_0 };
    ccw_machine_4_1_instance_options(machine);
    s390_set_qemu_cpu_model(0x2827, 12, 2, qemu_cpu_feat);
}

static void ccw_machine_4_0_class_options(MachineClass *mc)
{
    ccw_machine_4_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_4_0, hw_compat_4_0_len);
}
DEFINE_CCW_MACHINE(4_0, "4.0", false);

static void ccw_machine_3_1_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V3_1 };
    ccw_machine_4_0_instance_options(machine);
    s390_cpudef_featoff_greater(14, 1, S390_FEAT_MULTIPLE_EPOCH);
    s390_cpudef_group_featoff_greater(14, 1, S390_FEAT_GROUP_MULTIPLE_EPOCH_PTFF);
    s390_set_qemu_cpu_model(0x2827, 12, 2, qemu_cpu_feat);
}

static void ccw_machine_3_1_class_options(MachineClass *mc)
{
    ccw_machine_4_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_3_1, hw_compat_3_1_len);
}
DEFINE_CCW_MACHINE(3_1, "3.1", false);

static void ccw_machine_3_0_instance_options(MachineState *machine)
{
    ccw_machine_3_1_instance_options(machine);
}

static void ccw_machine_3_0_class_options(MachineClass *mc)
{
    S390CcwMachineClass *s390mc = S390_CCW_MACHINE_CLASS(mc);

    s390mc->hpage_1m_allowed = false;
    ccw_machine_3_1_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_3_0, hw_compat_3_0_len);
}
DEFINE_CCW_MACHINE(3_0, "3.0", false);

static void ccw_machine_2_12_instance_options(MachineState *machine)
{
    ccw_machine_3_0_instance_options(machine);
    s390_cpudef_featoff_greater(11, 1, S390_FEAT_PPA15);
    s390_cpudef_featoff_greater(11, 1, S390_FEAT_BPB);
}

static void ccw_machine_2_12_class_options(MachineClass *mc)
{
    ccw_machine_3_0_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_12, hw_compat_2_12_len);
}
DEFINE_CCW_MACHINE(2_12, "2.12", false);

static void ccw_machine_2_11_instance_options(MachineState *machine)
{
    static const S390FeatInit qemu_cpu_feat = { S390_FEAT_LIST_QEMU_V2_11 };
    ccw_machine_2_12_instance_options(machine);

    /* before 2.12 we emulated the very first z900 */
    s390_set_qemu_cpu_model(0x2064, 7, 1, qemu_cpu_feat);
}

static void ccw_machine_2_11_class_options(MachineClass *mc)
{
    static GlobalProperty compat[] = {
        { TYPE_SCLP_EVENT_FACILITY, "allow_all_mask_sizes", "off", },
    };

    ccw_machine_2_12_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_11, hw_compat_2_11_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}
DEFINE_CCW_MACHINE(2_11, "2.11", false);

static void ccw_machine_2_10_instance_options(MachineState *machine)
{
    ccw_machine_2_11_instance_options(machine);
}

static void ccw_machine_2_10_class_options(MachineClass *mc)
{
    ccw_machine_2_11_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_10, hw_compat_2_10_len);
}
DEFINE_CCW_MACHINE(2_10, "2.10", false);

static void ccw_machine_2_9_instance_options(MachineState *machine)
{
    ccw_machine_2_10_instance_options(machine);
    s390_cpudef_featoff_greater(12, 1, S390_FEAT_ESOP);
    s390_cpudef_featoff_greater(12, 1, S390_FEAT_SIDE_EFFECT_ACCESS_ESOP2);
    s390_cpudef_featoff_greater(12, 1, S390_FEAT_ZPCI);
    s390_cpudef_featoff_greater(12, 1, S390_FEAT_ADAPTER_INT_SUPPRESSION);
    s390_cpudef_featoff_greater(12, 1, S390_FEAT_ADAPTER_EVENT_NOTIFICATION);
}

static void ccw_machine_2_9_class_options(MachineClass *mc)
{
    S390CcwMachineClass *s390mc = S390_CCW_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { TYPE_S390_STATTRIB, "migration-enabled", "off", },
    };

    ccw_machine_2_10_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_9, hw_compat_2_9_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
    s390mc->css_migration_enabled = false;
}
DEFINE_CCW_MACHINE(2_9, "2.9", false);

static void ccw_machine_2_8_instance_options(MachineState *machine)
{
    ccw_machine_2_9_instance_options(machine);
}

static void ccw_machine_2_8_class_options(MachineClass *mc)
{
    static GlobalProperty compat[] = {
        { TYPE_S390_FLIC_COMMON, "adapter_routes_max_batch", "64", },
    };

    ccw_machine_2_9_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_8, hw_compat_2_8_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}
DEFINE_CCW_MACHINE(2_8, "2.8", false);

static void ccw_machine_2_7_instance_options(MachineState *machine)
{
    ccw_machine_2_8_instance_options(machine);
}

static void ccw_machine_2_7_class_options(MachineClass *mc)
{
    S390CcwMachineClass *s390mc = S390_CCW_MACHINE_CLASS(mc);

    s390mc->cpu_model_allowed = false;
    ccw_machine_2_8_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_7, hw_compat_2_7_len);
}
DEFINE_CCW_MACHINE(2_7, "2.7", false);

static void ccw_machine_2_6_instance_options(MachineState *machine)
{
    ccw_machine_2_7_instance_options(machine);
}

static void ccw_machine_2_6_class_options(MachineClass *mc)
{
    S390CcwMachineClass *s390mc = S390_CCW_MACHINE_CLASS(mc);
    static GlobalProperty compat[] = {
        { TYPE_S390_IPL, "iplbext_migration", "off", },
         { TYPE_VIRTUAL_CSS_BRIDGE, "css_dev_path", "off", },
    };

    s390mc->ri_allowed = false;
    ccw_machine_2_7_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_6, hw_compat_2_6_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}
DEFINE_CCW_MACHINE(2_6, "2.6", false);

static void ccw_machine_2_5_instance_options(MachineState *machine)
{
    ccw_machine_2_6_instance_options(machine);
}

static void ccw_machine_2_5_class_options(MachineClass *mc)
{
    ccw_machine_2_6_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_5, hw_compat_2_5_len);
}
DEFINE_CCW_MACHINE(2_5, "2.5", false);

static void ccw_machine_2_4_instance_options(MachineState *machine)
{
    ccw_machine_2_5_instance_options(machine);
}

static void ccw_machine_2_4_class_options(MachineClass *mc)
{
    static GlobalProperty compat[] = {
        { TYPE_S390_SKEYS, "migration-enabled", "off", },
        { "virtio-blk-ccw", "max_revision", "0", },
        { "virtio-balloon-ccw", "max_revision", "0", },
        { "virtio-serial-ccw", "max_revision", "0", },
        { "virtio-9p-ccw", "max_revision", "0", },
        { "virtio-rng-ccw", "max_revision", "0", },
        { "virtio-net-ccw", "max_revision", "0", },
        { "virtio-scsi-ccw", "max_revision", "0", },
        { "vhost-scsi-ccw", "max_revision", "0", },
    };

    ccw_machine_2_5_class_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_4, hw_compat_2_4_len);
    compat_props_add(mc->compat_props, compat, G_N_ELEMENTS(compat));
}
DEFINE_CCW_MACHINE(2_4, "2.4", false);

static void ccw_machine_register_types(void)
{
    type_register_static(&ccw_machine_info);
}

type_init(ccw_machine_register_types)
