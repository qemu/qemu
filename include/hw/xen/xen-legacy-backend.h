#ifndef HW_XEN_LEGACY_BACKEND_H
#define HW_XEN_LEGACY_BACKEND_H

#include "hw/xen/xen_backend_ops.h"
#include "hw/xen/xen_pvdev.h"
#include "qom/object.h"

#define TYPE_XENSYSDEV "xen-sysdev"
#define TYPE_XENSYSBUS "xen-sysbus"
#define TYPE_XENBACKEND "xen-backend"

typedef struct XenLegacyDevice XenLegacyDevice;
DECLARE_INSTANCE_CHECKER(XenLegacyDevice, XENBACKEND,
                         TYPE_XENBACKEND)

/* variables */
extern struct qemu_xs_handle *xenstore;
extern const char *xen_protocol;
extern DeviceState *xen_sysdev;
extern BusState *xen_sysbus;

int xenstore_mkdir(char *path, int p);
int xenstore_write_be_str(struct XenLegacyDevice *xendev, const char *node,
                          const char *val);
int xenstore_write_be_int(struct XenLegacyDevice *xendev, const char *node,
                          int ival);
int xenstore_write_be_int64(struct XenLegacyDevice *xendev, const char *node,
                            int64_t ival);
char *xenstore_read_be_str(struct XenLegacyDevice *xendev, const char *node);
int xenstore_read_be_int(struct XenLegacyDevice *xendev, const char *node,
                         int *ival);
char *xenstore_read_fe_str(struct XenLegacyDevice *xendev, const char *node);
int xenstore_read_fe_int(struct XenLegacyDevice *xendev, const char *node,
                         int *ival);
int xenstore_read_fe_uint64(struct XenLegacyDevice *xendev, const char *node,
                            uint64_t *uval);

void xen_be_check_state(struct XenLegacyDevice *xendev);

/* xen backend driver bits */
void xen_be_init(void);
int xen_be_register(const char *type, const struct XenDevOps *ops);
int xen_be_set_state(struct XenLegacyDevice *xendev, enum xenbus_state state);
int xen_be_bind_evtchn(struct XenLegacyDevice *xendev);
void xen_be_set_max_grant_refs(struct XenLegacyDevice *xendev,
                               unsigned int nr_refs);
void *xen_be_map_grant_refs(struct XenLegacyDevice *xendev, uint32_t *refs,
                            unsigned int nr_refs, int prot);
void xen_be_unmap_grant_refs(struct XenLegacyDevice *xendev, void *ptr,
                             uint32_t *refs, unsigned int nr_refs);

static inline void *xen_be_map_grant_ref(struct XenLegacyDevice *xendev,
                                         uint32_t ref, int prot)
{
    return xen_be_map_grant_refs(xendev, &ref, 1, prot);
}

static inline void xen_be_unmap_grant_ref(struct XenLegacyDevice *xendev,
                                          void *ptr, uint32_t ref)
{
    return xen_be_unmap_grant_refs(xendev, ptr, &ref, 1);
}

/* configuration (aka xenbus setup) */
void xen_config_cleanup(void);
int xen_config_dev_vfb(int vdev, const char *type);
int xen_config_dev_vkbd(int vdev);

#endif /* HW_XEN_LEGACY_BACKEND_H */
