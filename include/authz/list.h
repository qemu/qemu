/*
 * QEMU list authorization driver
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

#ifndef QAUTHZ_LIST_H
#define QAUTHZ_LIST_H

#include "authz/base.h"
#include "qapi/qapi-types-authz.h"
#include "qom/object.h"

#define TYPE_QAUTHZ_LIST "authz-list"

OBJECT_DECLARE_SIMPLE_TYPE(QAuthZList,
                           QAUTHZ_LIST)



/**
 * QAuthZList:
 *
 * This authorization driver provides a list mechanism
 * for granting access by matching user names against a
 * list of globs. Each match rule has an associated policy
 * and a catch all policy applies if no rule matches
 *
 * To create an instance of this class via QMP:
 *
 *  {
 *    "execute": "object-add",
 *    "arguments": {
 *      "qom-type": "authz-list",
 *      "id": "authz0",
 *      "props": {
 *        "rules": [
 *           { "match": "fred", "policy": "allow", "format": "exact" },
 *           { "match": "bob", "policy": "allow", "format": "exact" },
 *           { "match": "danb", "policy": "deny", "format": "exact" },
 *           { "match": "dan*", "policy": "allow", "format": "glob" }
 *        ],
 *        "policy": "deny"
 *      }
 *    }
 *  }
 *
 */
struct QAuthZList {
    QAuthZ parent_obj;

    QAuthZListPolicy policy;
    QAuthZListRuleList *rules;
};




QAuthZList *qauthz_list_new(const char *id,
                            QAuthZListPolicy policy,
                            Error **errp);

ssize_t qauthz_list_append_rule(QAuthZList *auth,
                                const char *match,
                                QAuthZListPolicy policy,
                                QAuthZListFormat format,
                                Error **errp);

ssize_t qauthz_list_insert_rule(QAuthZList *auth,
                                const char *match,
                                QAuthZListPolicy policy,
                                QAuthZListFormat format,
                                size_t index,
                                Error **errp);

ssize_t qauthz_list_delete_rule(QAuthZList *auth,
                                const char *match);


#endif /* QAUTHZ_LIST_H */
