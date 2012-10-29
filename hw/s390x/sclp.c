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
#include "kvm.h"
#include "memory.h"

#include "sclp.h"

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
    switch (code) {
    case SCLP_CMDW_READ_SCP_INFO:
    case SCLP_CMDW_READ_SCP_INFO_FORCED:
        read_SCP_info(sccb);
        break;
    default:
        sccb->h.response_code = cpu_to_be16(SCLP_RC_INVALID_SCLP_COMMAND);
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
    s390_sclp_extint(sccb & ~3);
}

/* qemu object creation and initialization functions */

static void s390_sclp_device_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *dc = SYS_BUS_DEVICE_CLASS(klass);

    dc->init = s390_sclp_dev_init;
}

static TypeInfo s390_sclp_device_info = {
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
