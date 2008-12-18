#ifndef QEMU_NET_H
#define QEMU_NET_H

#include "qemu-common.h"

/* VLANs support */

typedef ssize_t (IOReadvHandler)(void *, const struct iovec *, int);

typedef struct VLANClientState VLANClientState;

struct VLANClientState {
    IOReadHandler *fd_read;
    IOReadvHandler *fd_readv;
    /* Packets may still be sent if this returns zero.  It's used to
       rate-limit the slirp code.  */
    IOCanRWHandler *fd_can_read;
    void *opaque;
    struct VLANClientState *next;
    struct VLANState *vlan;
    char info_str[256];
};

struct VLANState {
    int id;
    VLANClientState *first_client;
    struct VLANState *next;
    unsigned int nb_guest_devs, nb_host_devs;
};

VLANState *qemu_find_vlan(int id);
VLANClientState *qemu_new_vlan_client(VLANState *vlan,
                                      IOReadHandler *fd_read,
                                      IOCanRWHandler *fd_can_read,
                                      void *opaque);
void qemu_del_vlan_client(VLANClientState *vc);
int qemu_can_send_packet(VLANClientState *vc);
ssize_t qemu_sendv_packet(VLANClientState *vc, const struct iovec *iov,
                          int iovcnt);
void qemu_send_packet(VLANClientState *vc, const uint8_t *buf, int size);
void qemu_handler_true(void *opaque);

void do_info_network(void);

/* NIC info */

#define MAX_NICS 8

struct NICInfo {
    uint8_t macaddr[6];
    const char *model;
    VLANState *vlan;
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
int net_client_init(const char *device, const char *p);
int net_client_parse(const char *str);
void net_slirp_smb(const char *exported_dir);
void net_slirp_redir(const char *redir_str);
void net_cleanup(void);
int slirp_is_inited(void);
void net_client_check(void);

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"
#ifdef __sun__
#define SMBD_COMMAND "/usr/sfw/sbin/smbd"
#else
#define SMBD_COMMAND "/usr/sbin/smbd"
#endif

#endif
