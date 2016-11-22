#ifndef QEMU_HW_XEN_PVDEV_H
#define QEMU_HW_XEN_PVDEV_H

#include "hw/xen/xen_common.h"
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
    int       (*backend_register)(void);
};

struct XenDevice {
    DeviceState        qdev;
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

/* xenstore helper functions */
int xenstore_write_str(const char *base, const char *node, const char *val);
int xenstore_write_int(const char *base, const char *node, int ival);
int xenstore_write_int64(const char *base, const char *node, int64_t ival);
char *xenstore_read_str(const char *base, const char *node);
int xenstore_read_int(const char *base, const char *node, int *ival);
int xenstore_read_uint64(const char *base, const char *node, uint64_t *uval);
void xenstore_update(void *unused);

const char *xenbus_strstate(enum xenbus_state state);

void xen_pv_evtchn_event(void *opaque);
void xen_pv_insert_xendev(struct XenDevice *xendev);
void xen_pv_del_xendev(struct XenDevice *xendev);
struct XenDevice *xen_pv_find_xendev(const char *type, int dom, int dev);

void xen_pv_unbind_evtchn(struct XenDevice *xendev);
int xen_pv_send_notify(struct XenDevice *xendev);

void xen_pv_printf(struct XenDevice *xendev, int msg_level,
                   const char *fmt, ...)  GCC_FMT_ATTR(3, 4);

#endif /* QEMU_HW_XEN_PVDEV_H */
