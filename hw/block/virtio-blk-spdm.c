#include "qemu/osdep.h"

#include "standard-headers/linux/virtio_blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-blk-common.h"
#include "hw/virtio/virtio-blk-spdm.h"


libspdm_return_t vblk_spdm_acquire_buffer(void *context, void **msg_buf_ptr)
{
    SpdmDev *spdm_dev = container_of(context, SpdmDev, spdm_context);

    LIBSPDM_ASSERT(!spdm_dev->sender_receiver_buffer_acquired);
    *msg_buf_ptr = spdm_dev->sender_receiver_buffer;
    libspdm_zero_mem (spdm_dev->sender_receiver_buffer, sizeof(spdm_dev->sender_receiver_buffer));
    spdm_dev->sender_receiver_buffer_acquired = true;
    
    return LIBSPDM_STATUS_SUCCESS;
}

void vblk_spdm_release_buffer(void *context, const void* msg_buf_ptr)
{
    SpdmDev *spdm_dev = container_of(context, SpdmDev, spdm_context);

    LIBSPDM_ASSERT(spdm_dev->sender_receiver_buffer_acquired);
    LIBSPDM_ASSERT(msg_buf_ptr == spdm_dev->sender_receiver_buffer);
    g_free((void *)msg_buf_ptr);
    spdm_dev->sender_receiver_buffer_acquired = false;

    return;
}

//*
libspdm_return_t vblk_spdm_send_message(void *spdm_context,
                                        size_t response_size,
                                        const void *response,
                                        uint64_t timeout)
{
    SpdmDev *spdm_dev = container_of(spdm_context, SpdmDev, spdm_context);

    if (response_size > sizeof(spdm_dev->sender_receiver_buffer)) {
        error_report("response_size requested is bigger than buffer size.",
                     LIBSPDM_STATUS_BUFFER_TOO_SMALL);
        return LIBSPDM_STATUS_BUFFER_TOO_SMALL;
    }

    qemu_mutex_lock(spdm_dev->spdm_io_mutex);
    memcpy(spdm_dev->sender_receiver_buffer, response, response_size);
    qemu_mutex_unlock(spdm_dev->spdm_io_mutex);

    return LIBSPDM_STATUS_SUCCESS;
}


libspdm_return_t vblk_spdm_receive_message(void *spdm_context,
                                           size_t *request_size,
                                           void **request,
                                           uint64_t timeout)
{
    SpdmDev *spdm_dev = container_of(spdm_context, SpdmDev, spdm_context);

    return LIBSPDM_STATUS_SUCCESS;
}

//*/

static const SpdmIO vblk_spdm_io = {
    .spdm_device_send_message = vblk_spdm_send_message,
    .spdm_device_receive_message = vblk_spdm_receive_message,
};

static const SpdmBufferIO vblk_spdm_buffer_io = {
    .spdm_device_acquire_sender_buffer = vblk_spdm_acquire_buffer,
    .spdm_device_acquire_sender_buffer = vblk_spdm_acquire_buffer,
    .spdm_device_release_receiver_buffer = vblk_spdm_release_buffer,
    .spdm_device_release_receiver_buffer = vblk_spdm_release_buffer,
};

void *vblk_init_spdm_dev(VirtIOBlock *s)
{
    SpdmDev *spdm_dev = s->spdm_dev;
    
    spdm_dev->spdm_io = vblk_spdm_io;
    spdm_dev->spdm_buffer_io = vblk_spdm_buffer_io;
    
    spdm_dev->use_transport_layer = SOCKET_TRANSPORT_TYPE_MCTP;
    
    spdm_dev->use_version = SPDM_MESSAGE_VERSION_12;
    spdm_dev->use_secured_message_version = SECURED_SPDM_VERSION_11;
    
    spdm_dev->use_responder_capability_flags =
        (0 | SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CACHE_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PUB_KEY_ID_CAP */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHAL_CAP |
        // SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_NO_SIG | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_SIG */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_SIG | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_CAP_NO_SIG */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MEAS_FRESH_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ENCRYPT_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MAC_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_MUT_AUTH_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_EX_CAP |
        // SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP_RESPONDER | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP_RESPONDER_WITH_CONTEXT */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP_RESPONDER_WITH_CONTEXT | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PSK_CAP_RESPONDER */
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_ENCAP_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_HBEAT_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_KEY_UPD_CAP |
        SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_HANDSHAKE_IN_THE_CLEAR_CAP |
        // SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_PUB_KEY_ID_CAP | /* conflict with SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP */
        0);

    spdm_dev->use_capability_flags = 0;
    spdm_dev->use_basic_mut_auth = 1;

    spdm_dev-> use_mut_auth = 
        SPDM_KEY_EXCHANGE_RESPONSE_MUT_AUTH_REQUESTED_WITH_ENCAP_REQUEST;
    /*
    SPDM_CHALLENGE_REQUEST_NO_MEASUREMENT_SUMMARY_HASH,
    SPDM_CHALLENGE_REQUEST_TCB_COMPONENT_MEASUREMENT_HASH,
    SPDM_CHALLENGE_REQUEST_ALL_MEASUREMENTS_HASH
    */
    spdm_dev->use_measurement_summary_hash_type =
        SPDM_CHALLENGE_REQUEST_ALL_MEASUREMENTS_HASH;
    /*
    SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_TOTAL_NUMBER_OF_MEASUREMENTS, // one by one
    SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_ALL_MEASUREMENTS
    */
    spdm_dev->use_measurement_operation =
        SPDM_GET_MEASUREMENTS_REQUEST_MEASUREMENT_OPERATION_TOTAL_NUMBER_OF_MEASUREMENTS;
    spdm_dev->use_slot_id = 0;
    spdm_dev->use_slot_count = 3;

    /*
    SPDM_KEY_UPDATE_ACTION_REQUESTER
    SPDM_KEY_UPDATE_ACTION_RESPONDER
    SPDM_KEY_UPDATE_ACTION_ALL
    */
    spdm_dev->use_key_update_action = LIBSPDM_KEY_UPDATE_ACTION_MAX;

    spdm_dev->use_hash_algo;
    spdm_dev->use_measurement_hash_algo;
    spdm_dev->use_asym_algo;
    spdm_dev->use_req_asym_algo;

    /*
    SPDM_MEASUREMENT_BLOCK_HEADER_SPECIFICATION_DMTF,
    */
    spdm_dev->support_measurement_spec =
        SPDM_MEASUREMENT_SPECIFICATION_DMTF;
    /*
    SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA3_512,
    SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA3_384,
    SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA3_256,
    SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_512,
    SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_384,
    SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_256,
    SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_RAW_BIT_STREAM_ONLY,
    */
    spdm_dev->support_measurement_hash_algo =
        SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_512 |
        SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_384;
    /*
    SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_512,
    SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384,
    SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_256,
    */
    spdm_dev->support_hash_algo = SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_384 |
                    SPDM_ALGORITHMS_BASE_HASH_ALGO_TPM_ALG_SHA_256;
    /*
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P521,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P256,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_4096,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_4096,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_3072,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_3072,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_2048,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_2048,
    */
    spdm_dev->support_asym_algo =
    // SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_3072;
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P256;

    /*
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_4096,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_4096,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_3072,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_3072,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_2048,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_2048,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P521,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384,
    SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P256,
    */
    spdm_dev->support_req_asym_algo =
    // SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_ECDSA_ECC_NIST_P384;
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_3072 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSAPSS_2048 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_3072 |
        SPDM_ALGORITHMS_BASE_ASYM_ALGO_TPM_ALG_RSASSA_2048;

    /*
    SPDM_ALGORITHMS_DHE_NAMED_GROUP_FFDHE_4096,
    SPDM_ALGORITHMS_DHE_NAMED_GROUP_FFDHE_3072,
    SPDM_ALGORITHMS_DHE_NAMED_GROUP_FFDHE_2048,
    SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_521_R1,
    SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_384_R1,
    SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_256_R1,
    */
    spdm_dev->support_dhe_algo =
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_384_R1 |
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_SECP_256_R1 |
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_FFDHE_3072 |
        SPDM_ALGORITHMS_DHE_NAMED_GROUP_FFDHE_2048;
    /*
    SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_AES_256_GCM,
    SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_AES_128_GCM,
    SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_CHACHA20_POLY1305,
    */
    spdm_dev->support_aead_algo =
        SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_AES_256_GCM |
        SPDM_ALGORITHMS_AEAD_CIPHER_SUITE_CHACHA20_POLY1305;
    /*
    SPDM_ALGORITHMS_KEY_SCHEDULE_HMAC_HASH,
    */
    spdm_dev->support_key_schedule_algo = SPDM_ALGORITHMS_KEY_SCHEDULE_HMAC_HASH;


}
//*/
