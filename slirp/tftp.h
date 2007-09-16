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
  u_int16_t tp_op;
  union {
    struct {
      u_int16_t tp_block_nr;
      u_int8_t tp_buf[512];
    } tp_data;
    struct {
      u_int16_t tp_error_code;
      u_int8_t tp_msg[512];
    } tp_error;
    u_int8_t tp_buf[512 + 2];
  } x;
};

void tftp_input(struct mbuf *m);
