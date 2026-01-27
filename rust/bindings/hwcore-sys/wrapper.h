/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * This header file is meant to be used as input to the `bindgen` application
 * in order to generate C FFI compatible Rust bindings.
 */

/*
 * We block include/qemu/typedefs.h from bindgen, add here symbols
 * that are needed as opaque types by other functions.
 */
typedef struct Clock Clock;
typedef struct DeviceState DeviceState;
typedef struct IRQState *qemu_irq;
typedef void (*qemu_irq_handler)(void *opaque, int n, int level);

/* Once bindings exist, these could move to a different *-sys crate.  */
typedef struct BlockBackend BlockBackend;
typedef struct Monitor Monitor;
typedef struct NetClientState NetClientState;

#include "qemu/osdep.h"

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/resettable.h"
