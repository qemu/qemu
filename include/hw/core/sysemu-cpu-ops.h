/*
 * CPU operations specific to system emulation
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_CPU_OPS_H
#define SYSEMU_CPU_OPS_H

#include "hw/core/cpu.h"

/*
 * struct SysemuCPUOps: System operations specific to a CPU class
 */
typedef struct SysemuCPUOps {
    /**
     * @virtio_is_big_endian: Callback to return %true if a CPU which supports
     * runtime configurable endianness is currently big-endian.
     * Non-configurable CPUs can use the default implementation of this method.
     * This method should not be used by any callers other than the pre-1.0
     * virtio devices.
     */
    bool (*virtio_is_big_endian)(CPUState *cpu);

    /**
     * @legacy_vmsd: Legacy state for migration.
     *               Do not use in new targets, use #DeviceClass::vmsd instead.
     */
    const VMStateDescription *legacy_vmsd;

} SysemuCPUOps;

#endif /* SYSEMU_CPU_OPS_H */
