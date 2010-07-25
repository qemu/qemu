/* tftp defines */

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
    uint8_t tp_buf[512 + 2];
  } x;
};

struct tftp_session {
    Slirp *slirp;
    char *filename;

    struct in_addr client_ip;
    uint16_t client_port;

    int timestamp;
};

void tftp_input(struct mbuf *m);
