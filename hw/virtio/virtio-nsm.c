/*
 * AWS Nitro Secure Module (NSM) device
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"

#include "crypto/hash.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-nsm.h"
#include "hw/virtio/cbor-helpers.h"
#include "standard-headers/linux/virtio_ids.h"

#define NSM_REQUEST_MAX_SIZE      0x1000
#define NSM_RESPONSE_BUF_SIZE     0x3000
#define NSM_RND_BUF_SIZE          256

enum NSMResponseTypes {
    NSM_SUCCESS = 0,
    NSM_INVALID_ARGUMENT = 1,
    NSM_INVALID_INDEX = 2,
    NSM_READONLY_INDEX = 3,
    NSM_INVALID_OPERATION = 4,
    NSM_BUFFER_TOO_SMALL = 5,
    NSM_INPUT_TOO_LARGE = 6,
    NSM_INTERNAL_ERROR = 7,
};

static const char *error_string(enum NSMResponseTypes type)
{
    const char *str;
    switch (type) {
    case NSM_INVALID_ARGUMENT:
        str = "InvalidArgument";
        break;
    case NSM_INVALID_INDEX:
        str = "InvalidIndex";
        break;
    case NSM_READONLY_INDEX:
        str = "ReadOnlyIndex";
        break;
    case NSM_INVALID_OPERATION:
        str = "InvalidOperation";
        break;
    case NSM_BUFFER_TOO_SMALL:
        str = "BufferTooSmall";
        break;
    case NSM_INPUT_TOO_LARGE:
        str = "InputTooLarge";
        break;
    default:
        str = "InternalError";
        break;
    }

    return str;
}

/*
 * Error response structure:
 *
 * {
 *   Map(1) {
 *     key = String("Error"),
 *     value = String(error_name)
 *   }
 * }
 *
 * where error_name can be one of the following:
 *   InvalidArgument
 *   InvalidIndex
 *   InvalidResponse
 *   ReadOnlyIndex
 *   InvalidOperation
 *   BufferTooSmall
 *   InputTooLarge
 *   InternalError
 */

static bool error_response(struct iovec *response, enum NSMResponseTypes error,
                           Error **errp)
{
    cbor_item_t *root;
    size_t len;
    bool r = false;

    root = cbor_new_definite_map(1);
    if (!root) {
        goto err;
    }

    if (!qemu_cbor_add_string_to_map(root, "Error", error_string(error))) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        error_setg(errp, "Response buffer is small for %s response",
                   error_string(error));
        goto out;
    }
    response->iov_len = len;
    r = true;

 out:
    if (root) {
        cbor_decref(&root);
    }
    return r;

 err:
    error_setg(errp, "Failed to initialize %s response", error_string(error));
    goto out;
}

/*
 * GetRandom response structure:
 *
 * {
 *   Map(1) {
 *     key = String("GetRandom"),
 *     value = Map(1) {
 *       key = String("random"),
 *       value = Byte_String()
 *     }
 *   }
 * }
 */
static bool handle_get_random(VirtIONSM *vnsm, struct iovec *request,
                              struct iovec *response, Error **errp)
{
    cbor_item_t *root, *nested_map;
    size_t len;
    uint8_t rnd[NSM_RND_BUF_SIZE];
    bool r = false;

    root = cbor_new_definite_map(1);
    if (!root) {
        goto err;
    }

    if (!qemu_cbor_add_map_to_map(root, "GetRandom", 1, &nested_map)) {
        goto err;
    }

    qemu_guest_getrandom_nofail(rnd, NSM_RND_BUF_SIZE);

    if (!qemu_cbor_add_bytestring_to_map(nested_map, "random", rnd,
                                         NSM_RND_BUF_SIZE)) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        if (error_response(response, NSM_INPUT_TOO_LARGE, errp)) {
            r = true;
        }
        goto out;
    }

    response->iov_len = len;
    r = true;

 out:
    if (root) {
        cbor_decref(&root);
    }
    return r;

 err:
    error_setg(errp, "Failed to initialize GetRandom response");
    goto out;
}

/*
 * DescribeNSM response structure:
 *
 * {
 *   Map(1) {
 *     key = String("DescribeNSM"),
 *     value = Map(7) {
 *       key = String("digest"),
 *       value = String("SHA384"),
 *       key = String("max_pcrs"),
 *       value = Uint8(32),
 *       key = String("module_id"),
 *       value = String("i-1234-enc5678"),
 *       key = String("locked_pcrs"),
 *       value = Array<Uint8>(),
 *       key = String("version_major"),
 *       value = Uint8(1),
 *       key = String("version_minor"),
 *       value = Uint8(0),
 *       key = String("version_patch"),
 *       value = Uint8(0)
 *     }
 *   }
 * }
 */
static bool handle_describe_nsm(VirtIONSM *vnsm, struct iovec *request,
                                struct iovec *response, Error **errp)
{
    cbor_item_t *root, *nested_map;
    uint16_t locked_pcrs_cnt = 0;
    uint8_t locked_pcrs_ind[NSM_MAX_PCRS];
    size_t len;
    bool r = false;

    root = cbor_new_definite_map(1);
    if (!root) {
        goto err;
    }

    if (!qemu_cbor_add_map_to_map(root, "DescribeNSM", 7, &nested_map)) {
        goto err;
    }

    if (!qemu_cbor_add_string_to_map(nested_map, "digest", vnsm->digest)) {
        goto err;
    }

    if (!qemu_cbor_add_uint8_to_map(nested_map, "max_pcrs", vnsm->max_pcrs)) {
        goto err;
    }

    if (!qemu_cbor_add_string_to_map(nested_map, "module_id",
                                     vnsm->module_id)) {
        goto err;
    }

    for (uint8_t i = 0; i < NSM_MAX_PCRS; ++i) {
        if (vnsm->pcrs[i].locked) {
            locked_pcrs_ind[locked_pcrs_cnt++] = i;
        }
    }
    if (!qemu_cbor_add_uint8_array_to_map(nested_map, "locked_pcrs",
                                          locked_pcrs_ind, locked_pcrs_cnt)) {
        goto err;
    }

    if (!qemu_cbor_add_uint8_to_map(nested_map, "version_major",
                                    vnsm->version_major)) {
        goto err;
    }

    if (!qemu_cbor_add_uint8_to_map(nested_map, "version_minor",
                                    vnsm->version_minor)) {
        goto err;
    }

    if (!qemu_cbor_add_uint8_to_map(nested_map, "version_patch",
                                    vnsm->version_patch)) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        if (error_response(response, NSM_INPUT_TOO_LARGE, errp)) {
            r = true;
        }
        goto out;
    }

    response->iov_len = len;
    r = true;

 out:
    if (root) {
        cbor_decref(&root);
    }
    return r;

 err:
    error_setg(errp, "Failed to initialize DescribeNSM response");
    goto out;
}

/*
 * DescribePCR request structure:
 *
 * {
 *   Map(1) {
 *     key = String("DescribePCR"),
 *     value = Map(1) {
 *       key = String("index"),
 *       value = Uint8(pcr)
 *     }
 *   }
 * }
 */
typedef struct NSMDescribePCRReq {
    uint8_t index;
} NSMDescribePCRReq;

static enum NSMResponseTypes get_nsm_describe_pcr_req(
    uint8_t *req, size_t len,
    NSMDescribePCRReq *nsm_req)
{
    size_t size;
    uint8_t *str;
    struct cbor_pair *pair;
    cbor_item_t *item = NULL;
    struct cbor_load_result result;
    enum NSMResponseTypes r = NSM_INVALID_OPERATION;

    item = cbor_load(req, len, &result);
    if (!item || result.error.code != CBOR_ERR_NONE) {
        goto cleanup;
    }

    pair = cbor_map_handle(item);
    if (!cbor_isa_map(pair->value)) {
        goto cleanup;
    }
    size = cbor_map_size(pair->value);
    if (size < 1) {
        goto cleanup;
    }

    pair = cbor_map_handle(pair->value);
    for (int i = 0; i < size; ++i) {
        if (!cbor_isa_string(pair[i].key)) {
            continue;
        }

        str = cbor_string_handle(pair[i].key);
        if (str && cbor_string_length(pair[i].key) == 5 &&
            memcmp(str, "index", 5) == 0) {
            if (!cbor_isa_uint(pair[i].value) ||
                cbor_int_get_width(pair[i].value) != CBOR_INT_8) {
                break;
            }

            nsm_req->index = cbor_get_uint8(pair[i].value);
            r = NSM_SUCCESS;
            break;
        }
    }

 cleanup:
    if (item) {
        cbor_decref(&item);
    }
    return r;
}

/*
 * DescribePCR response structure:
 *
 * {
 *   Map(1) {
 *     key = String("DescribePCR"),
 *     value = Map(2) {
 *       key = String("data"),
 *       value = Byte_String(),
 *       key = String("lock"),
 *       value = Bool()
 *     }
 *   }
 * }
 */
static bool handle_describe_pcr(VirtIONSM *vnsm, struct iovec *request,
                                struct iovec *response, Error **errp)
{
    cbor_item_t *root = NULL;
    cbor_item_t *nested_map;
    size_t len;
    NSMDescribePCRReq nsm_req;
    enum NSMResponseTypes type;
    struct PCRInfo *pcr;
    bool r = false;

    type = get_nsm_describe_pcr_req(request->iov_base, request->iov_len,
                                    &nsm_req);
    if (type != NSM_SUCCESS) {
        if (error_response(response, type, errp)) {
            r = true;
        }
        goto out;
    }
    if (nsm_req.index >= vnsm->max_pcrs) {
        if (error_response(response, NSM_INVALID_INDEX, errp)) {
            r = true;
        }
        goto out;
    }
    pcr = &(vnsm->pcrs[nsm_req.index]);

    root = cbor_new_definite_map(1);
    if (!root) {
        goto err;
    }

    if (!qemu_cbor_add_map_to_map(root, "DescribePCR", 2, &nested_map)) {
        goto err;
    }

    if (!qemu_cbor_add_bytestring_to_map(nested_map, "data", pcr->data,
                                         QCRYPTO_HASH_DIGEST_LEN_SHA384)) {
        goto err;
    }

    if (!qemu_cbor_add_bool_to_map(nested_map, "lock", pcr->locked)) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        if (error_response(response, NSM_INPUT_TOO_LARGE, errp)) {
            r = true;
        }
        goto out;
    }

    response->iov_len = len;
    r = true;

 out:
    if (root) {
        cbor_decref(&root);
    }
    return r;

 err:
    error_setg(errp, "Failed to initialize DescribePCR response");
    goto out;
}

/*
 * ExtendPCR request structure:
 *
 * {
 *   Map(1) {
 *     key = String("ExtendPCR"),
 *     value = Map(2) {
 *       key = String("index"),
 *       value = Uint8(pcr),
 *       key = String("data"),
 *       value = Byte_String(data) || String(data),
 *     }
 *   }
 * }
 */
typedef struct NSMExtendPCRReq {
    uint8_t index;
    uint16_t data_len;
    uint8_t data[NSM_REQUEST_MAX_SIZE];
} NSMExtendPCRReq;

static enum NSMResponseTypes get_nsm_extend_pcr_req(uint8_t *req, size_t len,
                                                    NSMExtendPCRReq *nsm_req)
{
    cbor_item_t *item = NULL;
    size_t size ;
    uint8_t *str;
    bool index_found = false;
    bool data_found = false;
    struct cbor_pair *pair;
    struct cbor_load_result result;
    enum NSMResponseTypes r = NSM_INVALID_OPERATION;

    item = cbor_load(req, len, &result);
    if (!item || result.error.code != CBOR_ERR_NONE) {
        goto cleanup;
    }

    pair = cbor_map_handle(item);
    if (!cbor_isa_map(pair->value)) {
        goto cleanup;
    }
    size = cbor_map_size(pair->value);
    if (size < 2) {
        goto cleanup;
    }

    pair = cbor_map_handle(pair->value);
    for (int i = 0; i < size; ++i) {
        if (!cbor_isa_string(pair[i].key)) {
            continue;
        }
        str = cbor_string_handle(pair[i].key);
        if (!str) {
            continue;
        }

        if (cbor_string_length(pair[i].key) == 5 &&
            memcmp(str, "index", 5) == 0) {
            if (!cbor_isa_uint(pair[i].value) ||
                cbor_int_get_width(pair[i].value) != CBOR_INT_8) {
                goto cleanup;
            }
            nsm_req->index = cbor_get_uint8(pair[i].value);
            index_found = true;
            continue;
        }

        if (cbor_string_length(pair[i].key) == 4 &&
            memcmp(str, "data", 4) == 0) {
            if (cbor_isa_bytestring(pair[i].value)) {
                str = cbor_bytestring_handle(pair[i].value);
                if (!str) {
                    goto cleanup;
                }
                nsm_req->data_len = cbor_bytestring_length(pair[i].value);
            } else if (cbor_isa_string(pair[i].value)) {
                str = cbor_string_handle(pair[i].value);
                if (!str) {
                    goto cleanup;
                }
                nsm_req->data_len = cbor_string_length(pair[i].value);
            } else {
                goto cleanup;
            }
            /*
             * nsm_req->data_len will be smaller than NSM_REQUEST_MAX_SIZE as
             * we already check for the max request size before processing
             * any request. So it's safe to copy.
             */
            memcpy(nsm_req->data, str, nsm_req->data_len);
            data_found = true;
            continue;
        }
    }

    if (index_found && data_found) {
        r = NSM_SUCCESS;
    }

 cleanup:
    if (item) {
        cbor_decref(&item);
    }
    return r;
}

/*
 * ExtendPCR response structure:
 *
 * {
 *   Map(1) {
 *     key = String("ExtendPCR"),
 *     value = Map(1) {
 *       key = String("data"),
 *       value = Byte_String()
 *     }
 *   }
 * }
 */
static bool handle_extend_pcr(VirtIONSM *vnsm, struct iovec *request,
                              struct iovec *response, Error **errp)
{
    cbor_item_t *root = NULL;
    cbor_item_t *nested_map;
    size_t len;
    struct PCRInfo *pcr;
    enum NSMResponseTypes type;
    bool r = false;
    g_autofree NSMExtendPCRReq *nsm_req = g_malloc(sizeof(NSMExtendPCRReq));

    type = get_nsm_extend_pcr_req(request->iov_base, request->iov_len,
                                  nsm_req);
    if (type != NSM_SUCCESS) {
        if (error_response(response, type, errp)) {
            r = true;
        }
        goto out;
    }
    if (nsm_req->index >= vnsm->max_pcrs) {
        if (error_response(response, NSM_INVALID_INDEX, errp)) {
            r = true;
        }
        goto out;
    }

    pcr = &(vnsm->pcrs[nsm_req->index]);

    if (pcr->locked) {
        if (error_response(response, NSM_READONLY_INDEX, errp)) {
            r = true;
        }
        goto out;
    }

    if (!vnsm->extend_pcr(vnsm, nsm_req->index, nsm_req->data,
                          nsm_req->data_len)) {
        if (error_response(response, NSM_INTERNAL_ERROR, errp)) {
            r = true;
        }
        goto out;
    }

    root = cbor_new_definite_map(1);
    if (!root) {
        goto err;
    }

    if (!qemu_cbor_add_map_to_map(root, "ExtendPCR", 1, &nested_map)) {
        goto err;
    }

    if (!qemu_cbor_add_bytestring_to_map(nested_map, "data", pcr->data,
                                         QCRYPTO_HASH_DIGEST_LEN_SHA384)) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        if (error_response(response, NSM_BUFFER_TOO_SMALL, errp)) {
            r = true;
        }
        goto out;
    }

    response->iov_len = len;
    r = true;

 out:
    if (root) {
        cbor_decref(&root);
    }
    return r;

 err:
    error_setg(errp, "Failed to initialize DescribePCR response");
    goto out;
}

/*
 * LockPCR request structure:
 *
 * {
 *   Map(1) {
 *     key = String("LockPCR"),
 *     value = Map(1) {
 *       key = String("index"),
 *       value = Uint8(pcr)
 *     }
 *   }
 * }
 */
typedef struct NSMLockPCRReq {
    uint8_t index;
} NSMLockPCRReq;

static enum NSMResponseTypes get_nsm_lock_pcr_req(uint8_t *req, size_t len,
                                                  NSMLockPCRReq *nsm_req)
{
    cbor_item_t *item = NULL;
    size_t size;
    uint8_t *str;
    struct cbor_pair *pair;
    struct cbor_load_result result;
    enum NSMResponseTypes r = NSM_INVALID_OPERATION;

    item = cbor_load(req, len, &result);
    if (!item || result.error.code != CBOR_ERR_NONE) {
        goto cleanup;
    }

    pair = cbor_map_handle(item);
    if (!cbor_isa_map(pair->value)) {
        goto cleanup;
    }
    size = cbor_map_size(pair->value);
    if (size < 1) {
        goto cleanup;
    }

    pair = cbor_map_handle(pair->value);
    for (int i = 0; i < size; ++i) {
        if (!cbor_isa_string(pair[i].key)) {
            continue;
        }
        str = cbor_string_handle(pair[i].key);
        if (str && cbor_string_length(pair[i].key) == 5 &&
            memcmp(str, "index", 5) == 0) {
            if (!cbor_isa_uint(pair[i].value) ||
                cbor_int_get_width(pair[i].value) != CBOR_INT_8) {
                break;
            }

            nsm_req->index = cbor_get_uint8(pair[i].value);
            r = NSM_SUCCESS;
            break;
        }
    }

 cleanup:
    if (item) {
        cbor_decref(&item);
    }
    return r;
}

/*
 * LockPCR success response structure:
 * {
 *   String("LockPCR")
 * }
 */
static bool handle_lock_pcr(VirtIONSM *vnsm, struct iovec *request,
                            struct iovec *response, Error **errp)
{
    cbor_item_t *root = NULL;
    size_t len;
    NSMLockPCRReq nsm_req;
    enum NSMResponseTypes type;
    struct PCRInfo *pcr;
    bool r = false;

    type = get_nsm_lock_pcr_req(request->iov_base, request->iov_len, &nsm_req);
    if (type != NSM_SUCCESS) {
        if (error_response(response, type, errp)) {
            r = true;
        }
        goto cleanup;
    }
    if (nsm_req.index >= vnsm->max_pcrs) {
        if (error_response(response, NSM_INVALID_INDEX, errp)) {
            r = true;
        }
        goto cleanup;
    }

    pcr = &(vnsm->pcrs[nsm_req.index]);

    if (pcr->locked) {
        if (error_response(response, NSM_READONLY_INDEX, errp)) {
            r = true;
        }
        goto cleanup;
    }

    pcr->locked = true;

    root = cbor_build_string("LockPCR");
    if (!root) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        if (error_response(response, NSM_BUFFER_TOO_SMALL, errp)) {
            r = true;
        }
        goto cleanup;
    }

    response->iov_len = len;
    r = true;
    goto cleanup;

 err:
    error_setg(errp, "Failed to initialize LockPCR response");

 cleanup:
    if (root) {
        cbor_decref(&root);
    }
    return r;
}

/*
 * LockPCRs request structure:
 *
 * {
 *   Map(1) {
 *     key = String("LockPCRs"),
 *     value = Map(1) {
 *       key = String("range"),
 *       value = Uint8(pcr)
 *     }
 *   }
 * }
 */
typedef struct NSMLockPCRsReq {
    uint16_t range;
} NSMLockPCRsReq;

static enum NSMResponseTypes get_nsm_lock_pcrs_req(uint8_t *req, size_t len,
                                                   NSMLockPCRsReq *nsm_req)
{
    cbor_item_t *item = NULL;
    size_t size;
    uint8_t *str;
    struct cbor_pair *pair;
    struct cbor_load_result result;
    enum NSMResponseTypes r = NSM_INVALID_OPERATION;

    item = cbor_load(req, len, &result);
    if (!item || result.error.code != CBOR_ERR_NONE) {
        goto cleanup;
    }

    pair = cbor_map_handle(item);
    if (!cbor_isa_map(pair->value)) {
        goto cleanup;
    }
    size = cbor_map_size(pair->value);
    if (size < 1) {
        goto cleanup;
    }

    pair = cbor_map_handle(pair->value);
    for (int i = 0; i < size; ++i) {
        if (!cbor_isa_string(pair[i].key)) {
            continue;
        }
        str = cbor_string_handle(pair[i].key);
        if (str && cbor_string_length(pair[i].key) == 5 &&
            memcmp(str, "range", 5) == 0) {
            if (!cbor_isa_uint(pair[i].value) ||
                cbor_int_get_width(pair[i].value) != CBOR_INT_8) {
                break;
            }

            nsm_req->range = cbor_get_uint8(pair[i].value);
            r = NSM_SUCCESS;
            break;
        }
    }

 cleanup:
    if (item) {
        cbor_decref(&item);
    }
    return r;
}

/*
 * LockPCRs success response structure:
 * {
 *   String("LockPCRs")
 * }
 */
static bool handle_lock_pcrs(VirtIONSM *vnsm, struct iovec *request,
                             struct iovec *response, Error **errp)
{
    cbor_item_t *root = NULL;
    size_t len;
    NSMLockPCRsReq nsm_req;
    enum NSMResponseTypes type;
    bool r = false;

    type = get_nsm_lock_pcrs_req(request->iov_base, request->iov_len, &nsm_req);
    if (type != NSM_SUCCESS) {
        if (error_response(response, type, errp)) {
            r = true;
        }
        goto cleanup;
    }
    if (nsm_req.range > vnsm->max_pcrs) {
        if (error_response(response, NSM_INVALID_INDEX, errp)) {
            r = true;
        }
        goto cleanup;
    }

    for (int i = 0; i < nsm_req.range; ++i) {
        vnsm->pcrs[i].locked = true;
    }

    root = cbor_build_string("LockPCRs");
    if (!root) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        if (error_response(response, NSM_BUFFER_TOO_SMALL, errp)) {
            r = true;
        }
        goto cleanup;
    }

    response->iov_len = len;
    r = true;
    goto cleanup;

 err:
    error_setg(errp, "Failed to initialize response");

 cleanup:
    if (root) {
        cbor_decref(&root);
    }
    return r;
}

/*
 * Attestation request structure:
 *
 *   Map(1) {
 *     key = String("Attestation"),
 *     value = Map(3) {
 *       key = String("user_data"),
 *       value = Byte_String() || null, // Optional
 *       key = String("nonce"),
 *       value = Byte_String() || null, // Optional
 *       key = String("public_key"),
 *       value = Byte_String() || null, // Optional
 *     }
 *   }
 * }
 */

struct AttestationProperty {
    bool is_null; /* True if property is not present in map or is null */
    uint16_t len;
    uint8_t buf[NSM_REQUEST_MAX_SIZE];
};

typedef struct NSMAttestationReq {
    struct AttestationProperty public_key;
    struct AttestationProperty user_data;
    struct AttestationProperty nonce;
} NSMAttestationReq;

static bool fill_attestation_property(struct AttestationProperty *prop,
                                      cbor_item_t *value)
{
    uint8_t *str;
    bool ret = false;

    if (cbor_is_null(value)) {
        prop->is_null = true;
        ret = true;
        goto out;
    } else if (cbor_isa_bytestring(value)) {
        str = cbor_bytestring_handle(value);
        if (!str) {
            goto out;
        }
        prop->len = cbor_bytestring_length(value);
    } else if (cbor_isa_string(value)) {
        str = cbor_string_handle(value);
        if (!str) {
            goto out;
        }
        prop->len = cbor_string_length(value);
    } else {
        goto out;
    }

    /*
     * prop->len will be smaller than NSM_REQUEST_MAX_SIZE as we
     * already check for the max request size before processing
     * any request. So it's safe to copy.
     */
    memcpy(prop->buf, str, prop->len);
    prop->is_null = false;
    ret = true;

 out:
    return ret;
}

static enum NSMResponseTypes get_nsm_attestation_req(uint8_t *req, size_t len,
                                                     NSMAttestationReq *nsm_req)
{
    cbor_item_t *item = NULL;
    size_t size;
    uint8_t *str;
    struct cbor_pair *pair;
    struct cbor_load_result result;
    enum NSMResponseTypes r = NSM_INVALID_OPERATION;

    nsm_req->public_key.is_null = true;
    nsm_req->user_data.is_null = true;
    nsm_req->nonce.is_null = true;

    item = cbor_load(req, len, &result);
    if (!item || result.error.code != CBOR_ERR_NONE) {
        goto cleanup;
    }

    pair = cbor_map_handle(item);
    if (!cbor_isa_map(pair->value)) {
        goto cleanup;
    }
    size = cbor_map_size(pair->value);
    if (size == 0) {
        r = NSM_SUCCESS;
        goto cleanup;
    }

    pair = cbor_map_handle(pair->value);
    for (int i = 0; i < size; ++i) {
        if (!cbor_isa_string(pair[i].key)) {
            continue;
        }

        str = cbor_string_handle(pair[i].key);
        if (!str) {
            continue;
        }

        if (cbor_string_length(pair[i].key) == 10 &&
            memcmp(str, "public_key", 10) == 0) {
            if (!fill_attestation_property(&(nsm_req->public_key),
                                           pair[i].value)) {
                goto cleanup;
            }
            continue;
        }

        if (cbor_string_length(pair[i].key) == 9 &&
            memcmp(str, "user_data", 9) == 0) {
            if (!fill_attestation_property(&(nsm_req->user_data),
                                           pair[i].value)) {
                goto cleanup;
            }
            continue;
        }

        if (cbor_string_length(pair[i].key) == 5 &&
            memcmp(str, "nonce", 5) == 0) {
            if (!fill_attestation_property(&(nsm_req->nonce), pair[i].value)) {
                goto cleanup;
            }
            continue;
        }
    }

    r = NSM_SUCCESS;

 cleanup:
    if (item) {
        cbor_decref(&item);
    }
    return r;
}

static bool add_protected_header_to_cose(cbor_item_t *cose)
{
    cbor_item_t *map = NULL;
    cbor_item_t *key = NULL;
    cbor_item_t *value = NULL;
    cbor_item_t *bs = NULL;
    size_t len;
    bool r = false;
    size_t buf_len = 4096;
    g_autofree uint8_t *buf = g_malloc(buf_len);

    map = cbor_new_definite_map(1);
    if (!map) {
        goto cleanup;
    }
    key = cbor_build_uint8(1);
    if (!key) {
        goto cleanup;
    }
    value = cbor_new_int8();
    if (!value) {
        goto cleanup;
    }
    cbor_mark_negint(value);
    /* we don't actually sign the data, so we use -1 as the 'alg' value */
    cbor_set_uint8(value, 0);

    if (!qemu_cbor_map_add(map, key, value)) {
        goto cleanup;
    }

    len = cbor_serialize(map, buf, buf_len);
    if (len == 0) {
        goto cleanup_map;
    }

    bs = cbor_build_bytestring(buf, len);
    if (!bs) {
        goto cleanup_map;
    }
    if (!qemu_cbor_array_push(cose, bs)) {
        cbor_decref(&bs);
        goto cleanup_map;
    }
    r = true;
    goto cleanup_map;

 cleanup:
    if (key) {
        cbor_decref(&key);
    }
    if (value) {
        cbor_decref(&value);
    }

 cleanup_map:
    if (map) {
        cbor_decref(&map);
    }
    return r;
}

static bool add_unprotected_header_to_cose(cbor_item_t *cose)
{
    cbor_item_t *map = cbor_new_definite_map(0);
    if (!map) {
        goto cleanup;
    }
    if (!qemu_cbor_array_push(cose, map)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (map) {
        cbor_decref(&map);
    }
    return false;
}

static bool add_ca_bundle_to_payload(cbor_item_t *map)
{
    cbor_item_t *key_cbor = NULL;
    cbor_item_t *value_cbor = NULL;
    cbor_item_t *bs = NULL;
    uint8_t zero[64] = {0};

    key_cbor = cbor_build_string("cabundle");
    if (!key_cbor) {
        goto cleanup;
    }
    value_cbor = cbor_new_definite_array(1);
    if (!value_cbor) {
        goto cleanup;
    }
    bs = cbor_build_bytestring(zero, 64);
    if (!bs) {
        goto cleanup;
    }
    if (!qemu_cbor_array_push(value_cbor, bs)) {
        cbor_decref(&bs);
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

static bool add_payload_to_cose(cbor_item_t *cose, VirtIONSM *vnsm,
                                NSMAttestationReq *req)
{
    cbor_item_t *root = NULL;
    cbor_item_t *nested_map;
    cbor_item_t *bs = NULL;
    size_t locked_cnt;
    uint8_t ind[NSM_MAX_PCRS];
    size_t payload_map_size = 9;
    size_t len;
    struct PCRInfo *pcr;
    uint8_t zero[64] = {0};
    bool r = false;
    size_t buf_len = 16384;
    g_autofree uint8_t *buf = g_malloc(buf_len);

    root = cbor_new_definite_map(payload_map_size);
    if (!root) {
        goto cleanup;
    }
    if (!qemu_cbor_add_string_to_map(root, "module_id", vnsm->module_id)) {
        goto cleanup;
    }
    if (!qemu_cbor_add_string_to_map(root, "digest", vnsm->digest)) {
        goto cleanup;
    }
    if (!qemu_cbor_add_uint64_to_map(root, "timestamp",
                                     (uint64_t) time(NULL) * 1000)) {
        goto cleanup;
    }

    locked_cnt = 0;
    for (uint8_t i = 0; i < NSM_MAX_PCRS; ++i) {
        if (vnsm->pcrs[i].locked) {
            ind[locked_cnt++] = i;
        }
    }
    if (!qemu_cbor_add_map_to_map(root, "pcrs", locked_cnt, &nested_map)) {
        goto cleanup;
    }
    for (uint8_t i = 0; i < locked_cnt; ++i) {
        pcr = &(vnsm->pcrs[ind[i]]);
        if (!qemu_cbor_add_uint8_key_bytestring_to_map(
                nested_map, ind[i],
                pcr->data,
                QCRYPTO_HASH_DIGEST_LEN_SHA384)) {
            goto cleanup;
        }
    }
    if (!qemu_cbor_add_bytestring_to_map(root, "certificate", zero, 64)) {
        goto cleanup;
    }
    if (!add_ca_bundle_to_payload(root)) {
        goto cleanup;
    }

    if (req->public_key.is_null) {
        if (!qemu_cbor_add_null_to_map(root, "public_key")) {
            goto cleanup;
        }
    } else if (!qemu_cbor_add_bytestring_to_map(root, "public_key",
                                                req->public_key.buf,
                                                req->public_key.len)) {
        goto cleanup;
    }

    if (req->user_data.is_null) {
        if (!qemu_cbor_add_null_to_map(root, "user_data")) {
            goto cleanup;
        }
    } else if (!qemu_cbor_add_bytestring_to_map(root, "user_data",
                                                req->user_data.buf,
                                                req->user_data.len)) {
            goto cleanup;
    }

    if (req->nonce.is_null) {
        if (!qemu_cbor_add_null_to_map(root, "nonce")) {
            goto cleanup;
        }
    } else if (!qemu_cbor_add_bytestring_to_map(root, "nonce",
                                                req->nonce.buf,
                                                req->nonce.len)) {
        goto cleanup;
    }

    len = cbor_serialize(root, buf, buf_len);
    if (len == 0) {
        goto cleanup;
    }

    bs = cbor_build_bytestring(buf, len);
    if (!bs) {
        goto cleanup;
    }
    if (!qemu_cbor_array_push(cose, bs)) {
        cbor_decref(&bs);
        goto cleanup;
    }

    r = true;

 cleanup:
    if (root) {
        cbor_decref(&root);
    }
    return r;
}

static bool add_signature_to_cose(cbor_item_t *cose)
{
    cbor_item_t *bs = NULL;
    uint8_t zero[64] = {0};

    /* we don't actually sign the data, so we just put 64 zero bytes */
    bs = cbor_build_bytestring(zero, 64);
    if (!bs) {
        goto cleanup;
    }

    if (!qemu_cbor_array_push(cose, bs)) {
        goto cleanup;
    }

    return true;

 cleanup:
    if (bs) {
        cbor_decref(&bs);
    }
    return false;
}

/*
 * Attestation response structure:
 *
 * {
 *   Map(1) {
 *     key = String("Attestation"),
 *     value = Map(1) {
 *       key = String("document"),
 *       value = Byte_String()
 *     }
 *   }
 * }
 *
 * The document is a serialized COSE sign1 blob of the structure:
 * {
 *   Array(4) {
 *     [0] { ByteString() }, // serialized protected header
 *     [1] { Map(0) },       // 0 length map
 *     [2] { ByteString() }, // serialized payload
 *     [3] { ByteString() }, // signature
 *   }
 * }
 *
 * where [0] protected header is a serialized CBOR blob of the structure:
 * {
 *   Map(1) {
 *     key = Uint8(1)         // alg
 *     value = NegativeInt8() // Signing algorithm
 *   }
 * }
 *
 * [2] payload is serialized CBOR blob of the structure:
 * {
 *   Map(9) {
 *     [0] { key = String("module_id"), value = String(module_id) },
 *     [1] { key = String("digest"), value = String("SHA384") },
 *     [2] {
 *           key = String("timestamp"),
 *           value = Uint64(unix epoch of  when document was created)
 *         },
 *     [3] {
 *           key = String("pcrs"),
 *           value = Map(locked_pcr_cnt) {
 *                       key = Uint8(pcr_index),
 *                       value = ByteString(pcr_data)
 *                   },
 *         },
 *     [4] {
 *           key = String("certificate"),
 *           value = ByteString(Signing certificate)
 *         },
 *     [5] { key = String("cabundle"), value = Array(N) { ByteString()... } },
 *     [6] { key = String("public_key"), value = ByteString() || null },
 *     [7] { key = String("user_data"), value = ByteString() || null},
 *     [8] { key = String("nonce"), value = ByteString() || null},
 *   }
 * }
 */
static bool handle_attestation(VirtIONSM *vnsm, struct iovec *request,
                               struct iovec *response, Error **errp)
{
    cbor_item_t *root = NULL;
    cbor_item_t *cose = NULL;
    cbor_item_t *nested_map;
    size_t len;
    enum NSMResponseTypes type;
    bool r = false;
    size_t buf_len = 16384;
    g_autofree uint8_t *buf = g_malloc(buf_len);
    g_autofree NSMAttestationReq *nsm_req = g_malloc(sizeof(NSMAttestationReq));

    nsm_req->public_key.is_null = true;
    nsm_req->user_data.is_null = true;
    nsm_req->nonce.is_null = true;

    type = get_nsm_attestation_req(request->iov_base, request->iov_len,
                                   nsm_req);
    if (type != NSM_SUCCESS) {
        if (error_response(response, type, errp)) {
            r = true;
        }
        goto out;
    }

    cose = cbor_new_definite_array(4);
    if (!cose) {
        goto err;
    }
    if (!add_protected_header_to_cose(cose)) {
        goto err;
    }
    if (!add_unprotected_header_to_cose(cose)) {
        goto err;
    }
    if (!add_payload_to_cose(cose, vnsm, nsm_req)) {
        goto err;
    }
    if (!add_signature_to_cose(cose)) {
        goto err;
    }

    len = cbor_serialize(cose, buf, buf_len);
    if (len == 0) {
        goto err;
    }

    root = cbor_new_definite_map(1);
    if (!root) {
        goto err;
    }
    if (!qemu_cbor_add_map_to_map(root, "Attestation", 1, &nested_map)) {
        goto err;
    }
    if (!qemu_cbor_add_bytestring_to_map(nested_map, "document", buf, len)) {
        goto err;
    }

    len = cbor_serialize(root, response->iov_base, response->iov_len);
    if (len == 0) {
        if (error_response(response, NSM_INPUT_TOO_LARGE, errp)) {
            r = true;
        }
        goto out;
    }

    response->iov_len = len;
    r = true;

 out:
    if (root) {
        cbor_decref(&root);
    }
    if (cose) {
        cbor_decref(&cose);
    }
    return r;

 err:
    error_setg(errp, "Failed to initialize Attestation response");
    goto out;
}

enum CBOR_ROOT_TYPE {
    CBOR_ROOT_TYPE_STRING = 0,
    CBOR_ROOT_TYPE_MAP = 1,
};

struct nsm_cmd {
    char name[16];
    /*
     * There are 2 types of request
     * 1) String(); "GetRandom", "DescribeNSM"
     * 2) Map(1) { key: String(), value: ... }
     */
    enum CBOR_ROOT_TYPE root_type;
    bool (*response_fn)(VirtIONSM *vnsm, struct iovec *request,
                        struct iovec *response, Error **errp);
};

const struct nsm_cmd nsm_cmds[] = {
    { "GetRandom",   CBOR_ROOT_TYPE_STRING,  handle_get_random },
    { "DescribeNSM", CBOR_ROOT_TYPE_STRING,  handle_describe_nsm },
    { "DescribePCR", CBOR_ROOT_TYPE_MAP,     handle_describe_pcr },
    { "ExtendPCR",   CBOR_ROOT_TYPE_MAP,     handle_extend_pcr },
    { "LockPCR",     CBOR_ROOT_TYPE_MAP,     handle_lock_pcr },
    { "LockPCRs",    CBOR_ROOT_TYPE_MAP,     handle_lock_pcrs },
    { "Attestation", CBOR_ROOT_TYPE_MAP,     handle_attestation },
};

static const struct nsm_cmd *get_nsm_request_cmd(uint8_t *buf, size_t len)
{
    size_t size;
    uint8_t *req;
    enum CBOR_ROOT_TYPE root_type;
    struct cbor_load_result result;
    cbor_item_t *item = cbor_load(buf, len, &result);
    if (!item || result.error.code != CBOR_ERR_NONE) {
        goto cleanup;
    }

    if (cbor_isa_string(item)) {
        size = cbor_string_length(item);
        req = cbor_string_handle(item);
        root_type = CBOR_ROOT_TYPE_STRING;
    } else if (cbor_isa_map(item) && cbor_map_size(item) == 1) {
        struct cbor_pair *handle = cbor_map_handle(item);
        if (cbor_isa_string(handle->key)) {
            size = cbor_string_length(handle->key);
            req = cbor_string_handle(handle->key);
            root_type = CBOR_ROOT_TYPE_MAP;
        } else {
            goto cleanup;
        }
    } else {
        goto cleanup;
    }

    if  (size == 0 || req == NULL) {
        goto cleanup;
    }

    for (int i = 0; i < ARRAY_SIZE(nsm_cmds); ++i) {
        if (nsm_cmds[i].root_type == root_type &&
            strlen(nsm_cmds[i].name) == size &&
            memcmp(nsm_cmds[i].name, req, size) == 0) {
            cbor_decref(&item);
            return &nsm_cmds[i];
        }
    }

 cleanup:
    if (item) {
        cbor_decref(&item);
    }
    return NULL;
}

static bool get_nsm_request_response(VirtIONSM *vnsm, struct iovec *req,
                                     struct iovec *resp, Error **errp)
{
    const struct nsm_cmd *cmd;

    if (req->iov_len > NSM_REQUEST_MAX_SIZE) {
        if (error_response(resp, NSM_INPUT_TOO_LARGE, errp)) {
            return true;
        }
        error_setg(errp, "Failed to initialize InputTooLarge response");
        return false;
    }

    cmd = get_nsm_request_cmd(req->iov_base, req->iov_len);

    if (cmd == NULL) {
        if (error_response(resp, NSM_INVALID_OPERATION, errp)) {
            return true;
        }
        error_setg(errp, "Failed to initialize InvalidOperation response");
        return false;
    }

    return cmd->response_fn(vnsm, req, resp, errp);
}

static void handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
    g_autofree VirtQueueElement *out_elem = NULL;
    g_autofree VirtQueueElement *in_elem = NULL;
    VirtIONSM *vnsm = VIRTIO_NSM(vdev);
    Error *err = NULL;
    size_t sz;
    struct iovec req = {.iov_base = NULL, .iov_len = 0};
    struct iovec res = {.iov_base = NULL, .iov_len = 0};

    out_elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!out_elem) {
        /* nothing in virtqueue */
        return;
    }

    sz = iov_size(out_elem->out_sg, out_elem->out_num);
    if (sz == 0) {
        virtio_error(vdev, "Expected non-zero sized request buffer in "
                     "virtqueue");
        goto cleanup;
    }

    in_elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!in_elem) {
        virtio_error(vdev, "Expected response buffer after request buffer "
                     "in virtqueue");
        goto cleanup;
    }
    if (iov_size(in_elem->in_sg, in_elem->in_num) != NSM_RESPONSE_BUF_SIZE) {
        virtio_error(vdev, "Expected response buffer of length 0x3000");
        goto cleanup;
    }

    req.iov_base = g_malloc(sz);
    req.iov_len = iov_to_buf(out_elem->out_sg, out_elem->out_num, 0,
                             req.iov_base, sz);
    if (req.iov_len != sz) {
        virtio_error(vdev, "Failed to copy request buffer");
        goto cleanup;
    }

    res.iov_base = g_malloc(NSM_RESPONSE_BUF_SIZE);
    res.iov_len = NSM_RESPONSE_BUF_SIZE;

    if (!get_nsm_request_response(vnsm, &req, &res, &err)) {
        error_report_err(err);
        virtio_error(vdev, "Failed to get NSM request response");
        goto cleanup;
    }

    sz = iov_from_buf(in_elem->in_sg, in_elem->in_num, 0, res.iov_base,
                      res.iov_len);
    if (sz != res.iov_len) {
        virtio_error(vdev, "Failed to copy response buffer");
        goto cleanup;
    }

    g_free(req.iov_base);
    g_free(res.iov_base);
    virtqueue_push(vq, out_elem, 0);
    virtqueue_push(vq, in_elem, sz);
    virtio_notify(vdev, vq);
    return;

 cleanup:
    g_free(req.iov_base);
    g_free(res.iov_base);
    if (out_elem) {
        virtqueue_detach_element(vq, out_elem, 0);
    }
    if (in_elem) {
        virtqueue_detach_element(vq, in_elem, 0);
    }
}

static uint64_t get_features(VirtIODevice *vdev, uint64_t f, Error **errp)
{
    return f;
}

static bool extend_pcr(VirtIONSM *vnsm, int ind, uint8_t *data, uint16_t len)
{
    Error *err = NULL;
    struct PCRInfo *pcr = &(vnsm->pcrs[ind]);
    size_t digest_len = QCRYPTO_HASH_DIGEST_LEN_SHA384;
    uint8_t result[QCRYPTO_HASH_DIGEST_LEN_SHA384];
    uint8_t *ptr = result;
    struct iovec iov[2] = {
        { .iov_base = pcr->data, .iov_len = QCRYPTO_HASH_DIGEST_LEN_SHA384 },
        { .iov_base = data, .iov_len = len },
    };

    if (qcrypto_hash_bytesv(QCRYPTO_HASH_ALGO_SHA384, iov, 2, &ptr, &digest_len,
                            &err) < 0) {
        return false;
    }

    memcpy(pcr->data, result, QCRYPTO_HASH_DIGEST_LEN_SHA384);
    return true;
}

static void lock_pcr(VirtIONSM *vnsm, int ind)
{
    vnsm->pcrs[ind].locked = true;
}

static void virtio_nsm_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIONSM *vnsm = VIRTIO_NSM(dev);

    vnsm->max_pcrs = NSM_MAX_PCRS;
    vnsm->digest = (char *) "SHA384";
    if (vnsm->module_id == NULL) {
        vnsm->module_id = (char *) "i-234-enc5678";
    }
    vnsm->version_major = 1;
    vnsm->version_minor = 0;
    vnsm->version_patch = 0;
    vnsm->extend_pcr = extend_pcr;
    vnsm->lock_pcr = lock_pcr;

    virtio_init(vdev, VIRTIO_ID_NITRO_SEC_MOD, 0);

    vnsm->vq = virtio_add_queue(vdev, 2, handle_input);
}

static void virtio_nsm_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    virtio_del_queue(vdev, 0);
    virtio_cleanup(vdev);
}

static const VMStateDescription vmstate_pcr_info_entry = {
    .name = "pcr_info_entry",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(locked, struct PCRInfo),
        VMSTATE_UINT8_ARRAY(data, struct PCRInfo,
                            QCRYPTO_HASH_DIGEST_LEN_SHA384),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_virtio_nsm_device = {
    .name = "virtio-nsm-device",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(pcrs, VirtIONSM, NSM_MAX_PCRS, 1,
                             vmstate_pcr_info_entry, struct PCRInfo),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_virtio_nsm = {
    .name = "virtio-nsm",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static const Property virtio_nsm_properties[] = {
    DEFINE_PROP_STRING("module-id", VirtIONSM, module_id),
};

static void virtio_nsm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_nsm_properties);
    dc->vmsd = &vmstate_virtio_nsm;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_nsm_device_realize;
    vdc->unrealize = virtio_nsm_device_unrealize;
    vdc->get_features = get_features;
    vdc->vmsd = &vmstate_virtio_nsm_device;
}

static const TypeInfo virtio_nsm_info = {
    .name = TYPE_VIRTIO_NSM,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIONSM),
    .class_init = virtio_nsm_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_nsm_info);
}

type_init(virtio_register_types)
