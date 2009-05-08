#ifndef _LIBSLIRP_H
#define _LIBSLIRP_H

#ifdef __cplusplus
extern "C" {
#endif

void slirp_init(int restricted, const char *special_ip);

void slirp_select_fill(int *pnfds,
                       fd_set *readfds, fd_set *writefds, fd_set *xfds);

void slirp_select_poll(fd_set *readfds, fd_set *writefds, fd_set *xfds);

void slirp_input(const uint8_t *pkt, int pkt_len);

/* you must provide the following functions: */
int slirp_can_output(void);
void slirp_output(const uint8_t *pkt, int pkt_len);

void slirp_redir_loop(void (*func)(void *opaque, int is_udp,
                                  struct in_addr *laddr, u_int lport,              
                                  struct in_addr *faddr, u_int fport),
                     void *opaque);
int slirp_redir_rm(int is_udp, int host_port);
int slirp_redir(int is_udp, int host_port,
                struct in_addr guest_addr, int guest_port);
int slirp_add_exec(int do_pty, const void *args, int addr_low_byte,
                   int guest_port);

extern const char *tftp_prefix;
extern char slirp_hostname[33];
extern const char *bootp_filename;

void slirp_stats(void);
void slirp_socket_recv(int addr_low_byte, int guest_port, const uint8_t *buf,
		int size);
size_t slirp_socket_can_recv(int addr_low_byte, int guest_port);

#ifdef __cplusplus
}
#endif

#endif
