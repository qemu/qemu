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

#include "cpu.h"
#include "sysemu/kvm.h"
#include "exec/memory.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "qemu/config-file.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"

static inline SCLPEventFacility *get_event_facility(void)
{
    ObjectProperty *op = object_property_find(qdev_get_machine(),
                                              TYPE_SCLP_EVENT_FACILITY,
                                              NULL);
    assert(op);
    return op->opaque;
}

/* Provide information about the configuration, CPUs and storage */
static void read_SCP_info(SCCB *sccb)
{
    ReadInfo *read_info = (ReadInfo *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();
    CPUState *cpu;
    int cpu_count = 0;
    int i = 0;
    int increment_size = 20;
    int rnsize, rnmax;
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("memory"), NULL);
    int slots = qemu_opt_get_number(opts, "slots", 0);
    int max_avail_slots = s390_get_memslot_count(kvm_state);

    if (slots > max_avail_slots) {
        slots = max_avail_slots;
    }

    CPU_FOREACH(cpu) {
        cpu_count++;
    }

    /* CPU information */
    read_info->entries_cpu = cpu_to_be16(cpu_count);
    read_info->offset_cpu = cpu_to_be16(offsetof(ReadInfo, entries));
    read_info->highest_cpu = cpu_to_be16(max_cpus);

    for (i = 0; i < cpu_count; i++) {
        read_info->entries[i].address = i;
        read_info->entries[i].type = 0;
    }

    read_info->facilities = cpu_to_be64(SCLP_HAS_CPU_INFO);

    /*
     * The storage increment size is a multiple of 1M and is a power of 2.
     * The number of storage increments must be MAX_STORAGE_INCREMENTS or fewer.
     */
    while ((ram_size >> increment_size) > MAX_STORAGE_INCREMENTS) {
        increment_size++;
    }
    rnmax = ram_size >> increment_size;

    /* Memory Hotplug is only supported for the ccw machine type */
    if (mhd) {
        while ((mhd->standby_mem_size >> increment_size) >
               MAX_STORAGE_INCREMENTS) {
            increment_size++;
        }
        assert(increment_size == mhd->increment_size);

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
        rnmax = ((ram_size + mhd->standby_mem_size + mhd->pad_size)
             >> mhd->increment_size);

        read_info->facilities |= cpu_to_be64(SCLP_FC_ASSIGN_ATTACH_READ_STOR);
    }

    rnsize = 1 << (increment_size - 20);
    if (rnsize <= 128) {
        read_info->rnsize = rnsize;
    } else {
        read_info->rnsize = 0;
        read_info->rnsize2 = cpu_to_be32(rnsize);
    }

    if (rnmax < 0x10000) {
        read_info->rnmax = cpu_to_be16(rnmax);
    } else {
        read_info->rnmax = cpu_to_be16(0);
        read_info->rnmax2 = cpu_to_be64(rnmax);
    }

    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

static void read_storage_element0_info(SCCB *sccb)
{
    int i, assigned;
    int subincrement_id = SCLP_STARTING_SUBINCREMENT_ID;
    ReadStorageElementInfo *storage_info = (ReadStorageElementInfo *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();

    assert(mhd);

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

static void read_storage_element1_info(SCCB *sccb)
{
    ReadStorageElementInfo *storage_info = (ReadStorageElementInfo *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();

    assert(mhd);

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

static void attach_storage_element(SCCB *sccb, uint16_t element)
{
    int i, assigned, subincrement_id;
    AttachStorageElement *attach_info = (AttachStorageElement *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();

    assert(mhd);

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

static void assign_storage(SCCB *sccb)
{
    MemoryRegion *mr = NULL;
    uint64_t this_subregion_size;
    AssignStorage *assign_info = (AssignStorage *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();
    assert(mhd);
    ram_addr_t assign_addr = (assign_info->rn - 1) * mhd->rzm;
    MemoryRegion *sysmem = get_system_memory();

    if ((assign_addr % MEM_SECTION_SIZE == 0) &&
        (assign_addr >= mhd->padded_ram_size)) {
        /* Re-use existing memory region if found */
        mr = memory_region_find(sysmem, assign_addr, 1).mr;
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

            memory_region_init_ram(standby_ram, NULL, id, this_subregion_size, &error_abort);
            vmstate_register_ram_global(standby_ram);
            memory_region_add_subregion(sysmem, offset, standby_ram);
        }
        /* The specified subregion is no longer in standby */
        mhd->standby_state_map[(assign_addr - mhd->padded_ram_size)
                               / MEM_SECTION_SIZE] = 1;
    }
    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_COMPLETION);
}

static void unassign_storage(SCCB *sccb)
{
    MemoryRegion *mr = NULL;
    AssignStorage *assign_info = (AssignStorage *) sccb;
    sclpMemoryHotplugDev *mhd = get_sclp_memory_hotplug_dev();
    assert(mhd);
    ram_addr_t unassign_addr = (assign_info->rn - 1) * mhd->rzm;
    MemoryRegion *sysmem = get_system_memory();

    /* if the addr is a multiple of 256 MB */
    if ((unassign_addr % MEM_SECTION_SIZE == 0) &&
        (unassign_addr >= mhd->padded_ram_size)) {
        mhd->standby_state_map[(unassign_addr -
                           mhd->padded_ram_size) / MEM_SECTION_SIZE] = 0;

        /* find the specified memory region and destroy it */
        mr = memory_region_find(sysmem, unassign_addr, 1).mr;
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
                object_unparent(OBJECT(mr));
                g_free(mr);
            }
        }
    }
    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_COMPLETION);
}

/* Provide information about the CPU */
static void sclp_read_cpu_info(SCCB *sccb)
{
    ReadCpuInfo *cpu_info = (ReadCpuInfo *) sccb;
    CPUState *cpu;
    int cpu_count = 0;
    int i = 0;

    CPU_FOREACH(cpu) {
        cpu_count++;
    }

    cpu_info->nr_configured = cpu_to_be16(cpu_count);
    cpu_info->offset_configured = cpu_to_be16(offsetof(ReadCpuInfo, entries));
    cpu_info->nr_standby = cpu_to_be16(0);

    /* The standby offset is 16-byte for each CPU */
    cpu_info->offset_standby = cpu_to_be16(cpu_info->offset_configured
        + cpu_info->nr_configured*sizeof(CPUEntry));

    for (i = 0; i < cpu_count; i++) {
        cpu_info->entries[i].address = i;
        cpu_info->entries[i].type = 0;
    }

    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

static void sclp_execute(SCCB *sccb, uint32_t code)
{
    SCLPEventFacility *ef = get_event_facility();
    SCLPEventFacilityClass *efc = EVENT_FACILITY_GET_CLASS(ef);

    switch (code & SCLP_CMD_CODE_MASK) {
    case SCLP_CMDW_READ_SCP_INFO:
    case SCLP_CMDW_READ_SCP_INFO_FORCED:
        read_SCP_info(sccb);
        break;
    case SCLP_CMDW_READ_CPU_INFO:
        sclp_read_cpu_info(sccb);
        break;
    case SCLP_READ_STORAGE_ELEMENT_INFO:
        if (code & 0xff00) {
            read_storage_element1_info(sccb);
        } else {
            read_storage_element0_info(sccb);
        }
        break;
    case SCLP_ATTACH_STORAGE_ELEMENT:
        attach_storage_element(sccb, (code & 0xff00) >> 8);
        break;
    case SCLP_ASSIGN_STORAGE:
        assign_storage(sccb);
        break;
    case SCLP_UNASSIGN_STORAGE:
        unassign_storage(sccb);
        break;
    default:
        efc->command_handler(ef, sccb, code);
        break;
    }
}

int sclp_service_call(CPUS390XState *env, uint64_t sccb, uint32_t code)
{
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

    sclp_execute((SCCB *)&work_sccb, code);

    cpu_physical_memory_write(sccb, &work_sccb,
                              be16_to_cpu(work_sccb.h.length));

    sclp_service_interrupt(sccb);

out:
    return r;
}

void sclp_service_interrupt(uint32_t sccb)
{
    SCLPEventFacility *ef = get_event_facility();
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

/* qemu object creation and initialization functions */

void s390_sclp_init(void)
{
    DeviceState *dev  = qdev_create(NULL, TYPE_SCLP_EVENT_FACILITY);

    object_property_add_child(qdev_get_machine(), TYPE_SCLP_EVENT_FACILITY,
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);
}

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

static TypeInfo sclp_memory_hotplug_dev_info = {
    .name = TYPE_SCLP_MEMORY_HOTPLUG_DEV,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(sclpMemoryHotplugDev),
};

static void register_types(void)
{
    type_register_static(&sclp_memory_hotplug_dev_info);
}
type_init(register_types);
