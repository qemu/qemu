/*
 * tftp.c - a simple, read-only tftp server for qemu
 *
 * Copyright (c) 2004 Magnus Damm <damm@opensource.se>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "slirp.h"
#include "qemu-common.h"
#include "qemu/cutils.h"

static inline int tftp_session_in_use(struct tftp_session *spt)
{
    return (spt->slirp != NULL);
}

static inline void tftp_session_update(struct tftp_session *spt)
{
    spt->timestamp = curtime;
}

static void tftp_session_terminate(struct tftp_session *spt)
{
    if (spt->fd >= 0) {
        close(spt->fd);
        spt->fd = -1;
    }
    g_free(spt->filename);
    spt->slirp = NULL;
}

static int tftp_session_allocate(Slirp *slirp, struct sockaddr_storage *srcsas,
                                 struct tftp_t *tp)
{
  struct tftp_session *spt;
  int k;

  for (k = 0; k < TFTP_SESSIONS_MAX; k++) {
    spt = &slirp->tftp_sessions[k];

    if (!tftp_session_in_use(spt))
        goto found;

    /* sessions time out after 5 inactive seconds */
    if ((int)(curtime - spt->timestamp) > 5000) {
        tftp_session_terminate(spt);
        goto found;
    }
  }

  return -1;

 found:
  memset(spt, 0, sizeof(*spt));
  memcpy(&spt->client_addr, srcsas, sockaddr_size(srcsas));
  spt->fd = -1;
  spt->block_size = 512;
  spt->client_port = tp->udp.uh_sport;
  spt->slirp = slirp;

  tftp_session_update(spt);

  return k;
}

static int tftp_session_find(Slirp *slirp, struct sockaddr_storage *srcsas,
                             struct tftp_t *tp)
{
  struct tftp_session *spt;
  int k;

  for (k = 0; k < TFTP_SESSIONS_MAX; k++) {
    spt = &slirp->tftp_sessions[k];

    if (tftp_session_in_use(spt)) {
      if (sockaddr_equal(&spt->client_addr, srcsas)) {
	if (spt->client_port == tp->udp.uh_sport) {
	  return k;
	}
      }
    }
  }

  return -1;
}

static int tftp_read_data(struct tftp_session *spt, uint32_t block_nr,
                          uint8_t *buf, int len)
{
    int bytes_read = 0;

    if (spt->fd < 0) {
        spt->fd = open(spt->filename, O_RDONLY | O_BINARY);
    }

    if (spt->fd < 0) {
        return -1;
    }

    if (len) {
        lseek(spt->fd, block_nr * spt->block_size, SEEK_SET);

        bytes_read = read(spt->fd, buf, len);
    }

    return bytes_read;
}

static struct tftp_t *tftp_prep_mbuf_data(struct tftp_session *spt,
                                          struct mbuf *m)
{
    struct tftp_t *tp;

    memset(m->m_data, 0, m->m_size);

    m->m_data += IF_MAXLINKHDR;
    if (spt->client_addr.ss_family == AF_INET6) {
        m->m_data += sizeof(struct ip6);
    } else {
        m->m_data += sizeof(struct ip);
    }
    tp = (void *)m->m_data;
    m->m_data += sizeof(struct udphdr);

    return tp;
}

static void tftp_udp_output(struct tftp_session *spt, struct mbuf *m,
                            struct tftp_t *recv_tp)
{
    if (spt->client_addr.ss_family == AF_INET6) {
        struct sockaddr_in6 sa6, da6;

        sa6.sin6_addr = spt->slirp->vhost_addr6;
        sa6.sin6_port = recv_tp->udp.uh_dport;
        da6.sin6_addr = ((struct sockaddr_in6 *)&spt->client_addr)->sin6_addr;
        da6.sin6_port = spt->client_port;

        udp6_output(NULL, m, &sa6, &da6);
    } else {
        struct sockaddr_in sa4, da4;

        sa4.sin_addr = spt->slirp->vhost_addr;
        sa4.sin_port = recv_tp->udp.uh_dport;
        da4.sin_addr = ((struct sockaddr_in *)&spt->client_addr)->sin_addr;
        da4.sin_port = spt->client_port;

        udp_output(NULL, m, &sa4, &da4, IPTOS_LOWDELAY);
    }
}

static int tftp_send_oack(struct tftp_session *spt,
                          const char *keys[], uint32_t values[], int nb,
                          struct tftp_t *recv_tp)
{
    struct mbuf *m;
    struct tftp_t *tp;
    int i, n = 0;

    m = m_get(spt->slirp);

    if (!m)
        return -1;

    tp = tftp_prep_mbuf_data(spt, m);

    tp->tp_op = htons(TFTP_OACK);
    for (i = 0; i < nb; i++) {
        n += snprintf(tp->x.tp_buf + n, sizeof(tp->x.tp_buf) - n, "%s",
                      keys[i]) + 1;
        n += snprintf(tp->x.tp_buf + n, sizeof(tp->x.tp_buf) - n, "%u",
                      values[i]) + 1;
    }

    m->m_len = sizeof(struct tftp_t) - (TFTP_BLOCKSIZE_MAX + 2) + n
               - sizeof(struct udphdr);
    tftp_udp_output(spt, m, recv_tp);

    return 0;
}

static void tftp_send_error(struct tftp_session *spt,
                            uint16_t errorcode, const char *msg,
                            struct tftp_t *recv_tp)
{
  struct mbuf *m;
  struct tftp_t *tp;

  m = m_get(spt->slirp);

  if (!m) {
    goto out;
  }

  tp = tftp_prep_mbuf_data(spt, m);

  tp->tp_op = htons(TFTP_ERROR);
  tp->x.tp_error.tp_error_code = htons(errorcode);
  pstrcpy((char *)tp->x.tp_error.tp_msg, sizeof(tp->x.tp_error.tp_msg), msg);

  m->m_len = sizeof(struct tftp_t) - (TFTP_BLOCKSIZE_MAX + 2) + 3 + strlen(msg)
             - sizeof(struct udphdr);
  tftp_udp_output(spt, m, recv_tp);

out:
  tftp_session_terminate(spt);
}

static void tftp_send_next_block(struct tftp_session *spt,
                                 struct tftp_t *recv_tp)
{
  struct mbuf *m;
  struct tftp_t *tp;
  int nobytes;

  m = m_get(spt->slirp);

  if (!m) {
    return;
  }

  tp = tftp_prep_mbuf_data(spt, m);

  tp->tp_op = htons(TFTP_DATA);
  tp->x.tp_data.tp_block_nr = htons((spt->block_nr + 1) & 0xffff);

  nobytes = tftp_read_data(spt, spt->block_nr, tp->x.tp_data.tp_buf,
                           spt->block_size);

  if (nobytes < 0) {
    m_free(m);

    /* send "file not found" error back */

    tftp_send_error(spt, 1, "File not found", tp);

    return;
  }

  m->m_len = sizeof(struct tftp_t) - (TFTP_BLOCKSIZE_MAX - nobytes)
             - sizeof(struct udphdr);
  tftp_udp_output(spt, m, recv_tp);

  if (nobytes == spt->block_size) {
    tftp_session_update(spt);
  }
  else {
    tftp_session_terminate(spt);
  }

  spt->block_nr++;
}

static void tftp_handle_rrq(Slirp *slirp, struct sockaddr_storage *srcsas,
                            struct tftp_t *tp, int pktlen)
{
  struct tftp_session *spt;
  int s, k;
  size_t prefix_len;
  char *req_fname;
  const char *option_name[2];
  uint32_t option_value[2];
  int nb_options = 0;

  /* check if a session already exists and if so terminate it */
  s = tftp_session_find(slirp, srcsas, tp);
  if (s >= 0) {
    tftp_session_terminate(&slirp->tftp_sessions[s]);
  }

  s = tftp_session_allocate(slirp, srcsas, tp);

  if (s < 0) {
    return;
  }

  spt = &slirp->tftp_sessions[s];

  /* unspecified prefix means service disabled */
  if (!slirp->tftp_prefix) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
  }

  /* skip header fields */
  k = 0;
  pktlen -= offsetof(struct tftp_t, x.tp_buf);

  /* prepend tftp_prefix */
  prefix_len = strlen(slirp->tftp_prefix);
  spt->filename = g_malloc(prefix_len + TFTP_FILENAME_MAX + 2);
  memcpy(spt->filename, slirp->tftp_prefix, prefix_len);
  spt->filename[prefix_len] = '/';

  /* get name */
  req_fname = spt->filename + prefix_len + 1;

  while (1) {
    if (k >= TFTP_FILENAME_MAX || k >= pktlen) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
    }
    req_fname[k] = tp->x.tp_buf[k];
    if (req_fname[k++] == '\0') {
      break;
    }
  }

  /* check mode */
  if ((pktlen - k) < 6) {
    tftp_send_error(spt, 2, "Access violation", tp);
    return;
  }

  if (strcasecmp(&tp->x.tp_buf[k], "octet") != 0) {
      tftp_send_error(spt, 4, "Unsupported transfer mode", tp);
      return;
  }

  k += 6; /* skipping octet */

  /* do sanity checks on the filename */
  if (!strncmp(req_fname, "../", 3) ||
      req_fname[strlen(req_fname) - 1] == '/' ||
      strstr(req_fname, "/../")) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
  }

  /* check if the file exists */
  if (tftp_read_data(spt, 0, NULL, 0) < 0) {
      tftp_send_error(spt, 1, "File not found", tp);
      return;
  }

  if (tp->x.tp_buf[pktlen - 1] != 0) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
  }

  while (k < pktlen && nb_options < ARRAY_SIZE(option_name)) {
      const char *key, *value;

      key = &tp->x.tp_buf[k];
      k += strlen(key) + 1;

      if (k >= pktlen) {
	  tftp_send_error(spt, 2, "Access violation", tp);
	  return;
      }

      value = &tp->x.tp_buf[k];
      k += strlen(value) + 1;

      if (strcasecmp(key, "tsize") == 0) {
	  int tsize = atoi(value);
	  struct stat stat_p;

	  if (tsize == 0) {
	      if (stat(spt->filename, &stat_p) == 0)
		  tsize = stat_p.st_size;
	      else {
		  tftp_send_error(spt, 1, "File not found", tp);
		  return;
	      }
	  }

          option_name[nb_options] = "tsize";
          option_value[nb_options] = tsize;
          nb_options++;
      } else if (strcasecmp(key, "blksize") == 0) {
          int blksize = atoi(value);

          /* Accept blksize up to our maximum size */
          if (blksize > 0) {
              spt->block_size = MIN(blksize, TFTP_BLOCKSIZE_MAX);
              option_name[nb_options] = "blksize";
              option_value[nb_options] = spt->block_size;
              nb_options++;
          }
      }
  }

  if (nb_options > 0) {
      assert(nb_options <= ARRAY_SIZE(option_name));
      tftp_send_oack(spt, option_name, option_value, nb_options, tp);
      return;
  }

  spt->block_nr = 0;
  tftp_send_next_block(spt, tp);
}

static void tftp_handle_ack(Slirp *slirp, struct sockaddr_storage *srcsas,
                            struct tftp_t *tp, int pktlen)
{
  int s;

  s = tftp_session_find(slirp, srcsas, tp);

  if (s < 0) {
    return;
  }

  tftp_send_next_block(&slirp->tftp_sessions[s], tp);
}

static void tftp_handle_error(Slirp *slirp, struct sockaddr_storage *srcsas,
                              struct tftp_t *tp, int pktlen)
{
  int s;

  s = tftp_session_find(slirp, srcsas, tp);

  if (s < 0) {
    return;
  }

  tftp_session_terminate(&slirp->tftp_sessions[s]);
}

void tftp_input(struct sockaddr_storage *srcsas, struct mbuf *m)
{
  struct tftp_t *tp = (struct tftp_t *)m->m_data;

  switch(ntohs(tp->tp_op)) {
  case TFTP_RRQ:
    tftp_handle_rrq(m->slirp, srcsas, tp, m->m_len);
    break;

  case TFTP_ACK:
    tftp_handle_ack(m->slirp, srcsas, tp, m->m_len);
    break;

  case TFTP_ERROR:
    tftp_handle_error(m->slirp, srcsas, tp, m->m_len);
    break;
  }
}
