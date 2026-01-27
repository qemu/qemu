/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * This header file is meant to be used as input to the `bindgen` application
 * in order to generate C FFI compatible Rust bindings.
 */

/*
 * We block include/qemu/typedefs.h from bindgen, add here symbols
 * that are needed as opaque types by other functions.
 */
typedef struct Object Object;
typedef struct ObjectClass ObjectClass;

#include "qemu/osdep.h"

#include "qom/object.h"
