/*
 * QEMU System Emulator - ZeroTier network backend
 *
 * This provides a Layer 2 network backend that connects QEMU VMs to ZeroTier
 * virtual Ethernet networks using ZeroTierOne core library.
 * Uses direct Layer 2 Ethernet frame access for pure tap functionality.
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
#include <ZeroTierOne.h>

#define ZEROTIER_DEFAULT_PORT 9993
#define ZEROTIER_MTU 2800

typedef struct ZeroTierState {
    NetClientState nc;
    ZT_Node *zt_node;
    uint64_t network_id;
    int port;
    char *storage_path;
    bool connected;
    bool network_ready;
    QEMUTimer *timer;
    uint8_t mac[6];        /* ZeroTier assigned MAC */
    uint8_t vm_mac[6];      /* VM's MAC address */
    int udp_sock;
} ZeroTierState;

static ZeroTierState *global_zt_state = NULL;

static void zerotier_poll(void *opaque);

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

/* ZeroTier callback: Send packets over physical wire */
static int zerotier_wire_packet_send(
    ZT_Node *node,
    void *uptr,
    void *tptr,
    int64_t localSocket,
    const struct sockaddr_storage *addr,
    const void *data,
    unsigned int len,
    unsigned int ttl)
{
    ZeroTierState *s = (ZeroTierState *)uptr;
    ssize_t sent;
    socklen_t addr_len;
    
    if (!s || s->udp_sock < 0) {
        return -1;
    }
    
    /* Determine address length */
    if (addr->ss_family == AF_INET) {
        addr_len = sizeof(struct sockaddr_in);
    } else if (addr->ss_family == AF_INET6) {
        addr_len = sizeof(struct sockaddr_in6);
    } else {
        return -1;
    }
    
    /* Send packet using the persistent UDP socket */
    sent = sendto(s->udp_sock, data, len, 0, (struct sockaddr*)addr, addr_len);
    
    return (sent == (ssize_t)len) ? 0 : -1;
}

/* ZeroTier callback: Handle frames FROM ZeroTier network TO QEMU */
static void zerotier_virtual_network_frame(
    ZT_Node *node,
    void *uptr,
    void *tptr,
    uint64_t nwid,
    void **nuptr,
    uint64_t sourceMac,
    uint64_t destMac,
    unsigned int etherType,
    unsigned int vlanId,
    const void *data,
    unsigned int len)
{
    ZeroTierState *s = (ZeroTierState *)uptr;
    uint8_t ethernet_frame[ZEROTIER_MTU];
    
    if (!s || !s->network_ready || nwid != s->network_id) {
        return;
    }
    
    /* Check if this frame is for us - accept broadcast, multicast, ZeroTier MAC, or VM MAC */
    uint64_t our_zt_mac = mac_to_uint64(s->mac);
    uint64_t our_vm_mac = mac_to_uint64(s->vm_mac);
    bool is_broadcast = (destMac == 0xFFFFFFFFFFFF);
    bool is_multicast = ((destMac >> 40) & 0x01) != 0;
    bool is_for_zt = (destMac == our_zt_mac);
    bool is_for_vm = (destMac == our_vm_mac);
    bool is_for_us = is_for_zt || is_for_vm;
    
    /* Log only ARP and new connections to reduce noise */
    if (etherType == 0x0806 || (is_for_vm && etherType != 0x0800)) {
        info_report("ZeroTier: RX %s from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x, len %u",
                    etherType == 0x0806 ? "ARP" : "frame",
                    (unsigned int)((sourceMac >> 40) & 0xff), (unsigned int)((sourceMac >> 32) & 0xff), (unsigned int)((sourceMac >> 24) & 0xff),
                    (unsigned int)((sourceMac >> 16) & 0xff), (unsigned int)((sourceMac >> 8) & 0xff), (unsigned int)(sourceMac & 0xff),
                    (unsigned int)((destMac >> 40) & 0xff), (unsigned int)((destMac >> 32) & 0xff), (unsigned int)((destMac >> 24) & 0xff),
                    (unsigned int)((destMac >> 16) & 0xff), (unsigned int)((destMac >> 8) & 0xff), (unsigned int)(destMac & 0xff),
                    len);
    }
    
    /* Only forward frames that are for us, broadcast, or multicast */
    if (!is_broadcast && !is_multicast && !is_for_us) {
        /* Don't log drops for regular traffic, too noisy */
        return;
    }
    
    if (len + 14 > sizeof(ethernet_frame)) {
        warn_report("ZeroTier: Frame too large (%u bytes)", len + 14);
        return;
    }
    
    /* Construct Ethernet frame for VM */
    /* For unicast to us (either ZeroTier MAC or VM MAC), use VM's MAC as destination */
    /* For broadcast/multicast, keep original destination */
    if ((is_for_zt || is_for_vm) && !is_broadcast && !is_multicast) {
        /* Use VM's MAC address as destination */
        memcpy(ethernet_frame, s->vm_mac, 6);
    } else {
        /* Keep original destination (broadcast/multicast) */
        ethernet_frame[0] = (destMac >> 40) & 0xff;
        ethernet_frame[1] = (destMac >> 32) & 0xff;
        ethernet_frame[2] = (destMac >> 24) & 0xff;
        ethernet_frame[3] = (destMac >> 16) & 0xff;
        ethernet_frame[4] = (destMac >> 8) & 0xff;
        ethernet_frame[5] = destMac & 0xff;
    }
    
    /* Source MAC (6 bytes) - keep original from ZeroTier network */
    ethernet_frame[6] = (sourceMac >> 40) & 0xff;
    ethernet_frame[7] = (sourceMac >> 32) & 0xff;
    ethernet_frame[8] = (sourceMac >> 24) & 0xff;
    ethernet_frame[9] = (sourceMac >> 16) & 0xff;
    ethernet_frame[10] = (sourceMac >> 8) & 0xff;
    ethernet_frame[11] = sourceMac & 0xff;
    
    /* EtherType (2 bytes) */
    ethernet_frame[12] = (etherType >> 8) & 0xff;
    ethernet_frame[13] = etherType & 0xff;
    
    /* Payload */
    memcpy(&ethernet_frame[14], data, len);
    
    /* Send to QEMU network stack */
    ssize_t sent = qemu_send_packet(&s->nc, ethernet_frame, len + 14);
    if (sent != (ssize_t)(len + 14)) {
        warn_report("ZeroTier: Failed to deliver full packet to VM! (%zd/%u bytes)", sent, len + 14);
    }
}

/* ZeroTier callback: Handle virtual network configuration */
static int zerotier_virtual_network_config(
    ZT_Node *node,
    void *uptr,
    void *tptr,
    uint64_t nwid,
    void **nuptr,
    enum ZT_VirtualNetworkConfigOperation op,
    const ZT_VirtualNetworkConfig *nwc)
{
    ZeroTierState *s = (ZeroTierState *)uptr;
    
    if (!s || nwid != s->network_id) {
        return 0;
    }
    
    switch (op) {
        case ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_UP:
            info_report("ZeroTier: Network %016llx is up", (unsigned long long)nwid);
            s->network_ready = true;
            
            /* Set MAC address from network config */
            if (nwc && nwc->mac) {
                s->mac[0] = (nwc->mac >> 40) & 0xff;
                s->mac[1] = (nwc->mac >> 32) & 0xff;
                s->mac[2] = (nwc->mac >> 24) & 0xff;
                s->mac[3] = (nwc->mac >> 16) & 0xff;
                s->mac[4] = (nwc->mac >> 8) & 0xff;
                s->mac[5] = nwc->mac & 0xff;
                
                /* Update QEMU's network client with the real MAC address */
                qemu_format_nic_info_str(&s->nc, s->mac);
                
                
                info_report("ZeroTier: MAC address updated: %02x:%02x:%02x:%02x:%02x:%02x",
                          s->mac[0], s->mac[1], s->mac[2], s->mac[3], s->mac[4], s->mac[5]);
            }
            break;
            
        case ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_DOWN:
            warn_report("ZeroTier: Network %016llx is down", (unsigned long long)nwid);
            s->network_ready = false;
            break;
            
        default:
            break;
    }
    return 0;
}

/* ZeroTier callback: Handle events */
static void zerotier_event_callback(
    ZT_Node *node,
    void *uptr,
    void *tptr,
    enum ZT_Event event,
    const void *metaData)
{
    ZeroTierState *s = (ZeroTierState *)uptr;
    
    switch (event) {
        case ZT_EVENT_ONLINE:
            info_report("ZeroTier: Node online");
            break;
        case ZT_EVENT_OFFLINE:
            warn_report("ZeroTier: Node offline");
            if (s) s->network_ready = false;
            break;
        default:
            break;
    }
}

/* ZeroTier state management functions */
static int zerotier_state_get(ZT_Node *node, void *uptr, void *tptr, enum ZT_StateObjectType type, const uint64_t objid[2], void *data, unsigned int maxlen)
{
    /* Simple file-based state storage */
    /* This could be enhanced with proper state management */
    return 0; /* Not found */
}

static void zerotier_state_put(ZT_Node *node, void *uptr, void *tptr, enum ZT_StateObjectType type, const uint64_t objid[2], const void *data, int len)
{
    /* Simple file-based state storage */
    /* This could be enhanced with proper state management */
}


/* QEMU network backend functions */
static ssize_t zerotier_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    ZeroTierState *s = DO_UPCAST(ZeroTierState, nc, nc);
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    volatile int64_t nextDeadline = 0;
    
    if (!s->network_ready || size < 14) {
        return -1;
    }
    
    /* Extract Ethernet header */
    uint64_t destMac = mac_to_uint64(&buf[0]);
    uint16_t etherType = (buf[12] << 8) | buf[13];
    
    /* Save the VM's MAC address on first packet */
    if (s->vm_mac[0] == 0 && s->vm_mac[1] == 0) {
        memcpy(s->vm_mac, &buf[6], 6);
        info_report("ZeroTier: Detected VM MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                    s->vm_mac[0], s->vm_mac[1], s->vm_mac[2],
                    s->vm_mac[3], s->vm_mac[4], s->vm_mac[5]);
    }
    
    /* Use ZeroTier's assigned MAC as source when sending to ZeroTier network */
    uint64_t zt_sourceMac = mac_to_uint64(s->mac);  /* Use ZeroTier's MAC */
    
    /* Debug outgoing packets - reduce noise for working ping */
    if (etherType == 0x0806) { /* Only log ARP for now */
        info_report("ZeroTier: TX ARP from VM %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x",
                    buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
                    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
        info_report("ZeroTier: Using ZT MAC %02x:%02x:%02x:%02x:%02x:%02x as source",
                    s->mac[0], s->mac[1], s->mac[2], s->mac[3], s->mac[4], s->mac[5]);
    }
    
    /* Send frame via ZeroTier with translated source MAC */
    enum ZT_ResultCode result = ZT_Node_processVirtualNetworkFrame(
        s->zt_node,
        NULL, /* tptr */
        now,
        s->network_id,
        zt_sourceMac,  /* Use ZeroTier's MAC as source */
        destMac,
        etherType,
        0, /* vlanId */
        &buf[14], /* payload */
        size - 14, /* payload length */
        &nextDeadline);
    
    if (result != ZT_RESULT_OK) {
        return -1;
    }
    
    return size;
}

static void zerotier_poll(void *opaque)
{
    ZeroTierState *s = opaque;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    volatile int64_t nextDeadline = 0;
    uint8_t buffer[ZEROTIER_MTU];
    struct sockaddr_storage from_addr;
    socklen_t from_len;
    ssize_t received;
    
    /* Check for incoming UDP packets */
    if (s->udp_sock >= 0) {
        from_len = sizeof(from_addr);
        received = recvfrom(s->udp_sock, buffer, sizeof(buffer), MSG_DONTWAIT, 
                           (struct sockaddr*)&from_addr, &from_len);
        if (received > 0) {
            /* Process incoming ZeroTier protocol packet */
            ZT_Node_processWirePacket(s->zt_node, NULL, now, -1, &from_addr, 
                                    buffer, received, &nextDeadline);
        }
    }
    
    /* Perform ZeroTier background tasks */
    ZT_Node_processBackgroundTasks(s->zt_node, NULL, now, &nextDeadline);
    
    /* Reschedule */
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 50);
}

static void zerotier_cleanup(NetClientState *nc)
{
    ZeroTierState *s = DO_UPCAST(ZeroTierState, nc, nc);
    
    if (s->timer) {
        timer_del(s->timer);
        timer_free(s->timer);
        s->timer = NULL;
    }
    
    if (s->udp_sock >= 0) {
        close(s->udp_sock);
        s->udp_sock = -1;
    }
    
    if (s->zt_node) {
        ZT_Node_delete(s->zt_node);
        s->zt_node = NULL;
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
    struct ZT_Node_Callbacks callbacks;
    
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
    s->zt_node = NULL;
    s->udp_sock = -1;
    memset(s->vm_mac, 0, 6);  /* Will be detected from first packet */
    
    global_zt_state = s;
    
    qemu_set_info_str(nc, "network=%s,port=%d", network, s->port);
    
    /* Initialize ZeroTier callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.version = 0;
    callbacks.stateGetFunction = zerotier_state_get;
    callbacks.statePutFunction = zerotier_state_put;
    callbacks.wirePacketSendFunction = zerotier_wire_packet_send;
    callbacks.virtualNetworkFrameFunction = zerotier_virtual_network_frame;
    callbacks.virtualNetworkConfigFunction = zerotier_virtual_network_config;
    callbacks.eventCallback = zerotier_event_callback;
    
    /* Create ZeroTier node */
    info_report("ZeroTier: Starting node, storage: %s", s->storage_path);
    
    enum ZT_ResultCode result = ZT_Node_new(&s->zt_node, s, NULL, &callbacks, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    if (result != ZT_RESULT_OK || !s->zt_node) {
        error_setg(errp, "Failed to create ZeroTier node: %d", result);
        goto error;
    }
    
    /* Join the network */
    info_report("ZeroTier: Joining network %016llx", (unsigned long long)network_id);
    result = ZT_Node_join(s->zt_node, network_id, s, NULL);
    if (result != ZT_RESULT_OK) {
        error_setg(errp, "Failed to join ZeroTier network: %d", result);
        goto error;
    }
    
    /* Generate initial MAC address (will be updated when network comes up) */
    s->mac[0] = 0x02; /* Locally administered */
    s->mac[1] = 0x00;
    s->mac[2] = 0x00;
    s->mac[3] = (network_id >> 16) & 0xff;
    s->mac[4] = (network_id >> 8) & 0xff;
    s->mac[5] = network_id & 0xff;
    
    qemu_format_nic_info_str(nc, s->mac);
    
    s->connected = true;
    
    /* Initialize UDP socket for ZeroTier protocol */
    s->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->udp_sock >= 0) {
        struct sockaddr_in bind_addr = {0};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        
        int opt = 1;
        setsockopt(s->udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        /* Try to bind to specified port, fall back to dynamic port */
        bind_addr.sin_port = htons(s->port);
        if (bind(s->udp_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            /* If binding to ZeroTier port fails, use dynamic port */
            bind_addr.sin_port = 0;
            if (bind(s->udp_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
                warn_report("ZeroTier: Failed to bind UDP socket");
                close(s->udp_sock);
                s->udp_sock = -1;
            } else {
                /* Get the actual port assigned */
                socklen_t addr_len = sizeof(bind_addr);
                getsockname(s->udp_sock, (struct sockaddr*)&bind_addr, &addr_len);
                s->port = ntohs(bind_addr.sin_port);
                info_report("ZeroTier: Bound UDP socket to dynamic port %d", s->port);
            }
        } else {
            info_report("ZeroTier: Bound UDP socket to port %d", s->port);
        }
    }
    
    /* Set up polling timer */
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, zerotier_poll, s);
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
    
    info_report("ZeroTier: Initialized for network %016llx", 
                (unsigned long long)network_id);
    
    return 0;
    
error:
    if (s->zt_node) {
        ZT_Node_delete(s->zt_node);
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
                            zerotier->storage,
                            errp);
}