/*
 * QEMU Hyper-V Synthetic Debugging device
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/loader.h"
#include "exec/target_page.h"
#include "hw/hyperv/hyperv.h"
#include "hw/hyperv/vmbus-bridge.h"
#include "hw/hyperv/hyperv-proto.h"
#include "net/net.h"
#include "net/eth.h"
#include "net/checksum.h"
#include "trace.h"

#define TYPE_HV_SYNDBG       "hv-syndbg"

typedef struct HvSynDbg {
    DeviceState parent_obj;

    char *host_ip;
    uint16_t host_port;
    bool use_hcalls;

    uint32_t target_ip;
    struct sockaddr_in servaddr;
    int socket;
    bool has_data_pending;
    uint64_t pending_page_gpa;
} HvSynDbg;

#define HVSYNDBG(obj) OBJECT_CHECK(HvSynDbg, (obj), TYPE_HV_SYNDBG)

/* returns NULL unless there is exactly one HV Synth debug device */
static HvSynDbg *hv_syndbg_find(void)
{
    /* Returns NULL unless there is exactly one hvsd device */
    return HVSYNDBG(object_resolve_path_type("", TYPE_HV_SYNDBG, NULL));
}

static void set_pending_state(HvSynDbg *syndbg, bool has_pending)
{
    hwaddr out_len;
    void *out_data;

    syndbg->has_data_pending = has_pending;

    if (!syndbg->pending_page_gpa) {
        return;
    }

    out_len = 1;
    out_data = cpu_physical_memory_map(syndbg->pending_page_gpa, &out_len, 1);
    if (out_data) {
        *(uint8_t *)out_data = !!has_pending;
        cpu_physical_memory_unmap(out_data, out_len, 1, out_len);
    }
}

static bool get_udb_pkt_data(void *p, uint32_t len, uint32_t *data_ofs,
                             uint32_t *src_ip)
{
    uint32_t offset, curr_len = len;

    if (curr_len < sizeof(struct eth_header) ||
        (be16_to_cpu(PKT_GET_ETH_HDR(p)->h_proto) != ETH_P_IP)) {
        return false;
    }
    offset = sizeof(struct eth_header);
    curr_len -= sizeof(struct eth_header);

    if (curr_len < sizeof(struct ip_header) ||
        PKT_GET_IP_HDR(p)->ip_p != IP_PROTO_UDP) {
        return false;
    }
    offset += PKT_GET_IP_HDR_LEN(p);
    curr_len -= PKT_GET_IP_HDR_LEN(p);

    if (curr_len < sizeof(struct udp_header)) {
        return false;
    }

    offset += sizeof(struct udp_header);
    *data_ofs = offset;
    *src_ip = PKT_GET_IP_HDR(p)->ip_src;
    return true;
}

static uint16_t handle_send_msg(HvSynDbg *syndbg, uint64_t ingpa,
                                uint32_t count, bool is_raw,
                                uint32_t *pending_count)
{
    uint16_t ret;
    hwaddr data_len;
    void *debug_data = NULL;
    uint32_t udp_data_ofs = 0;
    const void *pkt_data;
    int sent_count;

    data_len = count;
    debug_data = cpu_physical_memory_map(ingpa, &data_len, 0);
    if (!debug_data || data_len < count) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    if (is_raw &&
        !get_udb_pkt_data(debug_data, count, &udp_data_ofs,
                          &syndbg->target_ip)) {
        ret = HV_STATUS_SUCCESS;
        goto cleanup;
    }

    pkt_data = (const void *)((uintptr_t)debug_data + udp_data_ofs);
    sent_count = sendto(syndbg->socket, pkt_data, count - udp_data_ofs,
                             MSG_NOSIGNAL, NULL, 0);
    if (sent_count == -1) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup;
    }

    *pending_count = count - (sent_count + udp_data_ofs);
    ret = HV_STATUS_SUCCESS;
cleanup:
    if (debug_data) {
        cpu_physical_memory_unmap(debug_data, count, 0, data_len);
    }

    return ret;
}

#define UDP_PKT_HEADER_SIZE \
    (sizeof(struct eth_header) + sizeof(struct ip_header) +\
     sizeof(struct udp_header))

static bool create_udp_pkt(HvSynDbg *syndbg, void *pkt, uint32_t pkt_len,
                           void *udp_data, uint32_t udp_data_len)
{
    struct udp_header *udp_part;

    if (pkt_len < (UDP_PKT_HEADER_SIZE + udp_data_len)) {
        return false;
    }

    /* Setup the eth */
    memset(&PKT_GET_ETH_HDR(pkt)->h_source, 0, ETH_ALEN);
    memset(&PKT_GET_ETH_HDR(pkt)->h_dest, 0, ETH_ALEN);
    PKT_GET_ETH_HDR(pkt)->h_proto = cpu_to_be16(ETH_P_IP);

    /* Setup the ip */
    PKT_GET_IP_HDR(pkt)->ip_ver_len =
        (4 << 4) | (sizeof(struct ip_header) >> 2);
    PKT_GET_IP_HDR(pkt)->ip_tos = 0;
    PKT_GET_IP_HDR(pkt)->ip_id = 0;
    PKT_GET_IP_HDR(pkt)->ip_off = 0;
    PKT_GET_IP_HDR(pkt)->ip_ttl = 64; /* IPDEFTTL */
    PKT_GET_IP_HDR(pkt)->ip_p = IP_PROTO_UDP;
    PKT_GET_IP_HDR(pkt)->ip_src = syndbg->servaddr.sin_addr.s_addr;
    PKT_GET_IP_HDR(pkt)->ip_dst = syndbg->target_ip;
    PKT_GET_IP_HDR(pkt)->ip_len =
        cpu_to_be16(sizeof(struct ip_header) + sizeof(struct udp_header) +
                    udp_data_len);
    eth_fix_ip4_checksum(PKT_GET_IP_HDR(pkt), PKT_GET_IP_HDR_LEN(pkt));

    udp_part = (struct udp_header *)((uintptr_t)pkt +
                                     sizeof(struct eth_header) +
                                     PKT_GET_IP_HDR_LEN(pkt));
    udp_part->uh_sport = syndbg->servaddr.sin_port;
    udp_part->uh_dport = syndbg->servaddr.sin_port;
    udp_part->uh_ulen = cpu_to_be16(sizeof(struct udp_header) + udp_data_len);
    memcpy(udp_part + 1, udp_data, udp_data_len);
    net_checksum_calculate(pkt, UDP_PKT_HEADER_SIZE + udp_data_len, CSUM_ALL);
    return true;
}

#define MSG_BUFSZ (4 * KiB)

static uint16_t handle_recv_msg(HvSynDbg *syndbg, uint64_t outgpa,
                                uint32_t count, bool is_raw, uint32_t options,
                                uint64_t timeout, uint32_t *retrieved_count)
{
    uint16_t ret;
    g_assert(MSG_BUFSZ >= qemu_target_page_size());
    QEMU_UNINITIALIZED uint8_t data_buf[MSG_BUFSZ];
    hwaddr out_len;
    void *out_data;
    ssize_t recv_byte_count;

    /* TODO: Handle options and timeout */
    (void)options;
    (void)timeout;

    if (!syndbg->has_data_pending) {
        recv_byte_count = 0;
    } else {
        recv_byte_count = recv(syndbg->socket, data_buf,
                               MIN(MSG_BUFSZ, count), MSG_WAITALL);
        if (recv_byte_count == -1) {
            return HV_STATUS_INVALID_PARAMETER;
        }
    }

    if (!recv_byte_count) {
        *retrieved_count = 0;
        return HV_STATUS_NO_DATA;
    }

    set_pending_state(syndbg, false);

    out_len = recv_byte_count;
    if (is_raw) {
        out_len += UDP_PKT_HEADER_SIZE;
    }
    out_data = cpu_physical_memory_map(outgpa, &out_len, 1);
    if (!out_data) {
        return HV_STATUS_INSUFFICIENT_MEMORY;
    }

    if (is_raw &&
        !create_udp_pkt(syndbg, out_data,
                        recv_byte_count + UDP_PKT_HEADER_SIZE,
                        data_buf, recv_byte_count)) {
        ret = HV_STATUS_INSUFFICIENT_MEMORY;
        goto cleanup_out_data;
    } else if (!is_raw) {
        memcpy(out_data, data_buf, recv_byte_count);
    }

    *retrieved_count = recv_byte_count;
    if (is_raw) {
        *retrieved_count += UDP_PKT_HEADER_SIZE;
    }
    ret = HV_STATUS_SUCCESS;

cleanup_out_data:
    cpu_physical_memory_unmap(out_data, out_len, 1, out_len);
    return ret;
}

static uint16_t hv_syndbg_handler(void *context, HvSynDbgMsg *msg)
{
    HvSynDbg *syndbg = context;
    uint16_t ret = HV_STATUS_INVALID_HYPERCALL_CODE;

    switch (msg->type) {
    case HV_SYNDBG_MSG_CONNECTION_INFO:
        msg->u.connection_info.host_ip =
            ntohl(syndbg->servaddr.sin_addr.s_addr);
        msg->u.connection_info.host_port =
            ntohs(syndbg->servaddr.sin_port);
        ret = HV_STATUS_SUCCESS;
        break;
    case HV_SYNDBG_MSG_SEND:
        ret = handle_send_msg(syndbg, msg->u.send.buf_gpa, msg->u.send.count,
                              msg->u.send.is_raw, &msg->u.send.pending_count);
        break;
    case HV_SYNDBG_MSG_RECV:
        ret = handle_recv_msg(syndbg, msg->u.recv.buf_gpa, msg->u.recv.count,
                              msg->u.recv.is_raw, msg->u.recv.options,
                              msg->u.recv.timeout,
                              &msg->u.recv.retrieved_count);
        break;
    case HV_SYNDBG_MSG_SET_PENDING_PAGE:
        syndbg->pending_page_gpa = msg->u.pending_page.buf_gpa;
        ret = HV_STATUS_SUCCESS;
        break;
    case HV_SYNDBG_MSG_QUERY_OPTIONS:
        msg->u.query_options.options = 0;
        if (syndbg->use_hcalls) {
            msg->u.query_options.options = HV_X64_SYNDBG_OPTION_USE_HCALLS;
        }
        ret = HV_STATUS_SUCCESS;
        break;
    default:
        break;
    }

    return ret;
}

static void hv_syndbg_recv_event(void *opaque)
{
    HvSynDbg *syndbg = opaque;
    struct timeval tv;
    fd_set rfds;

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(syndbg->socket, &rfds);
    if (select(syndbg->socket + 1, &rfds, NULL, NULL, &tv) > 0) {
        set_pending_state(syndbg, true);
    }
}

static void hv_syndbg_realize(DeviceState *dev, Error **errp)
{
    HvSynDbg *syndbg = HVSYNDBG(dev);

    if (!hv_syndbg_find()) {
        error_setg(errp, "at most one %s device is permitted", TYPE_HV_SYNDBG);
        return;
    }

    if (!vmbus_bridge_find()) {
        error_setg(errp, "%s device requires vmbus-bridge device",
                   TYPE_HV_SYNDBG);
        return;
    }

    /* Parse and host_ip */
    if (qemu_isdigit(syndbg->host_ip[0])) {
        syndbg->servaddr.sin_addr.s_addr = inet_addr(syndbg->host_ip);
    } else {
        struct hostent *he = gethostbyname(syndbg->host_ip);
        if (!he) {
            error_setg(errp, "%s failed to resolve host name %s",
                       TYPE_HV_SYNDBG, syndbg->host_ip);
            return;
        }
        syndbg->servaddr.sin_addr = *(struct in_addr *)he->h_addr;
    }

    syndbg->socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (syndbg->socket < 0) {
        error_setg(errp, "%s failed to create socket", TYPE_HV_SYNDBG);
        return;
    }

    if (!qemu_set_blocking(syndbg->socket, false, errp)) {
        return;
    }

    syndbg->servaddr.sin_port = htons(syndbg->host_port);
    syndbg->servaddr.sin_family = AF_INET;
    if (connect(syndbg->socket, (struct sockaddr *)&syndbg->servaddr,
                sizeof(syndbg->servaddr)) < 0) {
        close(syndbg->socket);
        error_setg(errp, "%s failed to connect to socket", TYPE_HV_SYNDBG);
        return;
    }

    syndbg->pending_page_gpa = 0;
    syndbg->has_data_pending = false;
    hyperv_set_syndbg_handler(hv_syndbg_handler, syndbg);
    qemu_set_fd_handler(syndbg->socket, hv_syndbg_recv_event, NULL, syndbg);
}

static void hv_syndbg_unrealize(DeviceState *dev)
{
    HvSynDbg *syndbg = HVSYNDBG(dev);

    if (syndbg->socket > 0) {
        qemu_set_fd_handler(syndbg->socket, NULL, NULL, NULL);
        close(syndbg->socket);
    }
}

static const VMStateDescription vmstate_hv_syndbg = {
    .name = TYPE_HV_SYNDBG,
    .unmigratable = 1,
};

static const Property hv_syndbg_properties[] = {
    DEFINE_PROP_STRING("host_ip", HvSynDbg, host_ip),
    DEFINE_PROP_UINT16("host_port", HvSynDbg, host_port, 50000),
    DEFINE_PROP_BOOL("use_hcalls", HvSynDbg, use_hcalls, false),
};

static void hv_syndbg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, hv_syndbg_properties);
    dc->fw_name = TYPE_HV_SYNDBG;
    dc->vmsd = &vmstate_hv_syndbg;
    dc->realize = hv_syndbg_realize;
    dc->unrealize = hv_syndbg_unrealize;
    dc->user_creatable = true;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo hv_syndbg_type_info = {
    .name = TYPE_HV_SYNDBG,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(HvSynDbg),
    .class_init = hv_syndbg_class_init,
};

static void hv_syndbg_register_types(void)
{
    type_register_static(&hv_syndbg_type_info);
}

type_init(hv_syndbg_register_types)
