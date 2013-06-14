#ifndef QEMU_NET_H
#define QEMU_NET_H

#include "qemu/queue.h"
#include "qemu-common.h"
#include "qapi/qmp/qdict.h"
#include "qemu/option.h"
#include "net/queue.h"
#include "migration/vmstate.h"
#include "qapi-types.h"

#define MAX_QUEUE_NUM 1024

/* Maximum GSO packet size (64k) plus plenty of room for
 * the ethernet and virtio_net headers
 */
#define NET_BUFSIZE (4096 + 65536)

struct MACAddr {
    uint8_t a[6];
};

/* qdev nic properties */

typedef struct NICPeers {
    NetClientState *ncs[MAX_QUEUE_NUM];
} NICPeers;

typedef struct NICConf {
    MACAddr macaddr;
    NICPeers peers;
    int32_t bootindex;
    int32_t queues;
} NICConf;

#define DEFINE_NIC_PROPERTIES(_state, _conf)                            \
    DEFINE_PROP_MACADDR("mac",   _state, _conf.macaddr),                \
    DEFINE_PROP_VLAN("vlan",     _state, _conf.peers),                   \
    DEFINE_PROP_NETDEV("netdev", _state, _conf.peers),                   \
    DEFINE_PROP_INT32("bootindex", _state, _conf.bootindex, -1)


/* Net clients */

typedef void (NetPoll)(NetClientState *, bool enable);
typedef int (NetCanReceive)(NetClientState *);
typedef ssize_t (NetReceive)(NetClientState *, const uint8_t *, size_t);
typedef ssize_t (NetReceiveIOV)(NetClientState *, const struct iovec *, int);
typedef void (NetCleanup) (NetClientState *);
typedef void (LinkStatusChanged)(NetClientState *);
typedef void (NetClientDestructor)(NetClientState *);
typedef RxFilterInfo *(QueryRxFilter)(NetClientState *);

typedef struct NetClientInfo {
    NetClientOptionsKind type;
    size_t size;
    NetReceive *receive;
    NetReceive *receive_raw;
    NetReceiveIOV *receive_iov;
    NetCanReceive *can_receive;
    NetCleanup *cleanup;
    LinkStatusChanged *link_status_changed;
    QueryRxFilter *query_rx_filter;
    NetPoll *poll;
} NetClientInfo;

struct NetClientState {
    NetClientInfo *info;
    int link_down;
    QTAILQ_ENTRY(NetClientState) next;
    NetClientState *peer;
    NetQueue *send_queue;
    char *model;
    char *name;
    char info_str[256];
    unsigned receive_disabled : 1;
    NetClientDestructor *destructor;
    unsigned int queue_index;
    unsigned rxfilter_notify_enabled:1;
};

typedef struct NICState {
    NetClientState *ncs;
    NICConf *conf;
    void *opaque;
    bool peer_deleted;
} NICState;

NetClientState *qemu_find_netdev(const char *id);
int qemu_find_net_clients_except(const char *id, NetClientState **ncs,
                                 NetClientOptionsKind type, int max);
NetClientState *qemu_new_net_client(NetClientInfo *info,
                                    NetClientState *peer,
                                    const char *model,
                                    const char *name);
NICState *qemu_new_nic(NetClientInfo *info,
                       NICConf *conf,
                       const char *model,
                       const char *name,
                       void *opaque);
void qemu_del_nic(NICState *nic);
NetClientState *qemu_get_subqueue(NICState *nic, int queue_index);
NetClientState *qemu_get_queue(NICState *nic);
NICState *qemu_get_nic(NetClientState *nc);
void *qemu_get_nic_opaque(NetClientState *nc);
void qemu_del_net_client(NetClientState *nc);
NetClientState *qemu_find_vlan_client_by_name(Monitor *mon, int vlan_id,
                                              const char *client_str);
typedef void (*qemu_nic_foreach)(NICState *nic, void *opaque);
void qemu_foreach_nic(qemu_nic_foreach func, void *opaque);
int qemu_can_send_packet(NetClientState *nc);
ssize_t qemu_sendv_packet(NetClientState *nc, const struct iovec *iov,
                          int iovcnt);
ssize_t qemu_sendv_packet_async(NetClientState *nc, const struct iovec *iov,
                                int iovcnt, NetPacketSent *sent_cb);
void qemu_send_packet(NetClientState *nc, const uint8_t *buf, int size);
ssize_t qemu_send_packet_raw(NetClientState *nc, const uint8_t *buf, int size);
ssize_t qemu_send_packet_async(NetClientState *nc, const uint8_t *buf,
                               int size, NetPacketSent *sent_cb);
void qemu_purge_queued_packets(NetClientState *nc);
void qemu_flush_queued_packets(NetClientState *nc);
void qemu_format_nic_info_str(NetClientState *nc, uint8_t macaddr[6]);
void qemu_macaddr_default_if_unset(MACAddr *macaddr);
int qemu_show_nic_models(const char *arg, const char *const *models);
void qemu_check_nic_model(NICInfo *nd, const char *model);
int qemu_find_nic_model(NICInfo *nd, const char * const *models,
                        const char *default_model);

ssize_t qemu_deliver_packet(NetClientState *sender,
                            unsigned flags,
                            const uint8_t *data,
                            size_t size,
                            void *opaque);
ssize_t qemu_deliver_packet_iov(NetClientState *sender,
                            unsigned flags,
                            const struct iovec *iov,
                            int iovcnt,
                            void *opaque);

void print_net_client(Monitor *mon, NetClientState *nc);
void do_info_network(Monitor *mon, const QDict *qdict);

/* NIC info */

#define MAX_NICS 8

struct NICInfo {
    MACAddr macaddr;
    char *model;
    char *name;
    char *devaddr;
    NetClientState *netdev;
    int used;         /* is this slot in nd_table[] being used? */
    int instantiated; /* does this NICInfo correspond to an instantiated NIC? */
    int nvectors;
};

extern int nb_nics;
extern NICInfo nd_table[MAX_NICS];
extern int default_net;

/* from net.c */
extern const char *legacy_tftp_prefix;
extern const char *legacy_bootp_filename;

int net_client_init(QemuOpts *opts, int is_netdev, Error **errp);
int net_client_parse(QemuOptsList *opts_list, const char *str);
int net_init_clients(void);
void net_check_clients(void);
void net_cleanup(void);
void net_host_device_add(Monitor *mon, const QDict *qdict);
void net_host_device_remove(Monitor *mon, const QDict *qdict);
void netdev_add(QemuOpts *opts, Error **errp);
int qmp_netdev_add(Monitor *mon, const QDict *qdict, QObject **ret);

int net_hub_id_for_client(NetClientState *nc, int *id);
NetClientState *net_hub_port_find(int hub_id);

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"
#define DEFAULT_BRIDGE_HELPER CONFIG_QEMU_HELPERDIR "/qemu-bridge-helper"
#define DEFAULT_BRIDGE_INTERFACE "br0"

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd);

#define POLYNOMIAL 0x04c11db6
unsigned compute_mcast_idx(const uint8_t *ep);

#define vmstate_offset_macaddr(_state, _field)                       \
    vmstate_offset_array(_state, _field.a, uint8_t,                \
                         sizeof(typeof_field(_state, _field)))

#define VMSTATE_MACADDR(_field, _state) {                            \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(MACAddr),                                   \
    .info       = &vmstate_info_buffer,                              \
    .flags      = VMS_BUFFER,                                        \
    .offset     = vmstate_offset_macaddr(_state, _field),            \
}

#endif
