/*
 * QEMU TDX Quote Generation Support
 *
 * Copyright (c) 2025 Intel Corporation
 *
 * Author:
 *      Xiaoyao Li <xiaoyao.li@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"

#include "tdx-quote-generator.h"

#define QGS_MSG_LIB_MAJOR_VER 1
#define QGS_MSG_LIB_MINOR_VER 1

typedef enum _qgs_msg_type_t {
    GET_QUOTE_REQ = 0,
    GET_QUOTE_RESP = 1,
    GET_COLLATERAL_REQ = 2,
    GET_COLLATERAL_RESP = 3,
    GET_PLATFORM_INFO_REQ = 4,
    GET_PLATFORM_INFO_RESP = 5,
    QGS_MSG_TYPE_MAX
} qgs_msg_type_t;

typedef struct _qgs_msg_header_t {
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t type;
    uint32_t size;              // size of the whole message, include this header, in byte
    uint32_t error_code;        // used in response only
} qgs_msg_header_t;

typedef struct _qgs_msg_get_quote_req_t {
    qgs_msg_header_t header;    // header.type = GET_QUOTE_REQ
    uint32_t report_size;       // cannot be 0
    uint32_t id_list_size;      // length of id_list, in byte, can be 0
} qgs_msg_get_quote_req_t;

typedef struct _qgs_msg_get_quote_resp_s {
    qgs_msg_header_t header;    // header.type = GET_QUOTE_RESP
    uint32_t selected_id_size;  // can be 0 in case only one id is sent in request
    uint32_t quote_size;        // length of quote_data, in byte
    uint8_t id_quote[];         // selected id followed by quote
} qgs_msg_get_quote_resp_t;

#define HEADER_SIZE 4

static uint32_t decode_header(const char *buf, size_t len) {
    if (len < HEADER_SIZE) {
        return 0;
    }
    uint32_t msg_size = 0;
    for (uint32_t i = 0; i < HEADER_SIZE; ++i) {
        msg_size = msg_size * 256 + (buf[i] & 0xFF);
    }
    return msg_size;
}

static void encode_header(char *buf, size_t len, uint32_t size) {
    assert(len >= HEADER_SIZE);
    buf[0] = ((size >> 24) & 0xFF);
    buf[1] = ((size >> 16) & 0xFF);
    buf[2] = ((size >> 8) & 0xFF);
    buf[3] = (size & 0xFF);
}

static void tdx_generate_quote_cleanup(TdxGenerateQuoteTask *task)
{
    timer_del(&task->timer);

    if (task->watch) {
        g_source_remove(task->watch);
    }
    qio_channel_close(QIO_CHANNEL(task->sioc), NULL);
    object_unref(OBJECT(task->sioc));

    task->completion(task);
}

static gboolean tdx_get_quote_read(QIOChannel *ioc, GIOCondition condition,
                                   gpointer opaque)
{
    TdxGenerateQuoteTask *task = opaque;
    Error *err = NULL;
    int ret;

    ret = qio_channel_read(ioc, task->receive_buf + task->receive_buf_received,
                           task->payload_len - task->receive_buf_received, &err);
    if (ret < 0) {
        if (ret == QIO_CHANNEL_ERR_BLOCK) {
            return G_SOURCE_CONTINUE;
        } else {
            error_report_err(err);
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }
    }

    if (ret == 0) {
        error_report("End of file before reply received");
        task->status_code = TDX_VP_GET_QUOTE_ERROR;
        goto end;
    }

    task->receive_buf_received += ret;
    if (task->receive_buf_received >= HEADER_SIZE) {
        uint32_t len = decode_header(task->receive_buf,
                                     task->receive_buf_received);
        if (len == 0 ||
            len > (task->payload_len - HEADER_SIZE)) {
            error_report("Message len %u must be non-zero & less than %zu",
                         len, (task->payload_len - HEADER_SIZE));
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }

        /* Now we know the size, shrink to fit */
        task->payload_len = HEADER_SIZE + len;
        task->receive_buf = g_renew(char,
                                    task->receive_buf,
                                    task->payload_len);
    }

    if (task->receive_buf_received >= (sizeof(qgs_msg_header_t) + HEADER_SIZE)) {
        qgs_msg_header_t *hdr = (qgs_msg_header_t *)(task->receive_buf + HEADER_SIZE);
        if (hdr->major_version != QGS_MSG_LIB_MAJOR_VER ||
            hdr->minor_version != QGS_MSG_LIB_MINOR_VER) {
            error_report("Invalid QGS message header version %d.%d",
                         hdr->major_version,
                         hdr->minor_version);
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }
        if (hdr->type != GET_QUOTE_RESP) {
            error_report("Invalid QGS message type %d",
                         hdr->type);
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }
        if (hdr->size > (task->payload_len - HEADER_SIZE)) {
            error_report("QGS message size %d exceeds payload capacity %zu",
                         hdr->size, task->payload_len);
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }
        if (hdr->error_code != 0) {
            error_report("QGS message error code %d",
                         hdr->error_code);
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }
    }
    if (task->receive_buf_received >= (sizeof(qgs_msg_get_quote_resp_t) + HEADER_SIZE)) {
        qgs_msg_get_quote_resp_t *msg = (qgs_msg_get_quote_resp_t *)(task->receive_buf + HEADER_SIZE);
        if (msg->selected_id_size != 0) {
            error_report("QGS message selected ID was %d not 0",
                         msg->selected_id_size);
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }

        if ((task->payload_len - HEADER_SIZE - sizeof(qgs_msg_get_quote_resp_t)) !=
            msg->quote_size) {
            error_report("QGS quote size %d should be %zu",
                         msg->quote_size,
                         (task->payload_len - sizeof(qgs_msg_get_quote_resp_t)));
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            goto end;
        }
    }

    if (task->receive_buf_received == task->payload_len) {
        size_t strip = HEADER_SIZE + sizeof(qgs_msg_get_quote_resp_t);
        memmove(task->receive_buf,
                task->receive_buf + strip,
                task->receive_buf_received - strip);
        task->receive_buf_received -= strip;
        task->status_code = TDX_VP_GET_QUOTE_SUCCESS;
        goto end;
    }

    return G_SOURCE_CONTINUE;

end:
    tdx_generate_quote_cleanup(task);
    return G_SOURCE_REMOVE;
}

static gboolean tdx_send_report(QIOChannel *ioc, GIOCondition condition,
                                gpointer opaque)
{
    TdxGenerateQuoteTask *task = opaque;
    Error *err = NULL;
    int ret;

    ret = qio_channel_write(ioc, task->send_data + task->send_data_sent,
                            task->send_data_size - task->send_data_sent, &err);
    if (ret < 0) {
        if (ret == QIO_CHANNEL_ERR_BLOCK) {
            ret = 0;
        } else {
            error_report_err(err);
            task->status_code = TDX_VP_GET_QUOTE_ERROR;
            tdx_generate_quote_cleanup(task);
            goto end;
        }
    }
    task->send_data_sent += ret;

    if (task->send_data_sent == task->send_data_size) {
        task->watch = qio_channel_add_watch(QIO_CHANNEL(task->sioc), G_IO_IN,
                                            tdx_get_quote_read, task, NULL);
        goto end;
    }

    return G_SOURCE_CONTINUE;

end:
    return G_SOURCE_REMOVE;
}

static void tdx_quote_generator_connected(QIOTask *qio_task, gpointer opaque)
{
    TdxGenerateQuoteTask *task = opaque;
    Error *err = NULL;
    int ret;

    ret = qio_task_propagate_error(qio_task, &err);
    if (ret) {
        error_report_err(err);
        task->status_code = TDX_VP_GET_QUOTE_QGS_UNAVAILABLE;
        tdx_generate_quote_cleanup(task);
        return;
    }

    task->watch = qio_channel_add_watch(QIO_CHANNEL(task->sioc), G_IO_OUT,
                                        tdx_send_report, task, NULL);
}

#define TRANSACTION_TIMEOUT 30000

static void getquote_expired(void *opaque)
{
    TdxGenerateQuoteTask *task = opaque;

    task->status_code = TDX_VP_GET_QUOTE_ERROR;
    tdx_generate_quote_cleanup(task);
}

static void setup_get_quote_timer(TdxGenerateQuoteTask *task)
{
    int64_t time;

    timer_init_ms(&task->timer, QEMU_CLOCK_VIRTUAL, getquote_expired, task);
    time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    timer_mod(&task->timer, time + TRANSACTION_TIMEOUT);
}

void tdx_generate_quote(TdxGenerateQuoteTask *task,
                        SocketAddress *qg_sock_addr)
{
    QIOChannelSocket *sioc;
    qgs_msg_get_quote_req_t msg;

    /* Prepare a QGS message prelude */
    msg.header.major_version = QGS_MSG_LIB_MAJOR_VER;
    msg.header.minor_version = QGS_MSG_LIB_MINOR_VER;
    msg.header.type = GET_QUOTE_REQ;
    msg.header.size = sizeof(msg) + task->send_data_size;
    msg.header.error_code = 0;
    msg.report_size = task->send_data_size;
    msg.id_list_size = 0;

    /* Make room to add the QGS message prelude */
    task->send_data = g_renew(char,
                              task->send_data,
                              task->send_data_size + sizeof(msg) + HEADER_SIZE);
    memmove(task->send_data + sizeof(msg) + HEADER_SIZE,
            task->send_data,
            task->send_data_size);
    memcpy(task->send_data + HEADER_SIZE,
           &msg,
           sizeof(msg));
    encode_header(task->send_data, HEADER_SIZE, task->send_data_size + sizeof(msg));
    task->send_data_size += sizeof(msg) + HEADER_SIZE;

    sioc = qio_channel_socket_new();
    task->sioc = sioc;

    setup_get_quote_timer(task);

    qio_channel_socket_connect_async(sioc, qg_sock_addr,
                                     tdx_quote_generator_connected, task,
                                     NULL, NULL);
}
