/*
 * QEMU Crypto secret handling
 *
 * Copyright (c) 2015 Red Hat, Inc.
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

#include "crypto/init.h"
#include "crypto/secret.h"
#include "qapi/error.h"
#include "qemu/module.h"
#if defined(CONFIG_KEYUTILS) && defined(CONFIG_SECRET_KEYRING)
#include "crypto/secret_keyring.h"
#include <keyutils.h>
#endif

static void test_secret_direct(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "123456",
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);

    g_assert_cmpstr(pw, ==, "123456");

    object_unparent(sec);
    g_free(pw);
}


static void test_secret_indirect_good(void)
{
    Object *sec;
    char *fname = NULL;
    int fd = g_file_open_tmp("qemu-test-crypto-secret-XXXXXX",
                             &fname,
                             NULL);

    g_assert(fd >= 0);
    g_assert_nonnull(fname);

    g_assert(write(fd, "123456", 6) == 6);

    sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "file", fname,
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);

    g_assert_cmpstr(pw, ==, "123456");

    object_unparent(sec);
    g_free(pw);
    close(fd);
    unlink(fname);
    g_free(fname);
}


static void test_secret_indirect_badfile(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        NULL,
        "file", "does-not-exist",
        NULL);

    g_assert(sec == NULL);
}


static void test_secret_indirect_emptyfile(void)
{
    Object *sec;
    char *fname = NULL;
    int fd = g_file_open_tmp("qemu-test-crypto-secretXXXXXX",
                             &fname,
                             NULL);

    g_assert(fd >= 0);
    g_assert_nonnull(fname);

    sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "file", fname,
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);

    g_assert_cmpstr(pw, ==, "");

    object_unparent(sec);
    g_free(pw);
    close(fd);
    unlink(fname);
    g_free(fname);
}

#if defined(CONFIG_KEYUTILS) && defined(CONFIG_SECRET_KEYRING)

#define DESCRIPTION "qemu_test_secret"
#define PAYLOAD "Test Payload"


static void test_secret_keyring_good(void)
{
    char key_str[16];
    Object *sec;
    int32_t key = add_key("user", DESCRIPTION, PAYLOAD,
                          strlen(PAYLOAD), KEY_SPEC_PROCESS_KEYRING);

    g_assert(key >= 0);

    snprintf(key_str, sizeof(key_str), "0x%08x", key);
    sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET_KEYRING,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "serial", key_str,
        NULL);

    assert(0 <= keyctl_unlink(key, KEY_SPEC_PROCESS_KEYRING));
    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);
    g_assert_cmpstr(pw, ==, PAYLOAD);

    object_unparent(sec);
    g_free(pw);
}


static void test_secret_keyring_revoked_key(void)
{
    char key_str[16];
    Object *sec;
    int32_t key = add_key("user", DESCRIPTION, PAYLOAD,
                          strlen(PAYLOAD), KEY_SPEC_PROCESS_KEYRING);
    g_assert(key >= 0);
    g_assert_false(keyctl_revoke(key));

    snprintf(key_str, sizeof(key_str), "0x%08x", key);
    sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET_KEYRING,
        object_get_objects_root(),
        "sec0",
        NULL,
        "serial", key_str,
        NULL);

    g_assert(errno == EKEYREVOKED);
    g_assert(sec == NULL);

    keyctl_unlink(key, KEY_SPEC_PROCESS_KEYRING);
}


static void test_secret_keyring_expired_key(void)
{
    char key_str[16];
    Object *sec;
    int32_t key = add_key("user", DESCRIPTION, PAYLOAD,
                          strlen(PAYLOAD), KEY_SPEC_PROCESS_KEYRING);
    g_assert(key >= 0);
    g_assert_false(keyctl_set_timeout(key, 1));
    sleep(1);

    snprintf(key_str, sizeof(key_str), "0x%08x", key);
    sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET_KEYRING,
        object_get_objects_root(),
        "sec0",
        NULL,
        "serial", key_str,
        NULL);

    g_assert(errno == EKEYEXPIRED);
    g_assert(sec == NULL);

    keyctl_unlink(key, KEY_SPEC_PROCESS_KEYRING);
}


static void test_secret_keyring_bad_serial_key(void)
{
    Object *sec;

    sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET_KEYRING,
        object_get_objects_root(),
        "sec0",
        NULL,
        "serial", "1",
        NULL);

    g_assert(errno == ENOKEY);
    g_assert(sec == NULL);
}

/*
 * TODO
 * test_secret_keyring_bad_key_access_right() is not working yet.
 * We don't know yet if this due a bug in the Linux kernel or
 * whether it's normal syscall behavior.
 * We've requested information from kernel maintainers.
 * See: <https://www.spinics.net/lists/keyrings/index.html>
 * Thread: 'security/keys: remove possessor verify after key permission check'
 */

static void test_secret_keyring_bad_key_access_right(void)
{
    char key_str[16];
    Object *sec;

    g_test_skip("TODO: Need response from Linux kernel maintainers");
    return;

    int32_t key = add_key("user", DESCRIPTION, PAYLOAD,
                          strlen(PAYLOAD), KEY_SPEC_PROCESS_KEYRING);
    g_assert(key >= 0);
    g_assert_false(keyctl_setperm(key, KEY_POS_ALL & (~KEY_POS_READ)));

    snprintf(key_str, sizeof(key_str), "0x%08x", key);

    sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET_KEYRING,
        object_get_objects_root(),
        "sec0",
        NULL,
        "serial", key_str,
        NULL);

    g_assert(errno == EACCES);
    g_assert(sec == NULL);

    keyctl_unlink(key, KEY_SPEC_PROCESS_KEYRING);
}

#endif /* CONFIG_KEYUTILS && CONFIG_SECRET_KEYRING */

static void test_secret_noconv_base64_good(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "MTIzNDU2",
        "format", "base64",
        NULL);

    char *pw = qcrypto_secret_lookup_as_base64("sec0",
                                               &error_abort);

    g_assert_cmpstr(pw, ==, "MTIzNDU2");

    object_unparent(sec);
    g_free(pw);
}


static void test_secret_noconv_base64_bad(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        NULL,
        "data", "MTI$NDU2",
        "format", "base64",
        NULL);

    g_assert(sec == NULL);
}


static void test_secret_noconv_utf8(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "123456",
        "format", "raw",
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);

    g_assert_cmpstr(pw, ==, "123456");

    object_unparent(sec);
    g_free(pw);
}


static void test_secret_conv_base64_utf8valid(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "MTIzNDU2",
        "format", "base64",
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);

    g_assert_cmpstr(pw, ==, "123456");

    object_unparent(sec);
    g_free(pw);
}


static void test_secret_conv_base64_utf8invalid(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "f0VMRgIBAQAAAA==",
        "format", "base64",
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             NULL);
    g_assert(pw == NULL);

    object_unparent(sec);
}


static void test_secret_conv_utf8_base64(void)
{
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "123456",
        NULL);

    char *pw = qcrypto_secret_lookup_as_base64("sec0",
                                               &error_abort);

    g_assert_cmpstr(pw, ==, "MTIzNDU2");

    object_unparent(sec);
    g_free(pw);
}


static void test_secret_crypt_raw(void)
{
    Object *master = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "master",
        &error_abort,
        "data", "9miloPQCzGy+TL6aonfzVcptibCmCIhKzrnlfwiWivk=",
        "format", "base64",
        NULL);
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data",
        "\xCC\xBF\xF7\x09\x46\x19\x0B\x52\x2A\x3A\xB4\x6B\xCD\x7A\xB0\xB0",
        "format", "raw",
        "keyid", "master",
        "iv", "0I7Gw/TKuA+Old2W2apQ3g==",
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);

    g_assert_cmpstr(pw, ==, "123456");

    object_unparent(sec);
    object_unparent(master);
    g_free(pw);
}


static void test_secret_crypt_base64(void)
{
    Object *master = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "master",
        &error_abort,
        "data", "9miloPQCzGy+TL6aonfzVcptibCmCIhKzrnlfwiWivk=",
        "format", "base64",
        NULL);
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        &error_abort,
        "data", "zL/3CUYZC1IqOrRrzXqwsA==",
        "format", "base64",
        "keyid", "master",
        "iv", "0I7Gw/TKuA+Old2W2apQ3g==",
        NULL);

    char *pw = qcrypto_secret_lookup_as_utf8("sec0",
                                             &error_abort);

    g_assert_cmpstr(pw, ==, "123456");

    object_unparent(sec);
    object_unparent(master);
    g_free(pw);
}


static void test_secret_crypt_short_key(void)
{
    Object *master = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "master",
        &error_abort,
        "data", "9miloPQCzGy+TL6aonfzVc",
        "format", "base64",
        NULL);
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        NULL,
        "data", "zL/3CUYZC1IqOrRrzXqwsA==",
        "format", "raw",
        "keyid", "master",
        "iv", "0I7Gw/TKuA+Old2W2apQ3g==",
        NULL);

    g_assert(sec == NULL);
    object_unparent(master);
}


static void test_secret_crypt_short_iv(void)
{
    Object *master = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "master",
        &error_abort,
        "data", "9miloPQCzGy+TL6aonfzVcptibCmCIhKzrnlfwiWivk=",
        "format", "base64",
        NULL);
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        NULL,
        "data", "zL/3CUYZC1IqOrRrzXqwsA==",
        "format", "raw",
        "keyid", "master",
        "iv", "0I7Gw/TKuA+Old2W2a",
        NULL);

    g_assert(sec == NULL);
    object_unparent(master);
}


static void test_secret_crypt_missing_iv(void)
{
    Object *master = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "master",
        &error_abort,
        "data", "9miloPQCzGy+TL6aonfzVcptibCmCIhKzrnlfwiWivk=",
        "format", "base64",
        NULL);
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        NULL,
        "data", "zL/3CUYZC1IqOrRrzXqwsA==",
        "format", "raw",
        "keyid", "master",
        NULL);

    g_assert(sec == NULL);
    object_unparent(master);
}


static void test_secret_crypt_bad_iv(void)
{
    Object *master = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "master",
        &error_abort,
        "data", "9miloPQCzGy+TL6aonfzVcptibCmCIhKzrnlfwiWivk=",
        "format", "base64",
        NULL);
    Object *sec = object_new_with_props(
        TYPE_QCRYPTO_SECRET,
        object_get_objects_root(),
        "sec0",
        NULL,
        "data", "zL/3CUYZC1IqOrRrzXqwsA==",
        "format", "raw",
        "keyid", "master",
        "iv", "0I7Gw/TK$$uA+Old2W2a",
        NULL);

    g_assert(sec == NULL);
    object_unparent(master);
}


int main(int argc, char **argv)
{
    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    g_assert(qcrypto_init(NULL) == 0);

    g_test_add_func("/crypto/secret/direct",
                    test_secret_direct);
    g_test_add_func("/crypto/secret/indirect/good",
                    test_secret_indirect_good);
    g_test_add_func("/crypto/secret/indirect/badfile",
                    test_secret_indirect_badfile);
    g_test_add_func("/crypto/secret/indirect/emptyfile",
                    test_secret_indirect_emptyfile);

#if defined(CONFIG_KEYUTILS) && defined(CONFIG_SECRET_KEYRING)
    g_test_add_func("/crypto/secret/keyring/good",
                    test_secret_keyring_good);
    g_test_add_func("/crypto/secret/keyring/revoked_key",
                    test_secret_keyring_revoked_key);
    g_test_add_func("/crypto/secret/keyring/expired_key",
                    test_secret_keyring_expired_key);
    g_test_add_func("/crypto/secret/keyring/bad_serial_key",
                    test_secret_keyring_bad_serial_key);
    g_test_add_func("/crypto/secret/keyring/bad_key_access_right",
                    test_secret_keyring_bad_key_access_right);
#endif /* CONFIG_KEYUTILS && CONFIG_SECRET_KEYRING */

    g_test_add_func("/crypto/secret/noconv/base64/good",
                    test_secret_noconv_base64_good);
    g_test_add_func("/crypto/secret/noconv/base64/bad",
                    test_secret_noconv_base64_bad);
    g_test_add_func("/crypto/secret/noconv/utf8",
                    test_secret_noconv_utf8);
    g_test_add_func("/crypto/secret/conv/base64/utf8valid",
                    test_secret_conv_base64_utf8valid);
    g_test_add_func("/crypto/secret/conv/base64/utf8invalid",
                    test_secret_conv_base64_utf8invalid);
    g_test_add_func("/crypto/secret/conv/utf8/base64",
                    test_secret_conv_utf8_base64);

    g_test_add_func("/crypto/secret/crypt/raw",
                    test_secret_crypt_raw);
    g_test_add_func("/crypto/secret/crypt/base64",
                    test_secret_crypt_base64);
    g_test_add_func("/crypto/secret/crypt/shortkey",
                    test_secret_crypt_short_key);
    g_test_add_func("/crypto/secret/crypt/shortiv",
                    test_secret_crypt_short_iv);
    g_test_add_func("/crypto/secret/crypt/missingiv",
                    test_secret_crypt_missing_iv);
    g_test_add_func("/crypto/secret/crypt/badiv",
                    test_secret_crypt_bad_iv);

    return g_test_run();
}
