/*
 * vmnet-host.c
 *
 * Copyright(c) 2022 Vladislav Yaroshchuk <vladislav.yaroshchuk@jetbrains.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/uuid.h"
#include "qapi/qapi-types-net.h"
#include "qapi/error.h"
#include "clients.h"
#include "vmnet_int.h"

#include <vmnet/vmnet.h>


static bool validate_options(const Netdev *netdev, Error **errp)
{
    const NetdevVmnetHostOptions *options = &(netdev->u.vmnet_host);

#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0

    QemuUUID net_uuid;
    if (options->has_net_uuid &&
        qemu_uuid_parse(options->net_uuid, &net_uuid) < 0) {
        error_setg(errp, "Invalid UUID provided in 'net-uuid'");
        return false;
    }
#else
    if (options->has_isolated) {
        error_setg(errp,
                   "vmnet-host.isolated feature is "
                   "unavailable: outdated vmnet.framework API");
        return false;
    }

    if (options->has_net_uuid) {
        error_setg(errp,
                   "vmnet-host.net-uuid feature is "
                   "unavailable: outdated vmnet.framework API");
        return false;
    }
#endif

    if ((options->has_start_address ||
         options->has_end_address ||
         options->has_subnet_mask) &&
        !(options->has_start_address &&
          options->has_end_address &&
          options->has_subnet_mask)) {
        error_setg(errp,
                   "'start-address', 'end-address', 'subnet-mask' "
                   "should be provided together");
        return false;
    }

    return true;
}

static xpc_object_t build_if_desc(const Netdev *netdev)
{
    const NetdevVmnetHostOptions *options = &(netdev->u.vmnet_host);
    xpc_object_t if_desc = xpc_dictionary_create(NULL, NULL, 0);

    xpc_dictionary_set_uint64(if_desc,
                              vmnet_operation_mode_key,
                              VMNET_HOST_MODE);

#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0

    xpc_dictionary_set_bool(if_desc,
                            vmnet_enable_isolation_key,
                            options->isolated);

    QemuUUID net_uuid;
    if (options->has_net_uuid) {
        qemu_uuid_parse(options->net_uuid, &net_uuid);
        xpc_dictionary_set_uuid(if_desc,
                                vmnet_network_identifier_key,
                                net_uuid.data);
    }
#endif

    if (options->has_start_address) {
        xpc_dictionary_set_string(if_desc,
                                  vmnet_start_address_key,
                                  options->start_address);
        xpc_dictionary_set_string(if_desc,
                                  vmnet_end_address_key,
                                  options->end_address);
        xpc_dictionary_set_string(if_desc,
                                  vmnet_subnet_mask_key,
                                  options->subnet_mask);
    }

    return if_desc;
}

static NetClientInfo net_vmnet_host_info = {
    .type = NET_CLIENT_DRIVER_VMNET_HOST,
    .size = sizeof(VmnetState),
    .receive = vmnet_receive_common,
    .cleanup = vmnet_cleanup_common,
};

int net_init_vmnet_host(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    NetClientState *nc = qemu_new_net_client(&net_vmnet_host_info,
                                             peer, "vmnet-host", name);
    xpc_object_t if_desc;
    int result = -1;

    if (!validate_options(netdev, errp)) {
        return result;
    }

    if_desc = build_if_desc(netdev);
    result = vmnet_if_create(nc, if_desc, errp);
    xpc_release(if_desc);
    return result;
}
