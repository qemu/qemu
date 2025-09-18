/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * This header file is meant to be used as input to the `bindgen` application
 * in order to generate C FFI compatible Rust bindings.
 */

#ifndef __CLANG_STDATOMIC_H
#define __CLANG_STDATOMIC_H
/*
 * Fix potential missing stdatomic.h error in case bindgen does not insert the
 * correct libclang header paths on its own. We do not use stdatomic.h symbols
 * in QEMU code, so it's fine to declare dummy types instead.
 */
typedef enum memory_order {
  memory_order_relaxed,
  memory_order_consume,
  memory_order_acquire,
  memory_order_release,
  memory_order_acq_rel,
  memory_order_seq_cst,
} memory_order;
#endif /* __CLANG_STDATOMIC_H */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/error-internal.h"
#include "qemu/log-for-trace.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
