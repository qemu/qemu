/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifndef _IF_H_
#define _IF_H_

#define IF_COMPRESS	0x01	/* We want compression */
#define IF_NOCOMPRESS	0x02	/* Do not do compression */
#define IF_AUTOCOMP	0x04	/* Autodetect (default) */
#define IF_NOCIDCOMP	0x08	/* CID compression */

#define IF_MTU 1500
#define IF_MRU 1500
#define	IF_COMP IF_AUTOCOMP	/* Flags for compression */

#if 0
/*
 * Set if_maxlinkhdr to 48 because it's 40 bytes for TCP/IP,
 * and 8 bytes for PPP, but need to have it on an 8byte boundary
 */
#ifdef USE_PPP
#define IF_MAXLINKHDR 48
#else
#define IF_MAXLINKHDR 40
#endif
#else
        /* 2 for alignment, 14 for ethernet, 40 for TCP/IP */
#define IF_MAXLINKHDR (2 + 14 + 40)
#endif

extern int	if_queued;	/* Number of packets queued so far */

extern	struct mbuf if_fastq;                  /* fast queue (for interactive data) */
extern	struct mbuf if_batchq;                 /* queue for non-interactive data */
extern	struct mbuf *next_m;

#define ifs_init(ifm) ((ifm)->ifs_next = (ifm)->ifs_prev = (ifm))

#ifdef LOG_ENABLED
/* Interface statistics */
struct slirp_ifstats {
	u_int out_pkts;		/* Output packets */
	u_int out_bytes;		/* Output bytes */
	u_int out_errpkts;	/* Output Error Packets */
	u_int out_errbytes;	/* Output Error Bytes */
	u_int in_pkts;		/* Input packets */
	u_int in_bytes;		/* Input bytes */
	u_int in_errpkts;		/* Input Error Packets */
	u_int in_errbytes;	/* Input Error Bytes */

	u_int bytes_saved;	/* Number of bytes that compression "saved" */
				/* ie: number of bytes that didn't need to be sent over the link
				 * because of compression */

	u_int in_mbad;		/* Bad incoming packets */
};
#endif

#endif
