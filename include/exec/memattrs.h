/*
 * Memory transaction attributes
 *
 * Copyright (c) 2015 Linaro Limited.
 *
 * Authors:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MEMATTRS_H
#define MEMATTRS_H

/* Every memory transaction has associated with it a set of
 * attributes. Some of these are generic (such as the ID of
 * the bus master); some are specific to a particular kind of
 * bus (such as the ARM Secure/NonSecure bit). We define them
 * all as non-overlapping bitfields in a single struct to avoid
 * confusion if different parts of QEMU used the same bit for
 * different semantics.
 */
typedef struct MemTxAttrs {
    /* Bus masters which don't specify any attributes will get this
     * (via the MEMTXATTRS_UNSPECIFIED constant), so that we can
     * distinguish "all attributes deliberately clear" from
     * "didn't specify" if necessary.
     */
    unsigned int unspecified:1;
    /* ARM/AMBA: TrustZone Secure access
     * x86: System Management Mode access
     */
    unsigned int secure:1;
    /* Memory access is usermode (unprivileged) */
    unsigned int user:1;
    /* Requester ID (for MSI for example) */
    unsigned int requester_id:16;
} MemTxAttrs;

/* Bus masters which don't specify any attributes will get this,
 * which has all attribute bits clear except the topmost one
 * (so that we can distinguish "all attributes deliberately clear"
 * from "didn't specify" if necessary).
 */
#define MEMTXATTRS_UNSPECIFIED ((MemTxAttrs) { .unspecified = 1 })

#endif
