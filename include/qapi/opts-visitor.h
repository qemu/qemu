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
#include "qemu/option.h"

typedef struct OptsVisitor OptsVisitor;

/* Contrarily to qemu-option.c::parse_option_number(), OptsVisitor's "int"
 * parser relies on strtoll() instead of strtoull(). Consequences:
 * - string representations of negative numbers yield negative values,
 * - values below INT64_MIN or LLONG_MIN are rejected,
 * - values above INT64_MAX or LLONG_MAX are rejected.
 */
OptsVisitor *opts_visitor_new(const QemuOpts *opts);
void opts_visitor_cleanup(OptsVisitor *nv);
Visitor *opts_get_visitor(OptsVisitor *nv);

#endif
