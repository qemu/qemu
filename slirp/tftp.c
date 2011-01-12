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
    qemu_free(spt->filename);
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
        qemu_free(spt->filename);
        goto found;
    }
  }

  return -1;

 found:
  memset(spt, 0, sizeof(*spt));
  memcpy(&spt->client_ip, &tp->ip.ip_src, sizeof(spt->client_ip));
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

static int tftp_read_data(struct tftp_session *spt, uint16_t block_nr,
                          uint8_t *buf, int len)
{
  int fd;
  int bytes_read = 0;

  fd = open(spt->filename, O_RDONLY | O_BINARY);

  if (fd < 0) {
    return -1;
  }

  if (len) {
    lseek(fd, block_nr * 512, SEEK_SET);

    bytes_read = read(fd, buf, len);
  }

  close(fd);

  return bytes_read;
}

static int tftp_send_oack(struct tftp_session *spt,
                          const char *key, uint32_t value,
                          struct tftp_t *recv_tp)
{
    struct sockaddr_in saddr, daddr;
    struct mbuf *m;
    struct tftp_t *tp;
    int n = 0;

    m = m_get(spt->slirp);

    if (!m)
	return -1;

    memset(m->m_data, 0, m->m_size);

    m->m_data += IF_MAXLINKHDR;
    tp = (void *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);

    tp->tp_op = htons(TFTP_OACK);
    n += snprintf((char *)tp->x.tp_buf + n, sizeof(tp->x.tp_buf) - n, "%s",
                  key) + 1;
    n += snprintf((char *)tp->x.tp_buf + n, sizeof(tp->x.tp_buf) - n, "%u",
                  value) + 1;

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

static int tftp_send_data(struct tftp_session *spt,
                          uint16_t block_nr,
			  struct tftp_t *recv_tp)
{
  struct sockaddr_in saddr, daddr;
  struct mbuf *m;
  struct tftp_t *tp;
  int nobytes;

  if (block_nr < 1) {
    return -1;
  }

  m = m_get(spt->slirp);

  if (!m) {
    return -1;
  }

  memset(m->m_data, 0, m->m_size);

  m->m_data += IF_MAXLINKHDR;
  tp = (void *)m->m_data;
  m->m_data += sizeof(struct udpiphdr);

  tp->tp_op = htons(TFTP_DATA);
  tp->x.tp_data.tp_block_nr = htons(block_nr);

  saddr.sin_addr = recv_tp->ip.ip_dst;
  saddr.sin_port = recv_tp->udp.uh_dport;

  daddr.sin_addr = spt->client_ip;
  daddr.sin_port = spt->client_port;

  nobytes = tftp_read_data(spt, block_nr - 1, tp->x.tp_data.tp_buf, 512);

  if (nobytes < 0) {
    m_free(m);

    /* send "file not found" error back */

    tftp_send_error(spt, 1, "File not found", tp);

    return -1;
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

  return 0;
}

static void tftp_handle_rrq(Slirp *slirp, struct tftp_t *tp, int pktlen)
{
  struct tftp_session *spt;
  int s, k;
  size_t prefix_len;
  char *req_fname;

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
  pktlen -= ((uint8_t *)&tp->x.tp_buf[0] - (uint8_t *)tp);

  /* prepend tftp_prefix */
  prefix_len = strlen(slirp->tftp_prefix);
  spt->filename = qemu_malloc(prefix_len + TFTP_FILENAME_MAX + 2);
  memcpy(spt->filename, slirp->tftp_prefix, prefix_len);
  spt->filename[prefix_len] = '/';

  /* get name */
  req_fname = spt->filename + prefix_len + 1;

  while (1) {
    if (k >= TFTP_FILENAME_MAX || k >= pktlen) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
    }
    req_fname[k] = (char)tp->x.tp_buf[k];
    if (req_fname[k++] == '\0') {
      break;
    }
  }

  /* check mode */
  if ((pktlen - k) < 6) {
    tftp_send_error(spt, 2, "Access violation", tp);
    return;
  }

  if (strcasecmp((const char *)&tp->x.tp_buf[k], "octet") != 0) {
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

  while (k < pktlen) {
      const char *key, *value;

      key = (const char *)&tp->x.tp_buf[k];
      k += strlen(key) + 1;

      if (k >= pktlen) {
	  tftp_send_error(spt, 2, "Access violation", tp);
	  return;
      }

      value = (const char *)&tp->x.tp_buf[k];
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

	  tftp_send_oack(spt, "tsize", tsize, tp);
	  return;
      }
  }

  tftp_send_data(spt, 1, tp);
}

static void tftp_handle_ack(Slirp *slirp, struct tftp_t *tp, int pktlen)
{
  int s;

  s = tftp_session_find(slirp, tp);

  if (s < 0) {
    return;
  }

  if (tftp_send_data(&slirp->tftp_sessions[s],
		     ntohs(tp->x.tp_data.tp_block_nr) + 1,
		     tp) < 0) {
    return;
  }
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
