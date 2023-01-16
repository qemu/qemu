/*
 * QEMU Bonito64 north bridge support
 *
 * Copyright (c) 2008 yajin (yajin@vm-kernel.org)
 * Copyright (c) 2010 Huacai Chen (zltjiangshi@gmail.com)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCI_HOST_BONITO_H
#define HW_PCI_HOST_BONITO_H

#include "qom/object.h"

#define TYPE_BONITO_PCI_HOST_BRIDGE "Bonito-pcihost"
OBJECT_DECLARE_SIMPLE_TYPE(BonitoState, BONITO_PCI_HOST_BRIDGE)

#endif
