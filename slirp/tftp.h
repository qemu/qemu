/* tftp defines */
#ifndef SLIRP_TFTP_H
#define SLIRP_TFTP_H 1

#define TFTP_SESSIONS_MAX 3

#define TFTP_SERVER	69

#define TFTP_RRQ    1
#define TFTP_WRQ    2
#define TFTP_DATA   3
#define TFTP_ACK    4
#define TFTP_ERROR  5
#define TFTP_OACK   6

#define TFTP_FILENAME_MAX 512

struct tftp_t {
  struct ip ip;
  struct udphdr udp;
  uint16_t tp_op;
  union {
    struct {
      uint16_t tp_block_nr;
      uint8_t tp_buf[512];
    } tp_data;
    struct {
      uint16_t tp_error_code;
      uint8_t tp_msg[512];
    } tp_error;
    char tp_buf[512 + 2];
  } x;
};

struct tftp_session {
    Slirp *slirp;
    char *filename;
    int fd;

    struct in_addr client_ip;
    uint16_t client_port;
    uint32_t block_nr;

    int timestamp;
};

void tftp_input(struct mbuf *m);

#endif
