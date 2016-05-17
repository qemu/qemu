#ifndef __COMMON_H__
#define __COMMON_H__

#include "slirp_config.h"

#ifdef _WIN32

typedef char *caddr_t;

# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <sys/timeb.h>
# include <iphlpapi.h>

#else
# if !defined(__HAIKU__)
#  define O_BINARY 0
# endif
#endif

#ifdef HAVE_SYS_BITYPES_H
# include <sys/bitypes.h>
#endif

#ifndef _WIN32
#include <sys/uio.h>
#endif

#ifndef _WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifndef NO_UNIX_SOCKETS
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#endif
#ifndef _WIN32
#include <sys/socket.h>
#endif

#if defined(HAVE_SYS_IOCTL_H)
# include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif

/* Avoid conflicting with the libc insque() and remque(), which
   have different prototypes. */
#define insque slirp_insque
#define remque slirp_remque
#define quehead slirp_quehead

#ifdef HAVE_SYS_STROPTS_H
#include <sys/stropts.h>
#endif

#include <glib.h>

#include "debug.h"

#include "qemu/queue.h"
#include "qemu/sockets.h"
#include "net/eth.h"

#include "libslirp.h"
#include "ip.h"
#include "ip6.h"
#include "tcp.h"
#include "tcp_timer.h"
#include "tcp_var.h"
#include "tcpip.h"
#include "udp.h"
#include "ip_icmp.h"
#include "ip6_icmp.h"
#include "mbuf.h"
#include "sbuf.h"
#include "socket.h"
#include "if.h"
#include "main.h"
#include "misc.h"

#include "bootp.h"
#include "tftp.h"

#define ARPOP_REQUEST 1         /* ARP request */
#define ARPOP_REPLY   2         /* ARP reply   */

struct ethhdr {
    unsigned char  h_dest[ETH_ALEN];   /* destination eth addr */
    unsigned char  h_source[ETH_ALEN]; /* source ether addr    */
    unsigned short h_proto;            /* packet type ID field */
};

struct arphdr {
    unsigned short ar_hrd;      /* format of hardware address */
    unsigned short ar_pro;      /* format of protocol address */
    unsigned char  ar_hln;      /* length of hardware address */
    unsigned char  ar_pln;      /* length of protocol address */
    unsigned short ar_op;       /* ARP opcode (command)       */

    /*
     *  Ethernet looks like this : This bit is variable sized however...
     */
    unsigned char ar_sha[ETH_ALEN]; /* sender hardware address */
    uint32_t      ar_sip;           /* sender IP address       */
    unsigned char ar_tha[ETH_ALEN]; /* target hardware address */
    uint32_t      ar_tip;           /* target IP address       */
} QEMU_PACKED;

#define ARP_TABLE_SIZE 16

typedef struct ArpTable {
    struct arphdr table[ARP_TABLE_SIZE];
    int next_victim;
} ArpTable;

void arp_table_add(Slirp *slirp, uint32_t ip_addr, uint8_t ethaddr[ETH_ALEN]);

bool arp_table_search(Slirp *slirp, uint32_t ip_addr,
                      uint8_t out_ethaddr[ETH_ALEN]);

struct ndpentry {
    unsigned char   eth_addr[ETH_ALEN];     /* sender hardware address */
    struct in6_addr ip_addr;                /* sender IP address       */
} QEMU_PACKED;

#define NDP_TABLE_SIZE 16

typedef struct NdpTable {
    struct ndpentry table[NDP_TABLE_SIZE];
    int next_victim;
} NdpTable;

void ndp_table_add(Slirp *slirp, struct in6_addr ip_addr,
                   uint8_t ethaddr[ETH_ALEN]);
bool ndp_table_search(Slirp *slirp, struct in6_addr ip_addr,
                      uint8_t out_ethaddr[ETH_ALEN]);

struct Slirp {
    QTAILQ_ENTRY(Slirp) entry;
    u_int time_fasttimo;
    u_int last_slowtimo;
    bool do_slowtimo;

    bool in_enabled, in6_enabled;

    /* virtual network configuration */
    struct in_addr vnetwork_addr;
    struct in_addr vnetwork_mask;
    struct in_addr vhost_addr;
    struct in6_addr vprefix_addr6;
    uint8_t vprefix_len;
    struct in6_addr vhost_addr6;
    struct in_addr vdhcp_startaddr;
    struct in_addr vnameserver_addr;
    struct in6_addr vnameserver_addr6;

    struct in_addr client_ipaddr;
    char client_hostname[33];

    int restricted;
    struct ex_list *exec_list;

    /* mbuf states */
    struct quehead m_freelist;
    struct quehead m_usedlist;
    int mbuf_alloced;

    /* if states */
    struct quehead if_fastq;   /* fast queue (for interactive data) */
    struct quehead if_batchq;  /* queue for non-interactive data */
    struct mbuf *next_m;    /* pointer to next mbuf to output */
    bool if_start_busy;     /* avoid if_start recursion */

    /* ip states */
    struct ipq ipq;         /* ip reass. queue */
    uint16_t ip_id;         /* ip packet ctr, for ids */

    /* bootp/dhcp states */
    BOOTPClient bootp_clients[NB_BOOTP_CLIENTS];
    char *bootp_filename;
    size_t vdnssearch_len;
    uint8_t *vdnssearch;

    /* tcp states */
    struct socket tcb;
    struct socket *tcp_last_so;
    tcp_seq tcp_iss;        /* tcp initial send seq # */
    uint32_t tcp_now;       /* for RFC 1323 timestamps */

    /* udp states */
    struct socket udb;
    struct socket *udp_last_so;

    /* icmp states */
    struct socket icmp;
    struct socket *icmp_last_so;

    /* tftp states */
    char *tftp_prefix;
    struct tftp_session tftp_sessions[TFTP_SESSIONS_MAX];

    ArpTable arp_table;
    NdpTable ndp_table;

    GRand *grand;
    QEMUTimer *ra_timer;

    void *opaque;
};

extern Slirp *slirp_instance;

#ifndef NULL
#define NULL (void *)0
#endif

void if_start(Slirp *);

#ifndef _WIN32
#include <netdb.h>
#endif

#define SO_OPTIONS DO_KEEPALIVE
#define TCP_MAXIDLE (TCPTV_KEEPCNT * TCPTV_KEEPINTVL)

/* dnssearch.c */
int translate_dnssearch(Slirp *s, const char ** names);

/* cksum.c */
int cksum(struct mbuf *m, int len);
int ip6_cksum(struct mbuf *m);

/* if.c */
void if_init(Slirp *);
void if_output(struct socket *, struct mbuf *);

/* ip_input.c */
void ip_init(Slirp *);
void ip_cleanup(Slirp *);
void ip_input(struct mbuf *);
void ip_slowtimo(Slirp *);
void ip_stripoptions(register struct mbuf *, struct mbuf *);

/* ip_output.c */
int ip_output(struct socket *, struct mbuf *);

/* ip6_input.c */
void ip6_init(Slirp *);
void ip6_cleanup(Slirp *);
void ip6_input(struct mbuf *);

/* ip6_output */
int ip6_output(struct socket *, struct mbuf *, int fast);

/* tcp_input.c */
void tcp_input(register struct mbuf *, int, struct socket *, unsigned short af);
int tcp_mss(register struct tcpcb *, u_int);

/* tcp_output.c */
int tcp_output(register struct tcpcb *);
void tcp_setpersist(register struct tcpcb *);

/* tcp_subr.c */
void tcp_init(Slirp *);
void tcp_cleanup(Slirp *);
void tcp_template(struct tcpcb *);
void tcp_respond(struct tcpcb *, register struct tcpiphdr *,
        register struct mbuf *, tcp_seq, tcp_seq, int, unsigned short);
struct tcpcb * tcp_newtcpcb(struct socket *);
struct tcpcb * tcp_close(register struct tcpcb *);
void tcp_sockclosed(struct tcpcb *);
int tcp_fconnect(struct socket *, unsigned short af);
void tcp_connect(struct socket *);
int tcp_attach(struct socket *);
uint8_t tcp_tos(struct socket *);
int tcp_emu(struct socket *, struct mbuf *);
int tcp_ctl(struct socket *);
struct tcpcb *tcp_drop(struct tcpcb *tp, int err);

#ifndef _WIN32
#define min(x,y) ((x) < (y) ? (x) : (y))
#define max(x,y) ((x) > (y) ? (x) : (y))
#endif

#endif
