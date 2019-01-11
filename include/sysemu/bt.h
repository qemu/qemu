#ifndef SYSEMU_BT_H
#define SYSEMU_BT_H

/* BT HCI info */

typedef struct HCIInfo {
    int (*bdaddr_set)(struct HCIInfo *hci, const uint8_t *bd_addr);
    void (*cmd_send)(struct HCIInfo *hci, const uint8_t *data, int len);
    void (*sco_send)(struct HCIInfo *hci, const uint8_t *data, int len);
    void (*acl_send)(struct HCIInfo *hci, const uint8_t *data, int len);
    void *opaque;
    void (*evt_recv)(void *opaque, const uint8_t *data, int len);
    void (*acl_recv)(void *opaque, const uint8_t *data, int len);
} HCIInfo;

/* bt-host.c */
struct HCIInfo *bt_host_hci(const char *id);
struct HCIInfo *qemu_next_hci(void);

#endif
