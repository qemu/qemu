/*
 * Persistent reservation manager - stub for non-Linux platforms
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This code is licensed under the LGPL.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "scsi/pr-manager.h"
#include "trace.h"
#include "qapi/qapi-types-block.h"
#include "qapi/qapi-commands-block.h"

PRManager *pr_manager_lookup(const char *id, Error **errp)
{
    /* The classes do not exist at all!  */
    error_setg(errp, "No persistent reservation manager with id '%s'", id);
        return NULL;
}


PRManagerInfoList *qmp_query_pr_managers(Error **errp)
{
    return NULL;
}
