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

#include "hw/s390x/sclp.h"

static inline S390SCLPDevice *get_event_facility(void)
{
    ObjectProperty *op = object_property_find(qdev_get_machine(),
                                              "s390-sclp-event-facility",
                                              NULL);
    assert(op);
    return op->opaque;
}

/* Provide information about the configuration, CPUs and storage */
static void read_SCP_info(SCCB *sccb)
{
    ReadInfo *read_info = (ReadInfo *) sccb;
    int shift = 0;

    while ((ram_size >> (20 + shift)) > 65535) {
        shift++;
    }
    read_info->rnmax = cpu_to_be16(ram_size >> (20 + shift));
    read_info->rnsize = 1 << shift;
    sccb->h.response_code = cpu_to_be16(SCLP_RC_NORMAL_READ_COMPLETION);
}

static void sclp_execute(SCCB *sccb, uint64_t code)
{
    S390SCLPDevice *sdev = get_event_facility();

    switch (code) {
    case SCLP_CMDW_READ_SCP_INFO:
    case SCLP_CMDW_READ_SCP_INFO_FORCED:
        read_SCP_info(sccb);
        break;
    default:
        sdev->sclp_command_handler(sdev->ef, sccb, code);
        break;
    }
}

int sclp_service_call(uint32_t sccb, uint64_t code)
{
    int r = 0;
    SCCB work_sccb;

    hwaddr sccb_len = sizeof(SCCB);

    /* first some basic checks on program checks */
    if (cpu_physical_memory_is_io(sccb)) {
        r = -PGM_ADDRESSING;
        goto out;
    }
    if (sccb & ~0x7ffffff8ul) {
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
    S390SCLPDevice *sdev = get_event_facility();
    uint32_t param = sccb & ~3;

    /* Indicate whether an event is still pending */
    param |= sdev->event_pending(sdev->ef) ? 1 : 0;

    if (!param) {
        /* No need to send an interrupt, there's nothing to be notified about */
        return;
    }
    s390_sclp_extint(param);
}

/* qemu object creation and initialization functions */

void s390_sclp_init(void)
{
    DeviceState *dev  = qdev_create(NULL, "s390-sclp-event-facility");

    object_property_add_child(qdev_get_machine(), "s390-sclp-event-facility",
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);
}

static int s390_sclp_dev_init(SysBusDevice *dev)
{
    int r;
    S390SCLPDevice *sdev = (S390SCLPDevice *)dev;
    S390SCLPDeviceClass *sclp = SCLP_S390_DEVICE_GET_CLASS(dev);

    r = sclp->init(sdev);
    if (!r) {
        assert(sdev->event_pending);
        assert(sdev->sclp_command_handler);
    }

    return r;
}

static void s390_sclp_device_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *dc = SYS_BUS_DEVICE_CLASS(klass);

    dc->init = s390_sclp_dev_init;
}

static const TypeInfo s390_sclp_device_info = {
    .name = TYPE_DEVICE_S390_SCLP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S390SCLPDevice),
    .class_init = s390_sclp_device_class_init,
    .class_size = sizeof(S390SCLPDeviceClass),
    .abstract = true,
};

static void s390_sclp_register_types(void)
{
    type_register_static(&s390_sclp_device_info);
}

type_init(s390_sclp_register_types)
