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

#include <slirp.h>
#include "qemu-common.h"

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

static int tftp_session_allocate(Slirp *slirp, struct tftp_t *tp)
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
  memcpy(&spt->client_ip, &tp->ip.ip_src, sizeof(spt->client_ip));
  spt->fd = -1;
  spt->client_port = tp->udp.uh_sport;
  spt->slirp = slirp;

  tftp_session_update(spt);

  return k;
}

static int tftp_session_find(Slirp *slirp, struct tftp_t *tp)
{
  struct tftp_session *spt;
  int k;

  for (k = 0; k < TFTP_SESSIONS_MAX; k++) {
    spt = &slirp->tftp_sessions[k];

    if (tftp_session_in_use(spt)) {
      if (!memcmp(&spt->client_ip, &tp->ip.ip_src, sizeof(spt->client_ip))) {
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
        lseek(spt->fd, block_nr * 512, SEEK_SET);

        bytes_read = read(spt->fd, buf, len);
    }

    return bytes_read;
}

static int tftp_send_oack(struct tftp_session *spt,
                          const char *keys[], uint32_t values[], int nb,
                          struct tftp_t *recv_tp)
{
    struct sockaddr_in saddr, daddr;
    struct mbuf *m;
    struct tftp_t *tp;
    int i, n = 0;

    m = m_get(spt->slirp);

    if (!m)
	return -1;

    memset(m->m_data, 0, m->m_size);

    m->m_data += IF_MAXLINKHDR;
    tp = (void *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);

    tp->tp_op = htons(TFTP_OACK);
    for (i = 0; i < nb; i++) {
        n += snprintf(tp->x.tp_buf + n, sizeof(tp->x.tp_buf) - n, "%s",
                      keys[i]) + 1;
        n += snprintf(tp->x.tp_buf + n, sizeof(tp->x.tp_buf) - n, "%u",
                      values[i]) + 1;
    }

    saddr.sin_addr = recv_tp->ip.ip_dst;
    saddr.sin_port = recv_tp->udp.uh_dport;

    daddr.sin_addr = spt->client_ip;
    daddr.sin_port = spt->client_port;

    m->m_len = sizeof(struct tftp_t) - 514 + n -
        sizeof(struct ip) - sizeof(struct udphdr);
    udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);

    return 0;
}

static void tftp_send_error(struct tftp_session *spt,
                            uint16_t errorcode, const char *msg,
                            struct tftp_t *recv_tp)
{
  struct sockaddr_in saddr, daddr;
  struct mbuf *m;
  struct tftp_t *tp;

  m = m_get(spt->slirp);

  if (!m) {
    goto out;
  }

  memset(m->m_data, 0, m->m_size);

  m->m_data += IF_MAXLINKHDR;
  tp = (void *)m->m_data;
  m->m_data += sizeof(struct udpiphdr);

  tp->tp_op = htons(TFTP_ERROR);
  tp->x.tp_error.tp_error_code = htons(errorcode);
  pstrcpy((char *)tp->x.tp_error.tp_msg, sizeof(tp->x.tp_error.tp_msg), msg);

  saddr.sin_addr = recv_tp->ip.ip_dst;
  saddr.sin_port = recv_tp->udp.uh_dport;

  daddr.sin_addr = spt->client_ip;
  daddr.sin_port = spt->client_port;

  m->m_len = sizeof(struct tftp_t) - 514 + 3 + strlen(msg) -
        sizeof(struct ip) - sizeof(struct udphdr);

  udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);

out:
  tftp_session_terminate(spt);
}

static void tftp_send_next_block(struct tftp_session *spt,
                                 struct tftp_t *recv_tp)
{
  struct sockaddr_in saddr, daddr;
  struct mbuf *m;
  struct tftp_t *tp;
  int nobytes;

  m = m_get(spt->slirp);

  if (!m) {
    return;
  }

  memset(m->m_data, 0, m->m_size);

  m->m_data += IF_MAXLINKHDR;
  tp = (void *)m->m_data;
  m->m_data += sizeof(struct udpiphdr);

  tp->tp_op = htons(TFTP_DATA);
  tp->x.tp_data.tp_block_nr = htons((spt->block_nr + 1) & 0xffff);

  saddr.sin_addr = recv_tp->ip.ip_dst;
  saddr.sin_port = recv_tp->udp.uh_dport;

  daddr.sin_addr = spt->client_ip;
  daddr.sin_port = spt->client_port;

  nobytes = tftp_read_data(spt, spt->block_nr, tp->x.tp_data.tp_buf, 512);

  if (nobytes < 0) {
    m_free(m);

    /* send "file not found" error back */

    tftp_send_error(spt, 1, "File not found", tp);

    return;
  }

  m->m_len = sizeof(struct tftp_t) - (512 - nobytes) -
        sizeof(struct ip) - sizeof(struct udphdr);

  udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);

  if (nobytes == 512) {
    tftp_session_update(spt);
  }
  else {
    tftp_session_terminate(spt);
  }

  spt->block_nr++;
}

static void tftp_handle_rrq(Slirp *slirp, struct tftp_t *tp, int pktlen)
{
  struct tftp_session *spt;
  int s, k;
  size_t prefix_len;
  char *req_fname;
  const char *option_name[2];
  uint32_t option_value[2];
  int nb_options = 0;

  /* check if a session already exists and if so terminate it */
  s = tftp_session_find(slirp, tp);
  if (s >= 0) {
    tftp_session_terminate(&slirp->tftp_sessions[s]);
  }

  s = tftp_session_allocate(slirp, tp);

  if (s < 0) {
    return;
  }

  spt = &slirp->tftp_sessions[s];

  /* unspecifed prefix means service disabled */
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

          /* If blksize option is bigger than what we will
           * emit, accept the option with our packet size.
           * Otherwise, simply do as we didn't see the option.
           */
          if (blksize >= 512) {
              option_name[nb_options] = "blksize";
              option_value[nb_options] = 512;
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

static void tftp_handle_ack(Slirp *slirp, struct tftp_t *tp, int pktlen)
{
  int s;

  s = tftp_session_find(slirp, tp);

  if (s < 0) {
    return;
  }

  tftp_send_next_block(&slirp->tftp_sessions[s], tp);
}

static void tftp_handle_error(Slirp *slirp, struct tftp_t *tp, int pktlen)
{
  int s;

  s = tftp_session_find(slirp, tp);

  if (s < 0) {
    return;
  }

  tftp_session_terminate(&slirp->tftp_sessions[s]);
}

void tftp_input(struct mbuf *m)
{
  struct tftp_t *tp = (struct tftp_t *)m->m_data;

  switch(ntohs(tp->tp_op)) {
  case TFTP_RRQ:
    tftp_handle_rrq(m->slirp, tp, m->m_len);
    break;

  case TFTP_ACK:
    tftp_handle_ack(m->slirp, tp, m->m_len);
    break;

  case TFTP_ERROR:
    tftp_handle_error(m->slirp, tp, m->m_len);
    break;
  }
}
