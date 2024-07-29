/*
 * Utility functions to use LibSPDM
 */

#include "sysemu/spdm.h"

void *spdm_responder_init(SpdmDev *spdm_dev)
{
    SpdmIO *spdm_io = &spdm_dev->spdm_io;
    SpdmBufferIO *spdm_buffer_io = &spdm_dev->spdm_buffer_io;
    libspdm_data_parameter_t parameter;
    uint8_t data8;
    uint16_t data16;
    uint32_t data32;
    spdm_version_number_t spdm_version;

    /* Check if device is set to be responder */ 
    if (!spdm_dev->isResponder) {
        return NULL;
    }

    /*
     * Allocate and initialize the SPDM context, then the context 
     * data is configured using SpdmDev data
     */
    spdm_dev->spdm_context = g_malloc(libspdm_get_context_size());
    libspdm_init_context(spdm_dev->spdm_context);

    libspdm_register_device_io_func(spdm_dev->spdm_context,
                                    spdm_io->spdm_device_send_message,
                                    spdm_io->spdm_device_receive_message);

    if (spdm_dev->use_transport_layer == SOCKET_TRANSPORT_TYPE_MCTP) {
        libspdm_register_transport_layer_func(
            spdm_dev->spdm_context,
            LIBSPDM_MAX_SPDM_MSG_SIZE,
            LIBSPDM_TRANSPORT_HEADER_SIZE,
            LIBSPDM_TRANSPORT_TAIL_SIZE,
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
                                        spdm_buffer_io->spdm_device_acquire_sender_buffer,
                                        spdm_buffer_io->spdm_device_release_sender_buffer,
                                        spdm_buffer_io->spdm_device_acquire_receiver_buffer,
                                        spdm_buffer_io->spdm_device_release_receiver_buffer);

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

    if (spdm_dev->use_version != 0) {
        libspdm_zero_mem(&parameter, sizeof(parameter));
        parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
        spdm_version = spdm_dev->use_version << SPDM_VERSION_NUMBER_SHIFT_BIT;
        libspdm_set_data(spdm_dev->spdm_context, LIBSPDM_DATA_SPDM_VERSION,
                         &parameter, &spdm_version, sizeof(spdm_version));
    }

    libspdm_register_get_response_func(spdm_dev->spdm_context,
                                       spdm_dev->spdm_get_response_vendor_defined_request);

    return spdm_dev->spdm_context;
}
