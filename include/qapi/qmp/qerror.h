/*
 * QError Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef QERROR_H
#define QERROR_H

/*
 * These macros will go away, please don't use in new code, and do not
 * add new ones!
 */

#define QERR_INVALID_PARAMETER_VALUE \
    "Parameter '%s' expects %s"

#define QERR_MISSING_PARAMETER \
    "Parameter '%s' is missing"

#endif /* QERROR_H */
