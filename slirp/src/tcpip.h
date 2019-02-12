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
 *	@(#)tcpip.h	8.1 (Berkeley) 6/10/93
 * tcpip.h,v 1.3 1994/08/21 05:27:40 paul Exp
 */

#ifndef TCPIP_H
#define TCPIP_H

/*
 * Tcp+ip header, after ip options removed.
 */
struct tcpiphdr {
    struct mbuf_ptr ih_mbuf;	/* backpointer to mbuf */
    union {
        struct {
            struct  in_addr ih_src; /* source internet address */
            struct  in_addr ih_dst; /* destination internet address */
            uint8_t ih_x1;          /* (unused) */
            uint8_t ih_pr;          /* protocol */
        } ti_i4;
        struct {
            struct  in6_addr ih_src;
            struct  in6_addr ih_dst;
            uint8_t ih_x1;
            uint8_t ih_nh;
        } ti_i6;
    } ti;
    uint16_t    ti_x0;
    uint16_t    ti_len;             /* protocol length */
    struct      tcphdr ti_t;        /* tcp header */
};
#define	ti_mbuf		ih_mbuf.mptr
#define	ti_pr		ti.ti_i4.ih_pr
#define	ti_src		ti.ti_i4.ih_src
#define	ti_dst		ti.ti_i4.ih_dst
#define	ti_src6		ti.ti_i6.ih_src
#define	ti_dst6		ti.ti_i6.ih_dst
#define	ti_nh6		ti.ti_i6.ih_nh
#define	ti_sport	ti_t.th_sport
#define	ti_dport	ti_t.th_dport
#define	ti_seq		ti_t.th_seq
#define	ti_ack		ti_t.th_ack
#define	ti_x2		ti_t.th_x2
#define	ti_off		ti_t.th_off
#define	ti_flags	ti_t.th_flags
#define	ti_win		ti_t.th_win
#define	ti_sum		ti_t.th_sum
#define	ti_urp		ti_t.th_urp

#define tcpiphdr2qlink(T) ((struct qlink*)(((char*)(T)) - sizeof(struct qlink)))
#define qlink2tcpiphdr(Q) ((struct tcpiphdr*)(((char*)(Q)) + sizeof(struct qlink)))
#define tcpiphdr_next(T) qlink2tcpiphdr(tcpiphdr2qlink(T)->next)
#define tcpiphdr_prev(T) qlink2tcpiphdr(tcpiphdr2qlink(T)->prev)
#define tcpfrag_list_first(T) qlink2tcpiphdr((T)->seg_next)
#define tcpfrag_list_end(F, T) (tcpiphdr2qlink(F) == (struct qlink*)(T))
#define tcpfrag_list_empty(T) ((T)->seg_next == (struct tcpiphdr*)(T))

/* This is the difference between the size of a tcpiphdr structure, and the
 * size of actual ip+tcp headers, rounded up since we need to align data.  */
#define TCPIPHDR_DELTA\
    (MAX(0,\
         (sizeof(struct tcpiphdr)\
          - sizeof(struct ip) - sizeof(struct tcphdr) + 3) & ~3))

/*
 * Just a clean way to get to the first byte
 * of the packet
 */
struct tcpiphdr_2 {
	struct tcpiphdr dummy;
	char first_char;
};

#endif
