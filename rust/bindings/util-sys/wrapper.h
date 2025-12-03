/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * This header file is meant to be used as input to the `bindgen` application
 * in order to generate C FFI compatible Rust bindings.
 */

/*
 * We block include/qemu/typedefs.h from bindgen, add here symbols
 * that are needed as opaque types by other functions.
 */
typedef struct QEMUBH QEMUBH;
typedef struct QEMUFile QEMUFile;
typedef struct QemuOpts QemuOpts;
typedef struct JSONWriter JSONWriter;
typedef struct Visitor Visitor;

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/error-internal.h"
#include "qemu/event_notifier.h"
#include "qemu/main-loop.h"
#include "qemu/aio.h"
#include "qemu/log-for-trace.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#include "qapi/visitor.h"
#include "qobject/qbool.h"
#include "qobject/qdict.h"
#include "qobject/qjson.h"
#include "qobject/qlist.h"
#include "qobject/qnull.h"
#include "qobject/qnum.h"
#include "qobject/qobject.h"
#include "qobject/qstring.h"
#include "qobject/json-writer.h"
