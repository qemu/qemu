/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)ip_icmp.c	8.2 (Berkeley) 1/4/94
 * ip_icmp.c,v 1.7 1995/05/30 08:09:42 rgrimes Exp
 */

#include "qemu/osdep.h"
#include "slirp.h"
#include "ip_icmp.h"

/* The message sent when emulating PING */
/* Be nice and tell them it's just a pseudo-ping packet */
static const char icmp_ping_msg[] = "This is a pseudo-PING packet used by Slirp to emulate ICMP ECHO-REQUEST packets.\n";

/* list of actions for icmp_send_error() on RX of an icmp message */
static const int icmp_flush[19] = {
/*  ECHO REPLY (0)  */   0,
		         1,
		         1,
/* DEST UNREACH (3) */   1,
/* SOURCE QUENCH (4)*/   1,
/* REDIRECT (5) */       1,
		         1,
		         1,
/* ECHO (8) */           0,
/* ROUTERADVERT (9) */   1,
/* ROUTERSOLICIT (10) */ 1,
/* TIME EXCEEDED (11) */ 1,
/* PARAMETER PROBLEM (12) */ 1,
/* TIMESTAMP (13) */     0,
/* TIMESTAMP REPLY (14) */ 0,
/* INFO (15) */          0,
/* INFO REPLY (16) */    0,
/* ADDR MASK (17) */     0,
/* ADDR MASK REPLY (18) */ 0
};

void icmp_init(Slirp *slirp)
{
    slirp->icmp.so_next = slirp->icmp.so_prev = &slirp->icmp;
    slirp->icmp_last_so = &slirp->icmp;
}

void icmp_cleanup(Slirp *slirp)
{
    while (slirp->icmp.so_next != &slirp->icmp) {
        icmp_detach(slirp->icmp.so_next);
    }
}

static int icmp_send(struct socket *so, struct mbuf *m, int hlen)
{
    struct ip *ip = mtod(m, struct ip *);
    struct sockaddr_in addr;

    so->s = qemu_socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (so->s == -1) {
        return -1;
    }

    so->so_m = m;
    so->so_faddr = ip->ip_dst;
    so->so_laddr = ip->ip_src;
    so->so_iptos = ip->ip_tos;
    so->so_type = IPPROTO_ICMP;
    so->so_state = SS_ISFCONNECTED;
    so->so_expire = curtime + SO_EXPIRE;

    addr.sin_family = AF_INET;
    addr.sin_addr = so->so_faddr;

    insque(so, &so->slirp->icmp);

    if (sendto(so->s, m->m_data + hlen, m->m_len - hlen, 0,
               (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        DEBUG_MISC((dfd, "icmp_input icmp sendto tx errno = %d-%s\n",
                    errno, strerror(errno)));
        icmp_send_error(m, ICMP_UNREACH, ICMP_UNREACH_NET, 0, strerror(errno));
        icmp_detach(so);
    }

    return 0;
}

void icmp_detach(struct socket *so)
{
    closesocket(so->s);
    sofree(so);
}

/*
 * Process a received ICMP message.
 */
void
icmp_input(struct mbuf *m, int hlen)
{
  register struct icmp *icp;
  register struct ip *ip=mtod(m, struct ip *);
  int icmplen=ip->ip_len;
  Slirp *slirp = m->slirp;

  DEBUG_CALL("icmp_input");
  DEBUG_ARG("m = %p", m);
  DEBUG_ARG("m_len = %d", m->m_len);

  /*
   * Locate icmp structure in mbuf, and check
   * that its not corrupted and of at least minimum length.
   */
  if (icmplen < ICMP_MINLEN) {          /* min 8 bytes payload */
  freeit:
    m_free(m);
    goto end_error;
  }

  m->m_len -= hlen;
  m->m_data += hlen;
  icp = mtod(m, struct icmp *);
  if (cksum(m, icmplen)) {
    goto freeit;
  }
  m->m_len += hlen;
  m->m_data -= hlen;

  DEBUG_ARG("icmp_type = %d", icp->icmp_type);
  switch (icp->icmp_type) {
  case ICMP_ECHO:
    ip->ip_len += hlen;	             /* since ip_input subtracts this */
    if (ip->ip_dst.s_addr == slirp->vhost_addr.s_addr ||
        ip->ip_dst.s_addr == slirp->vnameserver_addr.s_addr) {
        icmp_reflect(m);
    } else if (slirp->restricted) {
        goto freeit;
    } else {
      struct socket *so;
      struct sockaddr_storage addr;
      so = socreate(slirp);
      if (icmp_send(so, m, hlen) == 0) {
        return;
      }
      if (udp_attach(so, AF_INET) == -1) {
	DEBUG_MISC((dfd,"icmp_input udp_attach errno = %d-%s\n",
		    errno,strerror(errno)));
	sofree(so);
	m_free(m);
	goto end_error;
      }
      so->so_m = m;
      so->so_ffamily = AF_INET;
      so->so_faddr = ip->ip_dst;
      so->so_fport = htons(7);
      so->so_lfamily = AF_INET;
      so->so_laddr = ip->ip_src;
      so->so_lport = htons(9);
      so->so_iptos = ip->ip_tos;
      so->so_type = IPPROTO_ICMP;
      so->so_state = SS_ISFCONNECTED;

      /* Send the packet */
      addr = so->fhost.ss;
      sotranslate_out(so, &addr);

      if(sendto(so->s, icmp_ping_msg, strlen(icmp_ping_msg), 0,
		(struct sockaddr *)&addr, sockaddr_size(&addr)) == -1) {
	DEBUG_MISC((dfd,"icmp_input udp sendto tx errno = %d-%s\n",
		    errno,strerror(errno)));
	icmp_send_error(m, ICMP_UNREACH, ICMP_UNREACH_NET, 0, strerror(errno));
	udp_detach(so);
      }
    } /* if ip->ip_dst.s_addr == alias_addr.s_addr */
    break;
  case ICMP_UNREACH:
    /* XXX? report error? close socket? */
  case ICMP_TIMXCEED:
  case ICMP_PARAMPROB:
  case ICMP_SOURCEQUENCH:
  case ICMP_TSTAMP:
  case ICMP_MASKREQ:
  case ICMP_REDIRECT:
    m_free(m);
    break;

  default:
    m_free(m);
  } /* swith */

end_error:
  /* m is m_free()'d xor put in a socket xor or given to ip_send */
  return;
}


/*
 *	Send an ICMP message in response to a situation
 *
 *	RFC 1122: 3.2.2	MUST send at least the IP header and 8 bytes of header. MAY send more (we do).
 *			MUST NOT change this header information.
 *			MUST NOT reply to a multicast/broadcast IP address.
 *			MUST NOT reply to a multicast/broadcast MAC address.
 *			MUST reply to only the first fragment.
 */
/*
 * Send ICMP_UNREACH back to the source regarding msrc.
 * mbuf *msrc is used as a template, but is NOT m_free()'d.
 * It is reported as the bad ip packet.  The header should
 * be fully correct and in host byte order.
 * ICMP fragmentation is illegal.  All machines must accept 576 bytes in one
 * packet.  The maximum payload is 576-20(ip hdr)-8(icmp hdr)=548
 */

#define ICMP_MAXDATALEN (IP_MSS-28)
void
icmp_send_error(struct mbuf *msrc, u_char type, u_char code, int minsize,
           const char *message)
{
  unsigned hlen, shlen, s_ip_len;
  register struct ip *ip;
  register struct icmp *icp;
  register struct mbuf *m;

  DEBUG_CALL("icmp_send_error");
  DEBUG_ARG("msrc = %p", msrc);
  DEBUG_ARG("msrc_len = %d", msrc->m_len);

  if(type!=ICMP_UNREACH && type!=ICMP_TIMXCEED) goto end_error;

  /* check msrc */
  if(!msrc) goto end_error;
  ip = mtod(msrc, struct ip *);
#ifdef DEBUG
  { char bufa[20], bufb[20];
    strcpy(bufa, inet_ntoa(ip->ip_src));
    strcpy(bufb, inet_ntoa(ip->ip_dst));
    DEBUG_MISC((dfd, " %.16s to %.16s\n", bufa, bufb));
  }
#endif
  if(ip->ip_off & IP_OFFMASK) goto end_error;    /* Only reply to fragment 0 */

  /* Do not reply to source-only IPs */
  if ((ip->ip_src.s_addr & htonl(~(0xf << 28))) == 0) {
      goto end_error;
  }

  shlen=ip->ip_hl << 2;
  s_ip_len=ip->ip_len;
  if(ip->ip_p == IPPROTO_ICMP) {
    icp = (struct icmp *)((char *)ip + shlen);
    /*
     *	Assume any unknown ICMP type is an error. This isn't
     *	specified by the RFC, but think about it..
     */
    if(icp->icmp_type>18 || icmp_flush[icp->icmp_type]) goto end_error;
  }

  /* make a copy */
  m = m_get(msrc->slirp);
  if (!m) {
      goto end_error;
  }

  { int new_m_size;
    new_m_size=sizeof(struct ip )+ICMP_MINLEN+msrc->m_len+ICMP_MAXDATALEN;
    if(new_m_size>m->m_size) m_inc(m, new_m_size);
  }
  memcpy(m->m_data, msrc->m_data, msrc->m_len);
  m->m_len = msrc->m_len;                        /* copy msrc to m */

  /* make the header of the reply packet */
  ip  = mtod(m, struct ip *);
  hlen= sizeof(struct ip );     /* no options in reply */

  /* fill in icmp */
  m->m_data += hlen;
  m->m_len -= hlen;

  icp = mtod(m, struct icmp *);

  if(minsize) s_ip_len=shlen+ICMP_MINLEN;   /* return header+8b only */
  else if(s_ip_len>ICMP_MAXDATALEN)         /* maximum size */
    s_ip_len=ICMP_MAXDATALEN;

  m->m_len=ICMP_MINLEN+s_ip_len;        /* 8 bytes ICMP header */

  /* min. size = 8+sizeof(struct ip)+8 */

  icp->icmp_type = type;
  icp->icmp_code = code;
  icp->icmp_id = 0;
  icp->icmp_seq = 0;

  memcpy(&icp->icmp_ip, msrc->m_data, s_ip_len);   /* report the ip packet */
  HTONS(icp->icmp_ip.ip_len);
  HTONS(icp->icmp_ip.ip_id);
  HTONS(icp->icmp_ip.ip_off);

#ifdef DEBUG
  if(message) {           /* DEBUG : append message to ICMP packet */
    int message_len;
    char *cpnt;
    message_len=strlen(message);
    if(message_len>ICMP_MAXDATALEN) message_len=ICMP_MAXDATALEN;
    cpnt=(char *)m->m_data+m->m_len;
    memcpy(cpnt, message, message_len);
    m->m_len+=message_len;
  }
#endif

  icp->icmp_cksum = 0;
  icp->icmp_cksum = cksum(m, m->m_len);

  m->m_data -= hlen;
  m->m_len += hlen;

  /* fill in ip */
  ip->ip_hl = hlen >> 2;
  ip->ip_len = m->m_len;

  ip->ip_tos=((ip->ip_tos & 0x1E) | 0xC0);  /* high priority for errors */

  ip->ip_ttl = MAXTTL;
  ip->ip_p = IPPROTO_ICMP;
  ip->ip_dst = ip->ip_src;    /* ip addresses */
  ip->ip_src = m->slirp->vhost_addr;

  (void ) ip_output((struct socket *)NULL, m);

end_error:
  return;
}
#undef ICMP_MAXDATALEN

/*
 * Reflect the ip packet back to the source
 */
void
icmp_reflect(struct mbuf *m)
{
  register struct ip *ip = mtod(m, struct ip *);
  int hlen = ip->ip_hl << 2;
  int optlen = hlen - sizeof(struct ip );
  register struct icmp *icp;

  /*
   * Send an icmp packet back to the ip level,
   * after supplying a checksum.
   */
  m->m_data += hlen;
  m->m_len -= hlen;
  icp = mtod(m, struct icmp *);

  icp->icmp_type = ICMP_ECHOREPLY;
  icp->icmp_cksum = 0;
  icp->icmp_cksum = cksum(m, ip->ip_len - hlen);

  m->m_data -= hlen;
  m->m_len += hlen;

  /* fill in ip */
  if (optlen > 0) {
    /*
     * Strip out original options by copying rest of first
     * mbuf's data back, and adjust the IP length.
     */
    memmove((caddr_t)(ip + 1), (caddr_t)ip + hlen,
	    (unsigned )(m->m_len - hlen));
    hlen -= optlen;
    ip->ip_hl = hlen >> 2;
    ip->ip_len -= optlen;
    m->m_len -= optlen;
  }

  ip->ip_ttl = MAXTTL;
  { /* swap */
    struct in_addr icmp_dst;
    icmp_dst = ip->ip_dst;
    ip->ip_dst = ip->ip_src;
    ip->ip_src = icmp_dst;
  }

  (void ) ip_output((struct socket *)NULL, m);
}

void icmp_receive(struct socket *so)
{
    struct mbuf *m = so->so_m;
    struct ip *ip = mtod(m, struct ip *);
    int hlen = ip->ip_hl << 2;
    u_char error_code;
    struct icmp *icp;
    int id, len;

    m->m_data += hlen;
    m->m_len -= hlen;
    icp = mtod(m, struct icmp *);

    id = icp->icmp_id;
    len = qemu_recv(so->s, icp, M_ROOM(m), 0);
    /*
     * The behavior of reading SOCK_DGRAM+IPPROTO_ICMP sockets is inconsistent
     * between host OSes.  On Linux, only the ICMP header and payload is
     * included.  On macOS/Darwin, the socket acts like a raw socket and
     * includes the IP header as well.  On other BSDs, SOCK_DGRAM+IPPROTO_ICMP
     * sockets aren't supported at all, so we treat them like raw sockets.  It
     * isn't possible to detect this difference at runtime, so we must use an
     * #ifdef to determine if we need to remove the IP header.
     */
#ifdef CONFIG_BSD
    if (len >= sizeof(struct ip)) {
        struct ip *inner_ip = mtod(m, struct ip *);
        int inner_hlen = inner_ip->ip_hl << 2;
        if (inner_hlen > len) {
            len = -1;
            errno = -EINVAL;
        } else {
            len -= inner_hlen;
            memmove(icp, (unsigned char *)icp + inner_hlen, len);
        }
    } else {
      len = -1;
      errno = -EINVAL;
    }
#endif
    icp->icmp_id = id;

    m->m_data -= hlen;
    m->m_len += hlen;

    if (len == -1 || len == 0) {
        if (errno == ENETUNREACH) {
            error_code = ICMP_UNREACH_NET;
        } else {
            error_code = ICMP_UNREACH_HOST;
        }
        DEBUG_MISC((dfd, " udp icmp rx errno = %d-%s\n", errno,
                    strerror(errno)));
        icmp_send_error(so->so_m, ICMP_UNREACH, error_code, 0, strerror(errno));
    } else {
        icmp_reflect(so->so_m);
        so->so_m = NULL; /* Don't m_free() it again! */
    }
    icmp_detach(so);
}
