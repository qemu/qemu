/*
 * SCLP Support
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Christian Borntraeger <borntraeger@de.ibm.com>
 *  Heinz Graalfs <graalfs@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "sysemu/kvm.h"
#include "exec/memory.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/ipl.h"

static inline SCLPDevice *get_sclp_device(void)
{
    static SCLPDevice *sclp;

    if (!sclp) {
        sclp = SCLP(object_resolve_path_type("", TYPE_SCLP, NULL));
    }
    return sclp;
}

static void prepare_cpu_entries(SCLPDevice *sclp, CPUEntry *entry, int count)
{
    uint8_t features[SCCB_CPU_FEATURE_LEN] = { 0 };
    int i;

    s390_get_feat_block(S390_FEAT_TYPE_SCLP_CPU, features);
    for (i = 0; i < count; i++) {
        entry[i].address = i;
        entry[i].type = 0;
        memcpy(entry[i].features, features, sizeof(entry[i].features));
    }
}

/* Provide information about the configuration, CPUs and storage */
static void read_SCP_info(SCLPDevice *sclp, SCCB *sccb)
{
    ReadInfo *read_info = (ReadInfo *) sccb;
    MachineState *machine = MACHINE(qdev_get_machine());
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();
    CPUState *cpu;
    int cpu_count = 0;
    int rnsize, rnmax;
    int slots = MIN(machine->ram_slots, s390_get_memslot_count(kvm_state));
    IplParameterBlock *ipib = s390_ipl_get_iplb();

    CPU_FOREACH(cpu) {
        cpu_count++;
    }

    /* CPU information */
    read_info->entries_cpu = cpu_to_be16(cpu_count);
    read_info->offset_cpu = cpu_to_be16(offsetof(ReadInfo, entries));
    read_info->highest_cpu = cpu_to_be16(max_cpus);

    read_info->ibc_val = cpu_to_be32(s390_get_ibc_val());

    /* Configuration Characteristic (Extension) */
    s390_get_feat_block(S390_FEAT_TYPE_SCLP_CONF_CHAR,
                         read_info->conf_char);
    s390_get_feat_block(S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT,
                         read_info->conf_char_ext);

    prepare_cpu_entries(sclp, read_info->entries, cpu_count);

    read_info->facilities = cpu_to_be64(SCLP_HAS_CPU_INFO |
                                        SCLP_HAS_PCI_RECONFIG);

    /* Memory Hotplug is only supported for the ccw machine type */
    if (mhd) {
        mhd->standby_subregion_size = MEM_SECTION_SIZE;
        /* Deduct the memory slot already used for core */
        if (slots > 0) {
            while ((mhd->standby_subregion_size * (slots - 1)
                    < mhd->standby_mem_size)) {
                mhd->standby_subregion_size = mhd->standby_subregion_size << 1;
            }
        }
        /*
         * Initialize mapping of guest standby memory sections indicating which
         * are and are not online. Assume all standby memory begins offline.
         */
        if (mhd->standby_state_map == 0) {
            if (mhd->standby_mem_size % mhd->standby_subregion_size) {
                mhd->standby_state_map = g_malloc0((mhd->standby_mem_size /
                                             mhd->standby_subregion_size + 1) *
                                             (mhd->standby_subregion_size /
                                             MEM_SECTION_SIZE));
            } else {
                mhd->standby_state_map = g_malloc0(mhd->standby_mem_size /
                                                   MEM_SECTION_SIZE);
            }
        }
        mhd->padded_ram_size = ram_size + mhd->pad_size;
        mhd->rzm = 1 << mhd->increment_size;

        read_info->facilities |= cpu_to_be64(SCLP_FC_ASSIGN_ATTACH_READ_STOR);
    }
    read_info->mha_pow = s390_get_mha_pow();
    read_info->hmfai = cpu_to_be32(s390_get_hmfai());

    rnsize = 1 << (sclp->increment_size - 20);
    if (rnsize <= 128) {
        read_info->rnsize = rnsize;
    } else {
        read_info->rnsize = 0;
        read_info->rnsize2 = cpu_to_be32(rnsize);
    }

    rnmax = machine->maxram_size >> sclp->increment_size;
    if (rnmax < 0x10000) {
        read_info->rnmax = cpu_to_be16(rnmax);
    } else {
        read_info->rnmax = cpu_to_be16(0);
        read_info->rnmax2 = cpu_to_be64(rnmax);
    }

    if (ipib && ipib->flags & DIAG308_FLAGS_LP_VALID) {
        memcpy(&read_info->loadparm, &ipib->loadparm,
               sizeof(read_info->loadparm));
    } else {
        s390_ipl_set_loadparm(read_info->loadparm);
    }

    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

static void read_storage_element0_info(SCLPDevice *sclp, SCCB *sccb)
{
    int i, assigned;
    int subincrement_id = SCLP_STARTING_SUBINCREMENT_ID;
    ReadStorageElementInfo *storage_info = (ReadStorageElementInfo *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();

    if (!mhd) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        return;
    }

    if ((ram_size >> mhd->increment_size) >= 0x10000) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_SCCB_BOUNDARY_VIOLATION);
        return;
    }

    /* Return information regarding core memory */
    storage_info->max_id = cpu_to_be16(mhd->standby_mem_size ? 1 : 0);
    assigned = ram_size >> mhd->increment_size;
    storage_info->assigned = cpu_to_be16(assigned);

    for (i = 0; i < assigned; i++) {
        storage_info->entries[i] = cpu_to_be32(subincrement_id);
        subincrement_id += SCLP_INCREMENT_UNIT;
    }
    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

static void read_storage_element1_info(SCLPDevice *sclp, SCCB *sccb)
{
    ReadStorageElementInfo *storage_info = (ReadStorageElementInfo *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();

    if (!mhd) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        return;
    }

    if ((mhd->standby_mem_size >> mhd->increment_size) >= 0x10000) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_SCCB_BOUNDARY_VIOLATION);
        return;
    }

    /* Return information regarding standby memory */
    storage_info->max_id = cpu_to_be16(mhd->standby_mem_size ? 1 : 0);
    storage_info->assigned = cpu_to_be16(mhd->standby_mem_size >>
                                         mhd->increment_size);
    storage_info->standby = cpu_to_be16(mhd->standby_mem_size >>
                                        mhd->increment_size);
    sccb->h.response_code = cpu_to_be16(SCLP_RC_STANDBY_READ_COMPLETION);
}

static void attach_storage_element(SCLPDevice *sclp, SCCB *sccb,
                                   uint16_t element)
{
    int i, assigned, subincrement_id;
    AttachStorageElement *attach_info = (AttachStorageElement *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();

    if (!mhd) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        return;
    }

    if (element != 1) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        return;
    }

    assigned = mhd->standby_mem_size >> mhd->increment_size;
    attach_info->assigned = cpu_to_be16(assigned);
    subincrement_id = ((ram_size >> mhd->increment_size) << 16)
                      + SCLP_STARTING_SUBINCREMENT_ID;
    for (i = 0; i < assigned; i++) {
        attach_info->entries[i] = cpu_to_be32(subincrement_id);
        subincrement_id += SCLP_INCREMENT_UNIT;
    }
    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_COMPLETION);
}

static void assign_storage(SCLPDevice *sclp, SCCB *sccb)
{
    MemoryRegion *mr = NULL;
    uint64_t this_subregion_size;
    AssignStorage *assign_info = (AssignStorage *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();
    ram_addr_t assign_addr;
    MemoryRegion *sysmem = get_system_memory();

    if (!mhd) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        return;
    }
    assign_addr = (assign_info->rn - 1) * mhd->rzm;

    if ((assign_addr % MEM_SECTION_SIZE == 0) &&
        (assign_addr >= mhd->padded_ram_size)) {
        /* Re-use existing memory region if found */
        mr = memory_region_find(sysmem, assign_addr, 1).mr;
        memory_region_unref(mr);
        if (!mr) {

            MemoryRegion *standby_ram = g_new(MemoryRegion, 1);

            /* offset to align to standby_subregion_size for allocation */
            ram_addr_t offset = assign_addr -
                                (assign_addr - mhd->padded_ram_size)
                                % mhd->standby_subregion_size;

            /* strlen("standby.ram") + 4 (Max of KVM_MEMORY_SLOTS) +  NULL */
            char id[16];
            snprintf(id, 16, "standby.ram%d",
                     (int)((offset - mhd->padded_ram_size) /
                     mhd->standby_subregion_size) + 1);

            /* Allocate a subregion of the calculated standby_subregion_size */
            if (offset + mhd->standby_subregion_size >
                mhd->padded_ram_size + mhd->standby_mem_size) {
                this_subregion_size = mhd->padded_ram_size +
                  mhd->standby_mem_size - offset;
            } else {
                this_subregion_size = mhd->standby_subregion_size;
            }

            memory_region_init_ram(standby_ram, NULL, id, this_subregion_size,
                                   &error_fatal);
            /* This is a hack to make memory hotunplug work again. Once we have
             * subdevices, we have to unparent them when unassigning memory,
             * instead of doing it via the ref count of the MemoryRegion. */
            object_ref(OBJECT(standby_ram));
            object_unparent(OBJECT(standby_ram));
            memory_region_add_subregion(sysmem, offset, standby_ram);
        }
        /* The specified subregion is no longer in standby */
        mhd->standby_state_map[(assign_addr - mhd->padded_ram_size)
                               / MEM_SECTION_SIZE] = 1;
    }
    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_COMPLETION);
}

static void unassign_storage(SCLPDevice *sclp, SCCB *sccb)
{
    MemoryRegion *mr = NULL;
    AssignStorage *assign_info = (AssignStorage *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();
    ram_addr_t unassign_addr;
    MemoryRegion *sysmem = get_system_memory();

    if (!mhd) {
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        return;
    }
    unassign_addr = (assign_info->rn - 1) * mhd->rzm;

    /* if the addr is a multiple of 256 MB */
    if ((unassign_addr % MEM_SECTION_SIZE == 0) &&
        (unassign_addr >= mhd->padded_ram_size)) {
        mhd->standby_state_map[(unassign_addr -
                           mhd->padded_ram_size) / MEM_SECTION_SIZE] = 0;

        /* find the specified memory region and destroy it */
        mr = memory_region_find(sysmem, unassign_addr, 1).mr;
        memory_region_unref(mr);
        if (mr) {
            int i;
            int is_removable = 1;
            ram_addr_t map_offset = (unassign_addr - mhd->padded_ram_size -
                                     (unassign_addr - mhd->padded_ram_size)
                                     % mhd->standby_subregion_size);
            /* Mark all affected subregions as 'standby' once again */
            for (i = 0;
                 i < (mhd->standby_subregion_size / MEM_SECTION_SIZE);
                 i++) {

                if (mhd->standby_state_map[i + map_offset / MEM_SECTION_SIZE]) {
                    is_removable = 0;
                    break;
                }
            }
            if (is_removable) {
                memory_region_del_subregion(sysmem, mr);
                object_unref(OBJECT(mr));
            }
        }
    }
    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_COMPLETION);
}

/* Provide information about the CPU */
static void sclp_read_cpu_info(SCLPDevice *sclp, SCCB *sccb)
{
    ReadCpuInfo *cpu_info = (ReadCpuInfo *) sccb;
    CPUState *cpu;
    int cpu_count = 0;

    CPU_FOREACH(cpu) {
        cpu_count++;
    }

    cpu_info->nr_configured = cpu_to_be16(cpu_count);
    cpu_info->offset_configured = cpu_to_be16(offsetof(ReadCpuInfo, entries));
    cpu_info->nr_standby = cpu_to_be16(0);

    /* The standby offset is 16-byte for each CPU */
    cpu_info->offset_standby = cpu_to_be16(cpu_info->offset_configured
        + cpu_info->nr_configured*sizeof(CPUEntry));

    prepare_cpu_entries(sclp, cpu_info->entries, cpu_count);

    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

static void sclp_execute(SCLPDevice *sclp, SCCB *sccb, uint32_t code)
{
    SCLPDeviceClass *sclp_c = SCLP_GET_CLASS(sclp);
    SCLPEventFacility *ef = sclp->event_facility;
    SCLPEventFacilityClass *efc = EVENT_FACILITY_GET_CLASS(ef);

    switch (code & SCLP_CMD_CODE_MASK) {
    case SCLP_CMDW_READ_SCP_INFO:
    case SCLP_CMDW_READ_SCP_INFO_FORCED:
        sclp_c->read_SCP_info(sclp, sccb);
        break;
    case SCLP_CMDW_READ_CPU_INFO:
        sclp_c->read_cpu_info(sclp, sccb);
        break;
    case SCLP_READ_STORAGE_ELEMENT_INFO:
        if (code & 0xff00) {
            sclp_c->read_storage_element1_info(sclp, sccb);
        } else {
            sclp_c->read_storage_element0_info(sclp, sccb);
        }
        break;
    case SCLP_ATTACH_STORAGE_ELEMENT:
        sclp_c->attach_storage_element(sclp, sccb, (code & 0xff00) >> 8);
        break;
    case SCLP_ASSIGN_STORAGE:
        sclp_c->assign_storage(sclp, sccb);
        break;
    case SCLP_UNASSIGN_STORAGE:
        sclp_c->unassign_storage(sclp, sccb);
        break;
    case SCLP_CMDW_CONFIGURE_PCI:
        s390_pci_sclp_configure(sccb);
        break;
    case SCLP_CMDW_DECONFIGURE_PCI:
        s390_pci_sclp_deconfigure(sccb);
        break;
    default:
        efc->command_handler(ef, sccb, code);
        break;
    }
}

int sclp_service_call(CPUS390XState *env, uint64_t sccb, uint32_t code)
{
    SCLPDevice *sclp = get_sclp_device();
    SCLPDeviceClass *sclp_c = SCLP_GET_CLASS(sclp);
    int r = 0;
    SCCB work_sccb;

    hwaddr sccb_len = sizeof(SCCB);

    /* first some basic checks on program checks */
    if (env->psw.mask & PSW_MASK_PSTATE) {
        r = -PGM_PRIVILEGED;
        goto out;
    }
    if (cpu_physical_memory_is_io(sccb)) {
        r = -PGM_ADDRESSING;
        goto out;
    }
    if ((sccb & ~0x1fffUL) == 0 || (sccb & ~0x1fffUL) == env->psa
        || (sccb & ~0x7ffffff8UL) != 0) {
        r = -PGM_SPECIFICATION;
        goto out;
    }

    /*
     * we want to work on a private copy of the sccb, to prevent guests
     * from playing dirty tricks by modifying the memory content after
     * the host has checked the values
     */
    cpu_physical_memory_read(sccb, &work_sccb, sccb_len);

    /* Valid sccb sizes */
    if (be16_to_cpu(work_sccb.h.length) < sizeof(SCCBHeader) ||
        be16_to_cpu(work_sccb.h.length) > SCCB_SIZE) {
        r = -PGM_SPECIFICATION;
        goto out;
    }

    sclp_c->execute(sclp, &work_sccb, code);

    cpu_physical_memory_write(sccb, &work_sccb,
                              be16_to_cpu(work_sccb.h.length));

    sclp_c->service_interrupt(sclp, sccb);

out:
    return r;
}

static void service_interrupt(SCLPDevice *sclp, uint32_t sccb)
{
    SCLPEventFacility *ef = sclp->event_facility;
    SCLPEventFacilityClass *efc = EVENT_FACILITY_GET_CLASS(ef);

    uint32_t param = sccb & ~3;

    /* Indicate whether an event is still pending */
    param |= efc->event_pending(ef) ? 1 : 0;

    if (!param) {
        /* No need to send an interrupt, there's nothing to be notified about */
        return;
    }
    s390_sclp_extint(param);
}

void sclp_service_interrupt(uint32_t sccb)
{
    SCLPDevice *sclp = get_sclp_device();
    SCLPDeviceClass *sclp_c = SCLP_GET_CLASS(sclp);

    sclp_c->service_interrupt(sclp, sccb);
}

/* qemu object creation and initialization functions */

void s390_sclp_init(void)
{
    Object *new = object_new(TYPE_SCLP);

    object_property_add_child(qdev_get_machine(), TYPE_SCLP, new,
                              NULL);
    object_unref(OBJECT(new));
    qdev_init_nofail(DEVICE(new));
}

static void sclp_realize(DeviceState *dev, Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    SCLPDevice *sclp = SCLP(dev);
    Error *err = NULL;
    uint64_t hw_limit;
    int ret;

    object_property_set_bool(OBJECT(sclp->event_facility), true, "realized",
                             &err);
    if (err) {
        goto out;
    }
    /*
     * qdev_device_add searches the sysbus for TYPE_SCLP_EVENTS_BUS. As long
     * as we can't find a fitting bus via the qom tree, we have to add the
     * event facility to the sysbus, so e.g. a sclp console can be created.
     */
    qdev_set_parent_bus(DEVICE(sclp->event_facility), sysbus_get_default());

    ret = s390_set_memory_limit(machine->maxram_size, &hw_limit);
    if (ret == -E2BIG) {
        error_setg(&err, "host supports a maximum of %" PRIu64 " GB",
                   hw_limit >> 30);
    } else if (ret) {
        error_setg(&err, "setting the guest size failed");
    }

out:
    error_propagate(errp, err);
}

static void sclp_memory_init(SCLPDevice *sclp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    ram_addr_t initial_mem = machine->ram_size;
    ram_addr_t max_mem = machine->maxram_size;
    ram_addr_t standby_mem = max_mem - initial_mem;
    ram_addr_t pad_mem = 0;
    int increment_size = 20;

    /* The storage increment size is a multiple of 1M and is a power of 2.
     * The number of storage increments must be MAX_STORAGE_INCREMENTS or fewer.
     * The variable 'increment_size' is an exponent of 2 that can be
     * used to calculate the size (in bytes) of an increment. */
    while ((initial_mem >> increment_size) > MAX_STORAGE_INCREMENTS) {
        increment_size++;
    }
    if (machine->ram_slots) {
        while ((standby_mem >> increment_size) > MAX_STORAGE_INCREMENTS) {
            increment_size++;
        }
    }
    sclp->increment_size = increment_size;

    /* The core and standby memory areas need to be aligned with
     * the increment size.  In effect, this can cause the
     * user-specified memory size to be rounded down to align
     * with the nearest increment boundary. */
    initial_mem = initial_mem >> increment_size << increment_size;
    standby_mem = standby_mem >> increment_size << increment_size;

    /* If the size of ram is not on a MEM_SECTION_SIZE boundary,
       calculate the pad size necessary to force this boundary. */
    if (machine->ram_slots && standby_mem) {
        sclpMemoryHotplugDev *mhd = init_sclp_memory_hotplug_dev();

        if (initial_mem % MEM_SECTION_SIZE) {
            pad_mem = MEM_SECTION_SIZE - initial_mem % MEM_SECTION_SIZE;
        }
        mhd->increment_size = increment_size;
        mhd->pad_size = pad_mem;
        mhd->standby_mem_size = standby_mem;
    }
    machine->ram_size = initial_mem;
    machine->maxram_size = initial_mem + pad_mem + standby_mem;
    /* let's propagate the changed ram size into the global variable. */
    ram_size = initial_mem;
}

static void sclp_init(Object *obj)
{
    SCLPDevice *sclp = SCLP(obj);
    Object *new;

    new = object_new(TYPE_SCLP_EVENT_FACILITY);
    object_property_add_child(obj, TYPE_SCLP_EVENT_FACILITY, new, NULL);
    object_unref(new);
    sclp->event_facility = EVENT_FACILITY(new);

    sclp_memory_init(sclp);
}

static void sclp_class_init(ObjectClass *oc, void *data)
{
    SCLPDeviceClass *sc = SCLP_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "SCLP (Service-Call Logical Processor)";
    dc->realize = sclp_realize;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    sc->read_SCP_info = read_SCP_info;
    sc->read_storage_element0_info = read_storage_element0_info;
    sc->read_storage_element1_info = read_storage_element1_info;
    sc->attach_storage_element = attach_storage_element;
    sc->assign_storage = assign_storage;
    sc->unassign_storage = unassign_storage;
    sc->read_cpu_info = sclp_read_cpu_info;
    sc->execute = sclp_execute;
    sc->service_interrupt = service_interrupt;
}

static TypeInfo sclp_info = {
    .name = TYPE_SCLP,
    .parent = TYPE_DEVICE,
    .instance_init = sclp_init,
    .instance_size = sizeof(SCLPDevice),
    .class_init = sclp_class_init,
    .class_size = sizeof(SCLPDeviceClass),
};

sclpMemoryHotplugDev *init_sclp_memory_hotplug_dev(void)
{
    DeviceState *dev;
    dev = qdev_create(NULL, TYPE_SCLP_MEMORY_HOTPLUG_DEV);
    object_property_add_child(qdev_get_machine(),
                              TYPE_SCLP_MEMORY_HOTPLUG_DEV,
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);
    return SCLP_MEMORY_HOTPLUG_DEV(object_resolve_path(
                                   TYPE_SCLP_MEMORY_HOTPLUG_DEV, NULL));
}

sclpMemoryHotplugDev *get_sclp_memory_hotplug_dev(void)
{
    return SCLP_MEMORY_HOTPLUG_DEV(object_resolve_path(
                                   TYPE_SCLP_MEMORY_HOTPLUG_DEV, NULL));
}

static void sclp_memory_hotplug_dev_class_init(ObjectClass *klass,
                                               void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static TypeInfo sclp_memory_hotplug_dev_info = {
    .name = TYPE_SCLP_MEMORY_HOTPLUG_DEV,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(sclpMemoryHotplugDev),
    .class_init = sclp_memory_hotplug_dev_class_init,
};

static void register_types(void)
{
    type_register_static(&sclp_memory_hotplug_dev_info);
    type_register_static(&sclp_info);
}
type_init(register_types);
