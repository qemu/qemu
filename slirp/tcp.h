/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp.h	8.1 (Berkeley) 6/10/93
 * tcp.h,v 1.3 1994/08/21 05:27:34 paul Exp
 */

#ifndef _TCP_H_
#define _TCP_H_

typedef	uint32_t tcp_seq;

#define      PR_SLOWHZ       2               /* 2 slow timeouts per second (approx) */
#define      PR_FASTHZ       5               /* 5 fast timeouts per second (not important) */

#define TCP_SNDSPACE 8192
#define TCP_RCVSPACE 8192

/*
 * TCP header.
 * Per RFC 793, September, 1981.
 */
#define tcphdr slirp_tcphdr
struct tcphdr {
	uint16_t th_sport;              /* source port */
	uint16_t th_dport;              /* destination port */
	tcp_seq	th_seq;			/* sequence number */
	tcp_seq	th_ack;			/* acknowledgement number */
#ifdef HOST_WORDS_BIGENDIAN
	uint8_t	th_off:4,		/* data offset */
		th_x2:4;		/* (unused) */
#else
	uint8_t	th_x2:4,		/* (unused) */
		th_off:4;		/* data offset */
#endif
	uint8_t th_flags;
	uint16_t th_win;                /* window */
	uint16_t th_sum;                /* checksum */
	uint16_t th_urp;                /* urgent pointer */
};

#include "tcp_var.h"

#ifndef TH_FIN
#define	TH_FIN	0x01
#define	TH_SYN	0x02
#define	TH_RST	0x04
#define	TH_PUSH	0x08
#define	TH_ACK	0x10
#define	TH_URG	0x20
#endif

#ifndef TCPOPT_EOL
#define	TCPOPT_EOL		0
#define	TCPOPT_NOP		1
#define	TCPOPT_MAXSEG		2
#define    TCPOLEN_MAXSEG		4
#define TCPOPT_WINDOW		3
#define    TCPOLEN_WINDOW		3
#define TCPOPT_SACK_PERMITTED	4		/* Experimental */
#define    TCPOLEN_SACK_PERMITTED	2
#define TCPOPT_SACK		5		/* Experimental */
#define TCPOPT_TIMESTAMP	8
#define    TCPOLEN_TIMESTAMP		10
#define    TCPOLEN_TSTAMP_APPA		(TCPOLEN_TIMESTAMP+2) /* appendix A */

#define TCPOPT_TSTAMP_HDR	\
    (TCPOPT_NOP<<24|TCPOPT_NOP<<16|TCPOPT_TIMESTAMP<<8|TCPOLEN_TIMESTAMP)
#endif

/*
 * Default maximum segment size for TCP.
 * With an IP MSS of 576, this is 536,
 * but 512 is probably more convenient.
 * This should be defined as MIN(512, IP_MSS - sizeof (struct tcpiphdr)).
 *
 * We make this 1460 because we only care about Ethernet in the qemu context.
 */
#undef TCP_MSS
#define	TCP_MSS	1460

#undef TCP_MAXWIN
#define	TCP_MAXWIN	65535	/* largest value for (unscaled) window */

#undef TCP_MAX_WINSHIFT
#define TCP_MAX_WINSHIFT	14	/* maximum window shift */

/*
 * User-settable options (used with setsockopt).
 *
 * We don't use the system headers on unix because we have conflicting
 * local structures. We can't avoid the system definitions on Windows,
 * so we undefine them.
 */
#undef TCP_NODELAY
#define	TCP_NODELAY	0x01	/* don't delay send to coalesce packets */
#undef TCP_MAXSEG

/*
 * TCP FSM state definitions.
 * Per RFC793, September, 1981.
 */

#define TCP_NSTATES     11

#define TCPS_CLOSED             0       /* closed */
#define TCPS_LISTEN             1       /* listening for connection */
#define TCPS_SYN_SENT           2       /* active, have sent syn */
#define TCPS_SYN_RECEIVED       3       /* have send and received syn */
/* states < TCPS_ESTABLISHED are those where connections not established */
#define TCPS_ESTABLISHED        4       /* established */
#define TCPS_CLOSE_WAIT         5       /* rcvd fin, waiting for close */
/* states > TCPS_CLOSE_WAIT are those where user has closed */
#define TCPS_FIN_WAIT_1         6       /* have closed, sent fin */
#define TCPS_CLOSING            7       /* closed xchd FIN; await FIN ACK */
#define TCPS_LAST_ACK           8       /* had fin and close; await FIN ACK */
/* states > TCPS_CLOSE_WAIT && < TCPS_FIN_WAIT_2 await ACK of FIN */
#define TCPS_FIN_WAIT_2         9       /* have closed, fin is acked */
#define TCPS_TIME_WAIT          10      /* in 2*msl quiet wait after close */

#define TCPS_HAVERCVDSYN(s)     ((s) >= TCPS_SYN_RECEIVED)
#define TCPS_HAVEESTABLISHED(s) ((s) >= TCPS_ESTABLISHED)
#define TCPS_HAVERCVDFIN(s)     ((s) >= TCPS_TIME_WAIT)

/*
 * TCP sequence numbers are 32 bit integers operated
 * on with modular arithmetic.  These macros can be
 * used to compare such integers.
 */
#define SEQ_LT(a,b)     ((int)((a)-(b)) < 0)
#define SEQ_LEQ(a,b)    ((int)((a)-(b)) <= 0)
#define SEQ_GT(a,b)     ((int)((a)-(b)) > 0)
#define SEQ_GEQ(a,b)    ((int)((a)-(b)) >= 0)

/*
 * Macros to initialize tcp sequence numbers for
 * send and receive from initial send and receive
 * sequence numbers.
 */
#define tcp_rcvseqinit(tp) \
     (tp)->rcv_adv = (tp)->rcv_nxt = (tp)->irs + 1

#define tcp_sendseqinit(tp) \
    (tp)->snd_una = (tp)->snd_nxt = (tp)->snd_max = (tp)->snd_up = (tp)->iss

#define TCP_ISSINCR     (125*1024)      /* increment for tcp_iss each second */

#endif
