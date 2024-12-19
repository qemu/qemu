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
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/s390x/sclp.h"
#include "hw/s390x/event-facility.h"
#include "hw/s390x/s390-pci-bus.h"
#include "hw/s390x/ipl.h"
#include "hw/s390x/cpu-topology.h"
#include "hw/s390x/s390-virtio-ccw.h"

static SCLPDevice *get_sclp_device(void)
{
    static SCLPDevice *sclp;

    if (!sclp) {
        sclp = S390_CCW_MACHINE(qdev_get_machine())->sclp;
    }
    return sclp;
}

static inline bool sclp_command_code_valid(uint32_t code)
{
    switch (code & SCLP_CMD_CODE_MASK) {
    case SCLP_CMDW_READ_SCP_INFO:
    case SCLP_CMDW_READ_SCP_INFO_FORCED:
    case SCLP_CMDW_READ_CPU_INFO:
    case SCLP_CMDW_CONFIGURE_IOA:
    case SCLP_CMDW_DECONFIGURE_IOA:
    case SCLP_CMD_READ_EVENT_DATA:
    case SCLP_CMD_WRITE_EVENT_DATA:
    case SCLP_CMD_WRITE_EVENT_MASK:
        return true;
    }
    return false;
}

static bool sccb_verify_boundary(uint64_t sccb_addr, uint16_t sccb_len,
                                 uint32_t code)
{
    uint64_t sccb_max_addr = sccb_addr + sccb_len - 1;
    uint64_t sccb_boundary = (sccb_addr & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;

    switch (code & SCLP_CMD_CODE_MASK) {
    case SCLP_CMDW_READ_SCP_INFO:
    case SCLP_CMDW_READ_SCP_INFO_FORCED:
    case SCLP_CMDW_READ_CPU_INFO:
        /*
         * An extended-length SCCB is only allowed for Read SCP/CPU Info and
         * is allowed to exceed the 4k boundary. The respective commands will
         * set the length field to the required length if an insufficient
         * SCCB length is provided.
         */
        if (s390_has_feat(S390_FEAT_EXTENDED_LENGTH_SCCB)) {
            return true;
        }
        /* fallthrough */
    default:
        if (sccb_max_addr < sccb_boundary) {
            return true;
        }
    }

    return false;
}

static void prepare_cpu_entries(MachineState *ms, CPUEntry *entry, int *count)
{
    uint8_t features[SCCB_CPU_FEATURE_LEN] = { 0 };
    int i;

    s390_get_feat_block(S390_FEAT_TYPE_SCLP_CPU, features);
    for (i = 0, *count = 0; i < ms->possible_cpus->len; i++) {
        if (!ms->possible_cpus->cpus[i].cpu) {
            continue;
        }
        entry[*count].address = ms->possible_cpus->cpus[i].arch_id;
        entry[*count].type = 0;
        memcpy(entry[*count].features, features, sizeof(features));
        (*count)++;
    }
}

#define SCCB_REQ_LEN(s, max_cpus) (sizeof(s) + max_cpus * sizeof(CPUEntry))

static inline bool ext_len_sccb_supported(SCCBHeader header)
{
    return s390_has_feat(S390_FEAT_EXTENDED_LENGTH_SCCB) &&
           header.control_mask[2] & SCLP_VARIABLE_LENGTH_RESPONSE;
}

/* Provide information about the configuration, CPUs and storage */
static void read_SCP_info(SCLPDevice *sclp, SCCB *sccb)
{
    ReadInfo *read_info = (ReadInfo *) sccb;
    MachineState *machine = MACHINE(qdev_get_machine());
    int cpu_count;
    int rnsize, rnmax;
    int required_len = SCCB_REQ_LEN(ReadInfo, machine->possible_cpus->len);
    int offset_cpu = s390_has_feat(S390_FEAT_EXTENDED_LENGTH_SCCB) ?
                     offsetof(ReadInfo, entries) :
                     SCLP_READ_SCP_INFO_FIXED_CPU_OFFSET;
    CPUEntry *entries_start = (void *)sccb + offset_cpu;

    if (be16_to_cpu(sccb->h.length) < required_len) {
        if (ext_len_sccb_supported(sccb->h)) {
            sccb->h.length = cpu_to_be16(required_len);
        }
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INSUFFICIENT_SCCB_LENGTH);
        return;
    }

    if (s390_has_topology()) {
        read_info->stsi_parm = SCLP_READ_SCP_INFO_MNEST;
    }

    /* CPU information */
    prepare_cpu_entries(machine, entries_start, &cpu_count);
    read_info->entries_cpu = cpu_to_be16(cpu_count);
    read_info->offset_cpu = cpu_to_be16(offset_cpu);
    read_info->highest_cpu = cpu_to_be16(machine->smp.max_cpus - 1);

    read_info->ibc_val = cpu_to_be32(s390_get_ibc_val());

    /* Configuration Characteristic (Extension) */
    s390_get_feat_block(S390_FEAT_TYPE_SCLP_CONF_CHAR,
                         read_info->conf_char);
    s390_get_feat_block(S390_FEAT_TYPE_SCLP_CONF_CHAR_EXT,
                         read_info->conf_char_ext);

    if (s390_has_feat(S390_FEAT_EXTENDED_LENGTH_SCCB)) {
        s390_get_feat_block(S390_FEAT_TYPE_SCLP_FAC134,
                            &read_info->fac134);
    }

    read_info->facilities = cpu_to_be64(SCLP_HAS_CPU_INFO |
                                        SCLP_HAS_IOA_RECONFIG);

    read_info->mha_pow = s390_get_mha_pow();
    read_info->hmfai = cpu_to_be32(s390_get_hmfai());

    rnsize = 1 << (sclp->increment_size - 20);
    if (rnsize <= 128) {
        read_info->rnsize = rnsize;
    } else {
        read_info->rnsize = 0;
        read_info->rnsize2 = cpu_to_be32(rnsize);
    }

    /*
     * We don't support standby memory. maxram_size is used for sizing the
     * memory device region, which is not exposed through SCLP but through
     * diag500.
     */
    rnmax = machine->ram_size >> sclp->increment_size;
    if (rnmax < 0x10000) {
        read_info->rnmax = cpu_to_be16(rnmax);
    } else {
        read_info->rnmax = cpu_to_be16(0);
        read_info->rnmax2 = cpu_to_be64(rnmax);
    }

    s390_ipl_convert_loadparm((char *)S390_CCW_MACHINE(machine)->loadparm,
                                read_info->loadparm);

    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

/* Provide information about the CPU */
static void sclp_read_cpu_info(SCLPDevice *sclp, SCCB *sccb)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    ReadCpuInfo *cpu_info = (ReadCpuInfo *) sccb;
    int cpu_count;
    int required_len = SCCB_REQ_LEN(ReadCpuInfo, machine->possible_cpus->len);

    if (be16_to_cpu(sccb->h.length) < required_len) {
        if (ext_len_sccb_supported(sccb->h)) {
            sccb->h.length = cpu_to_be16(required_len);
        }
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INSUFFICIENT_SCCB_LENGTH);
        return;
    }

    prepare_cpu_entries(machine, cpu_info->entries, &cpu_count);
    cpu_info->nr_configured = cpu_to_be16(cpu_count);
    cpu_info->offset_configured = cpu_to_be16(offsetof(ReadCpuInfo, entries));
    cpu_info->nr_standby = cpu_to_be16(0);

    /* The standby offset is 16-byte for each CPU */
    cpu_info->offset_standby = cpu_to_be16(cpu_info->offset_configured
        + cpu_info->nr_configured*sizeof(CPUEntry));


    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

static void sclp_configure_io_adapter(SCLPDevice *sclp, SCCB *sccb,
                                      bool configure)
{
    int rc;

    if (be16_to_cpu(sccb->h.length) < 16) {
        rc = SCLP_RC_INSUFFICIENT_SCCB_LENGTH;
        goto out_err;
    }

    switch (((IoaCfgSccb *)sccb)->atype) {
    case SCLP_RECONFIG_PCI_ATYPE:
        if (s390_has_feat(S390_FEAT_ZPCI)) {
            if (configure) {
                s390_pci_sclp_configure(sccb);
            } else {
                s390_pci_sclp_deconfigure(sccb);
            }
            return;
        }
        /* fallthrough */
    default:
        rc = SCLP_RC_ADAPTER_TYPE_NOT_RECOGNIZED;
    }

 out_err:
    sccb->h.response_code = cpu_to_be16(rc);
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
    case SCLP_CMDW_CONFIGURE_IOA:
        sclp_configure_io_adapter(sclp, sccb, true);
        break;
    case SCLP_CMDW_DECONFIGURE_IOA:
        sclp_configure_io_adapter(sclp, sccb, false);
        break;
    default:
        efc->command_handler(ef, sccb, code);
        break;
    }
}

/*
 * We only need the address to have something valid for the
 * service_interrupt call.
 */
#define SCLP_PV_DUMMY_ADDR 0x4000
int sclp_service_call_protected(S390CPU *cpu, uint64_t sccb, uint32_t code)
{
    CPUS390XState *env = &cpu->env;
    SCLPDevice *sclp = get_sclp_device();
    SCLPDeviceClass *sclp_c = SCLP_GET_CLASS(sclp);
    SCCBHeader header;
    g_autofree SCCB *work_sccb = NULL;

    s390_cpu_pv_mem_read(env_archcpu(env), 0, &header, sizeof(SCCBHeader));

    work_sccb = g_malloc0(be16_to_cpu(header.length));
    s390_cpu_pv_mem_read(env_archcpu(env), 0, work_sccb,
                         be16_to_cpu(header.length));

    if (!sclp_command_code_valid(code)) {
        work_sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        goto out_write;
    }

    sclp_c->execute(sclp, work_sccb, code);
out_write:
    s390_cpu_pv_mem_write(env_archcpu(env), 0, work_sccb,
                          be16_to_cpu(work_sccb->h.length));
    sclp_c->service_interrupt(sclp, SCLP_PV_DUMMY_ADDR);
    return 0;
}

int sclp_service_call(S390CPU *cpu, uint64_t sccb, uint32_t code)
{
    CPUS390XState *env = &cpu->env;
    SCLPDevice *sclp = get_sclp_device();
    SCLPDeviceClass *sclp_c = SCLP_GET_CLASS(sclp);
    SCCBHeader header;
    g_autofree SCCB *work_sccb = NULL;

    /* first some basic checks on program checks */
    if (env->psw.mask & PSW_MASK_PSTATE) {
        return -PGM_PRIVILEGED;
    }
    if (cpu_physical_memory_is_io(sccb)) {
        return -PGM_ADDRESSING;
    }
    if ((sccb & ~0x1fffUL) == 0 || (sccb & ~0x1fffUL) == env->psa
        || (sccb & ~0x7ffffff8UL) != 0) {
        return -PGM_SPECIFICATION;
    }

    /* the header contains the actual length of the sccb */
    cpu_physical_memory_read(sccb, &header, sizeof(SCCBHeader));

    /* Valid sccb sizes */
    if (be16_to_cpu(header.length) < sizeof(SCCBHeader)) {
        return -PGM_SPECIFICATION;
    }

    /*
     * we want to work on a private copy of the sccb, to prevent guests
     * from playing dirty tricks by modifying the memory content after
     * the host has checked the values
     */
    work_sccb = g_malloc0(be16_to_cpu(header.length));
    cpu_physical_memory_read(sccb, work_sccb, be16_to_cpu(header.length));

    if (!sclp_command_code_valid(code)) {
        work_sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
        goto out_write;
    }

    if (!sccb_verify_boundary(sccb, be16_to_cpu(work_sccb->h.length), code)) {
        work_sccb->h.response_code = cpu_to_be16(SCLP_RC_SCCB_BOUNDARY_VIOLATION);
        goto out_write;
    }

    sclp_c->execute(sclp, work_sccb, code);
out_write:
    cpu_physical_memory_write(sccb, work_sccb,
                              be16_to_cpu(work_sccb->h.length));

    sclp_c->service_interrupt(sclp, sccb);

    return 0;
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
static void sclp_realize(DeviceState *dev, Error **errp)
{
    SCLPDevice *sclp = SCLP(dev);

    /*
     * qdev_device_add searches the sysbus for TYPE_SCLP_EVENTS_BUS. As long
     * as we can't find a fitting bus via the qom tree, we have to add the
     * event facility to the sysbus, so e.g. a sclp console can be created.
     */
    if (!sysbus_realize(SYS_BUS_DEVICE(sclp->event_facility), errp)) {
        return;
    }
}

static void sclp_memory_init(SCLPDevice *sclp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *machine_class = MACHINE_GET_CLASS(qdev_get_machine());
    ram_addr_t initial_mem = machine->ram_size;
    int increment_size = 20;

    /* The storage increment size is a multiple of 1M and is a power of 2.
     * For some machine types, the number of storage increments must be
     * MAX_STORAGE_INCREMENTS or fewer.
     * The variable 'increment_size' is an exponent of 2 that can be
     * used to calculate the size (in bytes) of an increment. */
    while (machine_class->fixup_ram_size != NULL &&
           (initial_mem >> increment_size) > MAX_STORAGE_INCREMENTS) {
        increment_size++;
    }
    sclp->increment_size = increment_size;
}

static void sclp_init(Object *obj)
{
    SCLPDevice *sclp = SCLP(obj);
    Object *new;

    new = object_new(TYPE_SCLP_EVENT_FACILITY);
    object_property_add_child(obj, TYPE_SCLP_EVENT_FACILITY, new);
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
    /*
     * Reason: Creates TYPE_SCLP_EVENT_FACILITY in sclp_init
     * which is a non-pluggable sysbus device
     */
    dc->user_creatable = false;

    sc->read_SCP_info = read_SCP_info;
    sc->read_cpu_info = sclp_read_cpu_info;
    sc->execute = sclp_execute;
    sc->service_interrupt = service_interrupt;
}

static const TypeInfo sclp_info = {
    .name = TYPE_SCLP,
    .parent = TYPE_DEVICE,
    .instance_init = sclp_init,
    .instance_size = sizeof(SCLPDevice),
    .class_init = sclp_class_init,
    .class_size = sizeof(SCLPDeviceClass),
};

static void register_types(void)
{
    type_register_static(&sclp_info);
}
type_init(register_types);
