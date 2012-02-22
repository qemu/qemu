/*
 * Core Definitions for QAPI Visitor implementations
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Paolo Bonizni <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#ifndef QAPI_VISITOR_IMPL_H
#define QAPI_VISITOR_IMPL_H

#include "qapi/qapi-types-core.h"
#include "qapi/qapi-visit-core.h"

void input_type_enum(Visitor *v, int *obj, const char *strings[],
                     const char *kind, const char *name, Error **errp);
void output_type_enum(Visitor *v, int *obj, const char *strings[],
                      const char *kind, const char *name, Error **errp);

#endif
