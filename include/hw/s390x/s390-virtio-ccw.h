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
};

struct S390CcwMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    bool ri_allowed;
    bool cpu_model_allowed;
    bool css_migration_enabled;
    bool hpage_1m_allowed;
};

/* runtime-instrumentation allowed by the machine */
bool ri_allowed(void);
/* cpu model allowed by the machine */
bool cpu_model_allowed(void);
/* 1M huge page mappings allowed by the machine */
bool hpage_1m_allowed(void);

/**
 * Returns true if (vmstate based) migration of the channel subsystem
 * is enabled, false if it is disabled.
 */
bool css_migration_enabled(void);

#endif
