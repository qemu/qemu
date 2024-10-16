#include "hw/virtio/virtio-blk-spdm.h"

QemuMutex m_spdm_mutex;
QemuCond m_spdm_cond;
QemuThread m_spdm_thread;

struct SpdmDev vblk_spdm_dev = {
    .is_responder = true,
    .send_is_ready = false,
    .receive_is_ready = false,
    .use_transport_layer = SOCKET_TRANSPORT_TYPE_MCTP,
    .use_version = SPDM_MESSAGE_VERSION_11,
    .use_secured_message_version = SECURED_SPDM_VERSION_11,
    .use_responder_capability_flags =
        (0 | SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CACHE_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PUB_KEY_ID_CAP */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHAL_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_SIG | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_NO_SIG */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_FRESH_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ENCRYPT_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MAC_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MUT_AUTH_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP_RESPONDER_WITH_CONTEXT | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP_RESPONDER */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ENCAP_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_HBEAT_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_UPD_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_HANDSHAKE_IN_THE_CLEAR_CAP |
        0),
    .use_capability_flags = 0,
    .use_basic_mut_auth = 1,
    .use_mut_auth = 
        SPDM_KEY_EXCHANGE_RESPONSE_MUT_AUTH_REQUESTED_WITH_ENCAP_REQUEST,
    .use_measurement_summary_hash_type =
        SPDM_CHALLENGE_REQUEST_ALL_MEASUREMENTS_HASH,
    .use_measurement_operation =
        SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_TOTAL_NUMBER_OF_MEASUREMENTS,
    .use_slot_id = 0,
    .use_slot_count = 3,
    .use_key_update_action = LIBSPDM_KEY_UPDATE_ACTION_MAX,
    .support_measurement_spec =
        SPDM_MEASUREMENT_SPECIFICATION_DMTF,
    .support_measurement_hash_algo =
        SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_512 |
        SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_384,
    .support_hash_algo = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384 |
                    SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_256,
    .support_asym_algo =
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P256,
    .support_req_asym_algo =
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_3072 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_2048 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_3072 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_2048,
    .support_dhe_algo =
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_384_R1 |
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_256_R1 |
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_FFDHE_3072 |
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_FFDHE_2048,
    .support_aead_algo =
        SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_AES_256_GCM |
        SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_CHACHA20_POLY1305,
    .support_key_schedule_algo = SPDM_ALGORITHMS_KEY_SCHEDULE_HMAC_HASH,
    
    .spdm_device_send_message = vblk_spdm_send_message,
    .spdm_device_receive_message = vblk_spdm_receive_message,
    
    .spdm_device_acquire_sender_buffer = vblk_spdm_acquire_buffer,
    .spdm_device_release_sender_buffer = vblk_spdm_release_buffer,
    .spdm_device_acquire_receiver_buffer = vblk_spdm_acquire_buffer,
    .spdm_device_release_receiver_buffer = vblk_spdm_release_buffer,
};

libspdm_return_t vblk_spdm_acquire_buffer(void *context, void **msg_buf_ptr)
{
    SpdmDev *spdm_dev = &vblk_spdm_dev;

    qemu_mutex_lock(&m_spdm_mutex);

    LIBSPDM_ASSERT(!spdm_dev->sender_receiver_buffer_acquired);
    /*
    *msg_buf_ptr = spdm_dev->sender_receiver_buffer;
    libspdm_zero_mem (spdm_dev->sender_receiver_buffer, sizeof(spdm_dev->sender_receiver_buffer));
    LIBSPDM_ASSERT(*msg_buf_ptr == spdm_dev->sender_receiver_buffer);
    //*/
    *msg_buf_ptr = g_malloc(LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE);
    spdm_dev->sender_receiver_buffer_acquired = true;

    qemu_mutex_unlock(&m_spdm_mutex);
    
    return LIBSPDM_STATUS_SUCCESS;
}

void vblk_spdm_release_buffer(void *context, const void* msg_buf_ptr)
{
    SpdmDev *spdm_dev = &vblk_spdm_dev;

    qemu_mutex_lock(&m_spdm_mutex);

    LIBSPDM_ASSERT(spdm_dev->sender_receiver_buffer_acquired);
    /*
    LIBSPDM_ASSERT(msg_buf_ptr == spdm_dev->sender_receiver_buffer);
    //*/
    g_free((void *)msg_buf_ptr);
    spdm_dev->sender_receiver_buffer_acquired = false;

    qemu_mutex_unlock(&m_spdm_mutex);

    return;
}

libspdm_return_t vblk_spdm_send_message(void *spdm_context,
                                        size_t response_size,
                                        const void *response,
                                        uint64_t timeout)
{
    SpdmDev *spdm_dev = &vblk_spdm_dev;

    LIBSPDM_ASSERT(spdm_dev->spdm_context == spdm_context);
    if (response_size > sizeof(spdm_dev->sender_receiver_buffer)) {
        error_report("response_size requested is bigger than buffer size (%X).",
                     LIBSPDM_STATUS_BUFFER_TOO_SMALL);
        return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
    }
    spdm_dev->message_size = response_size;

    qemu_mutex_lock(&m_spdm_mutex);
    libspdm_zero_mem(spdm_dev->sender_receiver_buffer, LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE);
    memcpy(spdm_dev->sender_receiver_buffer, response, spdm_dev->message_size);

    /*
    SPDM_DEBUG("");
    for (int i = 0; i < spdm_dev->message_size; i++)
        g_printerr("%02X ", ((uint8_t *)spdm_dev->sender_receiver_buffer)[i]);
    g_printerr("\n");
    //*/
    spdm_dev->send_is_ready = true;
    qemu_cond_signal(&m_spdm_cond);

    qemu_mutex_unlock(&m_spdm_mutex);

    return LIBSPDM_STATUS_SUCCESS;
}


libspdm_return_t vblk_spdm_receive_message(void *spdm_context,
                                           size_t *request_size,
                                           void **request,
                                           uint64_t timeout)
{
    SpdmDev *spdm_dev = &vblk_spdm_dev;
    
    //*
    LIBSPDM_ASSERT(spdm_dev->spdm_context == spdm_context);
    if (*request_size > LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE) {
        error_report("response_size requested is bigger than buffer size (%X).",
                     LIBSPDM_STATUS_BUFFER_TOO_SMALL);
        return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
    }
    spdm_dev->message_size = *request_size;
    //*/

    /*
    g_printerr("\n[QEMU @ %s]:\n", __func__);
    for (int i = 0; i < 100; i++) {
        g_printerr("%02X ", ((uint8_t *)spdm_dev->sender_receiver_buffer)[i]);
    }
    g_printerr("\n");
    //*/

    //*
    qemu_mutex_lock(&m_spdm_mutex);
    memcpy(*request, spdm_dev->sender_receiver_buffer, spdm_dev->message_size);
    qemu_mutex_unlock(&m_spdm_mutex);
    //*/

    return LIBSPDM_STATUS_SUCCESS;
}

//*
void *vblk_spdm_io_thread(void *opaque) 
{
    VirtIOBlock *s = opaque;
    SpdmDev *spdm_dev = s->spdm_dev;
    libspdm_return_t status;
    /*
    uint8_t *request;
    size_t request_size;
    //*/

    while (true) {
        qemu_mutex_lock(&m_spdm_mutex);
        if (!spdm_dev->receive_is_ready) {
            qemu_cond_wait(&m_spdm_cond, &m_spdm_mutex);
        }
        spdm_dev->receive_is_ready = false;
        qemu_mutex_unlock(&m_spdm_mutex);

        /*
        g_printerr("\n[QEMU @ %s]:\n", __func__);
        for (int i = 0; i < 10; i++) {
            g_printerr("%02X ", ((uint8_t *)spdm_context->scratch_buffer)[i]);
        }
        g_printerr("\n");
        //*/

        /*
        SPDM_DEBUG("spdm_dev = %p\n", spdm_dev);
        SPDM_DEBUG("spdm_dev->spdm_context = %p\n", spdm_dev->spdm_context);
        SPDM_DEBUG("spdm_dev->spdm_context - spdm_dev = %p\n", spdm_dev->spdm_context - (void *)spdm_dev);
        SPDM_DEBUG("container_of((void **)spdm_dev->spdm_context, SpdmDev, spdm_context) = %p\n", container_of((void **)spdm_dev->spdm_context, SpdmDev, spdm_context));
        SPDM_DEBUG("container_of(spdm_dev->spdm_context, SpdmDev, spdm_context) = %p\n", container_of(spdm_dev->spdm_context, SpdmDev, spdm_context));
        SPDM_DEBUG("libspdm_get_context_size = 0x%x\n", libspdm_get_context_size());
        SPDM_DEBUG("sizeof libspdm_context_t = 0x%x\n", sizeof(libspdm_context_t));
        SPDM_DEBUG("spdm_dev->context - libspdm_get_context_size = %p\n\n", (char *)spdm_dev->spdm_context - libspdm_get_context_size());
        //*/
        spdm_dev->sender_receiver_buffer = g_malloc0(LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE);
        status = libspdm_responder_dispatch_message(spdm_dev->spdm_context);

        /*
        if (((libspdm_context_t *)spdm_dev->spdm_context)->receiver_buffer == spdm_dev->sender_receiver_buffer) {
            SPDM_DEBUG("spdm_context->receiver_buffer == spdm_dev->sender_receiver_buffer\n\n");
        } else {
            SPDM_DEBUG("spdm_context->receiver_buffer != spdm_dev->sender_receiver_buffer\n\n");
        }

        if (spdm_dev->spdm_context == (libspdm_context_t *)spdm_dev->spdm_context) {
            SPDM_DEBUG("spdm_dev == spdm_dev->spdm_context\n\n");
        } else {
            SPDM_DEBUG("spdm_dev != spdm_dev->spdm_context\n\n");
        }
        //*/

        if (status == LIBSPDM_STATUS_SUCCESS) {
            vblk_spdm_connection_state_callback(
                spdm_dev->spdm_context, ((libspdm_context_t *)spdm_dev->spdm_context)->connection_info.connection_state);
        } else {
            error_report("LibSPDM error while dispatching message (%X).",
                         status);
            return NULL;
        }
    }

    return NULL;
}
//*/

void vblk_spdm_connection_state_callback(
    void *spdm_context, libspdm_connection_state_t connection_state)
{
    SpdmDev *spdm_dev = &vblk_spdm_dev;
    bool res;
    void *data;
    void *data1;
    size_t data_size;
    size_t data1_size;
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    void *hash;
    size_t hash_size;
    const uint8_t *root_cert;
    size_t root_cert_size;
    uint8_t index;
    spdm_version_number_t spdm_version;

    switch (connection_state) {
        case LIBSPDM_CONNECTION_STATE_NOT_STARTED:

            /* clear perserved state*/
            /*
             * TODO: implement negotiated file
             * */
            break;

        case LIBSPDM_CONNECTION_STATE_AFTER_VERSION:
            /*
             * TODO: implement this case
             */ 
            break;

        case LIBSPDM_CONNECTION_STATE_NEGOTIATED:

            if (spdm_dev->use_version == 0) {
                libspdm_zero_mem(&parameter, sizeof(parameter));
                parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;
                data_size = sizeof(spdm_version);
                libspdm_get_data(spdm_context, LIBSPDM_DATA_SPDM_VERSION, &parameter,
                                &spdm_version, &data_size);
                spdm_dev->use_version = spdm_version >> SPDM_VERSION_NUMBER_SHIFT_BIT;
            }

            /* Provision new content*/

            libspdm_zero_mem(&parameter, sizeof(parameter));
            parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;

            data_size = sizeof(data32);
            libspdm_get_data(spdm_context, LIBSPDM_DATA_MEASUREMENT_HASH_ALGO,
                            &parameter, &data32, &data_size);
            spdm_dev->use_measurement_hash_algo = data32;
            data_size = sizeof(data32);
            libspdm_get_data(spdm_context, LIBSPDM_DATA_BASE_ASYM_ALGO,
                            &parameter, &data32, &data_size);
            spdm_dev->use_asym_algo = data32;
            data_size = sizeof(data32);
            libspdm_get_data(spdm_context, LIBSPDM_DATA_BASE_HASH_ALGO,
                            &parameter, &data32, &data_size);
            spdm_dev->use_hash_algo = data32;
            data_size = sizeof(data16);
            libspdm_get_data(spdm_context, LIBSPDM_DATA_REQ_BASE_ASYM_ALG,
                            &parameter, &data16, &data_size);
            spdm_dev->use_req_asym_algo = data16;

            libspdm_zero_mem(&parameter, sizeof(parameter));
            parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
            data_size = sizeof(data32);
            libspdm_get_data(spdm_context, LIBSPDM_DATA_CAPABILITY_FLAGS, &parameter,
                            &data32, &data_size);

            if ((data32 & SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ALIAS_CERT_CAP) == 0) {
                res = libspdm_read_responder_public_certificate_chain(spdm_dev->use_hash_algo,
                                                                      spdm_dev->use_asym_algo,
                                                                      &data, &data_size,
                                                                      NULL, NULL);
            } else {
                res = libspdm_read_responder_public_certificate_chain_alias_cert(
                    spdm_dev->use_hash_algo,
                    spdm_dev->use_asym_algo,
                    &data, &data_size,
                    NULL, NULL);
            }

            res = libspdm_read_responder_public_certificate_chain_per_slot(
                    1,
                    spdm_dev->use_hash_algo,
                    spdm_dev->use_asym_algo,
                    &data1, &data1_size,
                    NULL, NULL);
            if (res) {
                libspdm_zero_mem(&parameter, sizeof(parameter));
                parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;

                for (index = 0; index < spdm_dev->use_slot_count; index++) {
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

            if (spdm_dev->use_req_asym_algo != 0) {
                if ((spdm_dev->use_responder_capability_flags &
                    SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PUB_KEY_ID_CAP) != 0) {
                    spdm_dev->use_slot_id = 0xFF;
                }
                if (spdm_dev->use_slot_id == 0xFF) {
                    res = libspdm_read_responder_public_key(spdm_dev->use_asym_algo, &data, &data_size);
                    if (res) {
                        libspdm_zero_mem(&parameter, sizeof(parameter));
                        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
                        libspdm_set_data(spdm_context,
                                        LIBSPDM_DATA_LOCAL_PUBLIC_KEY,
                                        &parameter, data, data_size);
                        /* Do not free it.*/
                    }
                    res = libspdm_read_requester_public_key(spdm_dev->use_req_asym_algo, &data, &data_size);
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
                        spdm_dev->use_hash_algo, spdm_dev->use_req_asym_algo, &data,
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
                    if (spdm_dev->use_slot_id == 0xFF) {
                        /* 0xFF slot is only allowed in */
                        spdm_dev->use_mut_auth = SPDM_KEY_EXCHANGE_RESPONSE_MUT_AUTH_REQUESTED;
                    }
                    data8 = spdm_dev->use_mut_auth;
                    parameter.additional_data[0] =
                        spdm_dev->use_slot_id; /* req_slot_id;*/
                    libspdm_set_data(spdm_context,
                                    LIBSPDM_DATA_MUT_AUTH_REQUESTED, &parameter,
                                    &data8, sizeof(data8));

                    data8 = spdm_dev->use_basic_mut_auth;
                    parameter.additional_data[0] =
                        spdm_dev->use_slot_id; /* req_slot_id;*/
                    libspdm_set_data(spdm_context,
                                    LIBSPDM_DATA_BASIC_MUT_AUTH_REQUESTED,
                                    &parameter, &data8, sizeof(data8));
                }
            }

            libspdm_zero_mem(&parameter, sizeof(parameter));
            parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
            data8 = 0;
            for (index = 0; index < spdm_dev->use_slot_count; index++) {
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

void vblk_spdm_fix_internal_seqno(libspdm_context_t *spdm_context, uint8_t *msg_buffer) {
    /* 
     * hack to fix out of order sequence numbers, considering 16-bit overflows
     * considering the "danger zone" += 1/4 of the whole range
     */
    const uint64_t WRAP_DANGER_OUT = 0x4000;
    const uint64_t WRAP_DANGER_IN  = 0xC000;
    static uint64_t remaining_bits = 0;
    static uint8_t in_danger = 0;
    static uint8_t wrapped = 0;

    libspdm_session_info_t *session_info = NULL;
    libspdm_secured_message_context_t *secured_message_context = NULL;
    if (spdm_context->transport_decode_message != libspdm_transport_mctp_decode_message) {
      error_report("Only MCTP is supported.\n");
      return;
    }
    /* get seqno within the packet */
    uint64_t seqno = 0;
    uint8_t seqno_size = libspdm_mctp_get_sequence_number(0, (uint8_t*)&seqno);

    /* TODO: maybe we should worry about endianess... */
    memcpy(&seqno, msg_buffer + sizeof(mctp_message_header_t) + sizeof(spdm_secured_message_a_data_header1_t), seqno_size);

    if ((seqno & 0xFFFF) == WRAP_DANGER_OUT) {
        wrapped = 0;
        in_danger = 0;
    }

    if ((seqno & 0xFFFF) >= WRAP_DANGER_IN) {
        in_danger = 1;
    }

    if ((seqno & 0xFFFF) == 0xFFFF) {
        remaining_bits += 0x10000;
        wrapped = 1;
    }

    seqno += remaining_bits;

    if (in_danger && !wrapped && ((seqno & 0xFFFF) < WRAP_DANGER_OUT)) {
        seqno += 0x10000;
    }
    if (in_danger && wrapped && ((seqno & 0xFFFF) >= WRAP_DANGER_IN)) {
        seqno -= 0x10000;
    }

    /* set seqno in all active sessions */
    for (int i = 0; i <= 3; i++) {
        if (spdm_context->session_info[i].session_id != INVALID_SESSION_ID) {
            session_info = libspdm_get_session_info_via_session_id(spdm_context, spdm_context->session_info[i].session_id);
            if (session_info) {
                secured_message_context = session_info->secured_message_context;
                secured_message_context->application_secret.request_data_sequence_number = seqno;
            }
        }
    }
}
