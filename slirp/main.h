/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifndef SLIRP_MAIN_H
#define SLIRP_MAIN_H

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

extern u_int curtime;
extern struct in_addr loopback_addr;
extern unsigned long loopback_mask;

int if_encap(Slirp *slirp, struct mbuf *ifm);
ssize_t slirp_send(struct socket *so, const void *buf, size_t len, int flags);

#endif
