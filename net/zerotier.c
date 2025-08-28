/*
 * QEMU System Emulator - ZeroTier network backend
 *
 * This provides a Layer 2 network backend that connects QEMU VMs to ZeroTier
 * virtual Ethernet networks using libzt (ZeroTier userland networking library).
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

#include "qemu/osdep.h"
#include "net/net.h"
#include "clients.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include <ZeroTierSockets.h>

#define ZEROTIER_DEFAULT_PORT 9994
#define ZEROTIER_MTU 2800

typedef struct ZeroTierState {
    NetClientState nc;
    uint64_t network_id;
    int port;
    char *storage_path;
    bool connected;
    bool network_ready;
    QEMUTimer *timer;
    uint8_t mac[6];
} ZeroTierState;

/* Global ZeroTier callbacks - we only support one instance for now */
static ZeroTierState *global_zt_state = NULL;

static void zerotier_poll(void *opaque);

/* ZeroTier virtual network frame callback */
static void zerotier_frame_handler(uint64_t network_id,
                                   uint64_t src_mac,
                                   uint64_t dest_mac,
                                   unsigned int ethertype,
                                   unsigned int vlan_id,
                                   const void *frame_data,
                                   unsigned int frame_len,
                                   void *user_ptr)
{
    ZeroTierState *s = (ZeroTierState *)user_ptr;
    uint8_t ethernet_frame[ZEROTIER_MTU];
    size_t eth_len = 0;
    
    /* Construct Ethernet frame */
    if (frame_len + 14 > sizeof(ethernet_frame)) {
        warn_report("ZeroTier: Frame too large (%u bytes)", frame_len + 14);
        return;
    }
    
    /* Destination MAC (6 bytes) */
    ethernet_frame[0] = (dest_mac >> 40) & 0xff;
    ethernet_frame[1] = (dest_mac >> 32) & 0xff;
    ethernet_frame[2] = (dest_mac >> 24) & 0xff;
    ethernet_frame[3] = (dest_mac >> 16) & 0xff;
    ethernet_frame[4] = (dest_mac >> 8) & 0xff;
    ethernet_frame[5] = dest_mac & 0xff;
    
    /* Source MAC (6 bytes) */
    ethernet_frame[6] = (src_mac >> 40) & 0xff;
    ethernet_frame[7] = (src_mac >> 32) & 0xff;
    ethernet_frame[8] = (src_mac >> 24) & 0xff;
    ethernet_frame[9] = (src_mac >> 16) & 0xff;
    ethernet_frame[10] = (src_mac >> 8) & 0xff;
    ethernet_frame[11] = src_mac & 0xff;
    
    /* EtherType (2 bytes) */
    ethernet_frame[12] = (ethertype >> 8) & 0xff;
    ethernet_frame[13] = ethertype & 0xff;
    
    /* Payload */
    memcpy(&ethernet_frame[14], frame_data, frame_len);
    eth_len = frame_len + 14;
    
    /* Send to QEMU */
    qemu_send_packet(&s->nc, ethernet_frame, eth_len);
}

static void zerotier_event_callback(enum zts_event_t event_code, void *user_ptr)
{
    ZeroTierState *s = (ZeroTierState *)user_ptr;
    
    switch (event_code) {
        case ZTS_EVENT_NODE_ONLINE:
            info_report("ZeroTier: Node online");
            break;
        case ZTS_EVENT_NODE_OFFLINE:
            warn_report("ZeroTier: Node offline");
            s->network_ready = false;
            break;
        case ZTS_EVENT_NETWORK_READY:
            info_report("ZeroTier: Network ready");
            s->network_ready = true;
            break;
        case ZTS_EVENT_NETWORK_DOWN:
            warn_report("ZeroTier: Network down");
            s->network_ready = false;
            break;
        default:
            break;
    }
}

static void zerotier_poll(void *opaque)
{
    ZeroTierState *s = opaque;
    
    /* Service ZeroTier events */
    zts_core_query_addr_count(s->network_id);
    
    /* Reschedule */
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 50);
}

/* Convert MAC address to uint64_t */
static uint64_t mac_to_uint64(const uint8_t *mac)
{
    return ((uint64_t)mac[0] << 40) |
           ((uint64_t)mac[1] << 32) |
           ((uint64_t)mac[2] << 24) |
           ((uint64_t)mac[3] << 16) |
           ((uint64_t)mac[4] << 8) |
           ((uint64_t)mac[5]);
}

static ssize_t zerotier_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    ZeroTierState *s = DO_UPCAST(ZeroTierState, nc, nc);
    
    if (!s->network_ready || size < 14) {
        return -1;
    }
    
    /* Extract Ethernet header */
    uint64_t dest_mac = mac_to_uint64(&buf[0]);
    uint64_t src_mac = mac_to_uint64(&buf[6]);
    uint16_t ethertype = (buf[12] << 8) | buf[13];
    
    /* Send frame via ZeroTier */
    int ret = zts_core_network_send_l2_frame(s->network_id,
                                           src_mac,
                                           dest_mac,
                                           ethertype,
                                           0, /* vlan_id */
                                           &buf[14], /* payload */
                                           size - 14); /* payload_len */
    
    if (ret < 0) {
        return -1;
    }
    
    return size;
}

static void zerotier_cleanup(NetClientState *nc)
{
    ZeroTierState *s = DO_UPCAST(ZeroTierState, nc, nc);
    
    if (s->timer) {
        timer_del(s->timer);
        timer_free(s->timer);
        s->timer = NULL;
    }
    
    if (s->connected) {
        zts_core_network_leave(s->network_id);
        zts_core_node_stop();
        s->connected = false;
    }
    
    if (global_zt_state == s) {
        global_zt_state = NULL;
    }
    
    g_free(s->storage_path);
}

static NetClientInfo net_zerotier_info = {
    .type = NET_CLIENT_DRIVER_ZEROTIER,
    .size = sizeof(ZeroTierState),
    .receive = zerotier_receive,
    .cleanup = zerotier_cleanup,
};

static int net_zerotier_init(NetClientState *peer, const char *model,
                             const char *name, const char *network,
                             int port, const char *storage, Error **errp)
{
    NetClientState *nc;
    ZeroTierState *s;
    char *endptr;
    uint64_t network_id;
    int ret;
    
    /* Only one ZeroTier instance supported for now */
    if (global_zt_state != NULL) {
        error_setg(errp, "Only one ZeroTier network backend supported");
        return -1;
    }
    
    /* Parse network ID (16 hex characters) */
    if (!network || strlen(network) != 16) {
        error_setg(errp, "ZeroTier network ID must be 16 hex characters");
        return -1;
    }
    
    network_id = strtoull(network, &endptr, 16);
    if (*endptr != '\0') {
        error_setg(errp, "Invalid ZeroTier network ID: %s", network);
        return -1;
    }
    
    /* Create network client */
    nc = qemu_new_net_client(&net_zerotier_info, peer, model, name);
    s = DO_UPCAST(ZeroTierState, nc, nc);
    
    s->network_id = network_id;
    s->port = port ? port : ZEROTIER_DEFAULT_PORT;
    s->storage_path = g_strdup(storage ? storage : "/tmp/qemu-zerotier");
    s->connected = false;
    s->network_ready = false;
    
    global_zt_state = s;
    
    qemu_set_info_str(nc, "network=%s,port=%d", network, s->port);
    
    /* Initialize ZeroTier core */
    info_report("ZeroTier: Starting node, storage: %s", s->storage_path);
    
    ret = zts_core_init_from_storage(s->storage_path);
    if (ret < 0) {
        error_setg(errp, "Failed to initialize ZeroTier core: %d", ret);
        goto error;
    }
    
    /* Set callbacks */
    zts_core_set_event_handler(zerotier_event_callback, s);
    zts_core_set_network_frame_handler(s->network_id, zerotier_frame_handler, s);
    
    /* Start the node */
    ret = zts_core_node_start();
    if (ret < 0) {
        error_setg(errp, "Failed to start ZeroTier node: %d", ret);
        goto error;
    }
    
    /* Join the network */
    info_report("ZeroTier: Joining network %016llx", 
                (unsigned long long)network_id);
    ret = zts_core_network_join(network_id);
    if (ret < 0) {
        error_setg(errp, "Failed to join ZeroTier network: %d", ret);
        goto error;
    }
    
    /* Get node MAC address for this network */
    uint8_t mac_addr[6];
    ret = zts_core_network_get_mac(network_id, mac_addr);
    if (ret < 0) {
        /* Generate MAC based on node ID */
        uint64_t node_id = zts_core_node_get_id();
        s->mac[0] = 0x02; /* Locally administered */
        s->mac[1] = (node_id >> 32) & 0xff;
        s->mac[2] = (node_id >> 24) & 0xff;
        s->mac[3] = (node_id >> 16) & 0xff;
        s->mac[4] = (node_id >> 8) & 0xff;
        s->mac[5] = node_id & 0xff;
    } else {
        memcpy(s->mac, mac_addr, 6);
    }
    
    qemu_format_nic_info_str(nc, s->mac);
    
    s->connected = true;
    
    /* Set up polling timer */
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, zerotier_poll, s);
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
    
    info_report("ZeroTier: Initialized for network %016llx", 
                (unsigned long long)network_id);
    
    return 0;
    
error:
    if (s->connected) {
        zts_core_node_stop();
    }
    global_zt_state = NULL;
    qemu_del_net_client(nc);
    return -1;
}

int net_init_zerotier(const Netdev *netdev, const char *name,
                      NetClientState *peer, Error **errp)
{
    const NetdevZeroTierOptions *zerotier;
    
    assert(netdev->type == NET_CLIENT_DRIVER_ZEROTIER);
    zerotier = &netdev->u.zerotier;
    
    return net_zerotier_init(peer, "zerotier", name,
                            zerotier->network,
                            zerotier->has_port ? zerotier->port : 0,
                            zerotier->has_storage ? zerotier->storage : NULL,
                            errp);
}