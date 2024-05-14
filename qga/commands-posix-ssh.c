 /*
  * This work is licensed under the terms of the GNU GPL, version 2 or later.
  * See the COPYING file in the top-level directory.
  */
#include "qemu/osdep.h"

#include <glib-unix.h>
#include <glib/gstdio.h>
#include <locale.h>
#include <pwd.h>

#include "commands-common-ssh.h"
#include "qapi/error.h"
#include "qga-qapi-commands.h"

#ifdef QGA_BUILD_UNIT_TEST
static struct passwd *
test_get_passwd_entry(const gchar *user_name, GError **error)
{
    struct passwd *p;
    int ret;

    if (!user_name || g_strcmp0(user_name, g_get_user_name())) {
        g_set_error(error, G_UNIX_ERROR, 0, "Invalid user name");
        return NULL;
    }

    p = g_new0(struct passwd, 1);
    p->pw_dir = (char *)g_get_home_dir();
    p->pw_uid = geteuid();
    p->pw_gid = getegid();

    ret = g_mkdir_with_parents(p->pw_dir, 0700);
    g_assert(ret == 0);

    return p;
}

#define g_unix_get_passwd_entry(username, err) \
   test_get_passwd_entry(username, err)
#endif

static struct passwd *
get_passwd_entry(const char *username, Error **errp)
{
    g_autoptr(GError) err = NULL;
    struct passwd *p;

    p = g_unix_get_passwd_entry(username, &err);
    if (p == NULL) {
        error_setg(errp, "failed to lookup user '%s': %s",
                   username, err->message);
        return NULL;
    }

    return p;
}

static bool
mkdir_for_user(const char *path, const struct passwd *p,
               mode_t mode, Error **errp)
{
    if (g_mkdir(path, mode) == -1) {
        error_setg(errp, "failed to create directory '%s': %s",
                   path, g_strerror(errno));
        return false;
    }

    if (chown(path, p->pw_uid, p->pw_gid) == -1) {
        error_setg(errp, "failed to set ownership of directory '%s': %s",
                   path, g_strerror(errno));
        return false;
    }

    if (chmod(path, mode) == -1) {
        error_setg(errp, "failed to set permissions of directory '%s': %s",
                   path, g_strerror(errno));
        return false;
    }

    return true;
}

static bool
write_authkeys(const char *path, const GStrv keys,
               const struct passwd *p, Error **errp)
{
    g_autofree char *contents = NULL;
    g_autoptr(GError) err = NULL;

    contents = g_strjoinv("\n", keys);
    if (!g_file_set_contents(path, contents, -1, &err)) {
        error_setg(errp, "failed to write to '%s': %s", path, err->message);
        return false;
    }

    if (chown(path, p->pw_uid, p->pw_gid) == -1) {
        error_setg(errp, "failed to set ownership of directory '%s': %s",
                   path, g_strerror(errno));
        return false;
    }

    if (chmod(path, 0600) == -1) {
        error_setg(errp, "failed to set permissions of '%s': %s",
                   path, g_strerror(errno));
        return false;
    }

    return true;
}

void
qmp_guest_ssh_add_authorized_keys(const char *username, strList *keys,
                                  bool has_reset, bool reset,
                                  Error **errp)
{
    g_autofree struct passwd *p = NULL;
    g_autofree char *ssh_path = NULL;
    g_autofree char *authkeys_path = NULL;
    g_auto(GStrv) authkeys = NULL;
    strList *k;
    size_t nkeys, nauthkeys;

    reset = has_reset && reset;

    if (!check_openssh_pub_keys(keys, &nkeys, errp)) {
        return;
    }

    p = get_passwd_entry(username, errp);
    if (p == NULL) {
        return;
    }

    ssh_path = g_build_filename(p->pw_dir, ".ssh", NULL);
    authkeys_path = g_build_filename(ssh_path, "authorized_keys", NULL);

    if (!reset) {
        authkeys = read_authkeys(authkeys_path, NULL);
    }
    if (authkeys == NULL) {
        if (!g_file_test(ssh_path, G_FILE_TEST_IS_DIR) &&
            !mkdir_for_user(ssh_path, p, 0700, errp)) {
            return;
        }
    }

    nauthkeys = authkeys ? g_strv_length(authkeys) : 0;
    authkeys = g_realloc_n(authkeys, nauthkeys + nkeys + 1, sizeof(char *));
    memset(authkeys + nauthkeys, 0, (nkeys + 1) * sizeof(char *));

    for (k = keys; k != NULL; k = k->next) {
        if (g_strv_contains((const gchar * const *)authkeys, k->value)) {
            continue;
        }
        authkeys[nauthkeys++] = g_strdup(k->value);
    }

    write_authkeys(authkeys_path, authkeys, p, errp);
}

void
qmp_guest_ssh_remove_authorized_keys(const char *username, strList *keys,
                                     Error **errp)
{
    g_autofree struct passwd *p = NULL;
    g_autofree char *authkeys_path = NULL;
    g_autofree GStrv new_keys = NULL; /* do not own the strings */
    g_auto(GStrv) authkeys = NULL;
    GStrv a;
    size_t nkeys = 0;

    if (!check_openssh_pub_keys(keys, NULL, errp)) {
        return;
    }

    p = get_passwd_entry(username, errp);
    if (p == NULL) {
        return;
    }

    authkeys_path = g_build_filename(p->pw_dir, ".ssh",
                                     "authorized_keys", NULL);
    if (!g_file_test(authkeys_path, G_FILE_TEST_EXISTS)) {
        return;
    }
    authkeys = read_authkeys(authkeys_path, errp);
    if (authkeys == NULL) {
        return;
    }

    new_keys = g_new0(char *, g_strv_length(authkeys) + 1);
    for (a = authkeys; *a != NULL; a++) {
        strList *k;

        for (k = keys; k != NULL; k = k->next) {
            if (g_str_equal(k->value, *a)) {
                break;
            }
        }
        if (k != NULL) {
            continue;
        }

        new_keys[nkeys++] = *a;
    }

    write_authkeys(authkeys_path, new_keys, p, errp);
}

GuestAuthorizedKeys *
qmp_guest_ssh_get_authorized_keys(const char *username, Error **errp)
{
    g_autofree struct passwd *p = NULL;
    g_autofree char *authkeys_path = NULL;
    g_auto(GStrv) authkeys = NULL;
    g_autoptr(GuestAuthorizedKeys) ret = NULL;
    int i;

    p = get_passwd_entry(username, errp);
    if (p == NULL) {
        return NULL;
    }

    authkeys_path = g_build_filename(p->pw_dir, ".ssh",
                                     "authorized_keys", NULL);
    authkeys = read_authkeys(authkeys_path, errp);
    if (authkeys == NULL) {
        return NULL;
    }

    ret = g_new0(GuestAuthorizedKeys, 1);
    for (i = 0; authkeys[i] != NULL; i++) {
        g_strstrip(authkeys[i]);
        if (!authkeys[i][0] || authkeys[i][0] == '#') {
            continue;
        }

        QAPI_LIST_PREPEND(ret->keys, g_strdup(authkeys[i]));
    }

    return g_steal_pointer(&ret);
}

#ifdef QGA_BUILD_UNIT_TEST
static const strList test_key2 = {
    .value = (char *)"algo key2 comments"
};

static const strList test_key1_2 = {
    .value = (char *)"algo key1 comments",
    .next = (strList *)&test_key2,
};

static char *
test_get_authorized_keys_path(void)
{
    return g_build_filename(g_get_home_dir(), ".ssh", "authorized_keys", NULL);
}

static void
test_authorized_keys_set(const char *contents)
{
    g_autoptr(GError) err = NULL;
    g_autofree char *path = NULL;
    int ret;

    path = g_build_filename(g_get_home_dir(), ".ssh", NULL);
    ret = g_mkdir_with_parents(path, 0700);
    g_assert(ret == 0);
    g_free(path);

    path = test_get_authorized_keys_path();
    g_file_set_contents(path, contents, -1, &err);
    g_assert(err == NULL);
}

static void
test_authorized_keys_equal(const char *expected)
{
    g_autoptr(GError) err = NULL;
    g_autofree char *path = NULL;
    g_autofree char *contents = NULL;

    path = test_get_authorized_keys_path();
    g_file_get_contents(path, &contents, NULL, &err);
    g_assert(err == NULL);

    g_assert(g_strcmp0(contents, expected) == 0);
}

static void
test_invalid_user(void)
{
    Error *err = NULL;

    qmp_guest_ssh_add_authorized_keys("", NULL, FALSE, FALSE, &err);
    error_free_or_abort(&err);

    qmp_guest_ssh_remove_authorized_keys("", NULL, &err);
    error_free_or_abort(&err);
}

static void
test_invalid_key(void)
{
    strList key = {
        .value = (char *)"not a valid\nkey"
    };
    Error *err = NULL;

    qmp_guest_ssh_add_authorized_keys(g_get_user_name(), &key,
                                      FALSE, FALSE, &err);
    error_free_or_abort(&err);

    qmp_guest_ssh_remove_authorized_keys(g_get_user_name(), &key, &err);
    error_free_or_abort(&err);
}

static void
test_add_keys(void)
{
    Error *err = NULL;

    qmp_guest_ssh_add_authorized_keys(g_get_user_name(),
                                      (strList *)&test_key2,
                                      FALSE, FALSE,
                                      &err);
    g_assert(err == NULL);

    test_authorized_keys_equal("algo key2 comments");

    qmp_guest_ssh_add_authorized_keys(g_get_user_name(),
                                      (strList *)&test_key1_2,
                                      FALSE, FALSE,
                                      &err);
    g_assert(err == NULL);

    /*  key2 came first, and shouldn't be duplicated */
    test_authorized_keys_equal("algo key2 comments\n"
                               "algo key1 comments");
}

static void
test_add_reset_keys(void)
{
    Error *err = NULL;

    qmp_guest_ssh_add_authorized_keys(g_get_user_name(),
                                      (strList *)&test_key1_2,
                                      FALSE, FALSE,
                                      &err);
    g_assert(err == NULL);

    /* reset with key2 only */
    test_authorized_keys_equal("algo key1 comments\n"
                               "algo key2 comments");

    qmp_guest_ssh_add_authorized_keys(g_get_user_name(),
                                      (strList *)&test_key2,
                                      TRUE, TRUE,
                                      &err);
    g_assert(err == NULL);

    test_authorized_keys_equal("algo key2 comments");

    /* empty should clear file */
    qmp_guest_ssh_add_authorized_keys(g_get_user_name(),
                                      (strList *)NULL,
                                      TRUE, TRUE,
                                      &err);
    g_assert(err == NULL);

    test_authorized_keys_equal("");
}

static void
test_remove_keys(void)
{
    Error *err = NULL;
    static const char *authkeys =
        "algo key1 comments\n"
        /* originally duplicated */
        "algo key1 comments\n"
        "# a commented line\n"
        "algo some-key another\n";

    test_authorized_keys_set(authkeys);
    qmp_guest_ssh_remove_authorized_keys(g_get_user_name(),
                                         (strList *)&test_key2, &err);
    g_assert(err == NULL);
    test_authorized_keys_equal(authkeys);

    qmp_guest_ssh_remove_authorized_keys(g_get_user_name(),
                                         (strList *)&test_key1_2, &err);
    g_assert(err == NULL);
    test_authorized_keys_equal("# a commented line\n"
                               "algo some-key another\n");
}

static void
test_get_keys(void)
{
    Error *err = NULL;
    static const char *authkeys =
        "algo key1 comments\n"
        "# a commented line\n"
        "algo some-key another\n";
    g_autoptr(GuestAuthorizedKeys) ret = NULL;
    strList *k;
    size_t len = 0;

    test_authorized_keys_set(authkeys);

    ret = qmp_guest_ssh_get_authorized_keys(g_get_user_name(), &err);
    g_assert(err == NULL);

    for (len = 0, k = ret->keys; k != NULL; k = k->next) {
        g_assert(g_str_has_prefix(k->value, "algo "));
        len++;
    }

    g_assert(len == 2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    g_test_init(&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

    g_test_add_func("/qga/ssh/invalid_user", test_invalid_user);
    g_test_add_func("/qga/ssh/invalid_key", test_invalid_key);
    g_test_add_func("/qga/ssh/add_keys", test_add_keys);
    g_test_add_func("/qga/ssh/add_reset_keys", test_add_reset_keys);
    g_test_add_func("/qga/ssh/remove_keys", test_remove_keys);
    g_test_add_func("/qga/ssh/get_keys", test_get_keys);

    return g_test_run();
}
#endif /* BUILD_UNIT_TEST */
