/*
 * QEMU SPDM socket support
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef SPDM_REQUESTER_H
#define SPDM_REQUESTER_H

/**
 * spdm_socket_connect: connect to an external SPDM socket
 * @port: port to connect to
 * @errp: error object handle
 *
 * This will connect to an external SPDM socket server. On error
 * it will return -1 and errp will be set. On success this function
 * will return the socket number.
 */
int spdm_socket_connect(uint16_t port, Error **errp);

/**
 * spdm_socket_rsp: send and receive a message to a SPDM server
 * @socket: socket returned from spdm_socket_connect()
 * @transport_type: SPDM_SOCKET_TRANSPORT_TYPE_* macro
 * @req: request buffer
 * @req_len: request buffer length
 * @rsp: response buffer
 * @rsp_len: response buffer length
 *
 * Send platform data to a SPDM server on socket and then receive
 * a response.
 */
uint32_t spdm_socket_rsp(const int socket, uint32_t transport_type,
                         void *req, uint32_t req_len,
                         void *rsp, uint32_t rsp_len);

/**
 * spdm_socket_rsp: Receive a message from an SPDM server
 * @socket: socket returned from spdm_socket_connect()
 * @transport_type: SPDM_SOCKET_TRANSPORT_TYPE_* macro
 * @rsp: response buffer
 * @rsp_len: response buffer length
 *
 * Receives a message from the SPDM server and returns the number of bytes
 * received or 0 on failure. This can be used to receive a message from the SPDM
 * server without sending anything first.
 */
uint32_t spdm_socket_receive(const int socket, uint32_t transport_type,
                             void *rsp, uint32_t rsp_len);

/**
 * spdm_socket_rsp: Sends a message to an SPDM server
 * @socket: socket returned from spdm_socket_connect()
 * @socket_cmd: socket command type (normal/if_recv/if_send etc...)
 * @transport_type: SPDM_SOCKET_TRANSPORT_TYPE_* macro
 * @req: request buffer
 * @req_len: request buffer length
 *
 * Sends platform data to a SPDM server on socket, returns true on success.
 * The response from the server must then be fetched by using
 * spdm_socket_receive().
 */
bool spdm_socket_send(const int socket, uint32_t socket_cmd,
                      uint32_t transport_type, void *req, uint32_t req_len);

/**
 * spdm_socket_close: send a shutdown command to the server
 * @socket: socket returned from spdm_socket_connect()
 * @transport_type: SPDM_SOCKET_TRANSPORT_TYPE_* macro
 *
 * This will issue a shutdown command to the server.
 */
void spdm_socket_close(const int socket, uint32_t transport_type);

/*
 * Defines the transport encoding for SPDM, this information shall be passed
 * down to the SPDM server, when conforming to the SPDM over Storage standard
 * as defined by DSP0286.
 */
typedef struct {
    uint8_t security_protocol;              /* Must be 0xE8 for SPDM Commands
                                               as per SCSI Primary Commands 5 */
    uint16_t security_protocol_specific;    /* Bit[7:2] SPDM Operation
                                               Bit[0:1] Connection ID
                                               per DSP0286 1.0: Section 7.2 */
    uint32_t length;                        /* Length of the SPDM Message*/
} QEMU_PACKED StorageSpdmTransportHeader;

#define SPDM_SOCKET_COMMAND_NORMAL                0x0001
#define SPDM_SOCKET_STORAGE_CMD_IF_SEND           0x0002
#define SPDM_SOCKET_STORAGE_CMD_IF_RECV           0x0003
#define SOCKET_SPDM_STORAGE_ACK_STATUS            0x0004
#define SPDM_SOCKET_COMMAND_OOB_ENCAP_KEY_UPDATE  0x8001
#define SPDM_SOCKET_COMMAND_CONTINUE              0xFFFD
#define SPDM_SOCKET_COMMAND_SHUTDOWN              0xFFFE
#define SPDM_SOCKET_COMMAND_UNKOWN                0xFFFF
#define SPDM_SOCKET_COMMAND_TEST                  0xDEAD

#define SPDM_SOCKET_MAX_MESSAGE_BUFFER_SIZE       0x1200
#define SPDM_SOCKET_MAX_MSG_STATUS_LEN            0x02

typedef enum SpdmTransportType {
    SPDM_SOCKET_TRANSPORT_TYPE_UNSPEC = 0,
    SPDM_SOCKET_TRANSPORT_TYPE_MCTP,
    SPDM_SOCKET_TRANSPORT_TYPE_PCI_DOE,
    SPDM_SOCKET_TRANSPORT_TYPE_SCSI,
    SPDM_SOCKET_TRANSPORT_TYPE_NVME,
    SPDM_SOCKET_TRANSPORT_TYPE_MAX
} SpdmTransportType;

extern const PropertyInfo qdev_prop_spdm_trans;

#define DEFINE_PROP_SPDM_TRANS(_name, _state, _field, _default) \
    DEFINE_PROP_UNSIGNED(_name, _state, _field, _default, \
                         qdev_prop_spdm_trans, SpdmTransportType)

#endif
