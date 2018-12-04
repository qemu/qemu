#ifndef QEMU_NET_H
#define QEMU_NET_H

#include "qemu/queue.h"
#include "qapi/qapi-types-net.h"
#include "net/queue.h"
#include "migration/vmstate.h"

#define MAC_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC_ARG(x) ((uint8_t *)(x))[0], ((uint8_t *)(x))[1], \
                   ((uint8_t *)(x))[2], ((uint8_t *)(x))[3], \
                   ((uint8_t *)(x))[4], ((uint8_t *)(x))[5]

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
    int32_t queues;
} NICPeers;

typedef struct NICConf {
    MACAddr macaddr;
    NICPeers peers;
    int32_t bootindex;
} NICConf;

#define DEFINE_NIC_PROPERTIES(_state, _conf)                            \
    DEFINE_PROP_MACADDR("mac",   _state, _conf.macaddr),                \
    DEFINE_PROP_NETDEV("netdev", _state, _conf.peers)


/* Net clients */

typedef void (NetPoll)(NetClientState *, bool enable);
typedef int (NetCanReceive)(NetClientState *);
typedef ssize_t (NetReceive)(NetClientState *, const uint8_t *, size_t);
typedef ssize_t (NetReceiveIOV)(NetClientState *, const struct iovec *, int);
typedef void (NetCleanup) (NetClientState *);
typedef void (LinkStatusChanged)(NetClientState *);
typedef void (NetClientDestructor)(NetClientState *);
typedef RxFilterInfo *(QueryRxFilter)(NetClientState *);
typedef bool (HasUfo)(NetClientState *);
typedef bool (HasVnetHdr)(NetClientState *);
typedef bool (HasVnetHdrLen)(NetClientState *, int);
typedef void (UsingVnetHdr)(NetClientState *, bool);
typedef void (SetOffload)(NetClientState *, int, int, int, int, int);
typedef void (SetVnetHdrLen)(NetClientState *, int);
typedef int (SetVnetLE)(NetClientState *, bool);
typedef int (SetVnetBE)(NetClientState *, bool);
typedef struct SocketReadState SocketReadState;
typedef void (SocketReadStateFinalize)(SocketReadState *rs);

typedef struct NetClientInfo {
    NetClientDriver type;
    size_t size;
    NetReceive *receive;
    NetReceive *receive_raw;
    NetReceiveIOV *receive_iov;
    NetCanReceive *can_receive;
    NetCleanup *cleanup;
    LinkStatusChanged *link_status_changed;
    QueryRxFilter *query_rx_filter;
    NetPoll *poll;
    HasUfo *has_ufo;
    HasVnetHdr *has_vnet_hdr;
    HasVnetHdrLen *has_vnet_hdr_len;
    UsingVnetHdr *using_vnet_hdr;
    SetOffload *set_offload;
    SetVnetHdrLen *set_vnet_hdr_len;
    SetVnetLE *set_vnet_le;
    SetVnetBE *set_vnet_be;
} NetClientInfo;

struct NetClientState {
    NetClientInfo *info;
    int link_down;
    QTAILQ_ENTRY(NetClientState) next;
    NetClientState *peer;
    NetQueue *incoming_queue;
    char *model;
    char *name;
    char info_str[256];
    unsigned receive_disabled : 1;
    NetClientDestructor *destructor;
    unsigned int queue_index;
    unsigned rxfilter_notify_enabled:1;
    int vring_enable;
    int vnet_hdr_len;
    QTAILQ_HEAD(NetFilterHead, NetFilterState) filters;
};

typedef struct NICState {
    NetClientState *ncs;
    NICConf *conf;
    void *opaque;
    bool peer_deleted;
} NICState;

struct SocketReadState {
    /* 0 = getting length, 1 = getting vnet header length, 2 = getting data */
    int state;
    /* This flag decide whether to read the vnet_hdr_len field */
    bool vnet_hdr;
    uint32_t index;
    uint32_t packet_len;
    uint32_t vnet_hdr_len;
    uint8_t buf[NET_BUFSIZE];
    SocketReadStateFinalize *finalize;
};

int net_fill_rstate(SocketReadState *rs, const uint8_t *buf, int size);
char *qemu_mac_strdup_printf(const uint8_t *macaddr);
NetClientState *qemu_find_netdev(const char *id);
int qemu_find_net_clients_except(const char *id, NetClientState **ncs,
                                 NetClientDriver type, int max);
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
void qemu_flush_or_purge_queued_packets(NetClientState *nc, bool purge);
void qemu_format_nic_info_str(NetClientState *nc, uint8_t macaddr[6]);
bool qemu_has_ufo(NetClientState *nc);
bool qemu_has_vnet_hdr(NetClientState *nc);
bool qemu_has_vnet_hdr_len(NetClientState *nc, int len);
void qemu_using_vnet_hdr(NetClientState *nc, bool enable);
void qemu_set_offload(NetClientState *nc, int csum, int tso4, int tso6,
                      int ecn, int ufo);
void qemu_set_vnet_hdr_len(NetClientState *nc, int len);
int qemu_set_vnet_le(NetClientState *nc, bool is_le);
int qemu_set_vnet_be(NetClientState *nc, bool is_be);
void qemu_macaddr_default_if_unset(MACAddr *macaddr);
int qemu_show_nic_models(const char *arg, const char *const *models);
void qemu_check_nic_model(NICInfo *nd, const char *model);
int qemu_find_nic_model(NICInfo *nd, const char * const *models,
                        const char *default_model);

void print_net_client(Monitor *mon, NetClientState *nc);
void hmp_info_network(Monitor *mon, const QDict *qdict);
void net_socket_rs_init(SocketReadState *rs,
                        SocketReadStateFinalize *finalize,
                        bool vnet_hdr);

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
extern const char *host_net_devices[];

/* from net.c */
int net_client_parse(QemuOptsList *opts_list, const char *str);
int net_init_clients(Error **errp);
void net_check_clients(void);
void net_cleanup(void);
void hmp_host_net_add(Monitor *mon, const QDict *qdict);
void hmp_host_net_remove(Monitor *mon, const QDict *qdict);
void netdev_add(QemuOpts *opts, Error **errp);
void qmp_netdev_add(QDict *qdict, QObject **ret, Error **errp);

int net_hub_id_for_client(NetClientState *nc, int *id);
NetClientState *net_hub_port_find(int hub_id);

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"
#define DEFAULT_BRIDGE_HELPER CONFIG_QEMU_HELPERDIR "/qemu-bridge-helper"
#define DEFAULT_BRIDGE_INTERFACE "br0"

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd);

#define POLYNOMIAL_BE 0x04c11db6
#define POLYNOMIAL_LE 0xedb88320
uint32_t net_crc32(const uint8_t *p, int len);
uint32_t net_crc32_le(const uint8_t *p, int len);

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
