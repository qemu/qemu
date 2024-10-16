#include "sysemu/spdm.h"
#include "hw/pci/pci_ids.h"

extern SpdmDev nvme_spdm_dev;
extern DOEProtocol doe_spdm_prot[];

bool pcie_doe_spdm_rsp(DOECap *doe_cap);
libspdm_return_t nvme_spdm_acquire_buffer(void *context, void **msg_buf_ptr);
void nvme_spdm_release_buffer(void *context, const void *msg_buf_ptr);
libspdm_return_t nvme_spdm_send_message(void *context, size_t response_size,
                                        const void *response, uint64_t timeout);
libspdm_return_t nvme_spdm_receive_message(void *context, size_t *request_size,
                                           void **request, uint64_t timeout);
/**
 * Notify the session state to a session APP.
 *
 * @param  spdm_context                  A pointer to the SPDM context.
 * @param  session_id                    The session_id of a session.
 * @param  session_state                 The state of a session.
 **/
void nvme_spdm_server_session_state_callback(void *spdm_context,
                                             uint32_t session_id,
                                             libspdm_session_state_t session_state);

/**
 * Notify the connection state to an SPDM context register.
 *
 * @param  spdm_context                  A pointer to the SPDM context.
 * @param  connection_state              Indicate the SPDM connection state.
 **/
void nvme_spdm_server_connection_state_callback(
    void *spdm_context, libspdm_connection_state_t connection_state);
