/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>

static void sbappendsb(struct sbuf *sb, struct mbuf *m);

/* Done as a macro in socket.h */
/* int
 * sbspace(struct sockbuff *sb)
 * {
 *	return SB_DATALEN - sb->sb_cc;
 * }
 */

void
sbfree(sb)
	struct sbuf *sb;
{
	free(sb->sb_data);
}

void
sbdrop(sb, num)
	struct sbuf *sb;
	int num;
{
	/*
	 * We can only drop how much we have
	 * This should never succeed
	 */
	if(num > sb->sb_cc)
		num = sb->sb_cc;
	sb->sb_cc -= num;
	sb->sb_rptr += num;
	if(sb->sb_rptr >= sb->sb_data + sb->sb_datalen)
		sb->sb_rptr -= sb->sb_datalen;

}

void
sbreserve(sb, size)
	struct sbuf *sb;
	int size;
{
	if (sb->sb_data) {
		/* Already alloced, realloc if necessary */
		if (sb->sb_datalen != size) {
			sb->sb_wptr = sb->sb_rptr = sb->sb_data = (char *)realloc(sb->sb_data, size);
			sb->sb_cc = 0;
			if (sb->sb_wptr)
			   sb->sb_datalen = size;
			else
			   sb->sb_datalen = 0;
		}
	} else {
		sb->sb_wptr = sb->sb_rptr = sb->sb_data = (char *)malloc(size);
		sb->sb_cc = 0;
		if (sb->sb_wptr)
		   sb->sb_datalen = size;
		else
		   sb->sb_datalen = 0;
	}
}

/*
 * Try and write() to the socket, whatever doesn't get written
 * append to the buffer... for a host with a fast net connection,
 * this prevents an unnecessary copy of the data
 * (the socket is non-blocking, so we won't hang)
 */
void
sbappend(so, m)
	struct socket *so;
	struct mbuf *m;
{
	int ret = 0;

	DEBUG_CALL("sbappend");
	DEBUG_ARG("so = %lx", (long)so);
	DEBUG_ARG("m = %lx", (long)m);
	DEBUG_ARG("m->m_len = %d", m->m_len);

	/* Shouldn't happen, but...  e.g. foreign host closes connection */
	if (m->m_len <= 0) {
		m_free(m);
		return;
	}

	/*
	 * If there is urgent data, call sosendoob
	 * if not all was sent, sowrite will take care of the rest
	 * (The rest of this function is just an optimisation)
	 */
	if (so->so_urgc) {
		sbappendsb(&so->so_rcv, m);
		m_free(m);
		sosendoob(so);
		return;
	}

	/*
	 * We only write if there's nothing in the buffer,
	 * ottherwise it'll arrive out of order, and hence corrupt
	 */
	if (!so->so_rcv.sb_cc)
	   ret = send(so->s, m->m_data, m->m_len, 0);

	if (ret <= 0) {
		/*
		 * Nothing was written
		 * It's possible that the socket has closed, but
		 * we don't need to check because if it has closed,
		 * it will be detected in the normal way by soread()
		 */
		sbappendsb(&so->so_rcv, m);
	} else if (ret != m->m_len) {
		/*
		 * Something was written, but not everything..
		 * sbappendsb the rest
		 */
		m->m_len -= ret;
		m->m_data += ret;
		sbappendsb(&so->so_rcv, m);
	} /* else */
	/* Whatever happened, we free the mbuf */
	m_free(m);
}

/*
 * Copy the data from m into sb
 * The caller is responsible to make sure there's enough room
 */
static void
sbappendsb(struct sbuf *sb, struct mbuf *m)
{
	int len, n,  nn;

	len = m->m_len;

	if (sb->sb_wptr < sb->sb_rptr) {
		n = sb->sb_rptr - sb->sb_wptr;
		if (n > len) n = len;
		memcpy(sb->sb_wptr, m->m_data, n);
	} else {
		/* Do the right edge first */
		n = sb->sb_data + sb->sb_datalen - sb->sb_wptr;
		if (n > len) n = len;
		memcpy(sb->sb_wptr, m->m_data, n);
		len -= n;
		if (len) {
			/* Now the left edge */
			nn = sb->sb_rptr - sb->sb_data;
			if (nn > len) nn = len;
			memcpy(sb->sb_data,m->m_data+n,nn);
			n += nn;
		}
	}

	sb->sb_cc += n;
	sb->sb_wptr += n;
	if (sb->sb_wptr >= sb->sb_data + sb->sb_datalen)
		sb->sb_wptr -= sb->sb_datalen;
}

/*
 * Copy data from sbuf to a normal, straight buffer
 * Don't update the sbuf rptr, this will be
 * done in sbdrop when the data is acked
 */
void
sbcopy(sb, off, len, to)
	struct sbuf *sb;
	int off;
	int len;
	char *to;
{
	char *from;

	from = sb->sb_rptr + off;
	if (from >= sb->sb_data + sb->sb_datalen)
		from -= sb->sb_datalen;

	if (from < sb->sb_wptr) {
		if (len > sb->sb_cc) len = sb->sb_cc;
		memcpy(to,from,len);
	} else {
		/* re-use off */
		off = (sb->sb_data + sb->sb_datalen) - from;
		if (off > len) off = len;
		memcpy(to,from,off);
		len -= off;
		if (len)
		   memcpy(to+off,sb->sb_data,len);
	}
}
