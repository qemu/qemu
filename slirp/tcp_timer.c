/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
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
 *	@(#)tcp_timer.c	8.1 (Berkeley) 6/10/93
 * tcp_timer.c,v 1.2 1994/08/02 07:49:10 davidg Exp
 */

#include "qemu/osdep.h"
#include <slirp.h>

static struct tcpcb *tcp_timers(register struct tcpcb *tp, int timer);

/*
 * Fast timeout routine for processing delayed acks
 */
void
tcp_fasttimo(Slirp *slirp)
{
	register struct socket *so;
	register struct tcpcb *tp;

	DEBUG_CALL("tcp_fasttimo");

	so = slirp->tcb.so_next;
	if (so)
	for (; so != &slirp->tcb; so = so->so_next)
		if ((tp = (struct tcpcb *)so->so_tcpcb) &&
		    (tp->t_flags & TF_DELACK)) {
			tp->t_flags &= ~TF_DELACK;
			tp->t_flags |= TF_ACKNOW;
			(void) tcp_output(tp);
		}
}

/*
 * Tcp protocol timeout routine called every 500 ms.
 * Updates the timers in all active tcb's and
 * causes finite state machine actions if timers expire.
 */
void
tcp_slowtimo(Slirp *slirp)
{
	register struct socket *ip, *ipnxt;
	register struct tcpcb *tp;
	register int i;

	DEBUG_CALL("tcp_slowtimo");

	/*
	 * Search through tcb's and update active timers.
	 */
	ip = slirp->tcb.so_next;
        if (ip == NULL) {
            return;
        }
	for (; ip != &slirp->tcb; ip = ipnxt) {
		ipnxt = ip->so_next;
		tp = sototcpcb(ip);
                if (tp == NULL) {
                        continue;
                }
		for (i = 0; i < TCPT_NTIMERS; i++) {
			if (tp->t_timer[i] && --tp->t_timer[i] == 0) {
				tcp_timers(tp,i);
				if (ipnxt->so_prev != ip)
					goto tpgone;
			}
		}
		tp->t_idle++;
		if (tp->t_rtt)
		   tp->t_rtt++;
tpgone:
		;
	}
	slirp->tcp_iss += TCP_ISSINCR/PR_SLOWHZ;	/* increment iss */
	slirp->tcp_now++;				/* for timestamps */
}

/*
 * Cancel all timers for TCP tp.
 */
void
tcp_canceltimers(struct tcpcb *tp)
{
	register int i;

	for (i = 0; i < TCPT_NTIMERS; i++)
		tp->t_timer[i] = 0;
}

const int tcp_backoff[TCP_MAXRXTSHIFT + 1] =
   { 1, 2, 4, 8, 16, 32, 64, 64, 64, 64, 64, 64, 64 };

/*
 * TCP timer processing.
 */
static struct tcpcb *
tcp_timers(register struct tcpcb *tp, int timer)
{
	register int rexmt;

	DEBUG_CALL("tcp_timers");

	switch (timer) {

	/*
	 * 2 MSL timeout in shutdown went off.  If we're closed but
	 * still waiting for peer to close and connection has been idle
	 * too long, or if 2MSL time is up from TIME_WAIT, delete connection
	 * control block.  Otherwise, check again in a bit.
	 */
	case TCPT_2MSL:
		if (tp->t_state != TCPS_TIME_WAIT &&
		    tp->t_idle <= TCP_MAXIDLE)
			tp->t_timer[TCPT_2MSL] = TCPTV_KEEPINTVL;
		else
			tp = tcp_close(tp);
		break;

	/*
	 * Retransmission timer went off.  Message has not
	 * been acked within retransmit interval.  Back off
	 * to a longer retransmit interval and retransmit one segment.
	 */
	case TCPT_REXMT:

		/*
		 * XXXXX If a packet has timed out, then remove all the queued
		 * packets for that session.
		 */

		if (++tp->t_rxtshift > TCP_MAXRXTSHIFT) {
			/*
			 * This is a hack to suit our terminal server here at the uni of canberra
			 * since they have trouble with zeroes... It usually lets them through
			 * unharmed, but under some conditions, it'll eat the zeros.  If we
			 * keep retransmitting it, it'll keep eating the zeroes, so we keep
			 * retransmitting, and eventually the connection dies...
			 * (this only happens on incoming data)
			 *
			 * So, if we were gonna drop the connection from too many retransmits,
			 * don't... instead halve the t_maxseg, which might break up the NULLs and
			 * let them through
			 *
			 * *sigh*
			 */

			tp->t_maxseg >>= 1;
			if (tp->t_maxseg < 32) {
				/*
				 * We tried our best, now the connection must die!
				 */
				tp->t_rxtshift = TCP_MAXRXTSHIFT;
				tp = tcp_drop(tp, tp->t_softerror);
				/* tp->t_softerror : ETIMEDOUT); */ /* XXX */
				return (tp); /* XXX */
			}

			/*
			 * Set rxtshift to 6, which is still at the maximum
			 * backoff time
			 */
			tp->t_rxtshift = 6;
		}
		rexmt = TCP_REXMTVAL(tp) * tcp_backoff[tp->t_rxtshift];
		TCPT_RANGESET(tp->t_rxtcur, rexmt,
		    (short)tp->t_rttmin, TCPTV_REXMTMAX); /* XXX */
		tp->t_timer[TCPT_REXMT] = tp->t_rxtcur;
		/*
		 * If losing, let the lower level know and try for
		 * a better route.  Also, if we backed off this far,
		 * our srtt estimate is probably bogus.  Clobber it
		 * so we'll take the next rtt measurement as our srtt;
		 * move the current srtt into rttvar to keep the current
		 * retransmit times until then.
		 */
		if (tp->t_rxtshift > TCP_MAXRXTSHIFT / 4) {
			tp->t_rttvar += (tp->t_srtt >> TCP_RTT_SHIFT);
			tp->t_srtt = 0;
		}
		tp->snd_nxt = tp->snd_una;
		/*
		 * If timing a segment in this window, stop the timer.
		 */
		tp->t_rtt = 0;
		/*
		 * Close the congestion window down to one segment
		 * (we'll open it by one segment for each ack we get).
		 * Since we probably have a window's worth of unacked
		 * data accumulated, this "slow start" keeps us from
		 * dumping all that data as back-to-back packets (which
		 * might overwhelm an intermediate gateway).
		 *
		 * There are two phases to the opening: Initially we
		 * open by one mss on each ack.  This makes the window
		 * size increase exponentially with time.  If the
		 * window is larger than the path can handle, this
		 * exponential growth results in dropped packet(s)
		 * almost immediately.  To get more time between
		 * drops but still "push" the network to take advantage
		 * of improving conditions, we switch from exponential
		 * to linear window opening at some threshold size.
		 * For a threshold, we use half the current window
		 * size, truncated to a multiple of the mss.
		 *
		 * (the minimum cwnd that will give us exponential
		 * growth is 2 mss.  We don't allow the threshold
		 * to go below this.)
		 */
		{
		u_int win = min(tp->snd_wnd, tp->snd_cwnd) / 2 / tp->t_maxseg;
		if (win < 2)
			win = 2;
		tp->snd_cwnd = tp->t_maxseg;
		tp->snd_ssthresh = win * tp->t_maxseg;
		tp->t_dupacks = 0;
		}
		(void) tcp_output(tp);
		break;

	/*
	 * Persistence timer into zero window.
	 * Force a byte to be output, if possible.
	 */
	case TCPT_PERSIST:
		tcp_setpersist(tp);
		tp->t_force = 1;
		(void) tcp_output(tp);
		tp->t_force = 0;
		break;

	/*
	 * Keep-alive timer went off; send something
	 * or drop connection if idle for too long.
	 */
	case TCPT_KEEP:
		if (tp->t_state < TCPS_ESTABLISHED)
			goto dropit;

		if ((SO_OPTIONS) && tp->t_state <= TCPS_CLOSE_WAIT) {
		    	if (tp->t_idle >= TCPTV_KEEP_IDLE + TCP_MAXIDLE)
				goto dropit;
			/*
			 * Send a packet designed to force a response
			 * if the peer is up and reachable:
			 * either an ACK if the connection is still alive,
			 * or an RST if the peer has closed the connection
			 * due to timeout or reboot.
			 * Using sequence number tp->snd_una-1
			 * causes the transmitted zero-length segment
			 * to lie outside the receive window;
			 * by the protocol spec, this requires the
			 * correspondent TCP to respond.
			 */
			tcp_respond(tp, &tp->t_template, (struct mbuf *)NULL,
			    tp->rcv_nxt, tp->snd_una - 1, 0);
			tp->t_timer[TCPT_KEEP] = TCPTV_KEEPINTVL;
		} else
			tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_IDLE;
		break;

	dropit:
		tp = tcp_drop(tp, 0);
		break;
	}

	return (tp);
}
