/*
 *  NMI monitor handler class and helpers definitions.
 *
 *  Copyright IBM Corp., 2014
 *
 *  Author: Alexey Kardashevskiy <aik@ozlabs.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NMI_H
#define NMI_H

#include "qom/object.h"

#define TYPE_NMI "nmi"

typedef struct NMIClass NMIClass;
DECLARE_CLASS_CHECKERS(NMIClass, NMI,
                       TYPE_NMI)
#define NMI(obj) \
     INTERFACE_CHECK(NMIState, (obj), TYPE_NMI)

typedef struct NMIState NMIState;

struct NMIClass {
    InterfaceClass parent_class;

    /**
     * raise_nmi: Callback to handle NMI notifications.
     * @ns: Class #NMIState state
     *
     * Called by nmi_inject() to perform the machine-specific
     * action when a NMI is requested.
     */
    void (*raise_nmi)(NMIState *ns);
};

/**
 * nmi_inject: Inject an NMI, in a machine-specific way
 * @errp: pointer to error object
 *
 * This function injects an NMI, in a machine-specific way. The
 * intention is that this should typically trigger a guest kernel
 * dump or reboot, and might happen as a result of user request
 * from the monitor, watchdog timeouts, and similar events.
 * (For example on the x86 PC it triggers an NMI on all CPUs,
 * and on s390 it triggers the RESTART interrupt on the first CPU.)
 *
 * The NMI is injected by looking for a QOM object which implements
 * the TYPE_NMI interface, and calling its raise_nmi method. Usually
 * it is the machine model class that implements this interface.
 *
 * Not all machines implement NMI handling; this function
 * will return an error if used on a machine which does not
 * implement NMIs.
 *
 * On success, return %true.
 * On failure, store an error through @errp and return %false.
 */
bool nmi_inject(Error **errp);

#endif /* NMI_H */
