/*
 * QEMU list file authorization object tests
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

#include "qemu/osdep.h"

#include "authz/list.h"
#include "qemu/module.h"

static void test_authz_default_deny(void)
{
    QAuthZList *auth = qauthz_list_new("auth0",
                                       QAUTHZ_LIST_POLICY_DENY,
                                       &error_abort);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_default_allow(void)
{
    QAuthZList *auth = qauthz_list_new("auth0",
                                       QAUTHZ_LIST_POLICY_ALLOW,
                                       &error_abort);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_explicit_deny(void)
{
    QAuthZList *auth = qauthz_list_new("auth0",
                                       QAUTHZ_LIST_POLICY_ALLOW,
                                       &error_abort);

    qauthz_list_append_rule(auth, "fred", QAUTHZ_LIST_POLICY_DENY,
                            QAUTHZ_LIST_FORMAT_EXACT, &error_abort);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_explicit_allow(void)
{
    QAuthZList *auth = qauthz_list_new("auth0",
                                       QAUTHZ_LIST_POLICY_DENY,
                                       &error_abort);

    qauthz_list_append_rule(auth, "fred", QAUTHZ_LIST_POLICY_ALLOW,
                            QAUTHZ_LIST_FORMAT_EXACT, &error_abort);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}


static void test_authz_complex(void)
{
    QAuthZList *auth = qauthz_list_new("auth0",
                                       QAUTHZ_LIST_POLICY_DENY,
                                       &error_abort);

    qauthz_list_append_rule(auth, "fred", QAUTHZ_LIST_POLICY_ALLOW,
                            QAUTHZ_LIST_FORMAT_EXACT, &error_abort);
    qauthz_list_append_rule(auth, "bob", QAUTHZ_LIST_POLICY_ALLOW,
                            QAUTHZ_LIST_FORMAT_EXACT, &error_abort);
    qauthz_list_append_rule(auth, "dan", QAUTHZ_LIST_POLICY_DENY,
                            QAUTHZ_LIST_FORMAT_EXACT, &error_abort);
    qauthz_list_append_rule(auth, "dan*", QAUTHZ_LIST_POLICY_ALLOW,
                            QAUTHZ_LIST_FORMAT_GLOB, &error_abort);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));
    g_assert(qauthz_is_allowed(QAUTHZ(auth), "bob", &error_abort));
    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));
    g_assert(qauthz_is_allowed(QAUTHZ(auth), "danb", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_add_remove(void)
{
    QAuthZList *auth = qauthz_list_new("auth0",
                                       QAUTHZ_LIST_POLICY_ALLOW,
                                       &error_abort);

    g_assert_cmpint(qauthz_list_append_rule(auth, "fred",
                                            QAUTHZ_LIST_POLICY_ALLOW,
                                            QAUTHZ_LIST_FORMAT_EXACT,
                                            &error_abort),
                    ==, 0);
    g_assert_cmpint(qauthz_list_append_rule(auth, "bob",
                                            QAUTHZ_LIST_POLICY_ALLOW,
                                            QAUTHZ_LIST_FORMAT_EXACT,
                                            &error_abort),
                    ==, 1);
    g_assert_cmpint(qauthz_list_append_rule(auth, "dan",
                                            QAUTHZ_LIST_POLICY_DENY,
                                            QAUTHZ_LIST_FORMAT_EXACT,
                                            &error_abort),
                    ==, 2);
    g_assert_cmpint(qauthz_list_append_rule(auth, "frank",
                                            QAUTHZ_LIST_POLICY_DENY,
                                            QAUTHZ_LIST_FORMAT_EXACT,
                                            &error_abort),
                    ==, 3);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));

    g_assert_cmpint(qauthz_list_delete_rule(auth, "dan"),
                    ==, 2);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));

    g_assert_cmpint(qauthz_list_insert_rule(auth, "dan",
                                            QAUTHZ_LIST_POLICY_DENY,
                                            QAUTHZ_LIST_FORMAT_EXACT,
                                            2,
                                            &error_abort),
                    ==, 2);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));

    object_unparent(OBJECT(auth));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);

    g_test_add_func("/auth/list/default/deny", test_authz_default_deny);
    g_test_add_func("/auth/list/default/allow", test_authz_default_allow);
    g_test_add_func("/auth/list/explicit/deny", test_authz_explicit_deny);
    g_test_add_func("/auth/list/explicit/allow", test_authz_explicit_allow);
    g_test_add_func("/auth/list/complex", test_authz_complex);
    g_test_add_func("/auth/list/add-remove", test_authz_add_remove);

    return g_test_run();
}
