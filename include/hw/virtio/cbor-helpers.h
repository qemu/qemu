/*
 * QEMU CBOR helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VIRTIO_CBOR_HELPERS_H
#define QEMU_VIRTIO_CBOR_HELPERS_H

#include <cbor.h>

bool qemu_cbor_map_add(cbor_item_t *map, cbor_item_t *key, cbor_item_t *value);

bool qemu_cbor_array_push(cbor_item_t *array, cbor_item_t *value);

bool qemu_cbor_add_bool_to_map(cbor_item_t *map, const char *key, bool value);

bool qemu_cbor_add_uint8_to_map(cbor_item_t *map, const char *key,
                                uint8_t value);

bool qemu_cbor_add_map_to_map(cbor_item_t *map, const char *key,
                              size_t nested_map_size,
                              cbor_item_t **nested_map);

bool qemu_cbor_add_bytestring_to_map(cbor_item_t *map, const char *key,
                                     uint8_t *arr, size_t len);

bool qemu_cbor_add_null_to_map(cbor_item_t *map, const char *key);

bool qemu_cbor_add_string_to_map(cbor_item_t *map, const char *key,
                                 const char *value);

bool qemu_cbor_add_uint8_array_to_map(cbor_item_t *map, const char *key,
                                      uint8_t *arr, size_t len);

bool qemu_cbor_add_uint8_key_bytestring_to_map(cbor_item_t *map, uint8_t key,
                                               uint8_t *buf, size_t len);

bool qemu_cbor_add_uint64_to_map(cbor_item_t *map, const char *key,
                                 uint64_t value);
#endif
