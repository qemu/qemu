/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * envlist tests
 *
 * Copyright 2026 Virtuozzo International GmbH
 *
 * Authors:
 *  Denis V. Lunev <den@openvz.org>
 */

#include "qemu/osdep.h"
#include "qemu/envlist.h"

static void free_environ(char **env)
{
    char **p;

    for (p = env; *p != NULL; p++) {
        g_free(*p);
    }
    g_free(env);
}

static const char *find_env(char **env, const char *name)
{
    size_t name_len = strlen(name);
    char **p;

    for (p = env; *p != NULL; p++) {
        if (strncmp(*p, name, name_len) == 0 && (*p)[name_len] == '=') {
            return *p + name_len + 1;
        }
    }
    return NULL;
}

static void test_envlist_basic(void)
{
    envlist_t *el = envlist_create();
    char **env;
    size_t count;

    /* empty list */
    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 0);
    g_assert_null(env[0]);
    free_environ(env);

    /* add */
    g_assert_cmpint(envlist_setenv(el, "A=1"), ==, 0);
    g_assert_cmpint(envlist_setenv(el, "B=2"), ==, 0);

    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 2);
    g_assert_cmpstr(find_env(env, "A"), ==, "1");
    g_assert_cmpstr(find_env(env, "B"), ==, "2");
    free_environ(env);

    /* replace */
    g_assert_cmpint(envlist_setenv(el, "A=42"), ==, 0);
    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 2);
    g_assert_cmpstr(find_env(env, "A"), ==, "42");
    g_assert_cmpstr(find_env(env, "B"), ==, "2");
    free_environ(env);

    /* unset existing */
    g_assert_cmpint(envlist_unsetenv(el, "A"), ==, 0);
    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 1);
    g_assert_null(find_env(env, "A"));
    g_assert_cmpstr(find_env(env, "B"), ==, "2");
    free_environ(env);

    /* unset non-existing is a no-op success */
    g_assert_cmpint(envlist_unsetenv(el, "NOPE"), ==, 0);
    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 1);
    free_environ(env);

    envlist_free(el);
}

/*
 * envlist_setenv() inserts at the head; envlist_to_environ() walks
 * head-to-tail, so the last setenv comes out first.
 */
static void test_envlist_head_insertion_order(void)
{
    envlist_t *el = envlist_create();
    char **env;
    size_t count;

    g_assert_cmpint(envlist_setenv(el, "A=1"), ==, 0);
    g_assert_cmpint(envlist_setenv(el, "B=2"), ==, 0);
    g_assert_cmpint(envlist_setenv(el, "C=3"), ==, 0);

    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 3);
    g_assert_cmpstr(env[0], ==, "C=3");
    g_assert_cmpstr(env[1], ==, "B=2");
    g_assert_cmpstr(env[2], ==, "A=1");
    g_assert_null(env[3]);

    free_environ(env);
    envlist_free(el);
}

static void test_envlist_einval(void)
{
    envlist_t *el = envlist_create();

    /* NULL list */
    g_assert_cmpint(envlist_setenv(NULL, "A=1"), ==, EINVAL);
    g_assert_cmpint(envlist_unsetenv(NULL, "A"), ==, EINVAL);

    /* NULL string */
    g_assert_cmpint(envlist_setenv(el, NULL), ==, EINVAL);
    g_assert_cmpint(envlist_unsetenv(el, NULL), ==, EINVAL);

    /* setenv: missing '=' */
    g_assert_cmpint(envlist_setenv(el, "NOEQ"), ==, EINVAL);

    /* unsetenv: name must not contain '=' */
    g_assert_cmpint(envlist_unsetenv(el, "A=B"), ==, EINVAL);

    envlist_free(el);
}

/*
 * Regression: envlist_unsetenv("FOO") must not remove an entry named
 * "FOOBAR" -- the previous strncmp(entry, name, strlen(name)) lookup
 * prefix-matched. To trigger the bug, the longer-named entry has to
 * be ahead of the target in the list: envlist_setenv() inserts at
 * the head, so we add FOO first and FOOBAR last.
 */
static void test_envlist_unsetenv_no_prefix_match(void)
{
    envlist_t *el = envlist_create();
    char **env;
    size_t count;

    g_assert_cmpint(envlist_setenv(el, "FOO=y"), ==, 0);
    g_assert_cmpint(envlist_setenv(el, "FOOBAR=x"), ==, 0);

    g_assert_cmpint(envlist_unsetenv(el, "FOO"), ==, 0);

    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 1);
    g_assert_cmpstr(find_env(env, "FOOBAR"), ==, "x");
    g_assert_null(find_env(env, "FOO"));

    free_environ(env);
    envlist_free(el);
}

/*
 * envlist_setenv() must not replace a prior FOOBAR=... entry when
 * setting FOO=... The pre-fix code happened to be safe here only
 * because it included the trailing '=' byte in its strncmp length;
 * this test pins down the post-fix contract that the name boundary
 * is a property of the entry, not of the encoded form.
 */
static void test_envlist_setenv_no_prefix_match(void)
{
    envlist_t *el = envlist_create();
    char **env;
    size_t count;

    g_assert_cmpint(envlist_setenv(el, "FOOBAR=x"), ==, 0);
    g_assert_cmpint(envlist_setenv(el, "FOO=y"), ==, 0);

    env = envlist_to_environ(el, &count);
    g_assert_cmpuint(count, ==, 2);
    g_assert_cmpstr(find_env(env, "FOOBAR"), ==, "x");
    g_assert_cmpstr(find_env(env, "FOO"), ==, "y");

    free_environ(env);
    envlist_free(el);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/envlist/basic", test_envlist_basic);
    g_test_add_func("/envlist/head_insertion_order",
                    test_envlist_head_insertion_order);
    g_test_add_func("/envlist/einval", test_envlist_einval);
    g_test_add_func("/envlist/unsetenv_no_prefix_match",
                    test_envlist_unsetenv_no_prefix_match);
    g_test_add_func("/envlist/setenv_no_prefix_match",
                    test_envlist_setenv_no_prefix_match);

    return g_test_run();
}
