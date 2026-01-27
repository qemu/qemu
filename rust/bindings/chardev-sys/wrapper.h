/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * This header file is meant to be used as input to the `bindgen` application
 * in order to generate C FFI compatible Rust bindings.
 */

#include "qemu/osdep.h"

#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
