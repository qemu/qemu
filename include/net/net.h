#ifndef QEMU_NET_H
#define QEMU_NET_H

#include "qemu/queue.h"
#include "qapi/qapi-types-net.h"
#include "net/queue.h"
#include "hw/qdev-properties-system.h"

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
typedef bool (NetCanReceive)(NetClientState *);
typedef int (NetStart)(NetClientState *);
typedef int (NetLoad)(NetClientState *);
typedef void (NetStop)(NetClientState *);
typedef ssize_t (NetReceive)(NetClientState *, const uint8_t *, size_t);
typedef ssize_t (NetReceiveIOV)(NetClientState *, const struct iovec *, int);
typedef void (NetCleanup) (NetClientState *);
typedef void (LinkStatusChanged)(NetClientState *);
typedef void (NetClientDestructor)(NetClientState *);
typedef RxFilterInfo *(QueryRxFilter)(NetClientState *);
typedef bool (HasUfo)(NetClientState *);
typedef bool (HasUso)(NetClientState *);
typedef bool (HasVnetHdr)(NetClientState *);
typedef bool (HasVnetHdrLen)(NetClientState *, int);
typedef void (SetOffload)(NetClientState *, int, int, int, int, int, int, int);
typedef int (GetVnetHdrLen)(NetClientState *);
typedef void (SetVnetHdrLen)(NetClientState *, int);
typedef int (SetVnetLE)(NetClientState *, bool);
typedef int (SetVnetBE)(NetClientState *, bool);
typedef struct SocketReadState SocketReadState;
typedef void (SocketReadStateFinalize)(SocketReadState *rs);
typedef void (NetAnnounce)(NetClientState *);
typedef bool (SetSteeringEBPF)(NetClientState *, int);
typedef bool (NetCheckPeerType)(NetClientState *, ObjectClass *, Error **);

typedef struct NetClientInfo {
    NetClientDriver type;
    size_t size;
    NetReceive *receive;
    NetReceiveIOV *receive_iov;
    NetCanReceive *can_receive;
    NetStart *start;
    NetLoad *load;
    NetStop *stop;
    NetCleanup *cleanup;
    LinkStatusChanged *link_status_changed;
    QueryRxFilter *query_rx_filter;
    NetPoll *poll;
    HasUfo *has_ufo;
    HasUso *has_uso;
    HasVnetHdr *has_vnet_hdr;
    HasVnetHdrLen *has_vnet_hdr_len;
    SetOffload *set_offload;
    SetVnetHdrLen *set_vnet_hdr_len;
    SetVnetLE *set_vnet_le;
    SetVnetBE *set_vnet_be;
    NetAnnounce *announce;
    SetSteeringEBPF *set_steering_ebpf;
    NetCheckPeerType *check_peer_type;
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
    bool is_netdev;
    bool do_not_pad; /* do not pad to the minimum ethernet frame length */
    bool is_datapath;
    QTAILQ_HEAD(, NetFilterState) filters;
};

typedef QTAILQ_HEAD(NetClientStateList, NetClientState) NetClientStateList;

typedef struct NICState {
    NetClientState *ncs;
    NICConf *conf;
    MemReentrancyGuard *reentrancy_guard;
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
NetClientState *qemu_new_net_control_client(NetClientInfo *info,
                                        NetClientState *peer,
                                        const char *model,
                                        const char *name);
NICState *qemu_new_nic(NetClientInfo *info,
                       NICConf *conf,
                       const char *model,
                       const char *name,
                       MemReentrancyGuard *reentrancy_guard,
                       void *opaque);
void qemu_del_nic(NICState *nic);
NetClientState *qemu_get_subqueue(NICState *nic, int queue_index);
NetClientState *qemu_get_queue(NICState *nic);
NICState *qemu_get_nic(NetClientState *nc);
void *qemu_get_nic_opaque(NetClientState *nc);
void qemu_del_net_client(NetClientState *nc);
typedef void (*qemu_nic_foreach)(NICState *nic, void *opaque);
void qemu_foreach_nic(qemu_nic_foreach func, void *opaque);
int qemu_can_receive_packet(NetClientState *nc);
int qemu_can_send_packet(NetClientState *nc);
ssize_t qemu_sendv_packet(NetClientState *nc, const struct iovec *iov,
                          int iovcnt);
ssize_t qemu_sendv_packet_async(NetClientState *nc, const struct iovec *iov,
                                int iovcnt, NetPacketSent *sent_cb);
ssize_t qemu_send_packet(NetClientState *nc, const uint8_t *buf, int size);
ssize_t qemu_receive_packet(NetClientState *nc, const uint8_t *buf, int size);
ssize_t qemu_send_packet_raw(NetClientState *nc, const uint8_t *buf, int size);
ssize_t qemu_send_packet_async(NetClientState *nc, const uint8_t *buf,
                               int size, NetPacketSent *sent_cb);
void qemu_purge_queued_packets(NetClientState *nc);
void qemu_flush_queued_packets(NetClientState *nc);
void qemu_flush_or_purge_queued_packets(NetClientState *nc, bool purge);
void qemu_set_info_str(NetClientState *nc,
                       const char *fmt, ...) G_GNUC_PRINTF(2, 3);
void qemu_format_nic_info_str(NetClientState *nc, uint8_t macaddr[6]);
bool qemu_has_ufo(NetClientState *nc);
bool qemu_has_uso(NetClientState *nc);
bool qemu_has_vnet_hdr(NetClientState *nc);
bool qemu_has_vnet_hdr_len(NetClientState *nc, int len);
void qemu_set_offload(NetClientState *nc, int csum, int tso4, int tso6,
                      int ecn, int ufo, int uso4, int uso6);
int qemu_get_vnet_hdr_len(NetClientState *nc);
void qemu_set_vnet_hdr_len(NetClientState *nc, int len);
int qemu_set_vnet_le(NetClientState *nc, bool is_le);
int qemu_set_vnet_be(NetClientState *nc, bool is_be);
void qemu_macaddr_default_if_unset(MACAddr *macaddr);
/**
 * qemu_find_nic_info: Obtain NIC configuration information
 * @typename: Name of device object type
 * @match_default: Match NIC configurations with no model specified
 * @alias: Additional model string to match (for user convenience and
 *         backward compatibility).
 *
 * Search for a NIC configuration matching the NIC model constraints.
 */
NICInfo *qemu_find_nic_info(const char *typename, bool match_default,
                            const char *alias);
/**
 * qemu_configure_nic_device: Apply NIC configuration to a given device
 * @dev: Network device to be configured
 * @match_default: Match NIC configurations with no model specified
 * @alias: Additional model string to match
 *
 * Search for a NIC configuration for the provided device, using the
 * additionally specified matching constraints. If found, apply the
 * configuration using qdev_set_nic_properties() and return %true.
 *
 * This is used by platform code which creates the device anyway,
 * regardless of whether there is a configuration for it. This tends
 * to be platforms which ignore `--nodefaults` and create net devices
 * anyway, for example because the Ethernet device on that board is
 * always physically present.
 */
bool qemu_configure_nic_device(DeviceState *dev, bool match_default,
                               const char *alias);

/**
 * qemu_create_nic_device: Create a NIC device if a configuration exists for it
 * @typename: Object typename of network device
 * @match_default: Match NIC configurations with no model specified
 * @alias: Additional model string to match
 *
 * Search for a NIC configuration for the provided device type. If found,
 * create an object of the corresponding type and return it.
 */
DeviceState *qemu_create_nic_device(const char *typename, bool match_default,
                                    const char *alias);

/*
 * qemu_create_nic_bus_devices: Create configured NIC devices for a given bus
 * @bus: Bus on which to create devices
 * @parent_type: Object type for devices to be created (e.g. TYPE_PCI_DEVICE)
 * @default_model: Object type name for default NIC model (or %NULL)
 * @alias: Additional model string to replace, for user convenience
 * @alias_target: Actual object type name to be used in place of @alias
 *
 * Instantiate dynamic NICs on a given bus, typically a PCI bus. This scans
 * for available NIC configurations which either specify a model which is
 * a child type of @parent_type, or which do not specify a model when
 * @default_model is non-NULL. Each device is instantiated on the given @bus.
 *
 * A single substitution is supported, e.g. "xen" → "xen-net-device" for the
 * Xen bus, or "virtio" → "virtio-net-pci" for PCI. This allows the user to
 * specify a more understandable "model=" parameter on the command line, not
 * only the real object typename.
 */
void qemu_create_nic_bus_devices(BusState *bus, const char *parent_type,
                                 const char *default_model,
                                 const char *alias, const char *alias_target);
void print_net_client(Monitor *mon, NetClientState *nc);
void net_socket_rs_init(SocketReadState *rs,
                        SocketReadStateFinalize *finalize,
                        bool vnet_hdr);
NetClientState *qemu_get_peer(NetClientState *nc, int queue_index);

/**
 * qemu_get_nic_models:
 * @device_type: Defines which devices should be taken into consideration
 *               (e.g. TYPE_DEVICE for all devices, or TYPE_PCI_DEVICE for PCI)
 *
 * Get an array of pointers to names of NIC devices that are available in
 * the QEMU binary. The array is terminated with a NULL pointer entry.
 * The caller is responsible for freeing the memory when it is not required
 * anymore, e.g. with g_ptr_array_free(..., true).
 *
 * Returns: Pointer to the array that contains the pointers to the names.
 */
GPtrArray *qemu_get_nic_models(const char *device_type);

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

/* from net.c */
extern NetClientStateList net_clients;
bool netdev_is_modern(const char *optstr);
void netdev_parse_modern(const char *optstr);
void net_client_parse(QemuOptsList *opts_list, const char *optstr);
void show_netdevs(void);
void net_init_clients(void);
void net_check_clients(void);
void net_cleanup(void);
void hmp_host_net_add(Monitor *mon, const QDict *qdict);
void hmp_host_net_remove(Monitor *mon, const QDict *qdict);
void netdev_add(QemuOpts *opts, Error **errp);

int net_hub_id_for_client(NetClientState *nc, int *id);

#define DEFAULT_NETWORK_SCRIPT CONFIG_SYSCONFDIR "/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT CONFIG_SYSCONFDIR "/qemu-ifdown"
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

static inline bool net_peer_needs_padding(NetClientState *nc)
{
  return nc->peer && !nc->peer->do_not_pad;
}

#endif
