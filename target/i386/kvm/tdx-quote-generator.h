/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef QEMU_I386_TDX_QUOTE_GENERATOR_H
#define QEMU_I386_TDX_QUOTE_GENERATOR_H

#include "qom/object_interfaces.h"
#include "io/channel-socket.h"
#include "exec/hwaddr.h"

#define TDX_GET_QUOTE_STRUCTURE_VERSION         1ULL

#define TDX_VP_GET_QUOTE_SUCCESS                0ULL
#define TDX_VP_GET_QUOTE_IN_FLIGHT              (-1ULL)
#define TDX_VP_GET_QUOTE_ERROR                  0x8000000000000000ULL
#define TDX_VP_GET_QUOTE_QGS_UNAVAILABLE        0x8000000000000001ULL

/* Limit to avoid resource starvation. */
#define TDX_GET_QUOTE_MAX_BUF_LEN       (128 * 1024)
#define TDX_MAX_GET_QUOTE_REQUEST       16

#define TDX_GET_QUOTE_HDR_SIZE          24

/* Format of pages shared with guest. */
struct tdx_get_quote_header {
    /* Format version: must be 1 in little endian. */
    uint64_t structure_version;

    /*
     * GetQuote status code in little endian:
     *   Guest must set error_code to 0 to avoid information leak.
     *   Qemu sets this before interrupting guest.
     */
    uint64_t error_code;

    /*
     * in-message size in little endian: The message will follow this header.
     * The in-message will be send to QGS.
     */
    uint32_t in_len;

    /*
     * out-message size in little endian:
     * On request, out_len must be zero to avoid information leak.
     * On return, message size from QGS. Qemu overwrites this field.
     * The message will follows this header.  The in-message is overwritten.
     */
    uint32_t out_len;

    /*
     * Message buffer follows.
     * Guest sets message that will be send to QGS.  If out_len > in_len, guest
     * should zero remaining buffer to avoid information leak.
     * Qemu overwrites this buffer with a message returned from QGS.
     */
};

typedef struct TdxGenerateQuoteTask {
    hwaddr buf_gpa;
    hwaddr payload_gpa;
    uint64_t payload_len;

    char *send_data;
    uint64_t send_data_size;
    uint64_t send_data_sent;

    char *receive_buf;
    uint64_t receive_buf_received;

    uint64_t status_code;
    struct tdx_get_quote_header hdr;

    QIOChannelSocket *sioc;
    guint watch;
    QEMUTimer timer;

    void (*completion)(struct TdxGenerateQuoteTask *task);
    void *opaque;
} TdxGenerateQuoteTask;

void tdx_generate_quote(TdxGenerateQuoteTask *task, SocketAddress *qg_sock_addr);

#endif /* QEMU_I386_TDX_QUOTE_GENERATOR_H */
