/*
 * QEMU access control list authorization driver
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

#include "qemu/osdep.h"
#include "authz/list.h"
#include "trace.h"
#include "qom/object_interfaces.h"
#include "qapi/qapi-visit-authz.h"
#include "qemu/module.h"

static bool qauthz_list_is_allowed(QAuthZ *authz,
                                   const char *identity,
                                   Error **errp)
{
    QAuthZList *lauthz = QAUTHZ_LIST(authz);
    QAuthZListRuleList *rules = lauthz->rules;

    while (rules) {
        QAuthZListRule *rule = rules->value;
        QAuthZListFormat format = rule->has_format ? rule->format :
            QAUTHZ_LIST_FORMAT_EXACT;

        trace_qauthz_list_check_rule(authz, rule->match, identity,
                                     format, rule->policy);
        switch (format) {
        case QAUTHZ_LIST_FORMAT_EXACT:
            if (g_str_equal(rule->match, identity)) {
                return rule->policy == QAUTHZ_LIST_POLICY_ALLOW;
            }
            break;
        case QAUTHZ_LIST_FORMAT_GLOB:
            if (g_pattern_match_simple(rule->match, identity)) {
                return rule->policy == QAUTHZ_LIST_POLICY_ALLOW;
            }
            break;
        default:
            g_warn_if_reached();
            return false;
        }
        rules = rules->next;
    }

    trace_qauthz_list_default_policy(authz, identity, lauthz->policy);
    return lauthz->policy == QAUTHZ_LIST_POLICY_ALLOW;
}


static void
qauthz_list_prop_set_policy(Object *obj,
                            int value,
                            Error **errp G_GNUC_UNUSED)
{
    QAuthZList *lauthz = QAUTHZ_LIST(obj);

    lauthz->policy = value;
}


static int
qauthz_list_prop_get_policy(Object *obj,
                            Error **errp G_GNUC_UNUSED)
{
    QAuthZList *lauthz = QAUTHZ_LIST(obj);

    return lauthz->policy;
}


static void
qauthz_list_prop_get_rules(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    QAuthZList *lauthz = QAUTHZ_LIST(obj);

    visit_type_QAuthZListRuleList(v, name, &lauthz->rules, errp);
}

static void
qauthz_list_prop_set_rules(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    QAuthZList *lauthz = QAUTHZ_LIST(obj);
    QAuthZListRuleList *oldrules;

    oldrules = lauthz->rules;
    visit_type_QAuthZListRuleList(v, name, &lauthz->rules, errp);

    qapi_free_QAuthZListRuleList(oldrules);
}


static void
qauthz_list_finalize(Object *obj)
{
    QAuthZList *lauthz = QAUTHZ_LIST(obj);

    qapi_free_QAuthZListRuleList(lauthz->rules);
}


static void
qauthz_list_class_init(ObjectClass *oc, void *data)
{
    QAuthZClass *authz = QAUTHZ_CLASS(oc);

    object_class_property_add_enum(oc, "policy",
                                   "QAuthZListPolicy",
                                   &QAuthZListPolicy_lookup,
                                   qauthz_list_prop_get_policy,
                                   qauthz_list_prop_set_policy);

    object_class_property_add(oc, "rules", "QAuthZListRule",
                              qauthz_list_prop_get_rules,
                              qauthz_list_prop_set_rules,
                              NULL, NULL);

    authz->is_allowed = qauthz_list_is_allowed;
}


QAuthZList *qauthz_list_new(const char *id,
                            QAuthZListPolicy policy,
                            Error **errp)
{
    return QAUTHZ_LIST(
        object_new_with_props(TYPE_QAUTHZ_LIST,
                              object_get_objects_root(),
                              id, errp,
                              "policy", QAuthZListPolicy_str(policy),
                              NULL));
}

ssize_t qauthz_list_append_rule(QAuthZList *auth,
                                const char *match,
                                QAuthZListPolicy policy,
                                QAuthZListFormat format,
                                Error **errp)
{
    QAuthZListRule *rule;
    QAuthZListRuleList *rules, *tmp;
    size_t i = 0;

    rule = g_new0(QAuthZListRule, 1);
    rule->policy = policy;
    rule->match = g_strdup(match);
    rule->format = format;
    rule->has_format = true;

    tmp = g_new0(QAuthZListRuleList, 1);
    tmp->value = rule;

    rules = auth->rules;
    if (rules) {
        while (rules->next) {
            i++;
            rules = rules->next;
        }
        rules->next = tmp;
        return i + 1;
    } else {
        auth->rules = tmp;
        return 0;
    }
}


ssize_t qauthz_list_insert_rule(QAuthZList *auth,
                                const char *match,
                                QAuthZListPolicy policy,
                                QAuthZListFormat format,
                                size_t index,
                                Error **errp)
{
    QAuthZListRule *rule;
    QAuthZListRuleList *rules, *tmp;
    size_t i = 0;

    rule = g_new0(QAuthZListRule, 1);
    rule->policy = policy;
    rule->match = g_strdup(match);
    rule->format = format;
    rule->has_format = true;

    tmp = g_new0(QAuthZListRuleList, 1);
    tmp->value = rule;

    rules = auth->rules;
    if (rules && index > 0) {
        while (rules->next && i < (index - 1)) {
            i++;
            rules = rules->next;
        }
        tmp->next = rules->next;
        rules->next = tmp;
        return i + 1;
    } else {
        tmp->next = auth->rules;
        auth->rules = tmp;
        return 0;
    }
}


ssize_t qauthz_list_delete_rule(QAuthZList *auth, const char *match)
{
    QAuthZListRule *rule;
    QAuthZListRuleList *rules, *prev;
    size_t i = 0;

    prev = NULL;
    rules = auth->rules;
    while (rules) {
        rule = rules->value;
        if (g_str_equal(rule->match, match)) {
            if (prev) {
                prev->next = rules->next;
            } else {
                auth->rules = rules->next;
            }
            rules->next = NULL;
            qapi_free_QAuthZListRuleList(rules);
            return i;
        }
        prev = rules;
        rules = rules->next;
        i++;
    }

    return -1;
}


static const TypeInfo qauthz_list_info = {
    .parent = TYPE_QAUTHZ,
    .name = TYPE_QAUTHZ_LIST,
    .instance_size = sizeof(QAuthZList),
    .instance_finalize = qauthz_list_finalize,
    .class_size = sizeof(QAuthZListClass),
    .class_init = qauthz_list_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qauthz_list_register_types(void)
{
    type_register_static(&qauthz_list_info);
}


type_init(qauthz_list_register_types);
