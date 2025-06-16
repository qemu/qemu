/*
 * virtio ccw machine definitions
 *
 * Copyright 2012, 2016 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef HW_S390X_S390_VIRTIO_CCW_H
#define HW_S390X_S390_VIRTIO_CCW_H

#include "hw/boards.h"
#include "qom/object.h"
#include "hw/s390x/sclp.h"

#define TYPE_S390_CCW_MACHINE               "s390-ccw-machine"

OBJECT_DECLARE_TYPE(S390CcwMachineState, S390CcwMachineClass, S390_CCW_MACHINE)


struct S390CcwMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    bool aes_key_wrap;
    bool dea_key_wrap;
    bool pv;
    uint8_t loadparm[8];
    uint64_t memory_limit;
    uint64_t max_pagesize;

    SCLPDevice *sclp;
};

static inline uint64_t s390_get_memory_limit(S390CcwMachineState *s390ms)
{
    /* We expect to be called only after the limit was set. */
    assert(s390ms->memory_limit);
    return s390ms->memory_limit;
}

#define S390_PTF_REASON_NONE (0x00 << 8)
#define S390_PTF_REASON_DONE (0x01 << 8)
#define S390_PTF_REASON_BUSY (0x02 << 8)
#define S390_TOPO_FC_MASK 0xffUL
void s390_handle_ptf(S390CPU *cpu, uint8_t r1, uintptr_t ra);

struct S390CcwMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    int max_threads;
    bool use_cpi;
};

#endif
