/*
 * Dynamic sysbus device tree node generation API
 *
 * Copyright Linaro Limited, 2014
 *
 * Authors:
 *  Alex Graf <agraf@suse.de>
 *  Eric Auger <eric.auger@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HW_ARM_SYSBUS_FDT_H
#define HW_ARM_SYSBUS_FDT_H

#include "exec/hwaddr.h"

/**
 * platform_bus_add_all_fdt_nodes - create all the platform bus nodes
 *
 * builds the parent platform bus node and all the nodes of dynamic
 * sysbus devices attached to it.
 */
void platform_bus_add_all_fdt_nodes(void *fdt, const char *intc, hwaddr addr,
                                    hwaddr bus_size, int irq_start);
#endif
