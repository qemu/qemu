/*
 * QEMU list authorization object tests
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
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "authz/listfile.h"

static char *workdir;

static gchar *qemu_authz_listfile_test_save(const gchar *name,
                                            const gchar *cfg)
{
    gchar *path = g_strdup_printf("%s/default-deny.cfg", workdir);
    GError *gerr = NULL;

    if (!g_file_set_contents(path, cfg, -1, &gerr)) {
        g_printerr("Unable to save config %s: %s\n",
                   path, gerr->message);
        g_error_free(gerr);
        g_free(path);
        rmdir(workdir);
        abort();
    }

    return path;
}

static void test_authz_default_deny(void)
{
    gchar *file = qemu_authz_listfile_test_save(
        "default-deny.cfg",
        "{ \"policy\": \"deny\" }");
    Error *local_err = NULL;

    QAuthZListFile *auth = qauthz_list_file_new("auth0",
                                                file, false,
                                                &local_err);
    unlink(file);
    g_free(file);
    g_assert(local_err == NULL);
    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_default_allow(void)
{
    gchar *file = qemu_authz_listfile_test_save(
        "default-allow.cfg",
        "{ \"policy\": \"allow\" }");
    Error *local_err = NULL;

    QAuthZListFile *auth = qauthz_list_file_new("auth0",
                                                file, false,
                                                &local_err);
    unlink(file);
    g_free(file);
    g_assert(local_err == NULL);
    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_explicit_deny(void)
{
    gchar *file = qemu_authz_listfile_test_save(
        "explicit-deny.cfg",
        "{ \"rules\": [ "
        "    { \"match\": \"fred\","
        "      \"policy\": \"deny\","
        "      \"format\": \"exact\" } ],"
        "  \"policy\": \"allow\" }");
    Error *local_err = NULL;

    QAuthZListFile *auth = qauthz_list_file_new("auth0",
                                                file, false,
                                                &local_err);
    unlink(file);
    g_free(file);
    g_assert(local_err == NULL);

    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}

static void test_authz_explicit_allow(void)
{
    gchar *file = qemu_authz_listfile_test_save(
        "explicit-allow.cfg",
        "{ \"rules\": [ "
        "    { \"match\": \"fred\","
        "      \"policy\": \"allow\","
        "      \"format\": \"exact\" } ],"
        "  \"policy\": \"deny\" }");
    Error *local_err = NULL;

    QAuthZListFile *auth = qauthz_list_file_new("auth0",
                                                file, false,
                                                &local_err);
    unlink(file);
    g_free(file);
    g_assert(local_err == NULL);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));

    object_unparent(OBJECT(auth));
}


static void test_authz_complex(void)
{
    gchar *file = qemu_authz_listfile_test_save(
        "complex.cfg",
        "{ \"rules\": [ "
        "    { \"match\": \"fred\","
        "      \"policy\": \"allow\","
        "      \"format\": \"exact\" },"
        "    { \"match\": \"bob\","
        "      \"policy\": \"allow\","
        "      \"format\": \"exact\" },"
        "    { \"match\": \"dan\","
        "      \"policy\": \"deny\","
        "      \"format\": \"exact\" },"
        "    { \"match\": \"dan*\","
        "      \"policy\": \"allow\","
        "      \"format\": \"glob\" } ],"
        "  \"policy\": \"deny\" }");

    Error *local_err = NULL;

    QAuthZListFile *auth = qauthz_list_file_new("auth0",
                                                file, false,
                                                &local_err);
    unlink(file);
    g_free(file);
    g_assert(local_err == NULL);

    g_assert(qauthz_is_allowed(QAUTHZ(auth), "fred", &error_abort));
    g_assert(qauthz_is_allowed(QAUTHZ(auth), "bob", &error_abort));
    g_assert(!qauthz_is_allowed(QAUTHZ(auth), "dan", &error_abort));
    g_assert(qauthz_is_allowed(QAUTHZ(auth), "danb", &error_abort));

    object_unparent(OBJECT(auth));
}


int main(int argc, char **argv)
{
    int ret;
    GError *gerr = NULL;

    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);

    workdir = g_dir_make_tmp("qemu-test-authz-listfile-XXXXXX",
                             &gerr);
    if (!workdir) {
        g_printerr("Unable to create temporary dir: %s\n",
                   gerr->message);
        g_error_free(gerr);
        abort();
    }

    g_test_add_func("/auth/list/default/deny", test_authz_default_deny);
    g_test_add_func("/auth/list/default/allow", test_authz_default_allow);
    g_test_add_func("/auth/list/explicit/deny", test_authz_explicit_deny);
    g_test_add_func("/auth/list/explicit/allow", test_authz_explicit_allow);
    g_test_add_func("/auth/list/complex", test_authz_complex);

    ret = g_test_run();

    rmdir(workdir);
    g_free(workdir);

    return ret;
}
