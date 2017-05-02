/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifndef SLIRP_SOCKET_H
#define SLIRP_SOCKET_H

#define SO_EXPIRE 240000
#define SO_EXPIREFAST 10000

/*
 * Our socket structure
 */

union slirp_sockaddr {
    struct sockaddr_storage ss;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};

struct socket {
  struct socket *so_next,*so_prev;      /* For a linked list of sockets */

  int s;                           /* The actual socket */

  int pollfds_idx;                 /* GPollFD GArray index */

  Slirp *slirp;			   /* managing slirp instance */

			/* XXX union these with not-yet-used sbuf params */
  struct mbuf *so_m;	           /* Pointer to the original SYN packet,
				    * for non-blocking connect()'s, and
				    * PING reply's */
  struct tcpiphdr *so_ti;	   /* Pointer to the original ti within
				    * so_mconn, for non-blocking connections */
  uint32_t      so_urgc;
  union slirp_sockaddr fhost;      /* Foreign host */
#define so_faddr fhost.sin.sin_addr
#define so_fport fhost.sin.sin_port
#define so_faddr6 fhost.sin6.sin6_addr
#define so_fport6 fhost.sin6.sin6_port
#define so_ffamily fhost.ss.ss_family

  union slirp_sockaddr lhost;      /* Local host */
#define so_laddr lhost.sin.sin_addr
#define so_lport lhost.sin.sin_port
#define so_laddr6 lhost.sin6.sin6_addr
#define so_lport6 lhost.sin6.sin6_port
#define so_lfamily lhost.ss.ss_family

  uint8_t	so_iptos;	/* Type of service */
  uint8_t	so_emu;		/* Is the socket emulated? */

  uint8_t       so_type;        /* Type of socket, UDP or TCP */
  int32_t       so_state;       /* internal state flags SS_*, below */

  struct 	tcpcb *so_tcpcb;	/* pointer to TCP protocol control block */
  u_int	so_expire;		/* When the socket will expire */

  int	so_queued;		/* Number of packets queued from this socket */
  int	so_nqueued;		/* Number of packets queued in a row
				 * Used to determine when to "downgrade" a session
					 * from fastq to batchq */

  struct sbuf so_rcv;		/* Receive buffer */
  struct sbuf so_snd;		/* Send buffer */
  void * extra;			/* Extra pointer */
};


/*
 * Socket state bits. (peer means the host on the Internet,
 * local host means the host on the other end of the modem)
 */
#define SS_NOFDREF		0x001	/* No fd reference */

#define SS_ISFCONNECTING	0x002	/* Socket is connecting to peer (non-blocking connect()'s) */
#define SS_ISFCONNECTED		0x004	/* Socket is connected to peer */
#define SS_FCANTRCVMORE		0x008	/* Socket can't receive more from peer (for half-closes) */
#define SS_FCANTSENDMORE	0x010	/* Socket can't send more to peer (for half-closes) */
#define SS_FWDRAIN		0x040	/* We received a FIN, drain data and set SS_FCANTSENDMORE */

#define SS_CTL			0x080
#define SS_FACCEPTCONN		0x100	/* Socket is accepting connections from a host on the internet */
#define SS_FACCEPTONCE		0x200	/* If set, the SS_FACCEPTCONN socket will die after one accept */

#define SS_PERSISTENT_MASK	0xf000	/* Unremovable state bits */
#define SS_HOSTFWD		0x1000	/* Socket describes host->guest forwarding */
#define SS_INCOMING		0x2000	/* Connection was initiated by a host on the internet */

static inline int sockaddr_equal(struct sockaddr_storage *a,
        struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family) {
        return 0;
    }

    switch (a->ss_family) {
    case AF_INET:
    {
        struct sockaddr_in *a4 = (struct sockaddr_in *) a;
        struct sockaddr_in *b4 = (struct sockaddr_in *) b;
        return a4->sin_addr.s_addr == b4->sin_addr.s_addr
               && a4->sin_port == b4->sin_port;
    }
    case AF_INET6:
    {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6 *) a;
        struct sockaddr_in6 *b6 = (struct sockaddr_in6 *) b;
        return (in6_equal(&a6->sin6_addr, &b6->sin6_addr)
                && a6->sin6_port == b6->sin6_port);
    }
    default:
        g_assert_not_reached();
    }

    return 0;
}

static inline socklen_t sockaddr_size(struct sockaddr_storage *a)
{
    switch (a->ss_family) {
    case AF_INET:
        return sizeof(struct sockaddr_in);
    case AF_INET6:
        return sizeof(struct sockaddr_in6);
    default:
        g_assert_not_reached();
    }
}

struct socket *solookup(struct socket **, struct socket *,
        struct sockaddr_storage *, struct sockaddr_storage *);
struct socket *socreate(Slirp *);
void sofree(struct socket *);
int soread(struct socket *);
int sorecvoob(struct socket *);
int sosendoob(struct socket *);
int sowrite(struct socket *);
void sorecvfrom(struct socket *);
int sosendto(struct socket *, struct mbuf *);
struct socket * tcp_listen(Slirp *, uint32_t, u_int, uint32_t, u_int,
                               int);
void soisfconnecting(register struct socket *);
void soisfconnected(register struct socket *);
void sofwdrain(struct socket *);
struct iovec; /* For win32 */
size_t sopreprbuf(struct socket *so, struct iovec *iov, int *np);
int soreadbuf(struct socket *so, const char *buf, int size);

void sotranslate_out(struct socket *, struct sockaddr_storage *);
void sotranslate_in(struct socket *, struct sockaddr_storage *);
void sotranslate_accept(struct socket *);


#endif /* SLIRP_SOCKET_H */
