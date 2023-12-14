/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HV_BALLOON_H
#define HW_HV_BALLOON_H

#include "qom/object.h"

#define TYPE_HV_BALLOON "hv-balloon"
OBJECT_DECLARE_SIMPLE_TYPE(HvBalloon, HV_BALLOON)

#endif
