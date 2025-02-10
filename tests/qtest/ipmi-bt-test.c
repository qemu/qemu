/*
 * IPMI BT test cases, using the external interface for checking
 *
 * Copyright (c) 2012 Corey Minyard <cminyard@mvista.com>
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

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>


#include "libqtest-single.h"

#define IPMI_IRQ        5

#define IPMI_BT_BASE    0xe4

#define IPMI_BT_CTLREG_CLR_WR_PTR  0
#define IPMI_BT_CTLREG_CLR_RD_PTR  1
#define IPMI_BT_CTLREG_H2B_ATN     2
#define IPMI_BT_CTLREG_B2H_ATN     3
#define IPMI_BT_CTLREG_SMS_ATN     4
#define IPMI_BT_CTLREG_H_BUSY      6
#define IPMI_BT_CTLREG_B_BUSY      7

#define IPMI_BT_CTLREG_GET(b) ((bt_get_ctrlreg() >> (b)) & 1)
#define IPMI_BT_CTLREG_GET_H2B_ATN() IPMI_BT_CTLREG_GET(IPMI_BT_CTLREG_H2B_ATN)
#define IPMI_BT_CTLREG_GET_B2H_ATN() IPMI_BT_CTLREG_GET(IPMI_BT_CTLREG_B2H_ATN)
#define IPMI_BT_CTLREG_GET_SMS_ATN() IPMI_BT_CTLREG_GET(IPMI_BT_CTLREG_SMS_ATN)
#define IPMI_BT_CTLREG_GET_H_BUSY()  IPMI_BT_CTLREG_GET(IPMI_BT_CTLREG_H_BUSY)
#define IPMI_BT_CTLREG_GET_B_BUSY()  IPMI_BT_CTLREG_GET(IPMI_BT_CTLREG_B_BUSY)

#define IPMI_BT_CTLREG_SET(b) bt_write_ctrlreg(1 << (b))
#define IPMI_BT_CTLREG_SET_CLR_WR_PTR() IPMI_BT_CTLREG_SET( \
                                                IPMI_BT_CTLREG_CLR_WR_PTR)
#define IPMI_BT_CTLREG_SET_CLR_RD_PTR() IPMI_BT_CTLREG_SET( \
                                                IPMI_BT_CTLREG_CLR_RD_PTR)
#define IPMI_BT_CTLREG_SET_H2B_ATN()  IPMI_BT_CTLREG_SET(IPMI_BT_CTLREG_H2B_ATN)
#define IPMI_BT_CTLREG_SET_B2H_ATN()  IPMI_BT_CTLREG_SET(IPMI_BT_CTLREG_B2H_ATN)
#define IPMI_BT_CTLREG_SET_SMS_ATN()  IPMI_BT_CTLREG_SET(IPMI_BT_CTLREG_SMS_ATN)
#define IPMI_BT_CTLREG_SET_H_BUSY()   IPMI_BT_CTLREG_SET(IPMI_BT_CTLREG_H_BUSY)

static int bt_ints_enabled;

static uint8_t bt_get_ctrlreg(void)
{
    return inb(IPMI_BT_BASE);
}

static void bt_write_ctrlreg(uint8_t val)
{
    outb(IPMI_BT_BASE, val);
}

static uint8_t bt_get_buf(void)
{
    return inb(IPMI_BT_BASE + 1);
}

static void bt_write_buf(uint8_t val)
{
    outb(IPMI_BT_BASE + 1, val);
}

static uint8_t bt_get_irqreg(void)
{
    return inb(IPMI_BT_BASE + 2);
}

static void bt_write_irqreg(uint8_t val)
{
    outb(IPMI_BT_BASE + 2, val);
}

static void bt_wait_b_busy(void)
{
    unsigned int count = 1000;
    while (IPMI_BT_CTLREG_GET_B_BUSY() != 0) {
        --count;
        g_assert(count != 0);
        usleep(100);
    }
}

static void bt_wait_b2h_atn(void)
{
    unsigned int count = 1000;
    while (IPMI_BT_CTLREG_GET_B2H_ATN() == 0) {
        --count;
        g_assert(count != 0);
        usleep(100);
    }
}


static int emu_lfd;
static int emu_fd;
static in_port_t emu_port;
static uint8_t inbuf[100];
static unsigned int inbuf_len;
static unsigned int inbuf_pos;
static int last_was_aa;

static void read_emu_data(void)
{
    fd_set readfds;
    int rv;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(emu_fd, &readfds);
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    rv = select(emu_fd + 1, &readfds, NULL, NULL, &tv);
    if (rv == -1) {
        perror("select");
    }
    g_assert(rv == 1);
    rv = read(emu_fd, inbuf, sizeof(inbuf));
    if (rv == -1) {
        perror("read");
    }
    g_assert(rv > 0);
    inbuf_len = rv;
    inbuf_pos = 0;
}

static void write_emu_msg(uint8_t *msg, unsigned int len)
{
    int rv;

#ifdef DEBUG_TEST
    {
        unsigned int i;
        printf("sending:");
        for (i = 0; i < len; i++) {
            printf(" %2.2x", msg[i]);
        }
        printf("\n");
    }
#endif
    rv = write(emu_fd, msg, len);
    g_assert(rv == len);
}

static void get_emu_msg(uint8_t *msg, unsigned int *len)
{
    unsigned int outpos = 0;

    for (;;) {
        while (inbuf_pos < inbuf_len) {
            uint8_t ch = inbuf[inbuf_pos++];

            g_assert(outpos < *len);
            if (last_was_aa) {
                assert(ch & 0x10);
                msg[outpos++] = ch & ~0x10;
                last_was_aa = 0;
            } else if (ch == 0xaa) {
                last_was_aa = 1;
            } else {
                msg[outpos++] = ch;
                if ((ch == 0xa0) || (ch == 0xa1)) {
                    /* Message complete */
                    *len = outpos;
                    goto done;
                }
            }
        }
        read_emu_data();
    }
 done:
#ifdef DEBUG_TEST
    {
        unsigned int i;
        printf("Msg:");
        for (i = 0; i < outpos; i++) {
            printf(" %2.2x", msg[i]);
        }
        printf("\n");
    }
#endif
    return;
}

static uint8_t
ipmb_checksum(const unsigned char *data, int size, unsigned char start)
{
        unsigned char csum = start;

        for (; size > 0; size--, data++) {
                csum += *data;
        }
        return csum;
}

static uint8_t get_dev_id_cmd[] = { 0x18, 0x01 };
static uint8_t get_dev_id_rsp[] = { 0x1c, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
                                    0x02, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00 };

static uint8_t set_bmc_globals_cmd[] = { 0x18, 0x2e, 0x0f };
static uint8_t set_bmc_globals_rsp[] = { 0x1c, 0x2e, 0x00 };
static uint8_t enable_irq_cmd[] = { 0x05, 0xa1 };

static void emu_msg_handler(void)
{
    uint8_t msg[100];
    unsigned int msg_len = sizeof(msg);

    get_emu_msg(msg, &msg_len);
    g_assert(msg_len >= 5);
    g_assert(msg[msg_len - 1] == 0xa0);
    msg_len--;
    g_assert(ipmb_checksum(msg, msg_len, 0) == 0);
    msg_len--;
    if ((msg[1] == get_dev_id_cmd[0]) && (msg[2] == get_dev_id_cmd[1])) {
        memcpy(msg + 1, get_dev_id_rsp, sizeof(get_dev_id_rsp));
        msg_len = sizeof(get_dev_id_rsp) + 1;
        msg[msg_len] = -ipmb_checksum(msg, msg_len, 0);
        msg_len++;
        msg[msg_len++] = 0xa0;
        write_emu_msg(msg, msg_len);
    } else if ((msg[1] == set_bmc_globals_cmd[0]) &&
               (msg[2] == set_bmc_globals_cmd[1])) {
        write_emu_msg(enable_irq_cmd, sizeof(enable_irq_cmd));
        memcpy(msg + 1, set_bmc_globals_rsp, sizeof(set_bmc_globals_rsp));
        msg_len = sizeof(set_bmc_globals_rsp) + 1;
        msg[msg_len] = -ipmb_checksum(msg, msg_len, 0);
        msg_len++;
        msg[msg_len++] = 0xa0;
        write_emu_msg(msg, msg_len);
    } else {
        g_assert_not_reached();
    }
}

static void bt_cmd(uint8_t *cmd, unsigned int cmd_len,
                    uint8_t *rsp, unsigned int *rsp_len)
{
    unsigned int i, len, j = 0;
    uint8_t seq = 5;

    /* Should be idle */
    g_assert(bt_get_ctrlreg() == 0);

    bt_wait_b_busy();
    IPMI_BT_CTLREG_SET_CLR_WR_PTR();
    bt_write_buf(cmd_len + 1);
    bt_write_buf(cmd[0]);
    bt_write_buf(seq);
    for (i = 1; i < cmd_len; i++) {
        bt_write_buf(cmd[i]);
    }
    IPMI_BT_CTLREG_SET_H2B_ATN();

    emu_msg_handler(); /* We should get a message on the socket here. */

    bt_wait_b2h_atn();
    if (bt_ints_enabled) {
        g_assert((bt_get_irqreg() & 0x02) == 0x02);
        g_assert(get_irq(IPMI_IRQ));
        bt_write_irqreg(0x03);
    } else {
        g_assert(!get_irq(IPMI_IRQ));
    }
    IPMI_BT_CTLREG_SET_H_BUSY();
    IPMI_BT_CTLREG_SET_B2H_ATN();
    IPMI_BT_CTLREG_SET_CLR_RD_PTR();
    len = bt_get_buf();
    g_assert(len >= 4);
    rsp[0] = bt_get_buf();
    assert(bt_get_buf() == seq);
    len--;
    for (j = 1; j < len; j++) {
        rsp[j] = bt_get_buf();
    }
    IPMI_BT_CTLREG_SET_H_BUSY();
    *rsp_len = j;
}


/*
 * We should get a connect request and a short message with capabilities.
 */
static void test_connect(void)
{
    fd_set readfds;
    int rv;
    int val;
    struct timeval tv;
    uint8_t msg[100];
    unsigned int msglen;
    static uint8_t exp1[] = { 0xff, 0x01, 0xa1 }; /* A protocol version */
    static uint8_t exp2[] = { 0x08, 0x3f, 0xa1 }; /* A capabilities cmd */

    FD_ZERO(&readfds);
    FD_SET(emu_lfd, &readfds);
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    rv = select(emu_lfd + 1, &readfds, NULL, NULL, &tv);
    g_assert(rv == 1);
    emu_fd = accept(emu_lfd, NULL, 0);
    if (emu_fd < 0) {
        perror("accept");
    }
    g_assert(emu_fd >= 0);

    val = 1;
    rv = setsockopt(emu_fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    g_assert(rv != -1);

    /* Report our version */
    write_emu_msg(exp1, sizeof(exp1));

    /* Validate that we get the info we expect. */
    msglen = sizeof(msg);
    get_emu_msg(msg, &msglen);
    g_assert(msglen == sizeof(exp1));
    g_assert(memcmp(msg, exp1, msglen) == 0);
    msglen = sizeof(msg);
    get_emu_msg(msg, &msglen);
    g_assert(msglen == sizeof(exp2));
    g_assert(memcmp(msg, exp2, msglen) == 0);
}

/*
 * Send a get_device_id to do a basic test.
 */
static void test_bt_base(void)
{
    uint8_t rsp[20];
    unsigned int rsplen = sizeof(rsp);

    bt_cmd(get_dev_id_cmd, sizeof(get_dev_id_cmd), rsp, &rsplen);
    g_assert(rsplen == sizeof(get_dev_id_rsp));
    g_assert(memcmp(get_dev_id_rsp, rsp, rsplen) == 0);
}

/*
 * Enable IRQs for the interface.
 */
static void test_enable_irq(void)
{
    uint8_t rsp[20];
    unsigned int rsplen = sizeof(rsp);

    bt_cmd(set_bmc_globals_cmd, sizeof(set_bmc_globals_cmd), rsp, &rsplen);
    g_assert(rsplen == sizeof(set_bmc_globals_rsp));
    g_assert(memcmp(set_bmc_globals_rsp, rsp, rsplen) == 0);
    bt_write_irqreg(0x01);
    bt_ints_enabled = 1;
}

/*
 * Create a local TCP socket with any port, then save off the port we got.
 */
static void open_socket(void)
{
    struct sockaddr_in myaddr = {};
    socklen_t addrlen;

    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    myaddr.sin_port = 0;
    emu_lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (emu_lfd == -1) {
        perror("socket");
        exit(1);
    }
    if (bind(emu_lfd, (struct sockaddr *) &myaddr, sizeof(myaddr)) == -1) {
        perror("bind");
        exit(1);
    }
    addrlen = sizeof(myaddr);
    if (getsockname(emu_lfd, (struct sockaddr *) &myaddr , &addrlen) == -1) {
        perror("getsockname");
        exit(1);
    }
    emu_port = ntohs(myaddr.sin_port);
    assert(listen(emu_lfd, 1) != -1);
}

int main(int argc, char **argv)
{
    int ret;

    open_socket();

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    global_qtest = qtest_initf(
        " -chardev socket,id=ipmi0,host=127.0.0.1,port=%d,reconnect-ms=10000"
        " -device ipmi-bmc-extern,chardev=ipmi0,id=bmc0"
        " -device isa-ipmi-bt,bmc=bmc0", emu_port);
    qtest_irq_intercept_in(global_qtest, "ioapic");
    qtest_add_func("/ipmi/extern/connect", test_connect);
    qtest_add_func("/ipmi/extern/bt_base", test_bt_base);
    qtest_add_func("/ipmi/extern/bt_enable_irq", test_enable_irq);
    qtest_add_func("/ipmi/extern/bt_base_irq", test_bt_base);
    ret = g_test_run();
    qtest_quit(global_qtest);

    return ret;
}
