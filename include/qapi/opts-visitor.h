/*
 * Options Visitor
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Author: Laszlo Ersek <lersek@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef OPTS_VISITOR_H
#define OPTS_VISITOR_H

#include "qapi/visitor.h"

/* Inclusive upper bound on the size of any flattened range. This is a safety
 * (= anti-annoyance) measure; wrong ranges should not cause long startup
 * delays nor exhaust virtual memory.
 */
#define OPTS_VISITOR_RANGE_MAX 65536

typedef struct OptsVisitor OptsVisitor;

/* Contrarily to qemu-option.c::parse_option_number(), OptsVisitor's "int"
 * parser relies on strtoll() instead of strtoull(). Consequences:
 * - string representations of negative numbers yield negative values,
 * - values below INT64_MIN or LLONG_MIN are rejected,
 * - values above INT64_MAX or LLONG_MAX are rejected.
 *
 * The Opts input visitor does not implement support for visiting QAPI
 * alternates, numbers (other than integers), null, or arbitrary
 * QTypes.  It also requires a non-null list argument to
 * visit_start_list().
 */
Visitor *opts_visitor_new(const QemuOpts *opts);

#endif
