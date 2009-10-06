#ifndef QEMU_NET_H
#define QEMU_NET_H

#include "qemu-queue.h"
#include "qemu-common.h"
#include "qdict.h"

/* VLANs support */

typedef struct VLANClientState VLANClientState;

typedef int (NetCanReceive)(VLANClientState *);
typedef ssize_t (NetReceive)(VLANClientState *, const uint8_t *, size_t);
typedef ssize_t (NetReceiveIOV)(VLANClientState *, const struct iovec *, int);
typedef void (NetCleanup) (VLANClientState *);
typedef void (LinkStatusChanged)(VLANClientState *);

struct VLANClientState {
    NetReceive *receive;
    NetReceiveIOV *receive_iov;
    /* Packets may still be sent if this returns zero.  It's used to
       rate-limit the slirp code.  */
    NetCanReceive *can_receive;
    NetCleanup *cleanup;
    LinkStatusChanged *link_status_changed;
    int link_down;
    void *opaque;
    struct VLANClientState *next;
    struct VLANState *vlan;
    char *model;
    char *name;
    char info_str[256];
};

typedef struct VLANPacket VLANPacket;

typedef void (NetPacketSent) (VLANClientState *, ssize_t);

struct VLANPacket {
    QTAILQ_ENTRY(VLANPacket) entry;
    VLANClientState *sender;
    int size;
    NetPacketSent *sent_cb;
    uint8_t data[0];
};

struct VLANState {
    int id;
    VLANClientState *first_client;
    struct VLANState *next;
    unsigned int nb_guest_devs, nb_host_devs;
    QTAILQ_HEAD(send_queue, VLANPacket) send_queue;
    int delivering;
};

VLANState *qemu_find_vlan(int id, int allocate);
VLANClientState *qemu_new_vlan_client(VLANState *vlan,
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
int qemu_show_nic_models(const char *arg, const char *const *models);
void qemu_check_nic_model(NICInfo *nd, const char *model);
int qemu_find_nic_model(NICInfo *nd, const char * const *models,
                        const char *default_model);
void qemu_handler_true(void *opaque);

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
    char *id;
    VLANState *vlan;
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

int net_client_init(Monitor *mon, const char *device, const char *p);
void net_client_uninit(NICInfo *nd);
int net_client_parse(const char *str);
void net_slirp_smb(const char *exported_dir);
void net_slirp_hostfwd_add(Monitor *mon, const QDict *qdict);
void net_slirp_hostfwd_remove(Monitor *mon, const QDict *qdict);
void net_slirp_redir(const char *redir_str);
void net_cleanup(void);
void net_client_check(void);
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

void qdev_get_macaddr(DeviceState *dev, uint8_t *macaddr);
VLANClientState *qdev_get_vlan_client(DeviceState *dev,
                                      NetCanReceive *can_receive,
                                      NetReceive *receive,
                                      NetReceiveIOV *receive_iov,
                                      NetCleanup *cleanup,
                                      void *opaque);

#endif
