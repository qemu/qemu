/*
 * QEMU System Emulator
 *
 * Copyright (c) 2024 Linaro Ltd.
 *
 * Authors: Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


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
#include "qemu/module.h"
#include "qemu-io.h"
#include "system/system.h"
#include "hw/sysbus.h"
#include "system/memory.h"
#include "chardev/char-fe.h"
#include "hw/clock.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "chardev/char-serial.h"
#include "exec/memattrs.h"
#include "qemu/timer.h"
#include "system/address-spaces.h"
#include "hw/char/pl011.h"
