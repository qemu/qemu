#ifndef HW_I82596_H
#define HW_I82596_H

#define I82596_IOPORT_SIZE       0x20

#include "system/memory.h"
#include "system/address-spaces.h"

#define PACKET_QUEUE_SIZE 64
#define RX_RING_SIZE    16
#define PKT_BUF_SZ      1536

#define PORT_RESET              0x00
#define PORT_SELFTEST           0x01
#define PORT_ALTSCP             0x02
#define PORT_ALTDUMP            0x03
#define PORT_CA                 0x10

typedef struct I82596State_st I82596State;

struct I82596State_st {
    MemoryRegion mmio;
    MemoryRegion *as;
    qemu_irq irq;
    NICState *nic;
    NICConf conf;
    QEMUTimer *flush_queue_timer;
    uint8_t mode;

    QEMUTimer *throttle_timer;
    uint16_t t_on;
    uint16_t t_off;
    bool throttle_state;

    hwaddr scp;
    uint32_t iscp;
    uint8_t sysbus;
    uint32_t scb;
    uint32_t scb_base;
    uint16_t scb_status;
    uint8_t cu_status, rx_status;
    uint16_t lnkst;
    uint32_t last_tx_len;
    uint32_t collision_events;
    uint32_t total_collisions;

    uint32_t tx_retry_addr;
    int tx_retry_count;
    uint32_t tx_good_frames;
    uint32_t tx_collisions;
    uint32_t tx_aborted_errors;

    uint32_t cmd_p;
    int ca;
    int ca_active;
    int send_irq;

    uint8_t mult[8];
    uint8_t config[14];

    uint32_t crc_err;
    uint32_t align_err;
    uint32_t resource_err;
    uint32_t over_err;
    uint32_t rcvdt_err;
    uint32_t short_fr_error;
    uint32_t total_frames;
    uint32_t total_good_frames;

    uint8_t tx_buffer[PKT_BUF_SZ];
    uint8_t rx_buffer[PKT_BUF_SZ];
    uint16_t tx_frame_len;
    uint16_t rx_frame_len;

    hwaddr current_tx_desc;
    hwaddr current_rx_desc;
    uint32_t last_good_rfa;
    uint8_t packet_queue[PACKET_QUEUE_SIZE][PKT_BUF_SZ];
    size_t packet_queue_len[PACKET_QUEUE_SIZE];
    int queue_head;
    int queue_tail;
    int queue_count;
    bool rnr_signaled;
    bool flushing_queue;
};

void i82596_h_reset(void *opaque);
void i82596_ioport_writew(void *opaque, uint32_t addr, uint32_t val);
uint32_t i82596_ioport_readw(void *opaque, uint32_t addr);
ssize_t i82596_receive(NetClientState *nc, const uint8_t *buf, size_t size_);
ssize_t i82596_receive_iov(NetClientState *nc, const struct iovec *iov,
                           int iovcnt);
bool i82596_can_receive(NetClientState *nc);
void i82596_set_link_status(NetClientState *nc);
void i82596_poll(NetClientState *nc, bool enable);
void i82596_common_init(DeviceState *dev, I82596State *s,
                        NetClientInfo *info);
extern const VMStateDescription vmstate_i82596;
#endif
