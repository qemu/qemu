/*
 * QMP Target options - Commands handled based on a target config
 *                      versus a host config
 *
 * Copyright (c) 2015 David Ahern <dsahern@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-rocker.h"
#include "qapi/qmp/qerror.h"

RockerSwitch *qmp_query_rocker(const char *name, Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "rocker");
    return NULL;
};

RockerPortList *qmp_query_rocker_ports(const char *name, Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "rocker");
    return NULL;
};

RockerOfDpaFlowList *qmp_query_rocker_of_dpa_flows(const char *name,
                                                   bool has_tbl_id,
                                                   uint32_t tbl_id,
                                                   Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "rocker");
    return NULL;
};

RockerOfDpaGroupList *qmp_query_rocker_of_dpa_groups(const char *name,
                                                     bool has_type,
                                                     uint8_t type,
                                                     Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "rocker");
    return NULL;
};
