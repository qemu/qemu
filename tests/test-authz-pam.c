/*
 * QEMU PAM authorization object tests
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
#include "authz/pamacct.h"

#include <security/pam_appl.h>

static bool failauth;

/*
 * These three functions are exported by libpam.so.
 *
 * By defining them again here, our impls are resolved
 * by the linker instead of those in libpam.so
 *
 * The test suite is thus isolated from the host system
 * PAM setup, so we can do predictable test scenarios
 */
int
pam_start(const char *service_name, const char *user,
          const struct pam_conv *pam_conversation,
          pam_handle_t **pamh)
{
    failauth = true;
    if (!g_str_equal(service_name, "qemu-vnc")) {
        return PAM_AUTH_ERR;
    }

    if (g_str_equal(user, "fred")) {
        failauth = false;
    }

    *pamh = (pam_handle_t *)0xbadeaffe;
    return PAM_SUCCESS;
}


int
pam_acct_mgmt(pam_handle_t *pamh, int flags)
{
    if (failauth) {
        return PAM_AUTH_ERR;
    }

    return PAM_SUCCESS;
}


int
pam_end(pam_handle_t *pamh, int status)
{
    return PAM_SUCCESS;
}


static void test_authz_unknown_service(void)
{
    Error *local_err = NULL;
    QAuthZPAM *auth = qauthz_pam_new("auth0",
                                     "qemu-does-not-exist",
                                     &error_abort);

    g_assert_nonnull(auth);

    g_assert_false(qauthz_is_allowed(QAUTHZ(auth), "fred", &local_err));

    error_free_or_abort(&local_err);
    object_unparent(OBJECT(auth));
}


static void test_authz_good_user(void)
{
    QAuthZPAM *auth = qauthz_pam_new("auth0",
                                     "qemu-vnc",
                                     &error_abort);

    g_assert_nonnull(auth);

    g_assert_true(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}


static void test_authz_bad_user(void)
{
    Error *local_err = NULL;
    QAuthZPAM *auth = qauthz_pam_new("auth0",
                                     "qemu-vnc",
                                     &error_abort);

    g_assert_nonnull(auth);

    g_assert_false(qauthz_is_allowed(QAUTHZ(auth), "bob", &local_err));

    error_free_or_abort(&local_err);
    object_unparent(OBJECT(auth));
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);

    g_test_add_func("/auth/pam/unknown-service", test_authz_unknown_service);
    g_test_add_func("/auth/pam/good-user", test_authz_good_user);
    g_test_add_func("/auth/pam/bad-user", test_authz_bad_user);

    return g_test_run();
}
