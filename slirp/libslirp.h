#ifndef LIBSLIRP_H
#define LIBSLIRP_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <in6addr.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

typedef struct Slirp Slirp;

typedef ssize_t (*SlirpWriteCb)(const void *buf, size_t len, void *opaque);
typedef void (*SlirpTimerCb)(void *opaque);

/*
 * Callbacks from slirp
 */
typedef struct SlirpCb {
    /*
     * Send an ethernet frame to the guest network. The opaque
     * parameter is the one given to slirp_init(). The function
     * doesn't need to send all the data and may return <len (no
     * buffering is done on libslirp side, so the data will be dropped
     * in this case). <0 reports an IO error.
     */
    SlirpWriteCb send_packet;
    /* Print a message for an error due to guest misbehavior.  */
    void (*guest_error)(const char *msg);
    /* Return the virtual clock value in nanoseconds */
    int64_t (*clock_get_ns)(void);
    /* Create a new timer with the given callback and opaque data */
    void *(*timer_new)(SlirpTimerCb cb, void *opaque);
    /* Remove and free a timer */
    void (*timer_free)(void *timer);
    /* Modify a timer to expire at @expire_time */
    void (*timer_mod)(void *timer, int64_t expire_time);
    /* Register a fd for future polling */
    void (*register_poll_fd)(int fd);
    /* Unregister a fd */
    void (*unregister_poll_fd)(int fd);
    /* Kick the io-thread, to signal that new events may be processed */
    void (*notify)(void);
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

void slirp_pollfds_fill(Slirp *slirp, GArray *pollfds, uint32_t *timeout);

void slirp_pollfds_poll(Slirp *slirp, GArray *pollfds, int select_error);

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
