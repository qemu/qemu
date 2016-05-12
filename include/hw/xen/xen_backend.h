#ifndef QEMU_HW_XEN_BACKEND_H
#define QEMU_HW_XEN_BACKEND_H 1

#include "hw/xen/xen_common.h"
#include "sysemu/sysemu.h"
#include "net/net.h"

/* ------------------------------------------------------------- */

#define XEN_BUFSIZE 1024

struct XenDevice;

/* driver uses grant tables  ->  open gntdev device (xendev->gnttabdev) */
#define DEVOPS_FLAG_NEED_GNTDEV   1
/* don't expect frontend doing correct state transitions (aka console quirk) */
#define DEVOPS_FLAG_IGNORE_STATE  2

struct XenDevOps {
    size_t    size;
    uint32_t  flags;
    void      (*alloc)(struct XenDevice *xendev);
    int       (*init)(struct XenDevice *xendev);
    int       (*initialise)(struct XenDevice *xendev);
    void      (*connected)(struct XenDevice *xendev);
    void      (*event)(struct XenDevice *xendev);
    void      (*disconnect)(struct XenDevice *xendev);
    int       (*free)(struct XenDevice *xendev);
    void      (*backend_changed)(struct XenDevice *xendev, const char *node);
    void      (*frontend_changed)(struct XenDevice *xendev, const char *node);
};

struct XenDevice {
    const char         *type;
    int                dom;
    int                dev;
    char               name[64];
    int                debug;

    enum xenbus_state  be_state;
    enum xenbus_state  fe_state;
    int                online;
    char               be[XEN_BUFSIZE];
    char               *fe;
    char               *protocol;
    int                remote_port;
    int                local_port;

    xenevtchn_handle   *evtchndev;
    xengnttab_handle   *gnttabdev;

    struct XenDevOps   *ops;
    QTAILQ_ENTRY(XenDevice) next;
};

/* ------------------------------------------------------------- */

/* variables */
extern xc_interface *xen_xc;
extern xenforeignmemory_handle *xen_fmem;
extern struct xs_handle *xenstore;
extern const char *xen_protocol;
extern DeviceState *xen_sysdev;

/* xenstore helper functions */
int xenstore_write_str(const char *base, const char *node, const char *val);
int xenstore_write_int(const char *base, const char *node, int ival);
int xenstore_write_int64(const char *base, const char *node, int64_t ival);
char *xenstore_read_str(const char *base, const char *node);
int xenstore_read_int(const char *base, const char *node, int *ival);

int xenstore_write_be_str(struct XenDevice *xendev, const char *node, const char *val);
int xenstore_write_be_int(struct XenDevice *xendev, const char *node, int ival);
int xenstore_write_be_int64(struct XenDevice *xendev, const char *node, int64_t ival);
char *xenstore_read_be_str(struct XenDevice *xendev, const char *node);
int xenstore_read_be_int(struct XenDevice *xendev, const char *node, int *ival);
char *xenstore_read_fe_str(struct XenDevice *xendev, const char *node);
int xenstore_read_fe_int(struct XenDevice *xendev, const char *node, int *ival);
int xenstore_read_uint64(const char *base, const char *node, uint64_t *uval);
int xenstore_read_fe_uint64(struct XenDevice *xendev, const char *node, uint64_t *uval);

const char *xenbus_strstate(enum xenbus_state state);
struct XenDevice *xen_be_find_xendev(const char *type, int dom, int dev);
void xen_be_check_state(struct XenDevice *xendev);

/* xen backend driver bits */
int xen_be_init(void);
int xen_be_register(const char *type, struct XenDevOps *ops);
int xen_be_set_state(struct XenDevice *xendev, enum xenbus_state state);
int xen_be_bind_evtchn(struct XenDevice *xendev);
void xen_be_unbind_evtchn(struct XenDevice *xendev);
int xen_be_send_notify(struct XenDevice *xendev);
void xen_be_printf(struct XenDevice *xendev, int msg_level, const char *fmt, ...)
    GCC_FMT_ATTR(3, 4);

/* actual backend drivers */
extern struct XenDevOps xen_console_ops;      /* xen_console.c     */
extern struct XenDevOps xen_kbdmouse_ops;     /* xen_framebuffer.c */
extern struct XenDevOps xen_framebuffer_ops;  /* xen_framebuffer.c */
extern struct XenDevOps xen_blkdev_ops;       /* xen_disk.c        */
extern struct XenDevOps xen_netdev_ops;       /* xen_nic.c         */

void xen_init_display(int domid);

/* configuration (aka xenbus setup) */
void xen_config_cleanup(void);
int xen_config_dev_blk(DriveInfo *disk);
int xen_config_dev_nic(NICInfo *nic);
int xen_config_dev_vfb(int vdev, const char *type);
int xen_config_dev_vkbd(int vdev);
int xen_config_dev_console(int vdev);

#endif /* QEMU_HW_XEN_BACKEND_H */
