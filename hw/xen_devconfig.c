#include "xen_backend.h"

/* ------------------------------------------------------------- */

struct xs_dirs {
    char *xs_dir;
    TAILQ_ENTRY(xs_dirs) list;
};
static TAILQ_HEAD(xs_dirs_head, xs_dirs) xs_cleanup = TAILQ_HEAD_INITIALIZER(xs_cleanup);

static void xen_config_cleanup_dir(char *dir)
{
    struct xs_dirs *d;

    d = qemu_malloc(sizeof(*d));
    d->xs_dir = dir;
    TAILQ_INSERT_TAIL(&xs_cleanup, d, list);
}

void xen_config_cleanup(void)
{
    struct xs_dirs *d;

    TAILQ_FOREACH(d, &xs_cleanup, list) {
	xs_rm(xenstore, 0, d->xs_dir);
    }
}

/* ------------------------------------------------------------- */

static int xen_config_dev_mkdir(char *dev, int p)
{
    struct xs_permissions perms[2] = {{
            .id    = 0, /* set owner: dom0 */
        },{
            .id    = xen_domid,
            .perms = p,
        }};

    if (!xs_mkdir(xenstore, 0, dev)) {
	xen_be_printf(NULL, 0, "xs_mkdir %s: failed\n", dev);
	return -1;
    }
    xen_config_cleanup_dir(qemu_strdup(dev));

    if (!xs_set_permissions(xenstore, 0, dev, perms, 2)) {
	xen_be_printf(NULL, 0, "xs_set_permissions %s: failed\n", dev);
	return -1;
    }
    return 0;
}

static int xen_config_dev_dirs(const char *ftype, const char *btype, int vdev,
			       char *fe, char *be, int len)
{
    char *dom;

    dom = xs_get_domain_path(xenstore, xen_domid);
    snprintf(fe, len, "%s/device/%s/%d", dom, ftype, vdev);
    free(dom);

    dom = xs_get_domain_path(xenstore, 0);
    snprintf(be, len, "%s/backend/%s/%d/%d", dom, btype, xen_domid, vdev);
    free(dom);

    xen_config_dev_mkdir(fe, XS_PERM_READ | XS_PERM_WRITE);
    xen_config_dev_mkdir(be, XS_PERM_READ);
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

int xen_config_dev_blk(DriveInfo *disk)
{
    char fe[256], be[256];
    int vdev = 202 * 256 + 16 * disk->unit;
    int cdrom = disk->bdrv->type == BDRV_TYPE_CDROM;
    const char *devtype = cdrom ? "cdrom" : "disk";
    const char *mode    = cdrom ? "r"     : "w";

    snprintf(disk->bdrv->device_name, sizeof(disk->bdrv->device_name),
	     "xvd%c", 'a' + disk->unit);
    xen_be_printf(NULL, 1, "config disk %d [%s]: %s\n",
                  disk->unit, disk->bdrv->device_name, disk->bdrv->filename);
    xen_config_dev_dirs("vbd", "qdisk", vdev, fe, be, sizeof(fe));

    /* frontend */
    xenstore_write_int(fe, "virtual-device",  vdev);
    xenstore_write_str(fe, "device-type",     devtype);

    /* backend */
    xenstore_write_str(be, "dev",             disk->bdrv->device_name);
    xenstore_write_str(be, "type",            "file");
    xenstore_write_str(be, "params",          disk->bdrv->filename);
    xenstore_write_str(be, "mode",            mode);

    /* common stuff */
    return xen_config_dev_all(fe, be);
}

int xen_config_dev_nic(NICInfo *nic)
{
    char fe[256], be[256];
    char mac[20];

    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
	     nic->macaddr[0], nic->macaddr[1], nic->macaddr[2],
	     nic->macaddr[3], nic->macaddr[4], nic->macaddr[5]);
    xen_be_printf(NULL, 1, "config nic %d: mac=\"%s\"\n", nic->vlan->id, mac);
    xen_config_dev_dirs("vif", "qnic", nic->vlan->id, fe, be, sizeof(fe));

    /* frontend */
    xenstore_write_int(fe, "handle",     nic->vlan->id);
    xenstore_write_str(fe, "mac",        mac);

    /* backend */
    xenstore_write_int(be, "handle",     nic->vlan->id);
    xenstore_write_str(be, "mac",        mac);

    /* common stuff */
    return xen_config_dev_all(fe, be);
}

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

int xen_config_dev_console(int vdev)
{
    char fe[256], be[256];

    xen_config_dev_dirs("console", "console", vdev, fe, be, sizeof(fe));
    return xen_config_dev_all(fe, be);
}
