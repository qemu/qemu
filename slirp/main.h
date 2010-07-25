/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#define TOWRITEMAX 512

extern int slirp_socket;
extern int slirp_socket_unit;
extern int slirp_socket_port;
extern uint32_t slirp_socket_addr;
extern char *slirp_socket_passwd;
extern int ctty_closed;

/*
 * Get the difference in 2 times from updtim()
 * Allow for wraparound times, "just in case"
 * x is the greater of the 2 (current time) and y is
 * what it's being compared against.
 */
#define TIME_DIFF(x,y) (x)-(y) < 0 ? ~0-(y)+(x) : (x)-(y)

extern char *slirp_tty;
extern char *exec_shell;
extern u_int curtime;
extern fd_set *global_readfds, *global_writefds, *global_xfds;
extern struct in_addr loopback_addr;
extern char *username;
extern char *socket_path;
extern int towrite_max;
extern int ppp_exit;
extern int tcp_keepintvl;

#define PROTO_SLIP 0x1
#ifdef USE_PPP
#define PROTO_PPP 0x2
#endif

void if_encap(Slirp *slirp, const uint8_t *ip_data, int ip_data_len);
ssize_t slirp_send(struct socket *so, const void *buf, size_t len, int flags);
