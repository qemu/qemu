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

struct tftp_session {
    int in_use;
    unsigned char filename[TFTP_FILENAME_MAX];

    struct in_addr client_ip;
    u_int16_t client_port;

    int timestamp;
};

static struct tftp_session tftp_sessions[TFTP_SESSIONS_MAX];

const char *tftp_prefix;

static void tftp_session_update(struct tftp_session *spt)
{
    spt->timestamp = curtime;
    spt->in_use = 1;
}

static void tftp_session_terminate(struct tftp_session *spt)
{
  spt->in_use = 0;
}

static int tftp_session_allocate(struct tftp_t *tp)
{
  struct tftp_session *spt;
  int k;

  for (k = 0; k < TFTP_SESSIONS_MAX; k++) {
    spt = &tftp_sessions[k];

    if (!spt->in_use)
        goto found;

    /* sessions time out after 5 inactive seconds */
    if ((int)(curtime - spt->timestamp) > 5000)
        goto found;
  }

  return -1;

 found:
  memset(spt, 0, sizeof(*spt));
  memcpy(&spt->client_ip, &tp->ip.ip_src, sizeof(spt->client_ip));
  spt->client_port = tp->udp.uh_sport;

  tftp_session_update(spt);

  return k;
}

static int tftp_session_find(struct tftp_t *tp)
{
  struct tftp_session *spt;
  int k;

  for (k = 0; k < TFTP_SESSIONS_MAX; k++) {
    spt = &tftp_sessions[k];

    if (spt->in_use) {
      if (!memcmp(&spt->client_ip, &tp->ip.ip_src, sizeof(spt->client_ip))) {
	if (spt->client_port == tp->udp.uh_sport) {
	  return k;
	}
      }
    }
  }

  return -1;
}

static int tftp_read_data(struct tftp_session *spt, u_int16_t block_nr,
			  u_int8_t *buf, int len)
{
  int fd;
  int bytes_read = 0;
  char buffer[1024];
  int n;

  n = snprintf(buffer, sizeof(buffer), "%s/%s",
	       tftp_prefix, spt->filename);
  if (n >= sizeof(buffer))
    return -1;

  fd = open(buffer, O_RDONLY | O_BINARY);

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

    m = m_get();

    if (!m)
	return -1;

    memset(m->m_data, 0, m->m_size);

    m->m_data += IF_MAXLINKHDR;
    tp = (void *)m->m_data;
    m->m_data += sizeof(struct udpiphdr);

    tp->tp_op = htons(TFTP_OACK);
    n += sprintf(tp->x.tp_buf + n, "%s", key) + 1;
    n += sprintf(tp->x.tp_buf + n, "%u", value) + 1;

    saddr.sin_addr = recv_tp->ip.ip_dst;
    saddr.sin_port = recv_tp->udp.uh_dport;

    daddr.sin_addr = spt->client_ip;
    daddr.sin_port = spt->client_port;

    m->m_len = sizeof(struct tftp_t) - 514 + n -
        sizeof(struct ip) - sizeof(struct udphdr);
    udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);

    return 0;
}



static int tftp_send_error(struct tftp_session *spt,
			   u_int16_t errorcode, const char *msg,
			   struct tftp_t *recv_tp)
{
  struct sockaddr_in saddr, daddr;
  struct mbuf *m;
  struct tftp_t *tp;
  int nobytes;

  m = m_get();

  if (!m) {
    return -1;
  }

  memset(m->m_data, 0, m->m_size);

  m->m_data += IF_MAXLINKHDR;
  tp = (void *)m->m_data;
  m->m_data += sizeof(struct udpiphdr);

  tp->tp_op = htons(TFTP_ERROR);
  tp->x.tp_error.tp_error_code = htons(errorcode);
  strcpy(tp->x.tp_error.tp_msg, msg);

  saddr.sin_addr = recv_tp->ip.ip_dst;
  saddr.sin_port = recv_tp->udp.uh_dport;

  daddr.sin_addr = spt->client_ip;
  daddr.sin_port = spt->client_port;

  nobytes = 2;

  m->m_len = sizeof(struct tftp_t) - 514 + 3 + strlen(msg) -
        sizeof(struct ip) - sizeof(struct udphdr);

  udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);

  tftp_session_terminate(spt);

  return 0;
}

static int tftp_send_data(struct tftp_session *spt,
			  u_int16_t block_nr,
			  struct tftp_t *recv_tp)
{
  struct sockaddr_in saddr, daddr;
  struct mbuf *m;
  struct tftp_t *tp;
  int nobytes;

  if (block_nr < 1) {
    return -1;
  }

  m = m_get();

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

static void tftp_handle_rrq(struct tftp_t *tp, int pktlen)
{
  struct tftp_session *spt;
  int s, k, n;
  u_int8_t *src, *dst;

  s = tftp_session_allocate(tp);

  if (s < 0) {
    return;
  }

  spt = &tftp_sessions[s];

  src = tp->x.tp_buf;
  dst = spt->filename;
  n = pktlen - ((uint8_t *)&tp->x.tp_buf[0] - (uint8_t *)tp);

  /* get name */

  for (k = 0; k < n; k++) {
    if (k < TFTP_FILENAME_MAX) {
      dst[k] = src[k];
    }
    else {
      return;
    }

    if (src[k] == '\0') {
      break;
    }
  }

  if (k >= n) {
    return;
  }

  k++;

  /* check mode */
  if ((n - k) < 6) {
    return;
  }

  if (memcmp(&src[k], "octet\0", 6) != 0) {
      tftp_send_error(spt, 4, "Unsupported transfer mode", tp);
      return;
  }

  k += 6; /* skipping octet */

  /* do sanity checks on the filename */

  if ((spt->filename[0] != '/')
      || (spt->filename[strlen(spt->filename) - 1] == '/')
      ||  strstr(spt->filename, "/../")) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
  }

  /* only allow exported prefixes */

  if (!tftp_prefix) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
  }

  /* check if the file exists */

  if (tftp_read_data(spt, 0, spt->filename, 0) < 0) {
      tftp_send_error(spt, 1, "File not found", tp);
      return;
  }

  if (src[n - 1] != 0) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
  }

  while (k < n) {
      const char *key, *value;

      key = src + k;
      k += strlen(key) + 1;

      if (k >= n) {
	  tftp_send_error(spt, 2, "Access violation", tp);
	  return;
      }

      value = src + k;
      k += strlen(value) + 1;

      if (strcmp(key, "tsize") == 0) {
	  int tsize = atoi(value);
	  struct stat stat_p;

	  if (tsize == 0 && tftp_prefix) {
	      char buffer[1024];
	      int len;

	      len = snprintf(buffer, sizeof(buffer), "%s/%s",
			     tftp_prefix, spt->filename);

	      if (stat(buffer, &stat_p) == 0)
		  tsize = stat_p.st_size;
	      else {
		  tftp_send_error(spt, 1, "File not found", tp);
		  return;
	      }
	  }

	  tftp_send_oack(spt, "tsize", tsize, tp);
      }
  }

  tftp_send_data(spt, 1, tp);
}

static void tftp_handle_ack(struct tftp_t *tp, int pktlen)
{
  int s;

  s = tftp_session_find(tp);

  if (s < 0) {
    return;
  }

  if (tftp_send_data(&tftp_sessions[s],
		     ntohs(tp->x.tp_data.tp_block_nr) + 1,
		     tp) < 0) {
    return;
  }
}

void tftp_input(struct mbuf *m)
{
  struct tftp_t *tp = (struct tftp_t *)m->m_data;

  switch(ntohs(tp->tp_op)) {
  case TFTP_RRQ:
    tftp_handle_rrq(tp, m->m_len);
    break;

  case TFTP_ACK:
    tftp_handle_ack(tp, m->m_len);
    break;
  }
}
