/*
 * VMState interface
 *
 * Copyright (c) 2009-2019 Red Hat Inc
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef VMSTATE_IF_H
#define VMSTATE_IF_H

#include "qom/object.h"

#define TYPE_VMSTATE_IF "vmstate-if"

typedef struct VMStateIfClass VMStateIfClass;
DECLARE_CLASS_CHECKERS(VMStateIfClass, VMSTATE_IF,
                       TYPE_VMSTATE_IF)
#define VMSTATE_IF(obj)                             \
    INTERFACE_CHECK(VMStateIf, (obj), TYPE_VMSTATE_IF)

typedef struct VMStateIf VMStateIf;

struct VMStateIfClass {
    InterfaceClass parent_class;

    char * (*get_id)(VMStateIf *obj);
};

static inline char *vmstate_if_get_id(VMStateIf *vmif)
{
    if (!vmif) {
        return NULL;
    }

    return VMSTATE_IF_GET_CLASS(vmif)->get_id(vmif);
}

#endif /* VMSTATE_IF_H */
