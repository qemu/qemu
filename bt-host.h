#ifndef BT_HOST_H
#define BT_HOST_H

struct HCIInfo;

/* bt-host.c */
struct HCIInfo *bt_host_hci(const char *id);

#endif
