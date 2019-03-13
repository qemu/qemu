/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 1995 Danny Gasparovski.
 */

#ifndef SBUF_H
#define SBUF_H

#define sbspace(sb) ((sb)->sb_datalen - (sb)->sb_cc)

struct sbuf {
	uint32_t sb_cc;		/* actual chars in buffer */
	uint32_t sb_datalen;	/* Length of data  */
	char	*sb_wptr;	/* write pointer. points to where the next
				 * bytes should be written in the sbuf */
	char	*sb_rptr;	/* read pointer. points to where the next
				 * byte should be read from the sbuf */
	char	*sb_data;	/* Actual data */
};

void sbfree(struct sbuf *);
bool sbdrop(struct sbuf *, int);
void sbreserve(struct sbuf *, int);
void sbappend(struct socket *, struct mbuf *);
void sbcopy(struct sbuf *, int, int, char *);

#endif
