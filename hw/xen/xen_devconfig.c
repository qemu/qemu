#include "qemu/osdep.h"
#include "hw/xen/xen-legacy-backend.h"
#include "qemu/option.h"
#include "system/blockdev.h"
#include "system/system.h"

/* ------------------------------------------------------------- */

static int xen_config_dev_dirs(const char *ftype, const char *btype, int vdev,
                               char *fe, char *be, int len)
{
    char *dom;

    dom = qemu_xen_xs_get_domain_path(xenstore, xen_domid);
    snprintf(fe, len, "%s/device/%s/%d", dom, ftype, vdev);
    free(dom);

    dom = qemu_xen_xs_get_domain_path(xenstore, 0);
    snprintf(be, len, "%s/backend/%s/%d/%d", dom, btype, xen_domid, vdev);
    free(dom);

    xenstore_mkdir(fe, XS_PERM_READ | XS_PERM_WRITE);
    xenstore_mkdir(be, XS_PERM_READ);
    return 0;
}

static int xen_config_dev_all(char *fe, char *be)
{
    /* frontend */
    if (xen_protocol)
        xenstore_write_str(fe, "protocol", xen_protocol);

    xenstore_write_int(fe, "state",           XenbusStateInitialising);
    xenstore_write_int(fe, "backend-id",      0);
    xenstore_write_str(fe, "backend",         be);

    /* backend */
    xenstore_write_str(be, "domain",          qemu_name ? qemu_name : "no-name");
    xenstore_write_int(be, "online",          1);
    xenstore_write_int(be, "state",           XenbusStateInitialising);
    xenstore_write_int(be, "frontend-id",     xen_domid);
    xenstore_write_str(be, "frontend",        fe);

    return 0;
}

/* ------------------------------------------------------------- */

int xen_config_dev_vfb(int vdev, const char *type)
{
    char fe[256], be[256];

    xen_config_dev_dirs("vfb", "vfb", vdev, fe, be, sizeof(fe));

    /* backend */
    xenstore_write_str(be, "type",  type);

    /* common stuff */
    return xen_config_dev_all(fe, be);
}

int xen_config_dev_vkbd(int vdev)
{
    char fe[256], be[256];

    xen_config_dev_dirs("vkbd", "vkbd", vdev, fe, be, sizeof(fe));
    return xen_config_dev_all(fe, be);
}
