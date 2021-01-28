/*
 * QEMU authorization framework base class
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QAUTHZ_BASE_H
#define QAUTHZ_BASE_H

#include "qapi/error.h"
#include "qom/object.h"


#define TYPE_QAUTHZ "authz"

OBJECT_DECLARE_TYPE(QAuthZ, QAuthZClass,
                    QAUTHZ)


/**
 * QAuthZ:
 *
 * The QAuthZ class defines an API contract to be used
 * for providing an authorization driver for services
 * with user identities.
 */

struct QAuthZ {
    Object parent_obj;
};


struct QAuthZClass {
    ObjectClass parent_class;

    bool (*is_allowed)(QAuthZ *authz,
                       const char *identity,
                       Error **errp);
};


/**
 * qauthz_is_allowed:
 * @authz: the authorization object
 * @identity: the user identity to authorize
 * @errp: pointer to a NULL initialized error object
 *
 * Check if a user @identity is authorized. If an error
 * occurs this method will return false to indicate
 * denial, as well as setting @errp to contain the details.
 * Callers are recommended to treat the denial and error
 * scenarios identically. Specifically the error info in
 * @errp should never be fed back to the user being
 * authorized, it is merely for benefit of administrator
 * debugging.
 *
 * Returns: true if @identity is authorized, false if denied or if
 * an error occurred.
 */
bool qauthz_is_allowed(QAuthZ *authz,
                       const char *identity,
                       Error **errp);


/**
 * qauthz_is_allowed_by_id:
 * @authzid: ID of the authorization object
 * @identity: the user identity to authorize
 * @errp: pointer to a NULL initialized error object
 *
 * Check if a user @identity is authorized. If an error
 * occurs this method will return false to indicate
 * denial, as well as setting @errp to contain the details.
 * Callers are recommended to treat the denial and error
 * scenarios identically. Specifically the error info in
 * @errp should never be fed back to the user being
 * authorized, it is merely for benefit of administrator
 * debugging.
 *
 * Returns: true if @identity is authorized, false if denied or if
 * an error occurred.
 */
bool qauthz_is_allowed_by_id(const char *authzid,
                             const char *identity,
                             Error **errp);

#endif /* QAUTHZ_BASE_H */
