#ifndef QEMU_NET_H
#define QEMU_NET_H

/* VLANs support */

typedef struct VLANClientState VLANClientState;

struct VLANClientState {
    IOReadHandler *fd_read;
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
int qemu_can_send_packet(VLANClientState *vc);
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

#endif
