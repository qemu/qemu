/*
 * QEMU ARM CPU -- interface for the Arm v8M IDAU
 *
 * Copyright (c) 2018 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 *
 * In the v8M architecture, the IDAU is a small piece of hardware
 * typically implemented in the SoC which provides board or SoC
 * specific security attribution information for each address that
 * the CPU performs MPU/SAU checks on. For QEMU, we model this with a
 * QOM interface which is implemented by the board or SoC object and
 * connected to the CPU using a link property.
 */

#ifndef TARGET_ARM_IDAU_H
#define TARGET_ARM_IDAU_H

#include "qom/object.h"

#define TYPE_IDAU_INTERFACE "idau-interface"
#define IDAU_INTERFACE(obj) \
    INTERFACE_CHECK(IDAUInterface, (obj), TYPE_IDAU_INTERFACE)
typedef struct IDAUInterfaceClass IDAUInterfaceClass;
DECLARE_CLASS_CHECKERS(IDAUInterfaceClass, IDAU_INTERFACE,
                       TYPE_IDAU_INTERFACE)

typedef struct IDAUInterface IDAUInterface;

#define IREGION_NOTVALID -1

struct IDAUInterfaceClass {
    InterfaceClass parent;

    /* Check the specified address and return the IDAU security information
     * for it by filling in iregion, exempt, ns and nsc:
     *  iregion: IDAU region number, or IREGION_NOTVALID if not valid
     *  exempt: true if address is exempt from security attribution
     *  ns: true if the address is NonSecure
     *  nsc: true if the address is NonSecure-callable
     */
    void (*check)(IDAUInterface *ii, uint32_t address, int *iregion,
                  bool *exempt, bool *ns, bool *nsc);
};

#endif
