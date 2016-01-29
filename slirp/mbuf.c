/*
 * Copyright (c) 1995 Danny Gasparovski
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

/*
 * mbuf's in SLiRP are much simpler than the real mbufs in
 * FreeBSD.  They are fixed size, determined by the MTU,
 * so that one whole packet can fit.  Mbuf's cannot be
 * chained together.  If there's more data than the mbuf
 * could hold, an external malloced buffer is pointed to
 * by m_ext (and the data pointers) and M_EXT is set in
 * the flags
 */

#include "qemu/osdep.h"
#include <slirp.h>

#define MBUF_THRESH 30

/*
 * Find a nice value for msize
 * XXX if_maxlinkhdr already in mtu
 */
#define SLIRP_MSIZE (IF_MTU + IF_MAXLINKHDR + offsetof(struct mbuf, m_dat) + 6)

void
m_init(Slirp *slirp)
{
    slirp->m_freelist.m_next = slirp->m_freelist.m_prev = &slirp->m_freelist;
    slirp->m_usedlist.m_next = slirp->m_usedlist.m_prev = &slirp->m_usedlist;
}

void m_cleanup(Slirp *slirp)
{
    struct mbuf *m, *next;

    m = slirp->m_usedlist.m_next;
    while (m != &slirp->m_usedlist) {
        next = m->m_next;
        if (m->m_flags & M_EXT) {
            free(m->m_ext);
        }
        free(m);
        m = next;
    }
    m = slirp->m_freelist.m_next;
    while (m != &slirp->m_freelist) {
        next = m->m_next;
        free(m);
        m = next;
    }
}

/*
 * Get an mbuf from the free list, if there are none
 * malloc one
 *
 * Because fragmentation can occur if we alloc new mbufs and
 * free old mbufs, we mark all mbufs above mbuf_thresh as M_DOFREE,
 * which tells m_free to actually free() it
 */
struct mbuf *
m_get(Slirp *slirp)
{
	register struct mbuf *m;
	int flags = 0;

	DEBUG_CALL("m_get");

	if (slirp->m_freelist.m_next == &slirp->m_freelist) {
		m = (struct mbuf *)malloc(SLIRP_MSIZE);
		if (m == NULL) goto end_error;
		slirp->mbuf_alloced++;
		if (slirp->mbuf_alloced > MBUF_THRESH)
			flags = M_DOFREE;
		m->slirp = slirp;
	} else {
		m = slirp->m_freelist.m_next;
		remque(m);
	}

	/* Insert it in the used list */
	insque(m,&slirp->m_usedlist);
	m->m_flags = (flags | M_USEDLIST);

	/* Initialise it */
	m->m_size = SLIRP_MSIZE - offsetof(struct mbuf, m_dat);
	m->m_data = m->m_dat;
	m->m_len = 0;
        m->m_nextpkt = NULL;
        m->m_prevpkt = NULL;
        m->resolution_requested = false;
        m->expiration_date = (uint64_t)-1;
end_error:
	DEBUG_ARG("m = %p", m);
	return m;
}

void
m_free(struct mbuf *m)
{

  DEBUG_CALL("m_free");
  DEBUG_ARG("m = %p", m);

  if(m) {
	/* Remove from m_usedlist */
	if (m->m_flags & M_USEDLIST)
	   remque(m);

	/* If it's M_EXT, free() it */
	if (m->m_flags & M_EXT)
	   free(m->m_ext);

	/*
	 * Either free() it or put it on the free list
	 */
	if (m->m_flags & M_DOFREE) {
		m->slirp->mbuf_alloced--;
		free(m);
	} else if ((m->m_flags & M_FREELIST) == 0) {
		insque(m,&m->slirp->m_freelist);
		m->m_flags = M_FREELIST; /* Clobber other flags */
	}
  } /* if(m) */
}

/*
 * Copy data from one mbuf to the end of
 * the other.. if result is too big for one mbuf, malloc()
 * an M_EXT data segment
 */
void
m_cat(struct mbuf *m, struct mbuf *n)
{
	/*
	 * If there's no room, realloc
	 */
	if (M_FREEROOM(m) < n->m_len)
		m_inc(m,m->m_size+MINCSIZE);

	memcpy(m->m_data+m->m_len, n->m_data, n->m_len);
	m->m_len += n->m_len;

	m_free(n);
}


/* make m size bytes large */
void
m_inc(struct mbuf *m, int size)
{
	int datasize;

	/* some compiles throw up on gotos.  This one we can fake. */
        if(m->m_size>size) return;

        if (m->m_flags & M_EXT) {
	  datasize = m->m_data - m->m_ext;
	  m->m_ext = (char *)realloc(m->m_ext,size);
	  m->m_data = m->m_ext + datasize;
        } else {
	  char *dat;
	  datasize = m->m_data - m->m_dat;
	  dat = (char *)malloc(size);
	  memcpy(dat, m->m_dat, m->m_size);

	  m->m_ext = dat;
	  m->m_data = m->m_ext + datasize;
	  m->m_flags |= M_EXT;
        }

        m->m_size = size;

}



void
m_adj(struct mbuf *m, int len)
{
	if (m == NULL)
		return;
	if (len >= 0) {
		/* Trim from head */
		m->m_data += len;
		m->m_len -= len;
	} else {
		/* Trim from tail */
		len = -len;
		m->m_len -= len;
	}
}


/*
 * Copy len bytes from m, starting off bytes into n
 */
int
m_copy(struct mbuf *n, struct mbuf *m, int off, int len)
{
	if (len > M_FREEROOM(n))
		return -1;

	memcpy((n->m_data + n->m_len), (m->m_data + off), len);
	n->m_len += len;
	return 0;
}


/*
 * Given a pointer into an mbuf, return the mbuf
 * XXX This is a kludge, I should eliminate the need for it
 * Fortunately, it's not used often
 */
struct mbuf *
dtom(Slirp *slirp, void *dat)
{
	struct mbuf *m;

	DEBUG_CALL("dtom");
	DEBUG_ARG("dat = %p", dat);

	/* bug corrected for M_EXT buffers */
	for (m = slirp->m_usedlist.m_next; m != &slirp->m_usedlist;
	     m = m->m_next) {
	  if (m->m_flags & M_EXT) {
	    if( (char *)dat>=m->m_ext && (char *)dat<(m->m_ext + m->m_size) )
	      return m;
	  } else {
	    if( (char *)dat >= m->m_dat && (char *)dat<(m->m_dat + m->m_size) )
	      return m;
	  }
	}

	DEBUG_ERROR((dfd, "dtom failed"));

	return (struct mbuf *)0;
}
