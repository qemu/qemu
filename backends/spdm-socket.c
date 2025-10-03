/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * QEMU SPDM socket support
 *
 * This is based on:
 * https://github.com/DMTF/spdm-emu/blob/07c0a838bcc1c6207c656ac75885c0603e344b6f/spdm_emu/spdm_emu_common/command.c
 * but has been re-written to match QEMU style
 *
 * Copyright (c) 2021, DMTF. All rights reserved.
 * Copyright (c) 2023. Western Digital Corporation or its affiliates.
 */

#include "qemu/osdep.h"
#include "system/spdm-socket.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/core/qdev-prop-internal.h"

static bool read_bytes(const int socket, uint8_t *buffer,
                       size_t number_of_bytes)
{
    ssize_t number_received = 0;
    ssize_t result;

    while (number_received < number_of_bytes) {
        result = recv(socket, buffer + number_received,
                      number_of_bytes - number_received, 0);
        if (result <= 0) {
            return false;
        }
        number_received += result;
    }
    return true;
}

static bool read_data32(const int socket, uint32_t *data)
{
    bool result;

    result = read_bytes(socket, (uint8_t *)data, sizeof(uint32_t));
    if (!result) {
        return result;
    }
    *data = ntohl(*data);
    return true;
}

static bool read_multiple_bytes(const int socket, uint8_t *buffer,
                                uint32_t *bytes_received,
                                uint32_t max_buffer_length)
{
    uint32_t length;
    bool result;

    result = read_data32(socket, &length);
    if (!result) {
        return result;
    }

    if (length > max_buffer_length) {
        return false;
    }

    if (bytes_received) {
        *bytes_received = length;
    }

    if (length == 0) {
        return true;
    }

    return read_bytes(socket, buffer, length);
}

static bool receive_platform_data(const int socket,
                                  uint32_t transport_type,
                                  uint32_t *command,
                                  uint8_t *receive_buffer,
                                  uint32_t *bytes_to_receive)
{
    bool result;
    uint32_t response;
    uint32_t bytes_received;

    result = read_data32(socket, &response);
    if (!result) {
        return result;
    }
    *command = response;

    result = read_data32(socket, &transport_type);
    if (!result) {
        return result;
    }

    bytes_received = 0;
    result = read_multiple_bytes(socket, receive_buffer, &bytes_received,
                                 *bytes_to_receive);
    if (!result) {
        return result;
    }
    *bytes_to_receive = bytes_received;

    return result;
}

static bool write_bytes(const int socket, const uint8_t *buffer,
                        uint32_t number_of_bytes)
{
    ssize_t number_sent = 0;
    ssize_t result;

    while (number_sent < number_of_bytes) {
        result = send(socket, buffer + number_sent,
                      number_of_bytes - number_sent, 0);
        if (result == -1) {
            return false;
        }
        number_sent += result;
    }
    return true;
}

static bool write_data32(const int socket, uint32_t data)
{
    data = htonl(data);
    return write_bytes(socket, (uint8_t *)&data, sizeof(uint32_t));
}

static bool write_multiple_bytes(const int socket, const uint8_t *buffer,
                                 uint32_t bytes_to_send)
{
    bool result;

    result = write_data32(socket, bytes_to_send);
    if (!result) {
        return result;
    }

    return write_bytes(socket, buffer, bytes_to_send);
}

static bool send_platform_data(const int socket,
                               uint32_t transport_type, uint32_t command,
                               const uint8_t *send_buffer, size_t bytes_to_send)
{
    bool result;

    result = write_data32(socket, command);
    if (!result) {
        return result;
    }

    result = write_data32(socket, transport_type);
    if (!result) {
        return result;
    }

    return write_multiple_bytes(socket, send_buffer, bytes_to_send);
}

int spdm_socket_connect(uint16_t port, Error **errp)
{
    int client_socket;
    struct sockaddr_in server_addr;

    client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket < 0) {
        error_setg(errp, "cannot create socket: %s", strerror(errno));
        return -1;
    }

    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(port);


    if (connect(client_socket, (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0) {
        error_setg(errp, "cannot connect: %s", strerror(errno));
        close(client_socket);
        return -1;
    }

    return client_socket;
}

static bool spdm_socket_command_valid(uint32_t command)
{
    switch (command) {
    case SPDM_SOCKET_COMMAND_NORMAL:
    case SPDM_SOCKET_STORAGE_CMD_IF_SEND:
    case SPDM_SOCKET_STORAGE_CMD_IF_RECV:
    case SOCKET_SPDM_STORAGE_ACK_STATUS:
    case SPDM_SOCKET_COMMAND_OOB_ENCAP_KEY_UPDATE:
    case SPDM_SOCKET_COMMAND_CONTINUE:
    case SPDM_SOCKET_COMMAND_SHUTDOWN:
    case SPDM_SOCKET_COMMAND_UNKOWN:
    case SPDM_SOCKET_COMMAND_TEST:
        return true;
    default:
        return false;
    }
}

uint32_t spdm_socket_receive(const int socket, uint32_t transport_type,
                             void *rsp, uint32_t rsp_len)
{
    uint32_t command;
    bool result;

    result = receive_platform_data(socket, transport_type, &command,
                                   (uint8_t *)rsp, &rsp_len);

    /* we may have received some data, but check if the command is valid */
    if (!result || !spdm_socket_command_valid(command)) {
        return 0;
    }

    return rsp_len;
}

bool spdm_socket_send(const int socket, uint32_t socket_cmd,
                      uint32_t transport_type, void *req, uint32_t req_len)
{
    return send_platform_data(socket, transport_type, socket_cmd, req,
                              req_len);
}

uint32_t spdm_socket_rsp(const int socket, uint32_t transport_type,
                         void *req, uint32_t req_len,
                         void *rsp, uint32_t rsp_len)
{
    bool result;

    result = spdm_socket_send(socket, SPDM_SOCKET_COMMAND_NORMAL,
                              transport_type, req, req_len);
    if (!result) {
        return 0;
    }

    return spdm_socket_receive(socket, transport_type, rsp, rsp_len);
}

void spdm_socket_close(const int socket, uint32_t transport_type)
{
    send_platform_data(socket, transport_type,
                       SPDM_SOCKET_COMMAND_SHUTDOWN, NULL, 0);
}

const QEnumLookup SpdmTransport_lookup = {
    .array = (const char *const[]) {
        [SPDM_SOCKET_TRANSPORT_TYPE_UNSPEC] = "unspecified",
        [SPDM_SOCKET_TRANSPORT_TYPE_MCTP] = "mctp",
        [SPDM_SOCKET_TRANSPORT_TYPE_PCI_DOE] = "doe",
        [SPDM_SOCKET_TRANSPORT_TYPE_SCSI] = "scsi",
        [SPDM_SOCKET_TRANSPORT_TYPE_NVME] = "nvme",
    },
    .size = SPDM_SOCKET_TRANSPORT_TYPE_MAX
};

const PropertyInfo qdev_prop_spdm_trans = {
    .type = "SpdmTransportType",
    .description = "Spdm Transport, doe/nvme/mctp/scsi/unspecified",
    .enum_table = &SpdmTransport_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};
