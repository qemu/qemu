/*
 * Accelerator MSI route change tracking
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_ROUTE_H
#define ACCEL_ROUTE_H

#include "qemu/accel.h"

typedef struct AccelRouteChange {
    AccelState *accel;
    int changes;
} AccelRouteChange;

#endif /* ACCEL_ROUTE_H */
