/*
 * Xen 9p backend
 *
 * Copyright Aporeto 2017
 *
 * Authors:
 *  Stefano Stabellini <stefano@aporeto.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2 or
 * later. See the COPYING file in the top-level directory.
 *
 */

#include <xen/io/protocols.h>
#include "hw/xen/io/ring.h"

/*
 * Do not merge into xen-9p-backend.c: clang doesn't allow unused static
 * inline functions in c files.
 */
DEFINE_XEN_FLEX_RING_AND_INTF(xen_9pfs);
