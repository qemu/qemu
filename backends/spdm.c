/*
 * Utility functions to use LibSPDM
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include <stdio.h>
#include "sysemu/spdm.h"

SpdmDevNode null_node = {NULL, NULL};
SpdmDevNode *g_spdm_dev_list_entry = &null_node;

DOEProtocol doe_spdm_dev_prot[] = {
    { PCI_VENDOR_ID_PCI_SIG, PCI_SIG_DOE_CMA, pcie_doe_spdm_dev_rsp },
    { PCI_VENDOR_ID_PCI_SIG, PCI_SIG_DOE_SECURED_CMA, pcie_doe_spdm_dev_rsp },
    { }
};

SpdmDevNode *create_spdm_dev_node(SpdmDev *dev)
{
    SpdmDevNode *node = (SpdmDevNode *)g_malloc(sizeof(SpdmDevNode));

    node->spdm_dev = dev;
    node->next = NULL;

    return node;
}

bool record_spdm_dev_in_list(SpdmDev *dev)
{
    SpdmDevNode *node = create_spdm_dev_node(dev);
    SpdmDevNode *curr = g_spdm_dev_list_entry;

    if (curr->spdm_dev == NULL) {
        g_spdm_dev_list_entry = (SpdmDevNode *)g_malloc(sizeof(SpdmDevNode));
        g_spdm_dev_list_entry = node;

        return true;
    }

    while (curr != NULL) {
        if (curr->next == NULL) {
            curr->next = node;

            return true;
        }

        curr = curr->next;
    }

    return false;
}

bool delete_spdm_dev_in_list(SpdmDev *dev)
{
    SpdmDevNode *curr = g_spdm_dev_list_entry;
    SpdmDevNode *prev = NULL;

    if (curr->spdm_dev == dev) {
        g_spdm_dev_list_entry = curr->next == NULL ? NULL : curr->next;
        g_free(curr);

        return true;
    }

    while (curr != NULL) {
        if (curr->spdm_dev == dev) {
            prev->next = curr->next;
            g_free(curr);

            return true;
        }

        prev = curr;
        curr = curr->next;
    }

    return false;
}

SpdmDev *get_spdm_dev_from_context(void *context)
{
    SpdmDevNode *curr = g_spdm_dev_list_entry;

    while (curr != NULL) {
        if (curr->spdm_dev->spdm_context == context) {
            return curr->spdm_dev;
        }

        curr = curr->next;
    }

    return NULL;
}

SpdmDev *get_spdm_dev_from_doe_cap(DOECap *doe_cap)
{
    SpdmDevNode *curr = g_spdm_dev_list_entry;

    while (curr != NULL) {
        if (curr->spdm_dev->doe_cap == doe_cap) {
            return curr->spdm_dev;
        }

        curr = curr->next;
    }

    return NULL;
}

void libspdm_dump_hex_str(const uint8_t *buffer, size_t buffer_size)
{
    size_t index;

    for (index = 0; index < buffer_size; index++) {
        error_report("%02x", buffer[index]);
    }
}

void dump_data(const uint8_t *buffer, size_t buffer_size)
{
    size_t index;

    for (index = 0; index < buffer_size; index++) {
        error_report("%02x ", buffer[index]);
    }
}

void dump_hex(const uint8_t *data, size_t size)
{
    size_t index;
    size_t count;
    size_t left;

#define COLUME_SIZE (16 * 2)

    count = size / COLUME_SIZE;
    left = size % COLUME_SIZE;
    for (index = 0; index < count; index++) {
        error_report("%04x: ", (uint32_t)(index * COLUME_SIZE));
        dump_data(data + index * COLUME_SIZE, COLUME_SIZE);
        error_report("\n");
    }

    if (left != 0) {
        error_report("%04x: ", (uint32_t)(index * COLUME_SIZE));
        dump_data(data + index * COLUME_SIZE, left);
        error_report("\n");
    }
}

bool libspdm_read_input_file(const char *file_name, void **file_data,
                             size_t *file_size)
{
    FILE *fp_in;
    size_t temp_result;

    if ((fp_in = fopen(file_name, "rb")) == NULL) {
        error_report("Unable to open file %s\n", file_name);
        *file_data = NULL;
        return false;
    }

    fseek(fp_in, 0, SEEK_END);
    *file_size = ftell(fp_in);
    if (*file_size == -1) {
        error_report("Unable to get the file size %s\n", file_name);
        *file_data = NULL;
        fclose(fp_in);
        return false;
    }

    *file_data = (void *)malloc(*file_size);
    if (NULL == *file_data) {
        error_report("No sufficient memory to allocate %s\n", file_name);
        fclose(fp_in);
        return false;
    }

    fseek(fp_in, 0, SEEK_SET);
    temp_result = fread(*file_data, 1, *file_size, fp_in);
    if (temp_result != *file_size) {
        error_report("Read input file error %s", file_name);
        free((void *)*file_data);
        fclose(fp_in);
        return false;
    }

    fclose(fp_in);

    return true;
}

bool libspdm_write_output_file(const char *file_name, const void *file_data,
                               size_t file_size)
{
    FILE *fp_out;

    if ((fp_out = fopen(file_name, "w+b")) == NULL) {
        error_report("Unable to open file %s\n", file_name);
        return false;
    }

    if (file_size != 0) {
        if ((fwrite(file_data, 1, file_size, fp_out)) != file_size) {
            error_report("Write output file error %s\n", file_name);
            fclose(fp_out);
            return false;
        }
    }

    fclose(fp_out);

    return true;
}

static libspdm_return_t spdm_dev_provision_psk_version_only(
    void *spdm_context, bool is_requester)
{
    SpdmDev *dev = get_spdm_dev_from_context(spdm_context);
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    size_t data_size;
    spdm_version_number_t spdm_version;

    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;

    /* make sure it is called after GET_VERSION */
    data_size = sizeof(data32);
    libspdm_get_data(spdm_context, LIBSPDM_DATA_CONNECTION_STATE, &parameter,
                     &data32, &data_size);
    LIBSPDM_ASSERT(data32 == LIBSPDM_CONNECTION_STATE_AFTER_VERSION);

    if (is_requester) {
        /* get version from requester, because it is negotiated */
        data_size = sizeof(spdm_version);
        libspdm_get_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter,
                         &spdm_version, &data_size);
        dev->use_version = spdm_version >> SPDM_VERSION_NUMBER_SHIFT_BIT;
    } else {
        /* set version for responder, because it cannot be negotiated */
        spdm_version = dev->use_version << SPDM_VERSION_NUMBER_SHIFT_BIT;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter,
                         &spdm_version, sizeof(spdm_version));
    }

    if (dev->use_version == 0) {
        g_print("spdm_version is unknown, please provision it as well.\n");
        return LIBSPDM_STATUS_UNSUPPORTED_CAP;
    }

    /* Set connection info*/

    data8 = 0;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT,
                     &parameter, &data8, sizeof(data8));
    if (is_requester) {
        /* set responder's cap for requester */
        data32 = dev->use_responder_capability_flags;
        if (dev->use_peer_capability_flags != 0) {
            data32 = dev->use_peer_capability_flags;
            dev->use_responder_capability_flags = dev->use_peer_capability_flags;
        }
    } else {
        /* set requester's cap for responder */
        data32 = dev->use_requester_capability_flags;
        if (dev->use_peer_capability_flags != 0) {
            data32 = dev->use_peer_capability_flags;
            dev->use_requester_capability_flags = dev->use_peer_capability_flags;
        }
    }
    libspdm_set_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                     &data32, sizeof(data32));

    if (!libspdm_onehot0(dev->support_measurement_spec)) {
        g_print("measurement_spec has more bit set - 0x%02x\n", dev->support_measurement_spec);
        return LIBSPDM_STATUS_UNSUPPORTED_CAP;
    }
    data8 = dev->support_measurement_spec;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC, &parameter,
                     &data8, sizeof(data8));

    if (!libspdm_onehot0(dev->support_measurement_hash_algo)) {
        g_print("measurement_hash_algo has more bit set - 0x%08x\n",
               dev->support_measurement_hash_algo);
        return LIBSPDM_STATUS_UNSUPPORTED_CAP;
    }
    data32 = dev->support_measurement_hash_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO, &parameter,
                     &data32, sizeof(data32));

    if (!libspdm_onehot0(dev->support_asym_algo)) {
        g_print("base_asym_algo has more bit set - 0x%08x\n", dev->support_asym_algo);
        return LIBSPDM_STATUS_UNSUPPORTED_CAP;
    }
    data32 = dev->support_asym_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO, &parameter,
                     &data32, sizeof(data32));

    if (!libspdm_onehot0(dev->support_hash_algo)) {
        g_print("base_hash_algo has more bit set - 0x%08x\n", dev->support_hash_algo);
        return LIBSPDM_STATUS_UNSUPPORTED_CAP;
    }
    data32 = dev->support_hash_algo;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO, &parameter,
                     &data32, sizeof(data32));

    if (dev->use_version >= SPDM_MESSAGE_VERSION_11) {
        if (!libspdm_onehot0(dev->support_dhe_algo)) {
            g_print("dhe_algo has more bit set - 0x%04x\n", dev->support_dhe_algo);
            return LIBSPDM_STATUS_UNSUPPORTED_CAP;
        }
        data16 = dev->support_dhe_algo;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP,
                         &parameter, &data16, sizeof(data16));

        if (!libspdm_onehot0(dev->support_aead_algo)) {
            g_print("aead_algo has more bit set - 0x%04x\n", dev->support_aead_algo);
            return LIBSPDM_STATUS_UNSUPPORTED_CAP;
        }
        data16 = dev->support_aead_algo;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE,
                         &parameter, &data16, sizeof(data16));

        if (!libspdm_onehot0(dev->support_req_asym_algo)) {
            g_print("req_asym_algo has more bit set - 0x%04x\n", dev->support_req_asym_algo);
            return LIBSPDM_STATUS_UNSUPPORTED_CAP;
        }
        data16 = dev->support_req_asym_algo;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG,
                         &parameter, &data16, sizeof(data16));

        if (!libspdm_onehot0(dev->support_key_schedule_algo)) {
            g_print("key_schedule_algo has more bit set - 0x%04x\n", dev->support_key_schedule_algo);
            return LIBSPDM_STATUS_UNSUPPORTED_CAP;
        }
        data16 = dev->support_key_schedule_algo;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE, &parameter,
                         &data16, sizeof(data16));

        if (dev->use_version >= SPDM_MESSAGE_VERSION_12) {
            if (!libspdm_onehot0(dev->support_other_params_support)) {
                g_print("other_params has more bit set - 0x%02x\n", dev->support_other_params_support);
                return LIBSPDM_STATUS_UNSUPPORTED_CAP;
            }
            data8 = dev->support_other_params_support;
            libspdm_set_data(spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT, &parameter,
                             &data8, sizeof(data8));
            if (dev->use_version >= SPDM_MESSAGE_VERSION_13) {
                if (!libspdm_onehot0(dev->support_mel_spec)) {
                    g_print("mel_spec has more bit set - 0x%02x\n", dev->support_mel_spec);
                    return LIBSPDM_STATUS_UNSUPPORTED_CAP;
                }
                data8 = dev->support_mel_spec;
                libspdm_set_data(spdm_context, LIBSPDM_DATA_MEL_SPEC, &parameter,
                                 &data8, sizeof(data8));
            }
        }
    } else {
        data16 = 0;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP,
                         &parameter, &data16, sizeof(data16));
        data16 = 0;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE,
                         &parameter, &data16, sizeof(data16));
        data16 = 0;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG,
                         &parameter, &data16, sizeof(data16));
        data16 = 0;
        libspdm_set_data(spdm_context, LIBSPDM_DATA_KEY_SCHEDULE, &parameter,
                         &data16, sizeof(data16));
    }

    /* PSK version only - set to NEGOTIATED */
    data32 = LIBSPDM_CONNECTION_STATE_NEGOTIATED;
    libspdm_set_data(spdm_context, LIBSPDM_DATA_CONNECTION_STATE, &parameter,
                     &data32, sizeof(data32));

    return LIBSPDM_STATUS_SUCCESS;
}

/**
 * Notify the session state to a session APP.
 *
 * @param  spdm_context                  A pointer to the SPDM context.
 * @param  session_id                    The session_id of a session.
 * @param  session_state                 The state of a session.
 **/
void spdm_dev_server_session_state_callback(void *spdm_context,
                                             uint32_t session_id,
                                             libspdm_session_state_t session_state)
{
    SpdmDev *dev = get_spdm_dev_from_context(spdm_context);
    size_t data_size;
    libspdm_data_parameter_t parameter;
    uint8_t data8;

    switch (session_state) {
    case LIBSPDM_SESSION_STATE_NOT_STARTED:
        /* Session end*/
        break;

    case LIBSPDM_SESSION_STATE_HANDSHAKING:
        /* collect session policy*/
        if (dev->use_version >= SPDM_MESSAGE_VERSION_12) {
            libspdm_zero_mem(&parameter, sizeof(parameter));
            parameter.location = LIBSPDM_DATA_LOCATION_SESSION;
            *(uint32_t *)parameter.additional_data = session_id;

            data8 = 0;
            data_size = sizeof(data8);
            libspdm_get_data(spdm_context,
                             LIBSPDM_DATA_SESSION_POLICY,
                             &parameter, &data8, &data_size);
        }
        break;

    case LIBSPDM_SESSION_STATE_ESTABLISHED:
        /* no action*/
        break;

    default:
        LIBSPDM_ASSERT(false);
        break;
    }
}

/**
 * Notify the connection state to an SPDM context register.
 *
 * @param  spdm_context                  A pointer to the SPDM context.
 * @param  connection_state              Indicate the SPDM connection state.
 **/
void spdm_dev_server_connection_state_callback(
    void *spdm_context, libspdm_connection_state_t connection_state)
{
    SpdmDev *dev = get_spdm_dev_from_context(spdm_context);
    bool res;
    void *data;
    void *data1;
    size_t data_size;
    size_t data1_size;
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    libspdm_return_t status;
    void *hash;
    size_t hash_size;
    const uint8_t *root_cert;
    size_t root_cert_size;
    uint8_t index;
    spdm_version_number_t spdm_version;

    switch (connection_state) {
    case LIBSPDM_CONNECTION_STATE_NOT_STARTED:

        /* TODO: connection state saved in file */
        break;

    case LIBSPDM_CONNECTION_STATE_AFTER_VERSION:
        if ((dev->exe_connection & EXE_CONNECTION_VERSION_ONLY) != 0) {
            /* GET_VERSION is done, handle special PSK use case*/
            status = spdm_dev_provision_psk_version_only (spdm_context, false);
            if (LIBSPDM_STATUS_IS_ERROR(status)) {
                LIBSPDM_ASSERT (false);
                return;
            }
            /* pass through to NEGOTIATED */
        } else {
            /* normal action - do nothing */
            break;
        }

        break;

    case LIBSPDM_CONNECTION_STATE_NEGOTIATED:

        if (dev->use_version == 0) {
            libspdm_zero_mem(&parameter, sizeof(parameter));
            parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;
            data_size = sizeof(spdm_version);
            libspdm_get_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter,
                             &spdm_version, &data_size);
            dev->use_version = spdm_version >> SPDM_VERSION_NUMBER_SHIFT_BIT;
        }

        /* Provision new content*/

        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;

        data_size = sizeof(data32);
        libspdm_get_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO,
                         &parameter, &data32, &data_size);
        dev->use_measurement_hash_algo = data32;
        data_size = sizeof(data32);
        libspdm_get_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO,
                         &parameter, &data32, &data_size);
        dev->use_asym_algo = data32;
        data_size = sizeof(data32);
        libspdm_get_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO,
                         &parameter, &data32, &data_size);
        dev->use_hash_algo = data32;
        data_size = sizeof(data16);
        libspdm_get_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG,
                         &parameter, &data16, &data_size);
        dev->use_req_asym_algo = data16;

        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        data_size = sizeof(data32);
        libspdm_get_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                        &data32, &data_size);

        if ((data32 & SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ALIAS_CERT_CAP) == 0) {
            res = libspdm_read_responder_public_certificate_chain(dev->use_hash_algo,
                                                                dev->use_asym_algo,
                                                                &data, &data_size,
                                                                NULL, NULL);
        } else {
            res = libspdm_read_responder_public_certificate_chain_alias_cert(
                dev->use_hash_algo,
                dev->use_asym_algo,
                &data, &data_size,
                NULL, NULL);
        }

        res = libspdm_read_responder_public_certificate_chain_per_slot(1,
                                                                       dev->use_hash_algo,
                                                                       dev->use_asym_algo,
                                                                       &data1, &data1_size,
                                                                       NULL, NULL);
        if (res) {
            libspdm_zero_mem(&parameter, sizeof(parameter));
            parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

            for (index = 0; index < dev->use_slot_count; index++) {
                parameter.additional_data[0] = index;
                if (index == 1) {
                    libspdm_set_data(spdm_context,
                                     LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN,
                                     &parameter, data1, data1_size);
                } else {
                    libspdm_set_data(spdm_context,
                                     LIBSPDM_DATA_LOCAL_PUBLIC_CERT_CHAIN,
                                     &parameter, data, data_size);
                }
                data8 = (uint8_t)(0xA0 + index);
                libspdm_set_data(spdm_context,
                                 LIBSPDM_DATA_LOCAL_KEY_PAIR_ID,
                                 &parameter, &data8, sizeof(data8));
                data8 = SPDM_CERTIFICATE_INFO_CERT_MODEL_DEVICE_CERT;
                libspdm_set_data(spdm_context,
                                 LIBSPDM_DATA_LOCAL_CERT_INFO,
                                 &parameter, &data8, sizeof(data8));
                data16 = SPDM_KEY_USAGE_BIT_MASK_KEY_EX_USE | 
                         SPDM_KEY_USAGE_BIT_MASK_CHALLENGE_USE |
                         SPDM_KEY_USAGE_BIT_MASK_MEASUREMENT_USE |
                         SPDM_KEY_USAGE_BIT_MASK_ENDPOINT_INFO_USE;
                libspdm_set_data(spdm_context,
                                 LIBSPDM_DATA_LOCAL_KEY_USAGE_BIT_MASK,
                                 &parameter, &data16, sizeof(data16));
            }
            /* do not free it*/
        }

        if (dev->use_req_asym_algo != 0) {
            if ((dev->use_responder_capability_flags &
                 SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PUB_KEY_ID_CAP) != 0) {
                dev->use_slot_id = 0xFF;
            }
            if (dev->use_slot_id == 0xFF) {
                res = libspdm_read_responder_public_key(dev->use_asym_algo, &data, &data_size);
                if (res) {
                    libspdm_zero_mem(&parameter, sizeof(parameter));
                    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
                    libspdm_set_data(spdm_context,
                                     LIBSPDM_DATA_LOCAL_PUBLIC_KEY,
                                     &parameter, data, data_size);
                    /* Do not free it.*/
                }
                res = libspdm_read_requester_public_key(dev->use_req_asym_algo, &data, &data_size);
                if (res) {
                    libspdm_zero_mem(&parameter, sizeof(parameter));
                    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
                    libspdm_set_data(spdm_context,
                                     LIBSPDM_DATA_PEER_PUBLIC_KEY,
                                     &parameter, data, data_size);
                    /* Do not free it.*/
                }
            } else {
                res = libspdm_read_requester_root_public_certificate(
                    dev->use_hash_algo, dev->use_req_asym_algo, &data,
                    &data_size, &hash, &hash_size);
                libspdm_x509_get_cert_from_cert_chain(
                    (uint8_t *)data + sizeof(spdm_cert_chain_t) + hash_size,
                    data_size - sizeof(spdm_cert_chain_t) - hash_size, 0,
                    &root_cert, &root_cert_size);

                if (res) {
                    libspdm_zero_mem(&parameter, sizeof(parameter));
                    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
                    libspdm_set_data(
                        spdm_context,
                        LIBSPDM_DATA_PEER_PUBLIC_ROOT_CERT,
                        &parameter, (void *)root_cert, root_cert_size);
                    /* Do not free it.*/
                }
            }

            if (res) {
                if (dev->use_slot_id == 0xFF) {
                    /* 0xFF slot is only allowed in */
                    dev->use_mut_auth = SPDM_KEY_EXCHANGE_RESPONSE_MUT_AUTH_REQUESTED;
                }
                data8 = dev->use_mut_auth;
                parameter.additional_data[0] =
                    dev->use_slot_id; /* req_slot_id;*/
                libspdm_set_data(spdm_context,
                                 LIBSPDM_DATA_MUT_AUTH_REQUESTED, &parameter,
                                 &data8, sizeof(data8));

                data8 = dev->use_basic_mut_auth;
                parameter.additional_data[0] =
                    dev->use_slot_id; /* req_slot_id;*/
                libspdm_set_data(spdm_context,
                                 LIBSPDM_DATA_BASIC_MUT_AUTH_REQUESTED,
                                 &parameter, &data8, sizeof(data8));
            }
        }

        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        data8 = 0;
        for (index = 0; index < dev->use_slot_count; index++) {
            data8 |= (1 << index);
        }
        libspdm_set_data(spdm_context, LIBSPDM_DATA_LOCAL_SUPPORTED_SLOT_MASK, &parameter,
                         &data8, sizeof(data8));

        break;

    default:
        break;
    }

    return;
}

libspdm_return_t spdm_dev_acquire_buffer(void *context, void **msg_buf_ptr)
{
    SpdmDev *dev = get_spdm_dev_from_context(context);

    LIBSPDM_ASSERT(!dev->sender_receiver_buffer_acquired);
    *msg_buf_ptr = g_malloc0(LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE);
    dev->sender_receiver_buffer_acquired = true;

    return LIBSPDM_STATUS_SUCCESS;
}

void spdm_dev_release_buffer(void *context, const void *msg_buf_ptr)
{
    SpdmDev *dev = get_spdm_dev_from_context(context);

    LIBSPDM_ASSERT(dev->sender_receiver_buffer_acquired);
    g_free((void *)msg_buf_ptr);
    dev->sender_receiver_buffer_acquired = false;
}

libspdm_return_t spdm_dev_send_message(void *context, size_t response_size,
                                        const void *response, uint64_t timeout)
{
    SpdmDev *dev = get_spdm_dev_from_context(context);

    LIBSPDM_ASSERT(dev->spdm_context == context);

    dev->message_size = response_size;
    dev->sender_receiver_buffer = g_malloc0(response_size);
    memcpy(dev->sender_receiver_buffer, response, response_size);

    return LIBSPDM_STATUS_SUCCESS;
}

libspdm_return_t spdm_dev_receive_message(void *context, size_t *request_size,
                                           void **request, uint64_t timeout)
{
    SpdmDev *dev = get_spdm_dev_from_context(context);

    LIBSPDM_ASSERT(dev->spdm_context == context);

    *request_size = dev->message_size;
    memcpy(*request, dev->sender_receiver_buffer, dev->message_size);

    return LIBSPDM_STATUS_SUCCESS;
}

bool pcie_doe_spdm_dev_rsp(DOECap *doe_cap)
{
    SpdmDev *dev = get_spdm_dev_from_doe_cap(doe_cap);
    uint32_t *wmbox = doe_cap->write_mbox;
    uint32_t index = doe_cap->write_mbox_hd1;
    uint32_t header1 = doe_cap->write_mbox[index];
    uint32_t size = wmbox[index + 1];
    libspdm_context_t *spdm_context = dev->spdm_context;
    libspdm_return_t status;

    if (header1 == DATA_OBJ_BUILD_HEADER1(PCI_DOE_VENDOR_ID_PCISIG, PCI_DOE_DATA_OBJECT_TYPE_SPDM)) {
        dev->sender_receiver_buffer = g_malloc0(size * sizeof(uint32_t));
        memcpy(dev->sender_receiver_buffer, wmbox + index, size * sizeof(uint32_t));
        dev->message_size = size * sizeof(uint32_t);

        status = libspdm_responder_dispatch_message(dev->spdm_context);
        if (status == LIBSPDM_STATUS_SUCCESS) {
            dev->spdm_server_connection_state_callback(dev->spdm_context,
                                                       spdm_context->connection_info.connection_state);
        } else {
            return false;
        }

        memcpy(doe_cap->read_mbox, (uint32_t *)dev->sender_receiver_buffer, (uint32_t)dev->message_size);
        doe_cap->read_mbox_idx = 0;
        doe_cap->read_mbox_len = (uint32_t)(dev->message_size / 4);
    }

    return true;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
void *spdm_responder_init(SpdmDev *spdm_dev)
{
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    spdm_version_number_t spdm_version;

    /* Check if device is set to be responder */ 
    if (!spdm_dev->is_responder) {
        return NULL;
    }

    /*
     * Allocate and initialize the SPDM context, then the context 
     * data is configured using SpdmDev data
     */
    spdm_dev->spdm_context = g_malloc(libspdm_get_context_size());
    libspdm_init_context(spdm_dev->spdm_context);

    libspdm_register_device_io_func(spdm_dev->spdm_context,
                                    spdm_dev->spdm_device_send_message,
                                    spdm_dev->spdm_device_receive_message);

    if (spdm_dev->use_transport_layer == SOCKET_TRANSPORT_TYPE_MCTP) {
        libspdm_register_transport_layer_func(
            spdm_dev->spdm_context,
            LIBSPDM_MAX_SPDM_MSG_SIZE,
            LIBSPDM_MCTP_TRANSPORT_HEADER_SIZE,
            LIBSPDM_MCTP_TRANSPORT_TAIL_SIZE,
            libspdm_transport_mctp_encode_message,
            libspdm_transport_mctp_decode_message);
    } else if (spdm_dev->use_transport_layer == SOCKET_TRANSPORT_TYPE_PCI_DOE) {
        libspdm_register_transport_layer_func(
            spdm_dev->spdm_context,
            LIBSPDM_MAX_SPDM_MSG_SIZE,
            LIBSPDM_TRANSPORT_HEADER_SIZE,
            LIBSPDM_TRANSPORT_TAIL_SIZE,
            libspdm_transport_pci_doe_encode_message,
            libspdm_transport_pci_doe_decode_message);
    } else {
        g_free(spdm_dev->spdm_context);
        spdm_dev->spdm_context = NULL;
        return NULL;
    }
    libspdm_register_device_buffer_func(spdm_dev->spdm_context,
                                        LIBSPDM_SENDER_BUFFER_SIZE,
                                        LIBSPDM_RECEIVER_BUFFER_SIZE,
                                        spdm_dev->spdm_device_acquire_sender_buffer,
                                        spdm_dev->spdm_device_release_sender_buffer,
                                        spdm_dev->spdm_device_acquire_receiver_buffer,
                                        spdm_dev->spdm_device_release_receiver_buffer);

    spdm_dev->scratch_buffer_size = libspdm_get_sizeof_required_scratch_buffer(spdm_dev->spdm_context);
    spdm_dev->scratch_buffer = g_malloc(spdm_dev->scratch_buffer_size);
    libspdm_set_scratch_buffer(spdm_dev->spdm_context,
                               spdm_dev->scratch_buffer,
                               spdm_dev->scratch_buffer_size);

    spdm_dev->requester_cert_chain_buffer = g_malloc(SPDM_MAX_CERTIFICATE_CHAIN_SIZE);
    libspdm_register_cert_chain_buffer(spdm_dev->spdm_context,
                                       spdm_dev->requester_cert_chain_buffer,
                                       SPDM_MAX_CERTIFICATE_CHAIN_SIZE);

    if (!libspdm_check_context(spdm_dev->spdm_context)) {
        return NULL;
    }

    if (spdm_dev->use_version != 0) {
        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        spdm_version = spdm_dev->use_version << SPDM_VERSION_NUMBER_SHIFT_BIT;
        libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_SPDM_VERSION,
                         &parameter, &spdm_version, sizeof(spdm_version));
    }

    if (spdm_dev->use_secured_message_version != 0) {
        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        spdm_version = spdm_dev->use_secured_message_version << SPDM_VERSION_NUMBER_SHIFT_BIT;
        libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_SECURED_MESSAGE_VERSION,
                         &parameter, &spdm_version, sizeof(spdm_version));
    }

    libspdm_zero_mem(&parameter, sizeof(parameter));
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

    data8 = 0;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_CAPABILITY_CT_EXPONENT,
                     &parameter, &data8, sizeof(data8));

    data32 = spdm_dev->use_responder_capability_flags;
    if (spdm_dev->use_slot_id == 0xFF) {
        data32 |= SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PUB_KEY_ID_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ALIAS_CERT_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_SET_CERT_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CSR_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_INSTALL_RESET_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MULTI_KEY_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_GET_KEY_PAIR_INFO_CAP;
        data32 &= ~SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_SET_KEY_PAIR_INFO_CAP;
    }
    if (spdm_dev->use_capability_flags != 0) {
        data32 = spdm_dev->use_capability_flags;
        spdm_dev->use_responder_capability_flags = spdm_dev->use_capability_flags;
    }
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS,
                     &parameter,&data32, sizeof(data32));

    data8 = spdm_dev->support_measurement_spec;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_MEASUREMENT_SPEC,
                     &parameter, &data8, sizeof(data8));
    data32 = spdm_dev->support_measurement_hash_algo;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO,
                     &parameter, &data32, sizeof(data32));
    data32 = spdm_dev->support_asym_algo;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO,
                     &parameter, &data32, sizeof(data32));
    data32 = spdm_dev->support_hash_algo;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO,
                     &parameter, &data32, sizeof(data32));
    data16 = spdm_dev->support_dhe_algo;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_DHE_NAME_GROUP,
                     &parameter, &data16, sizeof(data16));
    data16 = spdm_dev->support_aead_algo;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_AEAD_CIPHER_SUITE,
                     &parameter, &data16, sizeof(data16));
    data16 = spdm_dev->support_req_asym_algo;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG,
                     &parameter, &data16, sizeof(data16));
    data16 = spdm_dev->support_key_schedule_algo;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_KEY_SCHEDULE,
                     &parameter, &data16, sizeof(data16));
    data8 = spdm_dev->support_other_params_support;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_OTHER_PARAMS_SUPPORT,
                     &parameter, &data8, sizeof(data8));
    data8 = spdm_dev->support_mel_spec;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_MEL_SPEC,
                     &parameter, &data8, sizeof(data8));

    data8 = 0xF0;
    libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_HEARTBEAT_PERIOD,
                     &parameter, &data8, sizeof(data8));

    libspdm_register_get_response_func(spdm_dev->spdm_context,
                                       spdm_dev->spdm_get_response_vendor_defined_request);
    libspdm_register_session_state_callback_func(
        spdm_dev->spdm_context, spdm_dev->spdm_server_session_state_callback);
    libspdm_register_connection_state_callback_func(
        spdm_dev->spdm_context, spdm_dev->spdm_server_connection_state_callback);

    record_spdm_dev_in_list(spdm_dev);

    return spdm_dev->spdm_context;
}
#pragma GCC diagnostic pop
