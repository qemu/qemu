/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include "qemu/osdep.h"
#include "slirp.h"
#include "qemu/timer.h"

static void
ifs_insque(struct mbuf *ifm, struct mbuf *ifmhead)
{
	ifm->ifs_next = ifmhead->ifs_next;
	ifmhead->ifs_next = ifm;
	ifm->ifs_prev = ifmhead;
	ifm->ifs_next->ifs_prev = ifm;
}

static void
ifs_remque(struct mbuf *ifm)
{
	ifm->ifs_prev->ifs_next = ifm->ifs_next;
	ifm->ifs_next->ifs_prev = ifm->ifs_prev;
}

void
if_init(Slirp *slirp)
{
    slirp->if_fastq.qh_link = slirp->if_fastq.qh_rlink = &slirp->if_fastq;
    slirp->if_batchq.qh_link = slirp->if_batchq.qh_rlink = &slirp->if_batchq;
    slirp->next_m = (struct mbuf *) &slirp->if_batchq;
}

/*
 * if_output: Queue packet into an output queue.
 * There are 2 output queue's, if_fastq and if_batchq.
 * Each output queue is a doubly linked list of double linked lists
 * of mbufs, each list belonging to one "session" (socket).  This
 * way, we can output packets fairly by sending one packet from each
 * session, instead of all the packets from one session, then all packets
 * from the next session, etc.  Packets on the if_fastq get absolute
 * priority, but if one session hogs the link, it gets "downgraded"
 * to the batchq until it runs out of packets, then it'll return
 * to the fastq (eg. if the user does an ls -alR in a telnet session,
 * it'll temporarily get downgraded to the batchq)
 */
void
if_output(struct socket *so, struct mbuf *ifm)
{
	Slirp *slirp = ifm->slirp;
	struct mbuf *ifq;
	int on_fastq = 1;

	DEBUG_CALL("if_output");
	DEBUG_ARG("so = %p", so);
	DEBUG_ARG("ifm = %p", ifm);

	/*
	 * First remove the mbuf from m_usedlist,
	 * since we're gonna use m_next and m_prev ourselves
	 * XXX Shouldn't need this, gotta change dtom() etc.
	 */
	if (ifm->m_flags & M_USEDLIST) {
		remque(ifm);
		ifm->m_flags &= ~M_USEDLIST;
	}

	/*
	 * See if there's already a batchq list for this session.
	 * This can include an interactive session, which should go on fastq,
	 * but gets too greedy... hence it'll be downgraded from fastq to batchq.
	 * We mustn't put this packet back on the fastq (or we'll send it out of order)
	 * XXX add cache here?
	 */
	for (ifq = (struct mbuf *) slirp->if_batchq.qh_rlink;
	     (struct quehead *) ifq != &slirp->if_batchq;
	     ifq = ifq->ifq_prev) {
		if (so == ifq->ifq_so) {
			/* A match! */
			ifm->ifq_so = so;
			ifs_insque(ifm, ifq->ifs_prev);
			goto diddit;
		}
	}

	/* No match, check which queue to put it on */
	if (so && (so->so_iptos & IPTOS_LOWDELAY)) {
		ifq = (struct mbuf *) slirp->if_fastq.qh_rlink;
		on_fastq = 1;
		/*
		 * Check if this packet is a part of the last
		 * packet's session
		 */
		if (ifq->ifq_so == so) {
			ifm->ifq_so = so;
			ifs_insque(ifm, ifq->ifs_prev);
			goto diddit;
		}
        } else {
		ifq = (struct mbuf *) slirp->if_batchq.qh_rlink;
                /* Set next_m if the queue was empty so far */
                if ((struct quehead *) slirp->next_m == &slirp->if_batchq) {
                    slirp->next_m = ifm;
                }
        }

	/* Create a new doubly linked list for this session */
	ifm->ifq_so = so;
	ifs_init(ifm);
	insque(ifm, ifq);

diddit:
	if (so) {
		/* Update *_queued */
		so->so_queued++;
		so->so_nqueued++;
		/*
		 * Check if the interactive session should be downgraded to
		 * the batchq.  A session is downgraded if it has queued 6
		 * packets without pausing, and at least 3 of those packets
		 * have been sent over the link
		 * (XXX These are arbitrary numbers, probably not optimal..)
		 */
		if (on_fastq && ((so->so_nqueued >= 6) &&
				 (so->so_nqueued - so->so_queued) >= 3)) {

			/* Remove from current queue... */
			remque(ifm->ifs_next);

			/* ...And insert in the new.  That'll teach ya! */
			insque(ifm->ifs_next, &slirp->if_batchq);
		}
	}

#ifndef FULL_BOLT
	/*
	 * This prevents us from malloc()ing too many mbufs
	 */
	if_start(ifm->slirp);
#endif
}

/*
 * Send a packet
 * We choose a packet based on its position in the output queues;
 * If there are packets on the fastq, they are sent FIFO, before
 * everything else.  Otherwise we choose the first packet from the
 * batchq and send it.  the next packet chosen will be from the session
 * after this one, then the session after that one, and so on..  So,
 * for example, if there are 3 ftp session's fighting for bandwidth,
 * one packet will be sent from the first session, then one packet
 * from the second session, then one packet from the third, then back
 * to the first, etc. etc.
 */
void if_start(Slirp *slirp)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    bool from_batchq, next_from_batchq;
    struct mbuf *ifm, *ifm_next, *ifqt;

    DEBUG_CALL("if_start");

    if (slirp->if_start_busy) {
        return;
    }
    slirp->if_start_busy = true;

    if (slirp->if_fastq.qh_link != &slirp->if_fastq) {
        ifm_next = (struct mbuf *) slirp->if_fastq.qh_link;
        next_from_batchq = false;
    } else if ((struct quehead *) slirp->next_m != &slirp->if_batchq) {
        /* Nothing on fastq, pick up from batchq via next_m */
        ifm_next = slirp->next_m;
        next_from_batchq = true;
    } else {
        ifm_next = NULL;
    }

    while (ifm_next) {
        ifm = ifm_next;
        from_batchq = next_from_batchq;

        ifm_next = ifm->ifq_next;
        if ((struct quehead *) ifm_next == &slirp->if_fastq) {
            /* No more packets in fastq, switch to batchq */
            ifm_next = slirp->next_m;
            next_from_batchq = true;
        }
        if ((struct quehead *) ifm_next == &slirp->if_batchq) {
            /* end of batchq */
            ifm_next = NULL;
        }

        /* Try to send packet unless it already expired */
        if (ifm->expiration_date >= now && !if_encap(slirp, ifm)) {
            /* Packet is delayed due to pending ARP or NDP resolution */
            continue;
        }

        if (ifm == slirp->next_m) {
            /* Set which packet to send on next iteration */
            slirp->next_m = ifm->ifq_next;
        }

        /* Remove it from the queue */
        ifqt = ifm->ifq_prev;
        remque(ifm);

        /* If there are more packets for this session, re-queue them */
        if (ifm->ifs_next != ifm) {
            struct mbuf *next = ifm->ifs_next;

            insque(next, ifqt);
            ifs_remque(ifm);

            if (!from_batchq) {
                /* Next packet in fastq is from the same session */
                ifm_next = next;
                next_from_batchq = false;
            } else if ((struct quehead *) slirp->next_m == &slirp->if_batchq) {
                /* Set next_m and ifm_next if the session packet is now the
                 * only one on batchq */
                slirp->next_m = ifm_next = next;
            }
        }

        /* Update so_queued */
        if (ifm->ifq_so && --ifm->ifq_so->so_queued == 0) {
            /* If there's no more queued, reset nqueued */
            ifm->ifq_so->so_nqueued = 0;
        }

        m_free(ifm);
    }

    slirp->if_start_busy = false;
}
