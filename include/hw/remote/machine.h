/*
 * Remote machine configuration
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef REMOTE_MACHINE_H
#define REMOTE_MACHINE_H

#include "qom/object.h"
#include "hw/boards.h"
#include "hw/pci-host/remote.h"

struct RemoteMachineState {
    MachineState parent_obj;

    RemotePCIHost *host;
};

#define TYPE_REMOTE_MACHINE "x-remote-machine"
OBJECT_DECLARE_SIMPLE_TYPE(RemoteMachineState, REMOTE_MACHINE)

#endif
