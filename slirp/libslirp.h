#ifndef LIBSLIRP_H
#define LIBSLIRP_H

#include "qemu-common.h"

typedef struct Slirp Slirp;

typedef int (*SlirpWriteCb)(const void *buf, size_t len, void *opaque);

/*
 * Callbacks from slirp
 *
 * The opaque parameter comes from the opaque parameter given to slirp_init().
 */
typedef struct SlirpCb {
    /* Send an ethernet frame to the guest network.  */
    void (*output)(void *opaque, const uint8_t *pkt, int pkt_len);
    /* Print a message for an error due to guest misbehavior.  */
    void (*guest_error)(const char *msg);
    /* Return the virtual clock value in nanoseconds */
    int64_t (*clock_get_ns)(void);
} SlirpCb;


Slirp *slirp_init(int restricted, bool in_enabled, struct in_addr vnetwork,
                  struct in_addr vnetmask, struct in_addr vhost,
                  bool in6_enabled,
                  struct in6_addr vprefix_addr6, uint8_t vprefix_len,
                  struct in6_addr vhost6, const char *vhostname,
                  const char *tftp_server_name,
                  const char *tftp_path, const char *bootfile,
                  struct in_addr vdhcp_start, struct in_addr vnameserver,
                  struct in6_addr vnameserver6, const char **vdnssearch,
                  const char *vdomainname,
                  const SlirpCb *callbacks,
                  void *opaque);
void slirp_cleanup(Slirp *slirp);

void slirp_pollfds_fill(GArray *pollfds, uint32_t *timeout);

void slirp_pollfds_poll(GArray *pollfds, int select_error);

void slirp_input(Slirp *slirp, const uint8_t *pkt, int pkt_len);

int slirp_add_hostfwd(Slirp *slirp, int is_udp,
                      struct in_addr host_addr, int host_port,
                      struct in_addr guest_addr, int guest_port);
int slirp_remove_hostfwd(Slirp *slirp, int is_udp,
                         struct in_addr host_addr, int host_port);
int slirp_add_exec(Slirp *slirp, const char *cmdline,
                   struct in_addr *guest_addr, int guest_port);
int slirp_add_guestfwd(Slirp *slirp, SlirpWriteCb write_cb, void *opaque,
                   struct in_addr *guest_addr, int guest_port);

char *slirp_connection_info(Slirp *slirp);

void slirp_socket_recv(Slirp *slirp, struct in_addr guest_addr,
                       int guest_port, const uint8_t *buf, int size);
size_t slirp_socket_can_recv(Slirp *slirp, struct in_addr guest_addr,
                             int guest_port);

#endif
