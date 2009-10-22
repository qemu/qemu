#ifndef QEMU_NET_H
#define QEMU_NET_H

#include "qemu-queue.h"
#include "qemu-common.h"
#include "qdict.h"
#include "qemu-option.h"
#include "net-queue.h"

struct MACAddr {
    uint8_t a[6];
};

/* qdev nic properties */

typedef struct NICConf {
    MACAddr macaddr;
    VLANState *vlan;
    VLANClientState *peer;
} NICConf;

#define DEFINE_NIC_PROPERTIES(_state, _conf)                            \
    DEFINE_PROP_MACADDR("mac",   _state, _conf.macaddr),                \
    DEFINE_PROP_VLAN("vlan",     _state, _conf.vlan),                   \
    DEFINE_PROP_NETDEV("netdev", _state, _conf.peer)

/* VLANs support */

typedef enum {
    NET_CLIENT_TYPE_NONE,
    NET_CLIENT_TYPE_NIC,
    NET_CLIENT_TYPE_SLIRP,
    NET_CLIENT_TYPE_TAP,
    NET_CLIENT_TYPE_SOCKET,
    NET_CLIENT_TYPE_VDE,
    NET_CLIENT_TYPE_DUMP
} net_client_type;

typedef int (NetCanReceive)(VLANClientState *);
typedef ssize_t (NetReceive)(VLANClientState *, const uint8_t *, size_t);
typedef ssize_t (NetReceiveIOV)(VLANClientState *, const struct iovec *, int);
typedef void (NetCleanup) (VLANClientState *);
typedef void (LinkStatusChanged)(VLANClientState *);

struct VLANClientState {
    net_client_type type;
    NetReceive *receive;
    NetReceiveIOV *receive_iov;
    /* Packets may still be sent if this returns zero.  It's used to
       rate-limit the slirp code.  */
    NetCanReceive *can_receive;
    NetCleanup *cleanup;
    LinkStatusChanged *link_status_changed;
    int link_down;
    void *opaque;
    QTAILQ_ENTRY(VLANClientState) next;
    struct VLANState *vlan;
    VLANClientState *peer;
    NetQueue *send_queue;
    char *model;
    char *name;
    char info_str[256];
};

struct VLANState {
    int id;
    QTAILQ_HEAD(, VLANClientState) clients;
    QTAILQ_ENTRY(VLANState) next;
    unsigned int nb_guest_devs, nb_host_devs;
    NetQueue *send_queue;
};

VLANState *qemu_find_vlan(int id, int allocate);
VLANClientState *qemu_find_netdev(const char *id);
VLANClientState *qemu_new_vlan_client(VLANState *vlan,
                                      VLANClientState *peer,
                                      const char *model,
                                      const char *name,
                                      NetCanReceive *can_receive,
                                      NetReceive *receive,
                                      NetReceiveIOV *receive_iov,
                                      NetCleanup *cleanup,
                                      void *opaque);
void qemu_del_vlan_client(VLANClientState *vc);
VLANClientState *qemu_find_vlan_client(VLANState *vlan, void *opaque);
int qemu_can_send_packet(VLANClientState *vc);
ssize_t qemu_sendv_packet(VLANClientState *vc, const struct iovec *iov,
                          int iovcnt);
ssize_t qemu_sendv_packet_async(VLANClientState *vc, const struct iovec *iov,
                                int iovcnt, NetPacketSent *sent_cb);
void qemu_send_packet(VLANClientState *vc, const uint8_t *buf, int size);
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
void do_set_link(Monitor *mon, const QDict *qdict);

void do_info_usernet(Monitor *mon);

/* NIC info */

#define MAX_NICS 8
enum {
	NIC_NVECTORS_UNSPECIFIED = -1
};

struct NICInfo {
    uint8_t macaddr[6];
    char *model;
    char *name;
    char *devaddr;
    VLANState *vlan;
    VLANClientState *netdev;
    VLANClientState *vc;
    void *private;
    int used;
    int bootable;
    int nvectors;
};

extern int nb_nics;
extern NICInfo nd_table[MAX_NICS];

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

/* checksumming functions (net-checksum.c) */
uint32_t net_checksum_add(int len, uint8_t *buf);
uint16_t net_checksum_finish(uint32_t sum);
uint16_t net_checksum_tcpudp(uint16_t length, uint16_t proto,
                             uint8_t *addrs, uint8_t *buf);
void net_checksum_calculate(uint8_t *data, int length);

/* from net.c */
extern const char *legacy_tftp_prefix;
extern const char *legacy_bootp_filename;

int net_client_init(Monitor *mon, QemuOpts *opts, int is_netdev);
void net_client_uninit(NICInfo *nd);
int net_client_parse(QemuOptsList *opts_list, const char *str);
int net_init_clients(void);
int net_slirp_smb(const char *exported_dir);
void net_slirp_hostfwd_add(Monitor *mon, const QDict *qdict);
void net_slirp_hostfwd_remove(Monitor *mon, const QDict *qdict);
int net_slirp_redir(const char *redir_str);
void net_cleanup(void);
void net_set_boot_mask(int boot_mask);
void net_host_device_add(Monitor *mon, const QDict *qdict);
void net_host_device_remove(Monitor *mon, const QDict *qdict);

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"
#ifdef __sun__
#define SMBD_COMMAND "/usr/sfw/sbin/smbd"
#else
#define SMBD_COMMAND "/usr/sbin/smbd"
#endif

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd);

int tap_has_vnet_hdr(VLANClientState *vc);
void tap_using_vnet_hdr(VLANClientState *vc, int using_vnet_hdr);

#endif
