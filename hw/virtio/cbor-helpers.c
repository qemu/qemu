/*
 * QEMU CBOR helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "hw/virtio/cbor-helpers.h"

bool qemu_cbor_map_add(cbor_item_t *map, cbor_item_t *key, cbor_item_t *value)
{
    bool success = false;
    struct cbor_pair pair = (struct cbor_pair) {
        .key = cbor_move(key),
        .value = cbor_move(value)
    };

    success = cbor_map_add(map, pair);
    if (!success) {
        cbor_incref(pair.key);
        cbor_incref(pair.value);
    }

    return success;
}

bool qemu_cbor_array_push(cbor_item_t *array, cbor_item_t *value)
{
    bool success = false;

    success = cbor_array_push(array, cbor_move(value));
    if (!success) {
        cbor_incref(value);
    }

    return success;
}

bool qemu_cbor_add_bool_to_map(cbor_item_t *map, const char *key, bool value)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_build_bool(value);
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_uint8_to_map(cbor_item_t *map, const char *key,
                                uint8_t value)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_build_uint8(value);
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_map_to_map(cbor_item_t *map, const char *key,
                              size_t nested_map_size,
                              cbor_item_t **nested_map)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_new_definite_map(nested_map_size);
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }
    *nested_map = value_cbor;

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_bytestring_to_map(cbor_item_t *map, const char *key,
                                     uint8_t *arr, size_t len)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_build_bytestring(arr, len);
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_null_to_map(cbor_item_t *map, const char *key)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_new_null();
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_string_to_map(cbor_item_t *map, const char *key,
                                 const char *value)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_build_string(value);
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_uint8_array_to_map(cbor_item_t *map, const char *key,
                                      uint8_t *arr, size_t len)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_new_definite_array(len);
    if (!value_cbor) {
        goto cleanup;
    }

    for (int i = 0; i < len; ++i) {
        cbor_item_t *tmp = cbor_build_uint8(arr[i]);
        if (!tmp) {
            goto cleanup;
        }
        if (!qemu_cbor_array_push(value_cbor, tmp)) {
            cbor_decref(&tmp);
            goto cleanup;
        }
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_uint8_key_bytestring_to_map(cbor_item_t *map, uint8_t key,
                                               uint8_t *buf, size_t len)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_uint8(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_build_bytestring(buf, len);
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}

bool qemu_cbor_add_uint64_to_map(cbor_item_t *map, const char *key,
                                 uint64_t value)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;

    key_cbor = cbor_build_string(key);
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_build_uint64(value);
    if (!value_cbor) {
        goto cleanup;
    }
    if (!qemu_cbor_map_add(map, key_cbor, value_cbor)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (key_cbor) {
        cbor_decref(&key_cbor);
    }
    if (value_cbor) {
        cbor_decref(&value_cbor);
    }
    return false;
}
