/*
 * vmnet_int.h
 *
 * Copyright(c) 2022 Vladislav Yaroshchuk <vladislav.yaroshchuk@jetbrains.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef VMNET_INT_H
#define VMNET_INT_H

#include "qemu/osdep.h"
#include "vmnet_int.h"
#include "clients.h"

#include <vmnet/vmnet.h>

typedef struct VmnetState {
  NetClientState nc;

} VmnetState;


#endif /* VMNET_INT_H */
