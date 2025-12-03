/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * This header file is meant to be used as input to the `bindgen` application
 * in order to generate C FFI compatible Rust bindings.
 */

/*
 * We block include/qemu/typedefs.h from bindgen, add here symbols
 * that are needed as opaque types by other functions.
 */
typedef struct DirtyBitmapSnapshot DirtyBitmapSnapshot;
typedef struct MemoryRegion MemoryRegion;
typedef struct RAMBlock RAMBlock;

#include "qemu/osdep.h"

#include "exec/hwaddr.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "hw/core/sysbus.h"
