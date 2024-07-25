/**
 *  Author: 
 *      htafr
 **/

#ifndef QEMU_SPDM_H
#define QEMU_SPDM_H

#include <glib.h>
#include "hal/base.h"
#include "hal/library/memlib.h"
#include "industry_standard/spdm.h"
#include "library/spdm_responder_lib.h"
#include "library/spdm_requester_lib.h"
#include "library/spdm_transport_mctp_lib.h"
#include "library/spdm_transport_pcidoe_lib.h"

#define SOCKET_TRANSPORT_TYPE_NONE 0x00
#define SOCKET_TRANSPORT_TYPE_MCTP 0x01
#define SOCKET_TRANSPORT_TYPE_PCI_DOE 0x02

#define EXE_MODE_SHUTDOWN 0
#define EXE_MODE_CONTINUE 1

#define EXE_CONNECTION_VERSION_ONLY 0x1
#define EXE_CONNECTION_DIGEST 0x2
#define EXE_CONNECTION_CERT 0x4
#define EXE_CONNECTION_CHAL 0x8
#define EXE_CONNECTION_MEAS 0x10
#define EXE_CONNECTION_SET_CERT 0x20
#define EXE_CONNECTION_GET_CSR 0x40
#define EXE_CONNECTION_MEL 0x80

#define EXE_SESSION_KEY_EX 0x1
#define EXE_SESSION_PSK 0x2
#define EXE_SESSION_NO_END 0x4
#define EXE_SESSION_KEY_UPDATE 0x8
#define EXE_SESSION_HEARTBEAT 0x10
#define EXE_SESSION_MEAS 0x20
#define EXE_SESSION_SET_CERT 0x40
#define EXE_SESSION_GET_CSR 0x80
#define EXE_SESSION_DIGEST 0x100
#define EXE_SESSION_CERT 0x200
#define EXE_SESSION_APP 0x400
#define EXE_SESSION_MEL 0x800

#define LIBSPDM_TRANSPORT_HEADER_SIZE 64
#define LIBSPDM_TRANSPORT_TAIL_SIZE 64

/* define common LIBSPDM_TRANSPORT_ADDITIONAL_SIZE. It should be the biggest one. */
#define LIBSPDM_TRANSPORT_ADDITIONAL_SIZE \
    (LIBSPDM_TRANSPORT_HEADER_SIZE + LIBSPDM_TRANSPORT_TAIL_SIZE)

#ifndef LIBSPDM_SENDER_BUFFER_SIZE
#define LIBSPDM_SENDER_BUFFER_SIZE (0x1100 + \
                                    LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#endif
#ifndef LIBSPDM_RECEIVER_BUFFER_SIZE
#define LIBSPDM_RECEIVER_BUFFER_SIZE (0x1200 + \
                                      LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#endif

/* Maximum size of a single SPDM message.
 * It matches DataTransferSize in SPDM specification. */
#define LIBSPDM_SENDER_DATA_TRANSFER_SIZE (LIBSPDM_SENDER_BUFFER_SIZE - \
                                           LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#define LIBSPDM_RECEIVER_DATA_TRANSFER_SIZE (LIBSPDM_RECEIVER_BUFFER_SIZE - \
                                             LIBSPDM_TRANSPORT_ADDITIONAL_SIZE)
#define LIBSPDM_DATA_TRANSFER_SIZE LIBSPDM_RECEIVER_DATA_TRANSFER_SIZE

#if (LIBSPDM_SENDER_BUFFER_SIZE > LIBSPDM_RECEIVER_BUFFER_SIZE)
#define LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE LIBSPDM_SENDER_BUFFER_SIZE
#else
#define LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE LIBSPDM_RECEIVER_BUFFER_SIZE
#endif

/* Maximum size of a large SPDM message.
 * If chunk is unsupported, it must be same as DATA_TRANSFER_SIZE.
 * If chunk is supported, it must be larger than DATA_TRANSFER_SIZE.
 * It matches MaxSPDMmsgSize in SPDM specification. */
#ifndef LIBSPDM_MAX_SPDM_MSG_SIZE
#define LIBSPDM_MAX_SPDM_MSG_SIZE 0x1200
#endif

#ifndef LIBSPDM_MAX_CSR_SIZE
#define LIBSPDM_MAX_CSR_SIZE 0xffff
#endif

typedef struct SpdmDev SpdmDev;
typedef struct SpdmIO SpdmIO;
typedef struct SpdmBufferIO SpdmBufferIO;

struct SpdmIO {
    libspdm_return_t (*spdm_device_send_message)(void *spdm_context,
                                                 size_t response_size,
                                                 const void *response,
                                                 uint64_t timeout);
    libspdm_return_t (*spdm_device_receive_message)(void *spdm_context,
                                                    size_t *request_size,
                                                    void **request,
                                                    uint64_t timeout);
};

struct SpdmBufferIO {
    libspdm_return_t (*spdm_device_acquire_sender_buffer)(void *context, void **msg_buffer_ptr);
    void (*spdm_device_release_sender_buffer)(void *context, const void *msg_buf_ptr);

    libspdm_return_t (*spdm_device_acquire_receiver_buffer)(void *context, void **msg_buf_ptr);
    void (*spdm_device_release_receiver_buffer)(void *context, const void *msg_buf_ptr);
};

struct SpdmDev {
    bool isResponder;
    bool isRequester;

    void *spdm_context;

    void *scratch_buffer;
    size_t scratch_buffer_size;

    void *requester_cert_chain_buffer;

    /* The developer can choose to use only a buffer or to separate them */
    uint8_t sender_buffer[LIBSPDM_SENDER_BUFFER_SIZE];
    uint8_t receiver_buffer[LIBSPDM_RECEIVER_BUFFER_SIZE];
    uint8_t sender_receiver_buffer[LIBSPDM_MAX_SENDER_RECEIVER_BUFFER_SIZE];
    bool sender_buffer_acquired;
    bool receiver_buffer_acquired;
    bool sender_receiver_buffer_acquired;

    uint32_t use_transport_layer;
    uint32_t use_tcp_handshake;
    uint8_t use_version;
    uint8_t use_secured_message_version;
    uint32_t use_requester_capability_flags;
    uint32_t use_responder_capability_flags;
    uint32_t use_capability_flags;
    uint32_t use_peer_capability_flags;

    uint8_t use_basic_mut_auth;
    uint8_t use_mut_auth;
    uint8_t use_measurement_summary_hash_type;
    uint8_t use_measurement_operation;
    uint8_t use_measurement_attribute;
    uint8_t use_slot_id;
    uint8_t use_slot_count;
    bool g_private_key_mode;

    libspdm_key_update_action_t use_key_update_action;

    uint32_t use_hash_algo;
    uint32_t use_measurement_hash_algo;
    uint32_t use_asym_algo;
    uint16_t use_req_asym_algo;

    uint8_t support_measurement_spec;
    uint8_t support_mel_spec;
    uint32_t support_measurement_hash_algo;
    uint32_t support_hash_algo;
    uint32_t support_asym_algo;
    uint16_t support_req_asym_algo;
    uint16_t support_dhe_algo;
    uint16_t support_aead_algo;
    uint16_t support_key_schedule_algo;
    uint8_t support_other_params_support;

    uint8_t session_policy;
    uint8_t end_session_attributes;

    char *load_state_file_name;
    char *save_state_file_name;

    uint32_t exe_mode;

    uint32_t exe_connection;

    uint32_t exe_session;
    
    /**/ 
    SpdmIO spdm_io;
    SpdmBufferIO spdm_buffer_io;

    libspdm_return_t (*spdm_get_response_vendor_defined_request)(
        void *spdm_context, const uint32_t *session_id, bool is_app_message,
        size_t request_size, const void *request, size_t *response_size,
        void *response);
};

void *spdm_responder_init(SpdmDev *spdm_dev);
void *spdm_requester_init(SpdmDev *spdm_dev);

#endif /* QEMU_SPDM_H */
