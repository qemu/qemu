/*
 * QEMU PAM authorization driver
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#ifndef QAUTHZ_PAMACCT_H
#define QAUTHZ_PAMACCT_H

#include "authz/base.h"


#define TYPE_QAUTHZ_PAM "authz-pam"

#define QAUTHZ_PAM_CLASS(klass) \
     OBJECT_CLASS_CHECK(QAuthZPAMClass, (klass), \
                        TYPE_QAUTHZ_PAM)
#define QAUTHZ_PAM_GET_CLASS(obj) \
     OBJECT_GET_CLASS(QAuthZPAMClass, (obj), \
                      TYPE_QAUTHZ_PAM)
#define QAUTHZ_PAM(obj) \
     OBJECT_CHECK(QAuthZPAM, (obj), \
                  TYPE_QAUTHZ_PAM)

typedef struct QAuthZPAM QAuthZPAM;
typedef struct QAuthZPAMClass QAuthZPAMClass;


/**
 * QAuthZPAM:
 *
 * This authorization driver provides a PAM mechanism
 * for granting access by matching user names against a
 * list of globs. Each match rule has an associated policy
 * and a catch all policy applies if no rule matches
 *
 * To create an instance of this class via QMP:
 *
 *  {
 *    "execute": "object-add",
 *    "arguments": {
 *      "qom-type": "authz-pam",
 *      "id": "authz0",
 *      "parameters": {
 *        "service": "qemu-vnc-tls"
 *      }
 *    }
 *  }
 *
 * The driver only uses the PAM "account" verification
 * subsystem. The above config would require a config
 * file /etc/pam.d/qemu-vnc-tls. For a simple file
 * lookup it would contain
 *
 *   account requisite  pam_listfile.so item=user sense=allow \
 *           file=/etc/qemu/vnc.allow
 *
 * The external file would then contain a list of usernames.
 * If x509 cert was being used as the username, a suitable
 * entry would match the distinguish name:
 *
 *  CN=laptop.berrange.com,O=Berrange Home,L=London,ST=London,C=GB
 *
 * On the command line it can be created using
 *
 *   -object authz-pam,id=authz0,service=qemu-vnc-tls
 *
 */
struct QAuthZPAM {
    QAuthZ parent_obj;

    char *service;
};


struct QAuthZPAMClass {
    QAuthZClass parent_class;
};


QAuthZPAM *qauthz_pam_new(const char *id,
                          const char *service,
                          Error **errp);

#endif /* QAUTHZ_PAMACCT_H */
