#ifndef _LIBSLIRP_H
#define _LIBSLIRP_H

#include <sys/select.h>

void slirp_init(void);

void slirp_select_fill(int *pnfds, 
                       fd_set *readfds, fd_set *writefds, fd_set *xfds);

void slirp_select_poll(fd_set *readfds, fd_set *writefds, fd_set *xfds);

void slirp_input(const uint8_t *pkt, int pkt_len);

/* you must provide the following functions: */
int slirp_can_output(void);
void slirp_output(const uint8_t *pkt, int pkt_len);

#endif
