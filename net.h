#ifndef QEMU_NET_H
#define QEMU_NET_H

#include "qemu-queue.h"
#include "qemu-common.h"
#include "qdict.h"
#include "qemu-option.h"
#include "net/queue.h"

struct MACAddr {
    uint8_t a[6];
};

/* qdev nic properties */

typedef struct NICConf {
    MACAddr macaddr;
    VLANState *vlan;
    VLANClientState *peer;
    int32_t bootindex;
} NICConf;

#define DEFINE_NIC_PROPERTIES(_state, _conf)                            \
    DEFINE_PROP_MACADDR("mac",   _state, _conf.macaddr),                \
    DEFINE_PROP_VLAN("vlan",     _state, _conf.vlan),                   \
    DEFINE_PROP_NETDEV("netdev", _state, _conf.peer),                   \
    DEFINE_PROP_INT32("bootindex", _state, _conf.bootindex, -1)

/* VLANs support */

typedef enum {
    NET_CLIENT_TYPE_NONE,
    NET_CLIENT_TYPE_NIC,
    NET_CLIENT_TYPE_USER,
    NET_CLIENT_TYPE_TAP,
    NET_CLIENT_TYPE_SOCKET,
    NET_CLIENT_TYPE_VDE,
    NET_CLIENT_TYPE_DUMP,

    NET_CLIENT_TYPE_MAX
} net_client_type;

typedef void (NetPoll)(VLANClientState *, bool enable);
typedef int (NetCanReceive)(VLANClientState *);
typedef ssize_t (NetReceive)(VLANClientState *, const uint8_t *, size_t);
typedef ssize_t (NetReceiveIOV)(VLANClientState *, const struct iovec *, int);
typedef void (NetCleanup) (VLANClientState *);
typedef void (LinkStatusChanged)(VLANClientState *);

typedef struct NetClientInfo {
    net_client_type type;
    size_t size;
    NetReceive *receive;
    NetReceive *receive_raw;
    NetReceiveIOV *receive_iov;
    NetCanReceive *can_receive;
    NetCleanup *cleanup;
    LinkStatusChanged *link_status_changed;
    NetPoll *poll;
} NetClientInfo;

struct VLANClientState {
    NetClientInfo *info;
    int link_down;
    QTAILQ_ENTRY(VLANClientState) next;
    struct VLANState *vlan;
    VLANClientState *peer;
    NetQueue *send_queue;
    char *model;
    char *name;
    char info_str[256];
    unsigned receive_disabled : 1;
};

typedef struct NICState {
    VLANClientState nc;
    NICConf *conf;
    void *opaque;
    bool peer_deleted;
} NICState;

struct VLANState {
    int id;
    QTAILQ_HEAD(, VLANClientState) clients;
    QTAILQ_ENTRY(VLANState) next;
    NetQueue *send_queue;
};

VLANState *qemu_find_vlan(int id, int allocate);
VLANClientState *qemu_find_netdev(const char *id);
VLANClientState *qemu_new_net_client(NetClientInfo *info,
                                     VLANState *vlan,
                                     VLANClientState *peer,
                                     const char *model,
                                     const char *name);
NICState *qemu_new_nic(NetClientInfo *info,
                       NICConf *conf,
                       const char *model,
                       const char *name,
                       void *opaque);
void qemu_del_vlan_client(VLANClientState *vc);
VLANClientState *qemu_find_vlan_client_by_name(Monitor *mon, int vlan_id,
                                               const char *client_str);
typedef void (*qemu_nic_foreach)(NICState *nic, void *opaque);
void qemu_foreach_nic(qemu_nic_foreach func, void *opaque);
int qemu_can_send_packet(VLANClientState *vc);
ssize_t qemu_sendv_packet(VLANClientState *vc, const struct iovec *iov,
                          int iovcnt);
ssize_t qemu_sendv_packet_async(VLANClientState *vc, const struct iovec *iov,
                                int iovcnt, NetPacketSent *sent_cb);
void qemu_send_packet(VLANClientState *vc, const uint8_t *buf, int size);
ssize_t qemu_send_packet_raw(VLANClientState *vc, const uint8_t *buf, int size);
ssize_t qemu_send_packet_async(VLANClientState *vc, const uint8_t *buf,
                               int size, NetPacketSent *sent_cb);
void qemu_purge_queued_packets(VLANClientState *vc);
void qemu_flush_queued_packets(VLANClientState *vc);
void qemu_format_nic_info_str(VLANClientState *vc, uint8_t macaddr[6]);
void qemu_macaddr_default_if_unset(MACAddr *macaddr);
int qemu_show_nic_models(const char *arg, const char *const *models);
void qemu_check_nic_model(NICInfo *nd, const char *model);
int qemu_find_nic_model(NICInfo *nd, const char * const *models,
                        const char *default_model);

void do_info_network(Monitor *mon);
int do_set_link(Monitor *mon, const QDict *qdict, QObject **ret_data);

/* NIC info */

#define MAX_NICS 8

struct NICInfo {
    MACAddr macaddr;
    char *model;
    char *name;
    char *devaddr;
    VLANState *vlan;
    VLANClientState *netdev;
    int used;         /* is this slot in nd_table[] being used? */
    int instantiated; /* does this NICInfo correspond to an instantiated NIC? */
    int nvectors;
};

extern int nb_nics;
extern NICInfo nd_table[MAX_NICS];
extern int default_net;

/* BT HCI info */

struct HCIInfo {
    int (*bdaddr_set)(struct HCIInfo *hci, const uint8_t *bd_addr);
    void (*cmd_send)(struct HCIInfo *hci, const uint8_t *data, int len);
    void (*sco_send)(struct HCIInfo *hci, const uint8_t *data, int len);
    void (*acl_send)(struct HCIInfo *hci, const uint8_t *data, int len);
    void *opaque;
    void (*evt_recv)(void *opaque, const uint8_t *data, int len);
    void (*acl_recv)(void *opaque, const uint8_t *data, int len);
};

struct HCIInfo *qemu_next_hci(void);

/* from net.c */
extern const char *legacy_tftp_prefix;
extern const char *legacy_bootp_filename;

int net_client_init(Monitor *mon, QemuOpts *opts, int is_netdev);
int net_client_parse(QemuOptsList *opts_list, const char *str);
int net_init_clients(void);
void net_check_clients(void);
void net_cleanup(void);
void net_host_device_add(Monitor *mon, const QDict *qdict);
void net_host_device_remove(Monitor *mon, const QDict *qdict);
int do_netdev_add(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_netdev_del(Monitor *mon, const QDict *qdict, QObject **ret_data);

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"
#ifdef __sun__
#define SMBD_COMMAND "/usr/sfw/sbin/smbd"
#else
#define SMBD_COMMAND "/usr/sbin/smbd"
#endif

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd);

int net_handle_fd_param(Monitor *mon, const char *param);

#endif
