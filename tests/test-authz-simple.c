/*
 * QEMU simple authorization object testing
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
#include "qapi/error.h"
#include "qemu/module.h"

#include "authz/simple.h"


static void test_authz_simple(void)
{
    QAuthZSimple *authz = qauthz_simple_new("authz0",
                                            "cthulu",
                                            &error_abort);

    g_assert(!qauthz_is_allowed(QAUTHZ(authz), "cthul", &error_abort));
    g_assert(qauthz_is_allowed(QAUTHZ(authz), "cthulu", &error_abort));
    g_assert(!qauthz_is_allowed(QAUTHZ(authz), "cthuluu", &error_abort));
    g_assert(!qauthz_is_allowed(QAUTHZ(authz), "fred", &error_abort));

    object_unparent(OBJECT(authz));
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);

    g_test_add_func("/authz/simple", test_authz_simple);

    return g_test_run();
}
