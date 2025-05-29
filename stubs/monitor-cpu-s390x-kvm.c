/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine-s390x.h"

void qmp_set_cpu_topology(uint16_t core,
                          bool has_socket, uint16_t socket,
                          bool has_book, uint16_t book,
                          bool has_drawer, uint16_t drawer,
                          bool has_entitlement, S390CpuEntitlement entitlement,
                          bool has_dedicated, bool dedicated,
                          Error **errp)
{
    error_setg(errp, "CPU topology change is not supported on this target");
}

CpuPolarizationInfo *qmp_query_s390x_cpu_polarization(Error **errp)
{
    error_setg(errp, "CPU polarization is not supported on this target");
    return NULL;
}
