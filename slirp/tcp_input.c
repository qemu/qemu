/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1994
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
 *	@(#)tcp_input.c	8.5 (Berkeley) 4/10/94
 * tcp_input.c,v 1.10 1994/10/13 18:36:32 wollman Exp
 */

/*
 * Changes and additions relating to SLiRP
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>
#include "ip_icmp.h"

struct socket tcb;

#define	TCPREXMTTHRESH 3
struct	socket *tcp_last_so = &tcb;

tcp_seq tcp_iss;                /* tcp initial send seq # */

#define TCP_PAWS_IDLE	(24 * 24 * 60 * 60 * PR_SLOWHZ)

/* for modulo comparisons of timestamps */
#define TSTMP_LT(a,b)	((int)((a)-(b)) < 0)
#define TSTMP_GEQ(a,b)	((int)((a)-(b)) >= 0)

/*
 * Insert segment ti into reassembly queue of tcp with
 * control block tp.  Return TH_FIN if reassembly now includes
 * a segment with FIN.  The macro form does the common case inline
 * (segment is the next to be received on an established connection,
 * and the queue is empty), avoiding linkage into and removal
 * from the queue and repetition of various conversions.
 * Set DELACK for segments received in order, but ack immediately
 * when segments are out of order (so fast retransmit can work).
 */
#ifdef TCP_ACK_HACK
#define TCP_REASS(tp, ti, m, so, flags) {\
       if ((ti)->ti_seq == (tp)->rcv_nxt && \
           tcpfrag_list_empty(tp) && \
           (tp)->t_state == TCPS_ESTABLISHED) {\
               if (ti->ti_flags & TH_PUSH) \
                       tp->t_flags |= TF_ACKNOW; \
               else \
                       tp->t_flags |= TF_DELACK; \
               (tp)->rcv_nxt += (ti)->ti_len; \
               flags = (ti)->ti_flags & TH_FIN; \
               STAT(tcpstat.tcps_rcvpack++);         \
               STAT(tcpstat.tcps_rcvbyte += (ti)->ti_len);   \
               if (so->so_emu) { \
		       if (tcp_emu((so),(m))) sbappend((so), (m)); \
	       } else \
	       	       sbappend((so), (m)); \
/*               sorwakeup(so); */ \
	} else {\
               (flags) = tcp_reass((tp), (ti), (m)); \
               tp->t_flags |= TF_ACKNOW; \
       } \
}
#else
#define	TCP_REASS(tp, ti, m, so, flags) { \
	if ((ti)->ti_seq == (tp)->rcv_nxt && \
        tcpfrag_list_empty(tp) && \
	    (tp)->t_state == TCPS_ESTABLISHED) { \
		tp->t_flags |= TF_DELACK; \
		(tp)->rcv_nxt += (ti)->ti_len; \
		flags = (ti)->ti_flags & TH_FIN; \
		STAT(tcpstat.tcps_rcvpack++);        \
		STAT(tcpstat.tcps_rcvbyte += (ti)->ti_len);  \
		if (so->so_emu) { \
			if (tcp_emu((so),(m))) sbappend(so, (m)); \
		} else \
			sbappend((so), (m)); \
/*		sorwakeup(so); */ \
	} else { \
		(flags) = tcp_reass((tp), (ti), (m)); \
		tp->t_flags |= TF_ACKNOW; \
	} \
}
#endif
static void tcp_dooptions(struct tcpcb *tp, u_char *cp, int cnt,
                          struct tcpiphdr *ti);
static void tcp_xmit_timer(register struct tcpcb *tp, int rtt);

static int
tcp_reass(register struct tcpcb *tp, register struct tcpiphdr *ti,
          struct mbuf *m)
{
	register struct tcpiphdr *q;
	struct socket *so = tp->t_socket;
	int flags;

	/*
	 * Call with ti==NULL after become established to
	 * force pre-ESTABLISHED data up to user socket.
	 */
        if (ti == NULL)
		goto present;

	/*
	 * Find a segment which begins after this one does.
	 */
	for (q = tcpfrag_list_first(tp); !tcpfrag_list_end(q, tp);
            q = tcpiphdr_next(q))
		if (SEQ_GT(q->ti_seq, ti->ti_seq))
			break;

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us.
	 */
	if (!tcpfrag_list_end(tcpiphdr_prev(q), tp)) {
		register int i;
		q = tcpiphdr_prev(q);
		/* conversion to int (in i) handles seq wraparound */
		i = q->ti_seq + q->ti_len - ti->ti_seq;
		if (i > 0) {
			if (i >= ti->ti_len) {
				STAT(tcpstat.tcps_rcvduppack++);
				STAT(tcpstat.tcps_rcvdupbyte += ti->ti_len);
				m_freem(m);
				/*
				 * Try to present any queued data
				 * at the left window edge to the user.
				 * This is needed after the 3-WHS
				 * completes.
				 */
				goto present;   /* ??? */
			}
			m_adj(m, i);
			ti->ti_len -= i;
			ti->ti_seq += i;
		}
		q = tcpiphdr_next(q);
	}
	STAT(tcpstat.tcps_rcvoopack++);
	STAT(tcpstat.tcps_rcvoobyte += ti->ti_len);
	ti->ti_mbuf = m;

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	while (!tcpfrag_list_end(q, tp)) {
		register int i = (ti->ti_seq + ti->ti_len) - q->ti_seq;
		if (i <= 0)
			break;
		if (i < q->ti_len) {
			q->ti_seq += i;
			q->ti_len -= i;
			m_adj(q->ti_mbuf, i);
			break;
		}
		q = tcpiphdr_next(q);
		m = tcpiphdr_prev(q)->ti_mbuf;
		remque(tcpiphdr2qlink(tcpiphdr_prev(q)));
		m_freem(m);
	}

	/*
	 * Stick new segment in its place.
	 */
	insque(tcpiphdr2qlink(ti), tcpiphdr2qlink(tcpiphdr_prev(q)));

present:
	/*
	 * Present data to user, advancing rcv_nxt through
	 * completed sequence space.
	 */
	if (!TCPS_HAVEESTABLISHED(tp->t_state))
		return (0);
	ti = tcpfrag_list_first(tp);
	if (tcpfrag_list_end(ti, tp) || ti->ti_seq != tp->rcv_nxt)
		return (0);
	if (tp->t_state == TCPS_SYN_RECEIVED && ti->ti_len)
		return (0);
	do {
		tp->rcv_nxt += ti->ti_len;
		flags = ti->ti_flags & TH_FIN;
		remque(tcpiphdr2qlink(ti));
		m = ti->ti_mbuf;
		ti = tcpiphdr_next(ti);
/*		if (so->so_state & SS_FCANTRCVMORE) */
		if (so->so_state & SS_FCANTSENDMORE)
			m_freem(m);
		else {
			if (so->so_emu) {
				if (tcp_emu(so,m)) sbappend(so, m);
			} else
				sbappend(so, m);
		}
	} while (ti != (struct tcpiphdr *)tp && ti->ti_seq == tp->rcv_nxt);
/*	sorwakeup(so); */
	return (flags);
}

/*
 * TCP input routine, follows pages 65-76 of the
 * protocol specification dated September, 1981 very closely.
 */
void
tcp_input(struct mbuf *m, int iphlen, struct socket *inso)
{
  	struct ip save_ip, *ip;
	register struct tcpiphdr *ti;
	caddr_t optp = NULL;
	int optlen = 0;
	int len, tlen, off;
        register struct tcpcb *tp = NULL;
	register int tiflags;
        struct socket *so = NULL;
	int todrop, acked, ourfinisacked, needoutput = 0;
/*	int dropsocket = 0; */
	int iss = 0;
	u_long tiwin;
	int ret;
/*	int ts_present = 0; */
    struct ex_list *ex_ptr;

	DEBUG_CALL("tcp_input");
	DEBUG_ARGS((dfd," m = %8lx  iphlen = %2d  inso = %lx\n",
		    (long )m, iphlen, (long )inso ));

	/*
	 * If called with m == 0, then we're continuing the connect
	 */
	if (m == NULL) {
		so = inso;

		/* Re-set a few variables */
		tp = sototcpcb(so);
		m = so->so_m;
                so->so_m = NULL;
		ti = so->so_ti;
		tiwin = ti->ti_win;
		tiflags = ti->ti_flags;

		goto cont_conn;
	}


	STAT(tcpstat.tcps_rcvtotal++);
	/*
	 * Get IP and TCP header together in first mbuf.
	 * Note: IP leaves IP header in first mbuf.
	 */
	ti = mtod(m, struct tcpiphdr *);
	if (iphlen > sizeof(struct ip )) {
	  ip_stripoptions(m, (struct mbuf *)0);
	  iphlen=sizeof(struct ip );
	}
	/* XXX Check if too short */


	/*
	 * Save a copy of the IP header in case we want restore it
	 * for sending an ICMP error message in response.
	 */
	ip=mtod(m, struct ip *);
	save_ip = *ip;
	save_ip.ip_len+= iphlen;

	/*
	 * Checksum extended TCP header and data.
	 */
	tlen = ((struct ip *)ti)->ip_len;
        tcpiphdr2qlink(ti)->next = tcpiphdr2qlink(ti)->prev = NULL;
        memset(&ti->ti_i.ih_mbuf, 0 , sizeof(struct mbuf_ptr));
	ti->ti_x1 = 0;
	ti->ti_len = htons((u_int16_t)tlen);
	len = sizeof(struct ip ) + tlen;
	/* keep checksum for ICMP reply
	 * ti->ti_sum = cksum(m, len);
	 * if (ti->ti_sum) { */
	if(cksum(m, len)) {
	  STAT(tcpstat.tcps_rcvbadsum++);
	  goto drop;
	}

	/*
	 * Check that TCP offset makes sense,
	 * pull out TCP options and adjust length.		XXX
	 */
	off = ti->ti_off << 2;
	if (off < sizeof (struct tcphdr) || off > tlen) {
	  STAT(tcpstat.tcps_rcvbadoff++);
	  goto drop;
	}
	tlen -= off;
	ti->ti_len = tlen;
	if (off > sizeof (struct tcphdr)) {
	  optlen = off - sizeof (struct tcphdr);
	  optp = mtod(m, caddr_t) + sizeof (struct tcpiphdr);

		/*
		 * Do quick retrieval of timestamp options ("options
		 * prediction?").  If timestamp is the only option and it's
		 * formatted as recommended in RFC 1323 appendix A, we
		 * quickly get the values now and not bother calling
		 * tcp_dooptions(), etc.
		 */
/*		if ((optlen == TCPOLEN_TSTAMP_APPA ||
 *		     (optlen > TCPOLEN_TSTAMP_APPA &&
 *			optp[TCPOLEN_TSTAMP_APPA] == TCPOPT_EOL)) &&
 *		     *(u_int32_t *)optp == htonl(TCPOPT_TSTAMP_HDR) &&
 *		     (ti->ti_flags & TH_SYN) == 0) {
 *			ts_present = 1;
 *			ts_val = ntohl(*(u_int32_t *)(optp + 4));
 *			ts_ecr = ntohl(*(u_int32_t *)(optp + 8));
 *			optp = NULL;   / * we've parsed the options * /
 *		}
 */
	}
	tiflags = ti->ti_flags;

	/*
	 * Convert TCP protocol specific fields to host format.
	 */
	NTOHL(ti->ti_seq);
	NTOHL(ti->ti_ack);
	NTOHS(ti->ti_win);
	NTOHS(ti->ti_urp);

	/*
	 * Drop TCP, IP headers and TCP options.
	 */
	m->m_data += sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);
	m->m_len  -= sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);

    if (slirp_restrict) {
        for (ex_ptr = exec_list; ex_ptr; ex_ptr = ex_ptr->ex_next)
            if (ex_ptr->ex_fport == ti->ti_dport &&
                    (ntohl(ti->ti_dst.s_addr) & 0xff) == ex_ptr->ex_addr)
                break;

        if (!ex_ptr)
            goto drop;
    }
	/*
	 * Locate pcb for segment.
	 */
findso:
	so = tcp_last_so;
	if (so->so_fport != ti->ti_dport ||
	    so->so_lport != ti->ti_sport ||
	    so->so_laddr.s_addr != ti->ti_src.s_addr ||
	    so->so_faddr.s_addr != ti->ti_dst.s_addr) {
		so = solookup(&tcb, ti->ti_src, ti->ti_sport,
			       ti->ti_dst, ti->ti_dport);
		if (so)
			tcp_last_so = so;
		STAT(tcpstat.tcps_socachemiss++);
	}

	/*
	 * If the state is CLOSED (i.e., TCB does not exist) then
	 * all data in the incoming segment is discarded.
	 * If the TCB exists but is in CLOSED state, it is embryonic,
	 * but should either do a listen or a connect soon.
	 *
	 * state == CLOSED means we've done socreate() but haven't
	 * attached it to a protocol yet...
	 *
	 * XXX If a TCB does not exist, and the TH_SYN flag is
	 * the only flag set, then create a session, mark it
	 * as if it was LISTENING, and continue...
	 */
        if (so == NULL) {
	  if ((tiflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) != TH_SYN)
	    goto dropwithreset;

	  if ((so = socreate()) == NULL)
	    goto dropwithreset;
	  if (tcp_attach(so) < 0) {
	    free(so); /* Not sofree (if it failed, it's not insqued) */
	    goto dropwithreset;
	  }

	  sbreserve(&so->so_snd, TCP_SNDSPACE);
	  sbreserve(&so->so_rcv, TCP_RCVSPACE);

	  /*		tcp_last_so = so; */  /* XXX ? */
	  /*		tp = sototcpcb(so);    */

	  so->so_laddr = ti->ti_src;
	  so->so_lport = ti->ti_sport;
	  so->so_faddr = ti->ti_dst;
	  so->so_fport = ti->ti_dport;

	  if ((so->so_iptos = tcp_tos(so)) == 0)
	    so->so_iptos = ((struct ip *)ti)->ip_tos;

	  tp = sototcpcb(so);
	  tp->t_state = TCPS_LISTEN;
	}

        /*
         * If this is a still-connecting socket, this probably
         * a retransmit of the SYN.  Whether it's a retransmit SYN
	 * or something else, we nuke it.
         */
        if (so->so_state & SS_ISFCONNECTING)
                goto drop;

	tp = sototcpcb(so);

	/* XXX Should never fail */
        if (tp == NULL)
		goto dropwithreset;
	if (tp->t_state == TCPS_CLOSED)
		goto drop;

	/* Unscale the window into a 32-bit value. */
/*	if ((tiflags & TH_SYN) == 0)
 *		tiwin = ti->ti_win << tp->snd_scale;
 *	else
 */
		tiwin = ti->ti_win;

	/*
	 * Segment received on connection.
	 * Reset idle time and keep-alive timer.
	 */
	tp->t_idle = 0;
	if (SO_OPTIONS)
	   tp->t_timer[TCPT_KEEP] = TCPTV_KEEPINTVL;
	else
	   tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_IDLE;

	/*
	 * Process options if not in LISTEN state,
	 * else do it below (after getting remote address).
	 */
	if (optp && tp->t_state != TCPS_LISTEN)
		tcp_dooptions(tp, (u_char *)optp, optlen, ti);
/* , */
/*			&ts_present, &ts_val, &ts_ecr); */

	/*
	 * Header prediction: check for the two common cases
	 * of a uni-directional data xfer.  If the packet has
	 * no control flags, is in-sequence, the window didn't
	 * change and we're not retransmitting, it's a
	 * candidate.  If the length is zero and the ack moved
	 * forward, we're the sender side of the xfer.  Just
	 * free the data acked & wake any higher level process
	 * that was blocked waiting for space.  If the length
	 * is non-zero and the ack didn't move, we're the
	 * receiver side.  If we're getting packets in-order
	 * (the reassembly queue is empty), add the data to
	 * the socket buffer and note that we need a delayed ack.
	 *
	 * XXX Some of these tests are not needed
	 * eg: the tiwin == tp->snd_wnd prevents many more
	 * predictions.. with no *real* advantage..
	 */
	if (tp->t_state == TCPS_ESTABLISHED &&
	    (tiflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) == TH_ACK &&
/*	    (!ts_present || TSTMP_GEQ(ts_val, tp->ts_recent)) && */
	    ti->ti_seq == tp->rcv_nxt &&
	    tiwin && tiwin == tp->snd_wnd &&
	    tp->snd_nxt == tp->snd_max) {
		/*
		 * If last ACK falls within this segment's sequence numbers,
		 *  record the timestamp.
		 */
/*		if (ts_present && SEQ_LEQ(ti->ti_seq, tp->last_ack_sent) &&
 *		   SEQ_LT(tp->last_ack_sent, ti->ti_seq + ti->ti_len)) {
 *			tp->ts_recent_age = tcp_now;
 *			tp->ts_recent = ts_val;
 *		}
 */
		if (ti->ti_len == 0) {
			if (SEQ_GT(ti->ti_ack, tp->snd_una) &&
			    SEQ_LEQ(ti->ti_ack, tp->snd_max) &&
			    tp->snd_cwnd >= tp->snd_wnd) {
				/*
				 * this is a pure ack for outstanding data.
				 */
				STAT(tcpstat.tcps_predack++);
/*				if (ts_present)
 *					tcp_xmit_timer(tp, tcp_now-ts_ecr+1);
 *				else
 */				     if (tp->t_rtt &&
					    SEQ_GT(ti->ti_ack, tp->t_rtseq))
					tcp_xmit_timer(tp, tp->t_rtt);
				acked = ti->ti_ack - tp->snd_una;
				STAT(tcpstat.tcps_rcvackpack++);
				STAT(tcpstat.tcps_rcvackbyte += acked);
				sbdrop(&so->so_snd, acked);
				tp->snd_una = ti->ti_ack;
				m_freem(m);

				/*
				 * If all outstanding data are acked, stop
				 * retransmit timer, otherwise restart timer
				 * using current (possibly backed-off) value.
				 * If process is waiting for space,
				 * wakeup/selwakeup/signal.  If data
				 * are ready to send, let tcp_output
				 * decide between more output or persist.
				 */
				if (tp->snd_una == tp->snd_max)
					tp->t_timer[TCPT_REXMT] = 0;
				else if (tp->t_timer[TCPT_PERSIST] == 0)
					tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;

				/*
				 * There's room in so_snd, sowwakup will read()
				 * from the socket if we can
				 */
/*				if (so->so_snd.sb_flags & SB_NOTIFY)
 *					sowwakeup(so);
 */
				/*
				 * This is called because sowwakeup might have
				 * put data into so_snd.  Since we don't so sowwakeup,
				 * we don't need this.. XXX???
				 */
				if (so->so_snd.sb_cc)
					(void) tcp_output(tp);

				return;
			}
		} else if (ti->ti_ack == tp->snd_una &&
		    tcpfrag_list_empty(tp) &&
		    ti->ti_len <= sbspace(&so->so_rcv)) {
			/*
			 * this is a pure, in-sequence data packet
			 * with nothing on the reassembly queue and
			 * we have enough buffer space to take it.
			 */
			STAT(tcpstat.tcps_preddat++);
			tp->rcv_nxt += ti->ti_len;
			STAT(tcpstat.tcps_rcvpack++);
			STAT(tcpstat.tcps_rcvbyte += ti->ti_len);
			/*
			 * Add data to socket buffer.
			 */
			if (so->so_emu) {
				if (tcp_emu(so,m)) sbappend(so, m);
			} else
				sbappend(so, m);

			/*
			 * XXX This is called when data arrives.  Later, check
			 * if we can actually write() to the socket
			 * XXX Need to check? It's be NON_BLOCKING
			 */
/*			sorwakeup(so); */

			/*
			 * If this is a short packet, then ACK now - with Nagel
			 *	congestion avoidance sender won't send more until
			 *	he gets an ACK.
			 *
			 * It is better to not delay acks at all to maximize
			 * TCP throughput.  See RFC 2581.
			 */
			tp->t_flags |= TF_ACKNOW;
			tcp_output(tp);
			return;
		}
	} /* header prediction */
	/*
	 * Calculate amount of space in receive window,
	 * and then do TCP input processing.
	 * Receive window is amount of space in rcv queue,
	 * but not less than advertised window.
	 */
	{ int win;
          win = sbspace(&so->so_rcv);
	  if (win < 0)
	    win = 0;
	  tp->rcv_wnd = max(win, (int)(tp->rcv_adv - tp->rcv_nxt));
	}

	switch (tp->t_state) {

	/*
	 * If the state is LISTEN then ignore segment if it contains an RST.
	 * If the segment contains an ACK then it is bad and send a RST.
	 * If it does not contain a SYN then it is not interesting; drop it.
	 * Don't bother responding if the destination was a broadcast.
	 * Otherwise initialize tp->rcv_nxt, and tp->irs, select an initial
	 * tp->iss, and send a segment:
	 *     <SEQ=ISS><ACK=RCV_NXT><CTL=SYN,ACK>
	 * Also initialize tp->snd_nxt to tp->iss+1 and tp->snd_una to tp->iss.
	 * Fill in remote peer address fields if not previously specified.
	 * Enter SYN_RECEIVED state, and process any other fields of this
	 * segment in this state.
	 */
	case TCPS_LISTEN: {

	  if (tiflags & TH_RST)
	    goto drop;
	  if (tiflags & TH_ACK)
	    goto dropwithreset;
	  if ((tiflags & TH_SYN) == 0)
	    goto drop;

	  /*
	   * This has way too many gotos...
	   * But a bit of spaghetti code never hurt anybody :)
	   */

	  /*
	   * If this is destined for the control address, then flag to
	   * tcp_ctl once connected, otherwise connect
	   */
	  if ((so->so_faddr.s_addr&htonl(0xffffff00)) == special_addr.s_addr) {
	    int lastbyte=ntohl(so->so_faddr.s_addr) & 0xff;
	    if (lastbyte!=CTL_ALIAS && lastbyte!=CTL_DNS) {
#if 0
	      if(lastbyte==CTL_CMD || lastbyte==CTL_EXEC) {
		/* Command or exec adress */
		so->so_state |= SS_CTL;
	      } else
#endif
              {
		/* May be an add exec */
		for(ex_ptr = exec_list; ex_ptr; ex_ptr = ex_ptr->ex_next) {
		  if(ex_ptr->ex_fport == so->so_fport &&
		     lastbyte == ex_ptr->ex_addr) {
		    so->so_state |= SS_CTL;
		    break;
		  }
		}
	      }
	      if(so->so_state & SS_CTL) goto cont_input;
	    }
	    /* CTL_ALIAS: Do nothing, tcp_fconnect will be called on it */
	  }

	  if (so->so_emu & EMU_NOCONNECT) {
	    so->so_emu &= ~EMU_NOCONNECT;
	    goto cont_input;
	  }

	  if((tcp_fconnect(so) == -1) && (errno != EINPROGRESS) && (errno != EWOULDBLOCK)) {
	    u_char code=ICMP_UNREACH_NET;
	    DEBUG_MISC((dfd," tcp fconnect errno = %d-%s\n",
			errno,strerror(errno)));
	    if(errno == ECONNREFUSED) {
	      /* ACK the SYN, send RST to refuse the connection */
	      tcp_respond(tp, ti, m, ti->ti_seq+1, (tcp_seq)0,
			  TH_RST|TH_ACK);
	    } else {
	      if(errno == EHOSTUNREACH) code=ICMP_UNREACH_HOST;
	      HTONL(ti->ti_seq);             /* restore tcp header */
	      HTONL(ti->ti_ack);
	      HTONS(ti->ti_win);
	      HTONS(ti->ti_urp);
	      m->m_data -= sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);
	      m->m_len  += sizeof(struct tcpiphdr)+off-sizeof(struct tcphdr);
	      *ip=save_ip;
	      icmp_error(m, ICMP_UNREACH,code, 0,strerror(errno));
	    }
	    tp = tcp_close(tp);
	    m_free(m);
	  } else {
	    /*
	     * Haven't connected yet, save the current mbuf
	     * and ti, and return
	     * XXX Some OS's don't tell us whether the connect()
	     * succeeded or not.  So we must time it out.
	     */
	    so->so_m = m;
	    so->so_ti = ti;
	    tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
	    tp->t_state = TCPS_SYN_RECEIVED;
	  }
	  return;

	cont_conn:
	  /* m==NULL
	   * Check if the connect succeeded
	   */
	  if (so->so_state & SS_NOFDREF) {
	    tp = tcp_close(tp);
	    goto dropwithreset;
	  }
	cont_input:
	  tcp_template(tp);

	  if (optp)
	    tcp_dooptions(tp, (u_char *)optp, optlen, ti);
	  /* , */
	  /*				&ts_present, &ts_val, &ts_ecr); */

	  if (iss)
	    tp->iss = iss;
	  else
	    tp->iss = tcp_iss;
	  tcp_iss += TCP_ISSINCR/2;
	  tp->irs = ti->ti_seq;
	  tcp_sendseqinit(tp);
	  tcp_rcvseqinit(tp);
	  tp->t_flags |= TF_ACKNOW;
	  tp->t_state = TCPS_SYN_RECEIVED;
	  tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
	  STAT(tcpstat.tcps_accepts++);
	  goto trimthenstep6;
	} /* case TCPS_LISTEN */

	/*
	 * If the state is SYN_SENT:
	 *	if seg contains an ACK, but not for our SYN, drop the input.
	 *	if seg contains a RST, then drop the connection.
	 *	if seg does not contain SYN, then drop it.
	 * Otherwise this is an acceptable SYN segment
	 *	initialize tp->rcv_nxt and tp->irs
	 *	if seg contains ack then advance tp->snd_una
	 *	if SYN has been acked change to ESTABLISHED else SYN_RCVD state
	 *	arrange for segment to be acked (eventually)
	 *	continue processing rest of data/controls, beginning with URG
	 */
	case TCPS_SYN_SENT:
		if ((tiflags & TH_ACK) &&
		    (SEQ_LEQ(ti->ti_ack, tp->iss) ||
		     SEQ_GT(ti->ti_ack, tp->snd_max)))
			goto dropwithreset;

		if (tiflags & TH_RST) {
			if (tiflags & TH_ACK)
				tp = tcp_drop(tp,0); /* XXX Check t_softerror! */
			goto drop;
		}

		if ((tiflags & TH_SYN) == 0)
			goto drop;
		if (tiflags & TH_ACK) {
			tp->snd_una = ti->ti_ack;
			if (SEQ_LT(tp->snd_nxt, tp->snd_una))
				tp->snd_nxt = tp->snd_una;
		}

		tp->t_timer[TCPT_REXMT] = 0;
		tp->irs = ti->ti_seq;
		tcp_rcvseqinit(tp);
		tp->t_flags |= TF_ACKNOW;
		if (tiflags & TH_ACK && SEQ_GT(tp->snd_una, tp->iss)) {
			STAT(tcpstat.tcps_connects++);
			soisfconnected(so);
			tp->t_state = TCPS_ESTABLISHED;

			/* Do window scaling on this connection? */
/*			if ((tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE)) ==
 *				(TF_RCVD_SCALE|TF_REQ_SCALE)) {
 * 				tp->snd_scale = tp->requested_s_scale;
 *				tp->rcv_scale = tp->request_r_scale;
 *			}
 */
			(void) tcp_reass(tp, (struct tcpiphdr *)0,
				(struct mbuf *)0);
			/*
			 * if we didn't have to retransmit the SYN,
			 * use its rtt as our initial srtt & rtt var.
			 */
			if (tp->t_rtt)
				tcp_xmit_timer(tp, tp->t_rtt);
		} else
			tp->t_state = TCPS_SYN_RECEIVED;

trimthenstep6:
		/*
		 * Advance ti->ti_seq to correspond to first data byte.
		 * If data, trim to stay within window,
		 * dropping FIN if necessary.
		 */
		ti->ti_seq++;
		if (ti->ti_len > tp->rcv_wnd) {
			todrop = ti->ti_len - tp->rcv_wnd;
			m_adj(m, -todrop);
			ti->ti_len = tp->rcv_wnd;
			tiflags &= ~TH_FIN;
			STAT(tcpstat.tcps_rcvpackafterwin++);
			STAT(tcpstat.tcps_rcvbyteafterwin += todrop);
		}
		tp->snd_wl1 = ti->ti_seq - 1;
		tp->rcv_up = ti->ti_seq;
		goto step6;
	} /* switch tp->t_state */
	/*
	 * States other than LISTEN or SYN_SENT.
	 * First check timestamp, if present.
	 * Then check that at least some bytes of segment are within
	 * receive window.  If segment begins before rcv_nxt,
	 * drop leading data (and SYN); if nothing left, just ack.
	 *
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment
	 * and it's less than ts_recent, drop it.
	 */
/*	if (ts_present && (tiflags & TH_RST) == 0 && tp->ts_recent &&
 *	    TSTMP_LT(ts_val, tp->ts_recent)) {
 *
 */		/* Check to see if ts_recent is over 24 days old.  */
/*		if ((int)(tcp_now - tp->ts_recent_age) > TCP_PAWS_IDLE) {
 */			/*
 *			 * Invalidate ts_recent.  If this segment updates
 *			 * ts_recent, the age will be reset later and ts_recent
 *			 * will get a valid value.  If it does not, setting
 *			 * ts_recent to zero will at least satisfy the
 *			 * requirement that zero be placed in the timestamp
 *			 * echo reply when ts_recent isn't valid.  The
 *			 * age isn't reset until we get a valid ts_recent
 *			 * because we don't want out-of-order segments to be
 *			 * dropped when ts_recent is old.
 *			 */
/*			tp->ts_recent = 0;
 *		} else {
 *			tcpstat.tcps_rcvduppack++;
 *			tcpstat.tcps_rcvdupbyte += ti->ti_len;
 *			tcpstat.tcps_pawsdrop++;
 *			goto dropafterack;
 *		}
 *	}
 */

	todrop = tp->rcv_nxt - ti->ti_seq;
	if (todrop > 0) {
		if (tiflags & TH_SYN) {
			tiflags &= ~TH_SYN;
			ti->ti_seq++;
			if (ti->ti_urp > 1)
				ti->ti_urp--;
			else
				tiflags &= ~TH_URG;
			todrop--;
		}
		/*
		 * Following if statement from Stevens, vol. 2, p. 960.
		 */
		if (todrop > ti->ti_len
		    || (todrop == ti->ti_len && (tiflags & TH_FIN) == 0)) {
			/*
			 * Any valid FIN must be to the left of the window.
			 * At this point the FIN must be a duplicate or out
			 * of sequence; drop it.
			 */
			tiflags &= ~TH_FIN;

			/*
			 * Send an ACK to resynchronize and drop any data.
			 * But keep on processing for RST or ACK.
			 */
			tp->t_flags |= TF_ACKNOW;
			todrop = ti->ti_len;
			STAT(tcpstat.tcps_rcvduppack++);
			STAT(tcpstat.tcps_rcvdupbyte += todrop);
		} else {
			STAT(tcpstat.tcps_rcvpartduppack++);
			STAT(tcpstat.tcps_rcvpartdupbyte += todrop);
		}
		m_adj(m, todrop);
		ti->ti_seq += todrop;
		ti->ti_len -= todrop;
		if (ti->ti_urp > todrop)
			ti->ti_urp -= todrop;
		else {
			tiflags &= ~TH_URG;
			ti->ti_urp = 0;
		}
	}
	/*
	 * If new data are received on a connection after the
	 * user processes are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) &&
	    tp->t_state > TCPS_CLOSE_WAIT && ti->ti_len) {
		tp = tcp_close(tp);
		STAT(tcpstat.tcps_rcvafterclose++);
		goto dropwithreset;
	}

	/*
	 * If segment ends after window, drop trailing data
	 * (and PUSH and FIN); if nothing left, just ACK.
	 */
	todrop = (ti->ti_seq+ti->ti_len) - (tp->rcv_nxt+tp->rcv_wnd);
	if (todrop > 0) {
		STAT(tcpstat.tcps_rcvpackafterwin++);
		if (todrop >= ti->ti_len) {
			STAT(tcpstat.tcps_rcvbyteafterwin += ti->ti_len);
			/*
			 * If a new connection request is received
			 * while in TIME_WAIT, drop the old connection
			 * and start over if the sequence numbers
			 * are above the previous ones.
			 */
			if (tiflags & TH_SYN &&
			    tp->t_state == TCPS_TIME_WAIT &&
			    SEQ_GT(ti->ti_seq, tp->rcv_nxt)) {
				iss = tp->rcv_nxt + TCP_ISSINCR;
				tp = tcp_close(tp);
				goto findso;
			}
			/*
			 * If window is closed can only take segments at
			 * window edge, and have to drop data and PUSH from
			 * incoming segments.  Continue processing, but
			 * remember to ack.  Otherwise, drop segment
			 * and ack.
			 */
			if (tp->rcv_wnd == 0 && ti->ti_seq == tp->rcv_nxt) {
				tp->t_flags |= TF_ACKNOW;
				STAT(tcpstat.tcps_rcvwinprobe++);
			} else
				goto dropafterack;
		} else
			STAT(tcpstat.tcps_rcvbyteafterwin += todrop);
		m_adj(m, -todrop);
		ti->ti_len -= todrop;
		tiflags &= ~(TH_PUSH|TH_FIN);
	}

	/*
	 * If last ACK falls within this segment's sequence numbers,
	 * record its timestamp.
	 */
/*	if (ts_present && SEQ_LEQ(ti->ti_seq, tp->last_ack_sent) &&
 *	    SEQ_LT(tp->last_ack_sent, ti->ti_seq + ti->ti_len +
 *		   ((tiflags & (TH_SYN|TH_FIN)) != 0))) {
 *		tp->ts_recent_age = tcp_now;
 *		tp->ts_recent = ts_val;
 *	}
 */

	/*
	 * If the RST bit is set examine the state:
	 *    SYN_RECEIVED STATE:
	 *	If passive open, return to LISTEN state.
	 *	If active open, inform user that connection was refused.
	 *    ESTABLISHED, FIN_WAIT_1, FIN_WAIT2, CLOSE_WAIT STATES:
	 *	Inform user that connection was reset, and close tcb.
	 *    CLOSING, LAST_ACK, TIME_WAIT STATES
	 *	Close the tcb.
	 */
	if (tiflags&TH_RST) switch (tp->t_state) {

	case TCPS_SYN_RECEIVED:
/*		so->so_error = ECONNREFUSED; */
		goto close;

	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
/*		so->so_error = ECONNRESET; */
	close:
		tp->t_state = TCPS_CLOSED;
		STAT(tcpstat.tcps_drops++);
		tp = tcp_close(tp);
		goto drop;

	case TCPS_CLOSING:
	case TCPS_LAST_ACK:
	case TCPS_TIME_WAIT:
		tp = tcp_close(tp);
		goto drop;
	}

	/*
	 * If a SYN is in the window, then this is an
	 * error and we send an RST and drop the connection.
	 */
	if (tiflags & TH_SYN) {
		tp = tcp_drop(tp,0);
		goto dropwithreset;
	}

	/*
	 * If the ACK bit is off we drop the segment and return.
	 */
	if ((tiflags & TH_ACK) == 0) goto drop;

	/*
	 * Ack processing.
	 */
	switch (tp->t_state) {
	/*
	 * In SYN_RECEIVED state if the ack ACKs our SYN then enter
	 * ESTABLISHED state and continue processing, otherwise
	 * send an RST.  una<=ack<=max
	 */
	case TCPS_SYN_RECEIVED:

		if (SEQ_GT(tp->snd_una, ti->ti_ack) ||
		    SEQ_GT(ti->ti_ack, tp->snd_max))
			goto dropwithreset;
		STAT(tcpstat.tcps_connects++);
		tp->t_state = TCPS_ESTABLISHED;
		/*
		 * The sent SYN is ack'ed with our sequence number +1
		 * The first data byte already in the buffer will get
		 * lost if no correction is made.  This is only needed for
		 * SS_CTL since the buffer is empty otherwise.
		 * tp->snd_una++; or:
		 */
		tp->snd_una=ti->ti_ack;
		if (so->so_state & SS_CTL) {
		  /* So tcp_ctl reports the right state */
		  ret = tcp_ctl(so);
		  if (ret == 1) {
		    soisfconnected(so);
		    so->so_state &= ~SS_CTL;   /* success XXX */
		  } else if (ret == 2) {
		    so->so_state = SS_NOFDREF; /* CTL_CMD */
		  } else {
		    needoutput = 1;
		    tp->t_state = TCPS_FIN_WAIT_1;
		  }
		} else {
		  soisfconnected(so);
		}

		/* Do window scaling? */
/*		if ((tp->t_flags & (TF_RCVD_SCALE|TF_REQ_SCALE)) ==
 *			(TF_RCVD_SCALE|TF_REQ_SCALE)) {
 *			tp->snd_scale = tp->requested_s_scale;
 *			tp->rcv_scale = tp->request_r_scale;
 *		}
 */
		(void) tcp_reass(tp, (struct tcpiphdr *)0, (struct mbuf *)0);
		tp->snd_wl1 = ti->ti_seq - 1;
		/* Avoid ack processing; snd_una==ti_ack  =>  dup ack */
		goto synrx_to_est;
		/* fall into ... */

	/*
	 * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
	 * ACKs.  If the ack is in the range
	 *	tp->snd_una < ti->ti_ack <= tp->snd_max
	 * then advance tp->snd_una to ti->ti_ack and drop
	 * data from the retransmission queue.  If this ACK reflects
	 * more up to date window information we update our window information.
	 */
	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
	case TCPS_CLOSING:
	case TCPS_LAST_ACK:
	case TCPS_TIME_WAIT:

		if (SEQ_LEQ(ti->ti_ack, tp->snd_una)) {
			if (ti->ti_len == 0 && tiwin == tp->snd_wnd) {
			  STAT(tcpstat.tcps_rcvdupack++);
			  DEBUG_MISC((dfd," dup ack  m = %lx  so = %lx \n",
				      (long )m, (long )so));
				/*
				 * If we have outstanding data (other than
				 * a window probe), this is a completely
				 * duplicate ack (ie, window info didn't
				 * change), the ack is the biggest we've
				 * seen and we've seen exactly our rexmt
				 * threshold of them, assume a packet
				 * has been dropped and retransmit it.
				 * Kludge snd_nxt & the congestion
				 * window so we send only this one
				 * packet.
				 *
				 * We know we're losing at the current
				 * window size so do congestion avoidance
				 * (set ssthresh to half the current window
				 * and pull our congestion window back to
				 * the new ssthresh).
				 *
				 * Dup acks mean that packets have left the
				 * network (they're now cached at the receiver)
				 * so bump cwnd by the amount in the receiver
				 * to keep a constant cwnd packets in the
				 * network.
				 */
				if (tp->t_timer[TCPT_REXMT] == 0 ||
				    ti->ti_ack != tp->snd_una)
					tp->t_dupacks = 0;
				else if (++tp->t_dupacks == TCPREXMTTHRESH) {
					tcp_seq onxt = tp->snd_nxt;
					u_int win =
					    min(tp->snd_wnd, tp->snd_cwnd) / 2 /
						tp->t_maxseg;

					if (win < 2)
						win = 2;
					tp->snd_ssthresh = win * tp->t_maxseg;
					tp->t_timer[TCPT_REXMT] = 0;
					tp->t_rtt = 0;
					tp->snd_nxt = ti->ti_ack;
					tp->snd_cwnd = tp->t_maxseg;
					(void) tcp_output(tp);
					tp->snd_cwnd = tp->snd_ssthresh +
					       tp->t_maxseg * tp->t_dupacks;
					if (SEQ_GT(onxt, tp->snd_nxt))
						tp->snd_nxt = onxt;
					goto drop;
				} else if (tp->t_dupacks > TCPREXMTTHRESH) {
					tp->snd_cwnd += tp->t_maxseg;
					(void) tcp_output(tp);
					goto drop;
				}
			} else
				tp->t_dupacks = 0;
			break;
		}
	synrx_to_est:
		/*
		 * If the congestion window was inflated to account
		 * for the other side's cached packets, retract it.
		 */
		if (tp->t_dupacks > TCPREXMTTHRESH &&
		    tp->snd_cwnd > tp->snd_ssthresh)
			tp->snd_cwnd = tp->snd_ssthresh;
		tp->t_dupacks = 0;
		if (SEQ_GT(ti->ti_ack, tp->snd_max)) {
			STAT(tcpstat.tcps_rcvacktoomuch++);
			goto dropafterack;
		}
		acked = ti->ti_ack - tp->snd_una;
		STAT(tcpstat.tcps_rcvackpack++);
		STAT(tcpstat.tcps_rcvackbyte += acked);

		/*
		 * If we have a timestamp reply, update smoothed
		 * round trip time.  If no timestamp is present but
		 * transmit timer is running and timed sequence
		 * number was acked, update smoothed round trip time.
		 * Since we now have an rtt measurement, cancel the
		 * timer backoff (cf., Phil Karn's retransmit alg.).
		 * Recompute the initial retransmit timer.
		 */
/*		if (ts_present)
 *			tcp_xmit_timer(tp, tcp_now-ts_ecr+1);
 *		else
 */
		     if (tp->t_rtt && SEQ_GT(ti->ti_ack, tp->t_rtseq))
			tcp_xmit_timer(tp,tp->t_rtt);

		/*
		 * If all outstanding data is acked, stop retransmit
		 * timer and remember to restart (more output or persist).
		 * If there is more data to be acked, restart retransmit
		 * timer, using current (possibly backed-off) value.
		 */
		if (ti->ti_ack == tp->snd_max) {
			tp->t_timer[TCPT_REXMT] = 0;
			needoutput = 1;
		} else if (tp->t_timer[TCPT_PERSIST] == 0)
			tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;
		/*
		 * When new data is acked, open the congestion window.
		 * If the window gives us less than ssthresh packets
		 * in flight, open exponentially (maxseg per packet).
		 * Otherwise open linearly: maxseg per window
		 * (maxseg^2 / cwnd per packet).
		 */
		{
		  register u_int cw = tp->snd_cwnd;
		  register u_int incr = tp->t_maxseg;

		  if (cw > tp->snd_ssthresh)
		    incr = incr * incr / cw;
		  tp->snd_cwnd = min(cw + incr, TCP_MAXWIN<<tp->snd_scale);
		}
		if (acked > so->so_snd.sb_cc) {
			tp->snd_wnd -= so->so_snd.sb_cc;
			sbdrop(&so->so_snd, (int )so->so_snd.sb_cc);
			ourfinisacked = 1;
		} else {
			sbdrop(&so->so_snd, acked);
			tp->snd_wnd -= acked;
			ourfinisacked = 0;
		}
		/*
		 * XXX sowwakup is called when data is acked and there's room for
		 * for more data... it should read() the socket
		 */
/*		if (so->so_snd.sb_flags & SB_NOTIFY)
 *			sowwakeup(so);
 */
		tp->snd_una = ti->ti_ack;
		if (SEQ_LT(tp->snd_nxt, tp->snd_una))
			tp->snd_nxt = tp->snd_una;

		switch (tp->t_state) {

		/*
		 * In FIN_WAIT_1 STATE in addition to the processing
		 * for the ESTABLISHED state if our FIN is now acknowledged
		 * then enter FIN_WAIT_2.
		 */
		case TCPS_FIN_WAIT_1:
			if (ourfinisacked) {
				/*
				 * If we can't receive any more
				 * data, then closing user can proceed.
				 * Starting the timer is contrary to the
				 * specification, but if we don't get a FIN
				 * we'll hang forever.
				 */
				if (so->so_state & SS_FCANTRCVMORE) {
					soisfdisconnected(so);
					tp->t_timer[TCPT_2MSL] = TCP_MAXIDLE;
				}
				tp->t_state = TCPS_FIN_WAIT_2;
			}
			break;

	 	/*
		 * In CLOSING STATE in addition to the processing for
		 * the ESTABLISHED state if the ACK acknowledges our FIN
		 * then enter the TIME-WAIT state, otherwise ignore
		 * the segment.
		 */
		case TCPS_CLOSING:
			if (ourfinisacked) {
				tp->t_state = TCPS_TIME_WAIT;
				tcp_canceltimers(tp);
				tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
				soisfdisconnected(so);
			}
			break;

		/*
		 * In LAST_ACK, we may still be waiting for data to drain
		 * and/or to be acked, as well as for the ack of our FIN.
		 * If our FIN is now acknowledged, delete the TCB,
		 * enter the closed state and return.
		 */
		case TCPS_LAST_ACK:
			if (ourfinisacked) {
				tp = tcp_close(tp);
				goto drop;
			}
			break;

		/*
		 * In TIME_WAIT state the only thing that should arrive
		 * is a retransmission of the remote FIN.  Acknowledge
		 * it and restart the finack timer.
		 */
		case TCPS_TIME_WAIT:
			tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
			goto dropafterack;
		}
	} /* switch(tp->t_state) */

step6:
	/*
	 * Update window information.
	 * Don't look at window if no ACK: TAC's send garbage on first SYN.
	 */
	if ((tiflags & TH_ACK) &&
	    (SEQ_LT(tp->snd_wl1, ti->ti_seq) ||
	    (tp->snd_wl1 == ti->ti_seq && (SEQ_LT(tp->snd_wl2, ti->ti_ack) ||
	    (tp->snd_wl2 == ti->ti_ack && tiwin > tp->snd_wnd))))) {
		/* keep track of pure window updates */
		if (ti->ti_len == 0 &&
		    tp->snd_wl2 == ti->ti_ack && tiwin > tp->snd_wnd)
			STAT(tcpstat.tcps_rcvwinupd++);
		tp->snd_wnd = tiwin;
		tp->snd_wl1 = ti->ti_seq;
		tp->snd_wl2 = ti->ti_ack;
		if (tp->snd_wnd > tp->max_sndwnd)
			tp->max_sndwnd = tp->snd_wnd;
		needoutput = 1;
	}

	/*
	 * Process segments with URG.
	 */
	if ((tiflags & TH_URG) && ti->ti_urp &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		/*
		 * This is a kludge, but if we receive and accept
		 * random urgent pointers, we'll crash in
		 * soreceive.  It's hard to imagine someone
		 * actually wanting to send this much urgent data.
		 */
		if (ti->ti_urp + so->so_rcv.sb_cc > so->so_rcv.sb_datalen) {
			ti->ti_urp = 0;
			tiflags &= ~TH_URG;
			goto dodata;
		}
		/*
		 * If this segment advances the known urgent pointer,
		 * then mark the data stream.  This should not happen
		 * in CLOSE_WAIT, CLOSING, LAST_ACK or TIME_WAIT STATES since
		 * a FIN has been received from the remote side.
		 * In these states we ignore the URG.
		 *
		 * According to RFC961 (Assigned Protocols),
		 * the urgent pointer points to the last octet
		 * of urgent data.  We continue, however,
		 * to consider it to indicate the first octet
		 * of data past the urgent section as the original
		 * spec states (in one of two places).
		 */
		if (SEQ_GT(ti->ti_seq+ti->ti_urp, tp->rcv_up)) {
			tp->rcv_up = ti->ti_seq + ti->ti_urp;
			so->so_urgc =  so->so_rcv.sb_cc +
				(tp->rcv_up - tp->rcv_nxt); /* -1; */
			tp->rcv_up = ti->ti_seq + ti->ti_urp;

		}
	} else
		/*
		 * If no out of band data is expected,
		 * pull receive urgent pointer along
		 * with the receive window.
		 */
		if (SEQ_GT(tp->rcv_nxt, tp->rcv_up))
			tp->rcv_up = tp->rcv_nxt;
dodata:

	/*
	 * Process the segment text, merging it into the TCP sequencing queue,
	 * and arranging for acknowledgment of receipt if necessary.
	 * This process logically involves adjusting tp->rcv_wnd as data
	 * is presented to the user (this happens in tcp_usrreq.c,
	 * case PRU_RCVD).  If a FIN has already been received on this
	 * connection then we just ignore the text.
	 */
	if ((ti->ti_len || (tiflags&TH_FIN)) &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		TCP_REASS(tp, ti, m, so, tiflags);
		/*
		 * Note the amount of data that peer has sent into
		 * our window, in order to estimate the sender's
		 * buffer size.
		 */
		len = so->so_rcv.sb_datalen - (tp->rcv_adv - tp->rcv_nxt);
	} else {
		m_free(m);
		tiflags &= ~TH_FIN;
	}

	/*
	 * If FIN is received ACK the FIN and let the user know
	 * that the connection is closing.
	 */
	if (tiflags & TH_FIN) {
		if (TCPS_HAVERCVDFIN(tp->t_state) == 0) {
			/*
			 * If we receive a FIN we can't send more data,
			 * set it SS_FDRAIN
                         * Shutdown the socket if there is no rx data in the
			 * buffer.
			 * soread() is called on completion of shutdown() and
			 * will got to TCPS_LAST_ACK, and use tcp_output()
			 * to send the FIN.
			 */
/*			sofcantrcvmore(so); */
			sofwdrain(so);

			tp->t_flags |= TF_ACKNOW;
			tp->rcv_nxt++;
		}
		switch (tp->t_state) {

	 	/*
		 * In SYN_RECEIVED and ESTABLISHED STATES
		 * enter the CLOSE_WAIT state.
		 */
		case TCPS_SYN_RECEIVED:
		case TCPS_ESTABLISHED:
		  if(so->so_emu == EMU_CTL)        /* no shutdown on socket */
		    tp->t_state = TCPS_LAST_ACK;
		  else
		    tp->t_state = TCPS_CLOSE_WAIT;
		  break;

	 	/*
		 * If still in FIN_WAIT_1 STATE FIN has not been acked so
		 * enter the CLOSING state.
		 */
		case TCPS_FIN_WAIT_1:
			tp->t_state = TCPS_CLOSING;
			break;

	 	/*
		 * In FIN_WAIT_2 state enter the TIME_WAIT state,
		 * starting the time-wait timer, turning off the other
		 * standard timers.
		 */
		case TCPS_FIN_WAIT_2:
			tp->t_state = TCPS_TIME_WAIT;
			tcp_canceltimers(tp);
			tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
			soisfdisconnected(so);
			break;

		/*
		 * In TIME_WAIT state restart the 2 MSL time_wait timer.
		 */
		case TCPS_TIME_WAIT:
			tp->t_timer[TCPT_2MSL] = 2 * TCPTV_MSL;
			break;
		}
	}

	/*
	 * If this is a small packet, then ACK now - with Nagel
	 *      congestion avoidance sender won't send more until
	 *      he gets an ACK.
	 *
	 * See above.
	 */
/*	if (ti->ti_len && (unsigned)ti->ti_len < tp->t_maxseg) {
 */
/*	if ((ti->ti_len && (unsigned)ti->ti_len < tp->t_maxseg &&
 *		(so->so_iptos & IPTOS_LOWDELAY) == 0) ||
 *	       ((so->so_iptos & IPTOS_LOWDELAY) &&
 *	       ((struct tcpiphdr_2 *)ti)->first_char == (char)27)) {
 */
	if (ti->ti_len && (unsigned)ti->ti_len <= 5 &&
	    ((struct tcpiphdr_2 *)ti)->first_char == (char)27) {
		tp->t_flags |= TF_ACKNOW;
	}

	/*
	 * Return any desired output.
	 */
	if (needoutput || (tp->t_flags & TF_ACKNOW)) {
		(void) tcp_output(tp);
	}
	return;

dropafterack:
	/*
	 * Generate an ACK dropping incoming segment if it occupies
	 * sequence space, where the ACK reflects our state.
	 */
	if (tiflags & TH_RST)
		goto drop;
	m_freem(m);
	tp->t_flags |= TF_ACKNOW;
	(void) tcp_output(tp);
	return;

dropwithreset:
	/* reuses m if m!=NULL, m_free() unnecessary */
	if (tiflags & TH_ACK)
		tcp_respond(tp, ti, m, (tcp_seq)0, ti->ti_ack, TH_RST);
	else {
		if (tiflags & TH_SYN) ti->ti_len++;
		tcp_respond(tp, ti, m, ti->ti_seq+ti->ti_len, (tcp_seq)0,
		    TH_RST|TH_ACK);
	}

	return;

drop:
	/*
	 * Drop space held by incoming segment and return.
	 */
	m_free(m);

	return;
}

 /* , ts_present, ts_val, ts_ecr) */
/*	int *ts_present;
 *	u_int32_t *ts_val, *ts_ecr;
 */
static void
tcp_dooptions(struct tcpcb *tp, u_char *cp, int cnt, struct tcpiphdr *ti)
{
	u_int16_t mss;
	int opt, optlen;

	DEBUG_CALL("tcp_dooptions");
	DEBUG_ARGS((dfd," tp = %lx  cnt=%i \n", (long )tp, cnt));

	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[0];
		if (opt == TCPOPT_EOL)
			break;
		if (opt == TCPOPT_NOP)
			optlen = 1;
		else {
			optlen = cp[1];
			if (optlen <= 0)
				break;
		}
		switch (opt) {

		default:
			continue;

		case TCPOPT_MAXSEG:
			if (optlen != TCPOLEN_MAXSEG)
				continue;
			if (!(ti->ti_flags & TH_SYN))
				continue;
			memcpy((char *) &mss, (char *) cp + 2, sizeof(mss));
			NTOHS(mss);
			(void) tcp_mss(tp, mss);	/* sets t_maxseg */
			break;

/*		case TCPOPT_WINDOW:
 *			if (optlen != TCPOLEN_WINDOW)
 *				continue;
 *			if (!(ti->ti_flags & TH_SYN))
 *				continue;
 *			tp->t_flags |= TF_RCVD_SCALE;
 *			tp->requested_s_scale = min(cp[2], TCP_MAX_WINSHIFT);
 *			break;
 */
/*		case TCPOPT_TIMESTAMP:
 *			if (optlen != TCPOLEN_TIMESTAMP)
 *				continue;
 *			*ts_present = 1;
 *			memcpy((char *) ts_val, (char *)cp + 2, sizeof(*ts_val));
 *			NTOHL(*ts_val);
 *			memcpy((char *) ts_ecr, (char *)cp + 6, sizeof(*ts_ecr));
 *			NTOHL(*ts_ecr);
 *
 */			/*
 *			 * A timestamp received in a SYN makes
 *			 * it ok to send timestamp requests and replies.
 *			 */
/*			if (ti->ti_flags & TH_SYN) {
 *				tp->t_flags |= TF_RCVD_TSTMP;
 *				tp->ts_recent = *ts_val;
 *				tp->ts_recent_age = tcp_now;
 *			}
 */			break;
		}
	}
}


/*
 * Pull out of band byte out of a segment so
 * it doesn't appear in the user's data queue.
 * It is still reflected in the segment length for
 * sequencing purposes.
 */

#ifdef notdef

void
tcp_pulloutofband(so, ti, m)
	struct socket *so;
	struct tcpiphdr *ti;
	register struct mbuf *m;
{
	int cnt = ti->ti_urp - 1;

	while (cnt >= 0) {
		if (m->m_len > cnt) {
			char *cp = mtod(m, caddr_t) + cnt;
			struct tcpcb *tp = sototcpcb(so);

			tp->t_iobc = *cp;
			tp->t_oobflags |= TCPOOB_HAVEDATA;
			memcpy(sp, cp+1, (unsigned)(m->m_len - cnt - 1));
			m->m_len--;
			return;
		}
		cnt -= m->m_len;
		m = m->m_next; /* XXX WRONG! Fix it! */
		if (m == 0)
			break;
	}
	panic("tcp_pulloutofband");
}

#endif /* notdef */

/*
 * Collect new round-trip time estimate
 * and update averages and current timeout.
 */

static void
tcp_xmit_timer(register struct tcpcb *tp, int rtt)
{
	register short delta;

	DEBUG_CALL("tcp_xmit_timer");
	DEBUG_ARG("tp = %lx", (long)tp);
	DEBUG_ARG("rtt = %d", rtt);

	STAT(tcpstat.tcps_rttupdated++);
	if (tp->t_srtt != 0) {
		/*
		 * srtt is stored as fixed point with 3 bits after the
		 * binary point (i.e., scaled by 8).  The following magic
		 * is equivalent to the smoothing algorithm in rfc793 with
		 * an alpha of .875 (srtt = rtt/8 + srtt*7/8 in fixed
		 * point).  Adjust rtt to origin 0.
		 */
		delta = rtt - 1 - (tp->t_srtt >> TCP_RTT_SHIFT);
		if ((tp->t_srtt += delta) <= 0)
			tp->t_srtt = 1;
		/*
		 * We accumulate a smoothed rtt variance (actually, a
		 * smoothed mean difference), then set the retransmit
		 * timer to smoothed rtt + 4 times the smoothed variance.
		 * rttvar is stored as fixed point with 2 bits after the
		 * binary point (scaled by 4).  The following is
		 * equivalent to rfc793 smoothing with an alpha of .75
		 * (rttvar = rttvar*3/4 + |delta| / 4).  This replaces
		 * rfc793's wired-in beta.
		 */
		if (delta < 0)
			delta = -delta;
		delta -= (tp->t_rttvar >> TCP_RTTVAR_SHIFT);
		if ((tp->t_rttvar += delta) <= 0)
			tp->t_rttvar = 1;
	} else {
		/*
		 * No rtt measurement yet - use the unsmoothed rtt.
		 * Set the variance to half the rtt (so our first
		 * retransmit happens at 3*rtt).
		 */
		tp->t_srtt = rtt << TCP_RTT_SHIFT;
		tp->t_rttvar = rtt << (TCP_RTTVAR_SHIFT - 1);
	}
	tp->t_rtt = 0;
	tp->t_rxtshift = 0;

	/*
	 * the retransmit should happen at rtt + 4 * rttvar.
	 * Because of the way we do the smoothing, srtt and rttvar
	 * will each average +1/2 tick of bias.  When we compute
	 * the retransmit timer, we want 1/2 tick of rounding and
	 * 1 extra tick because of +-1/2 tick uncertainty in the
	 * firing of the timer.  The bias will give us exactly the
	 * 1.5 tick we need.  But, because the bias is
	 * statistical, we have to test that we don't drop below
	 * the minimum feasible timer (which is 2 ticks).
	 */
	TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp),
	    (short)tp->t_rttmin, TCPTV_REXMTMAX); /* XXX */

	/*
	 * We received an ack for a packet that wasn't retransmitted;
	 * it is probably safe to discard any error indications we've
	 * received recently.  This isn't quite right, but close enough
	 * for now (a route might have failed after we sent a segment,
	 * and the return path might not be symmetrical).
	 */
	tp->t_softerror = 0;
}

/*
 * Determine a reasonable value for maxseg size.
 * If the route is known, check route for mtu.
 * If none, use an mss that can be handled on the outgoing
 * interface without forcing IP to fragment; if bigger than
 * an mbuf cluster (MCLBYTES), round down to nearest multiple of MCLBYTES
 * to utilize large mbufs.  If no route is found, route has no mtu,
 * or the destination isn't local, use a default, hopefully conservative
 * size (usually 512 or the default IP max size, but no more than the mtu
 * of the interface), as we can't discover anything about intervening
 * gateways or networks.  We also initialize the congestion/slow start
 * window to be a single segment if the destination isn't local.
 * While looking at the routing entry, we also initialize other path-dependent
 * parameters from pre-set or cached values in the routing entry.
 */

int
tcp_mss(struct tcpcb *tp, u_int offer)
{
	struct socket *so = tp->t_socket;
	int mss;

	DEBUG_CALL("tcp_mss");
	DEBUG_ARG("tp = %lx", (long)tp);
	DEBUG_ARG("offer = %d", offer);

	mss = min(IF_MTU, IF_MRU) - sizeof(struct tcpiphdr);
	if (offer)
		mss = min(mss, offer);
	mss = max(mss, 32);
	if (mss < tp->t_maxseg || offer != 0)
	   tp->t_maxseg = mss;

	tp->snd_cwnd = mss;

	sbreserve(&so->so_snd, TCP_SNDSPACE + ((TCP_SNDSPACE % mss) ?
                                               (mss - (TCP_SNDSPACE % mss)) :
                                               0));
	sbreserve(&so->so_rcv, TCP_RCVSPACE + ((TCP_RCVSPACE % mss) ?
                                               (mss - (TCP_RCVSPACE % mss)) :
                                               0));

	DEBUG_MISC((dfd, " returning mss = %d\n", mss));

	return mss;
}
