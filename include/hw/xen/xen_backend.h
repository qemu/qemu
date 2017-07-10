#ifndef QEMU_HW_XEN_BACKEND_H
#define QEMU_HW_XEN_BACKEND_H

#include "hw/xen/xen_common.h"
#include "hw/xen/xen_pvdev.h"
#include "sysemu/sysemu.h"
#include "net/net.h"

#define TYPE_XENSYSDEV "xen-sysdev"
#define TYPE_XENSYSBUS "xen-sysbus"
#define TYPE_XENBACKEND "xen-backend"

#define XENBACKEND_DEVICE(obj) \
    OBJECT_CHECK(XenDevice, (obj), TYPE_XENBACKEND)

/* variables */
extern struct xs_handle *xenstore;
extern const char *xen_protocol;
extern DeviceState *xen_sysdev;
extern BusState *xen_sysbus;

int xenstore_mkdir(char *path, int p);
int xenstore_write_be_str(struct XenDevice *xendev, const char *node, const char *val);
int xenstore_write_be_int(struct XenDevice *xendev, const char *node, int ival);
int xenstore_write_be_int64(struct XenDevice *xendev, const char *node, int64_t ival);
char *xenstore_read_be_str(struct XenDevice *xendev, const char *node);
int xenstore_read_be_int(struct XenDevice *xendev, const char *node, int *ival);
void xenstore_update_fe(char *watch, struct XenDevice *xendev);
void xenstore_update_be(char *watch, char *type, int dom,
                        struct XenDevOps *ops);
char *xenstore_read_fe_str(struct XenDevice *xendev, const char *node);
int xenstore_read_fe_int(struct XenDevice *xendev, const char *node, int *ival);
int xenstore_read_fe_uint64(struct XenDevice *xendev, const char *node,
                            uint64_t *uval);

void xen_be_check_state(struct XenDevice *xendev);

/* xen backend driver bits */
int xen_be_init(void);
void xen_be_register_common(void);
int xen_be_register(const char *type, struct XenDevOps *ops);
int xen_be_set_state(struct XenDevice *xendev, enum xenbus_state state);
int xen_be_bind_evtchn(struct XenDevice *xendev);

/* actual backend drivers */
extern struct XenDevOps xen_console_ops;      /* xen_console.c     */
extern struct XenDevOps xen_kbdmouse_ops;     /* xen_framebuffer.c */
extern struct XenDevOps xen_framebuffer_ops;  /* xen_framebuffer.c */
extern struct XenDevOps xen_blkdev_ops;       /* xen_disk.c        */
#ifdef CONFIG_VIRTFS
extern struct XenDevOps xen_9pfs_ops;       /* xen-9p-backend.c        */
#endif
extern struct XenDevOps xen_netdev_ops;       /* xen_nic.c         */
#ifdef CONFIG_USB_LIBUSB
extern struct XenDevOps xen_usb_ops;          /* xen-usb.c         */
#endif

/* configuration (aka xenbus setup) */
void xen_config_cleanup(void);
int xen_config_dev_blk(DriveInfo *disk);
int xen_config_dev_nic(NICInfo *nic);
int xen_config_dev_vfb(int vdev, const char *type);
int xen_config_dev_vkbd(int vdev);
int xen_config_dev_console(int vdev);

#endif /* QEMU_HW_XEN_BACKEND_H */
