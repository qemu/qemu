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

#define TYPE_S390_CCW_MACHINE               "s390-ccw-machine"

#define S390_CCW_MACHINE(obj) \
    OBJECT_CHECK(S390CcwMachineState, (obj), TYPE_S390_CCW_MACHINE)

#define S390_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390CcwMachineClass, (klass), TYPE_S390_CCW_MACHINE)

typedef struct S390CcwMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    bool aes_key_wrap;
    bool dea_key_wrap;
} S390CcwMachineState;

typedef struct S390CcwMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    bool ri_allowed;
    bool cpu_model_allowed;
} S390CcwMachineClass;

/* runtime-instrumentation allowed by the machine */
bool ri_allowed(void);
/* cpu model allowed by the machine */
bool cpu_model_allowed(void);

#endif
