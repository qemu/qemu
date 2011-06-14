/*
 * QEMU Error Objects
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */
#ifndef QEMU_ERROR_INT_H
#define QEMU_ERROR_INT_H

#include "qemu-common.h"
#include "qobject.h"
#include "qdict.h"
#include "error.h"

/**
 * Internal QEMU functions for working with Error.
 *
 * These are used to convert QErrors to Errors
 */
QDict *error_get_data(Error *err);
QObject *error_get_qobject(Error *err);
void error_set_qobject(Error **errp, QObject *obj);
  
#endif
