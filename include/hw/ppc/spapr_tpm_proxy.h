/*
 * SPAPR TPM Proxy/Hypercall
 *
 * Copyright IBM Corp. 2019
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_SPAPR_TPM_PROXY_H
#define HW_SPAPR_TPM_PROXY_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_SPAPR_TPM_PROXY "spapr-tpm-proxy"
OBJECT_DECLARE_SIMPLE_TYPE(SpaprTpmProxy, SPAPR_TPM_PROXY)

struct SpaprTpmProxy {
    /*< private >*/
    DeviceState parent;

    char *host_path;
    int host_fd;
};

#endif /* HW_SPAPR_TPM_PROXY_H */
