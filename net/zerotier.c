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
#include "net/clients.h"
#include "clients.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include <ZeroTierOne.h>
#include <unistd.h>
#include <glib/gstdio.h>

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
    int udp_sock;
    bool mac_provided;      /* MAC has been provided to peer */
    bool initial_save_done; /* Reduce save spam */
    IOHandler *udp_read_handler; /* Event-driven UDP handler */
} ZeroTierState;

static ZeroTierState *global_zt_state = NULL;
static uint8_t zerotier_suggested_mac[6] = {0};  /* MAC for NIC to use */


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
    
    /* Check if this frame is for us - accept broadcast, multicast, or our ZeroTier MAC */
    uint64_t our_mac = mac_to_uint64(s->mac);
    bool is_broadcast = (destMac == 0xFFFFFFFFFFFF);
    bool is_multicast = ((destMac >> 40) & 0x01) != 0;
    bool is_for_us = (destMac == our_mac);
    
    /* Process ARP silently for better performance */
    
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
    /* No MAC translation needed - VM uses ZeroTier MAC */
    /* Destination MAC */
    ethernet_frame[0] = (destMac >> 40) & 0xff;
    ethernet_frame[1] = (destMac >> 32) & 0xff;
    ethernet_frame[2] = (destMac >> 24) & 0xff;
    ethernet_frame[3] = (destMac >> 16) & 0xff;
    ethernet_frame[4] = (destMac >> 8) & 0xff;
    ethernet_frame[5] = destMac & 0xff;
    
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
    
    /* Send to QEMU network stack using async mechanism like TAP driver */
    ssize_t sent = qemu_send_packet_async(&s->nc, ethernet_frame, len + 14, NULL);
    if (sent == 0) {
        /* 
         * Queue is full - packet will be queued and sent later.
         * QEMU's queue system handles this automatically.
         * No need to warn as this is normal flow control.
         */
    } else if (sent < 0) {
        /* Error occurred - but don't spam logs in production */
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
                
                /* Store MAC globally for NIC device to use */
                memcpy(zerotier_suggested_mac, s->mac, 6);
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
    ZeroTierState *s = (ZeroTierState *)uptr;
    char filename[512];
    FILE *f;
    size_t read_len;
    
    if (!s || !s->storage_path) {
        return -1;
    }
    
    /* Create filename based on object type and ID */
    if (type == ZT_STATE_OBJECT_IDENTITY_SECRET) {
        snprintf(filename, sizeof(filename), "%s/identity.secret", s->storage_path);
    } else if (type == ZT_STATE_OBJECT_IDENTITY_PUBLIC) {
        snprintf(filename, sizeof(filename), "%s/identity.public", s->storage_path);
    } else if (type == ZT_STATE_OBJECT_NETWORK_CONFIG) {
        snprintf(filename, sizeof(filename), "%s/networks.d/%016llx.conf",
                 s->storage_path, (unsigned long long)objid[0]);
    } else {
        snprintf(filename, sizeof(filename), "%s/object_%d_%016llx_%016llx.dat",
                 s->storage_path, type, (unsigned long long)objid[0], (unsigned long long)objid[1]);
    }
    
    f = fopen(filename, "rb");
    if (!f) {
        return -1; /* Not found */
    }
    
    read_len = fread(data, 1, maxlen, f);
    fclose(f);
    
    /* Only log initial loads to reduce noise */
    if (!s->initial_save_done) {
        info_report("ZeroTier: Loaded %s (%zu bytes)", filename, read_len);
    }
    return read_len;
}

static void zerotier_state_put(ZT_Node *node, void *uptr, void *tptr, enum ZT_StateObjectType type, const uint64_t objid[2], const void *data, int len)
{
    ZeroTierState *s = (ZeroTierState *)uptr;
    char filename[512];
    char dirname[512];
    FILE *f;
    
    if (!s || !s->storage_path) {
        return;
    }
    
    /* Create filename based on object type and ID */
    if (type == ZT_STATE_OBJECT_IDENTITY_SECRET) {
        snprintf(filename, sizeof(filename), "%s/identity.secret", s->storage_path);
    } else if (type == ZT_STATE_OBJECT_IDENTITY_PUBLIC) {
        snprintf(filename, sizeof(filename), "%s/identity.public", s->storage_path);
    } else if (type == ZT_STATE_OBJECT_NETWORK_CONFIG) {
        snprintf(dirname, sizeof(dirname), "%s/networks.d", s->storage_path);
        g_mkdir_with_parents(dirname, 0700);
        snprintf(filename, sizeof(filename), "%s/%016llx.conf",
                 dirname, (unsigned long long)objid[0]);
    } else {
        snprintf(filename, sizeof(filename), "%s/object_%d_%016llx_%016llx.dat",
                 s->storage_path, type, (unsigned long long)objid[0], (unsigned long long)objid[1]);
    }
    
    if (len < 0) {
        /* Delete object */
        unlink(filename);
        return;
    }
    
    f = fopen(filename, "wb");
    if (f) {
        fwrite(data, 1, len, f);
        fclose(f);
        
        /* Only log initial saves and important files to reduce noise */
        if (!s->initial_save_done || 
            type == ZT_STATE_OBJECT_IDENTITY_SECRET ||
            type == ZT_STATE_OBJECT_IDENTITY_PUBLIC) {
            info_report("ZeroTier: Saved %s (%d bytes)", filename, len);
        }
    } else {
        warn_report("ZeroTier: Failed to save %s", filename);
    }
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
    
    /* The VM should be using our ZeroTier MAC, no translation needed */
    uint64_t sourceMac = mac_to_uint64(&buf[6]);
    uint64_t zt_sourceMac = sourceMac; /* Pass through - VM should have ZT MAC */
    
    /* Process ARP silently for optimal performance */
    
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

/* Event-driven UDP packet handler following QEMU TAP driver patterns */
static void zerotier_udp_read(void *opaque)
{
    ZeroTierState *s = opaque;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    volatile int64_t nextDeadline = 0;
    uint8_t buffer[ZEROTIER_MTU];
    struct sockaddr_storage from_addr;
    socklen_t from_len;
    ssize_t received;
    int packets_processed = 0;
    
    /* 
     * Process packets in batches like TAP driver (50 packets max).
     * This prevents BQL stalling while maintaining good throughput.
     * Based on net/tap.c tap_send() function.
     */
    while (packets_processed < 50) { /* Match TAP driver batch size */
        from_len = sizeof(from_addr);
        received = recvfrom(s->udp_sock, buffer, sizeof(buffer), MSG_DONTWAIT | MSG_TRUNC,
                           (struct sockaddr*)&from_addr, &from_len);
        if (received > 0) {
            if (received > sizeof(buffer)) {
                /* Packet was truncated - silently continue */
                continue;
            }
            ZT_Node_processWirePacket(s->zt_node, NULL, now, -1, &from_addr,
                                    buffer, received, &nextDeadline);
            packets_processed++;
        } else if (received == 0) {
            /* Connection closed - shouldn't happen with UDP but handle it */
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; /* No more packets available */
            } else {
                /* Socket error - break without logging */
                break;
            }
        }
    }
    
    /*
     * If we processed the maximum batch, there might be more packets.
     * Let the event loop run other tasks before processing more packets.
     * This prevents monopolizing the BQL like the TAP driver does.
     */
    if (packets_processed >= 50) {
        /* Event handler will be called again if more data arrives */
        return;
    }
}

/* Background task timer (less frequent) */
static void zerotier_background_tasks(void *opaque)
{
    ZeroTierState *s = opaque;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    volatile int64_t nextDeadline = 0;
    
    /* Perform ZeroTier background tasks */
    ZT_Node_processBackgroundTasks(s->zt_node, NULL, now, &nextDeadline);
    
    /* Reschedule background tasks every 100ms */
    timer_mod(s->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
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
        qemu_set_fd_handler(s->udp_sock, NULL, NULL, NULL);
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


static bool zerotier_can_receive(NetClientState *nc)
{
    /* Always ready to receive from VM */
    return true;
}

static NetClientInfo net_zerotier_info = {
    .type = NET_CLIENT_DRIVER_ZEROTIER,
    .size = sizeof(ZeroTierState),
    .receive = zerotier_receive,
    .can_receive = zerotier_can_receive,
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
    /* Use proper storage path for persistent credentials */
    if (storage) {
        s->storage_path = g_strdup(storage);
    } else {
        /* Default to user's home directory */
        const char *home = getenv("HOME");
        if (home) {
            s->storage_path = g_strdup_printf("%s/.qemu-zerotier", home);
        } else {
            s->storage_path = g_strdup("/tmp/qemu-zerotier");
        }
    }
    
    /* Create storage directory if it doesn't exist */
    g_mkdir_with_parents(s->storage_path, 0700);
    s->connected = false;
    s->network_ready = false;
    s->zt_node = NULL;
    s->udp_sock = -1;
    s->mac_provided = false;
    s->initial_save_done = false;
    
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
    
    /* Pass storage path context to callbacks via uptr */
    enum ZT_ResultCode result = ZT_Node_new(&s->zt_node, s, NULL, &callbacks, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    if (result != ZT_RESULT_OK || !s->zt_node) {
        error_setg(errp, "Failed to create ZeroTier node: %d", result);
        goto error;
    }
    
    info_report("ZeroTier: Node initialized, checking for existing identity...");
    
    /* Join the network */
    info_report("ZeroTier: Joining network %016llx", (unsigned long long)network_id);
    result = ZT_Node_join(s->zt_node, network_id, s, NULL);
    if (result != ZT_RESULT_OK) {
        error_setg(errp, "Failed to join ZeroTier network: %d", result);
        goto error;
    }
    
    /* Wait for network to come up and MAC to be assigned */
    info_report("ZeroTier: Waiting for network configuration...");
    int wait_cycles = 0;
    while (!s->network_ready && wait_cycles < 50) { /* 5 second timeout - faster */
        int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        volatile int64_t nextDeadline = 0;
        
        /* Process background tasks */
        ZT_Node_processBackgroundTasks(s->zt_node, NULL, now, &nextDeadline);
        
        /* Process incoming packets - reasonable batch size */
        if (s->udp_sock >= 0) {
            for (int i = 0; i < 5; i++) {
                struct sockaddr_storage from_addr;
                socklen_t from_len = sizeof(from_addr);
                uint8_t buffer[2048];
                ssize_t received = recvfrom(s->udp_sock, buffer, sizeof(buffer), MSG_DONTWAIT,
                                           (struct sockaddr*)&from_addr, &from_len);
                if (received > 0) {
                    ZT_Node_processWirePacket(s->zt_node, NULL, now, -1, &from_addr,
                                             buffer, received, &nextDeadline);
                } else {
                    break; /* No more packets */
                }
            }
        }
        
        if (!s->network_ready) {
            usleep(100000); /* Sleep 100ms between checks */
            wait_cycles++;
        }
    }
    
    if (!s->network_ready) {
        warn_report("ZeroTier: Network not ready after 5 seconds - continuing anyway");
        /* Don't fail - ZeroTier might come up later */
        /* Generate a temporary MAC based on network ID */
        s->mac[0] = 0x02; /* Locally administered */
        s->mac[1] = 0x00;
        s->mac[2] = 0x00;
        s->mac[3] = (network_id >> 16) & 0xff;
        s->mac[4] = (network_id >> 8) & 0xff;
        s->mac[5] = network_id & 0xff;
    }
    
    /* MAC address has been set by ZeroTier network config callback */
    qemu_format_nic_info_str(nc, s->mac);
    
    /* Store MAC globally for NIC device to use */
    memcpy(zerotier_suggested_mac, s->mac, 6);
    info_report("ZeroTier: Ready with MAC %02x:%02x:%02x:%02x:%02x:%02x (NIC should use this)",
                s->mac[0], s->mac[1], s->mac[2], s->mac[3], s->mac[4], s->mac[5]);
    
    /* Mark initial save phase as done to reduce logging noise */
    s->initial_save_done = true;
    
    s->connected = true;
    
    /* Initialize UDP socket for ZeroTier protocol */
    s->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->udp_sock >= 0) {
        struct sockaddr_in bind_addr = {0};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        
        int opt = 1;
        setsockopt(s->udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        /* Increase UDP socket buffer sizes to reduce packet loss */
        int buf_size = 2 * 1024 * 1024; /* 2MB buffer */
        if (setsockopt(s->udp_sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) < 0) {
            warn_report("ZeroTier: Failed to set UDP receive buffer size");
        }
        if (setsockopt(s->udp_sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0) {
            warn_report("ZeroTier: Failed to set UDP send buffer size");
        }
        
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
    
    /* Set up event-driven UDP handling */
    if (s->udp_sock >= 0) {
        qemu_set_fd_handler(s->udp_sock, zerotier_udp_read, NULL, s);
    }
    
    /* Set up background task timer (less frequent) */
    s->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, zerotier_background_tasks, s);
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

/* Export function to get suggested MAC for NIC device */
bool net_zerotier_get_mac(uint8_t *mac)
{
    if (zerotier_suggested_mac[0] || zerotier_suggested_mac[1] || zerotier_suggested_mac[2] ||
        zerotier_suggested_mac[3] || zerotier_suggested_mac[4] || zerotier_suggested_mac[5]) {
        memcpy(mac, zerotier_suggested_mac, 6);
        return true;
    }
    return false;
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