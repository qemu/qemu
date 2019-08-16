#include "qemu/osdep.h"
#include "hw/xen/xen-legacy-backend.h"
#include "qemu/option.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"

/* ------------------------------------------------------------- */

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

int xen_config_dev_blk(DriveInfo *disk)
{
    char fe[256], be[256], device_name[32];
    int vdev = 202 * 256 + 16 * disk->unit;
    int cdrom = disk->media_cd;
    const char *devtype = cdrom ? "cdrom" : "disk";
    const char *mode    = cdrom ? "r"     : "w";
    const char *filename = qemu_opt_get(disk->opts, "file");

    snprintf(device_name, sizeof(device_name), "xvd%c", 'a' + disk->unit);
    xen_pv_printf(NULL, 1, "config disk %d [%s]: %s\n",
                  disk->unit, device_name, filename);
    xen_config_dev_dirs("vbd", "qdisk", vdev, fe, be, sizeof(fe));

    /* frontend */
    xenstore_write_int(fe, "virtual-device",  vdev);
    xenstore_write_str(fe, "device-type",     devtype);

    /* backend */
    xenstore_write_str(be, "dev",             device_name);
    xenstore_write_str(be, "type",            "file");
    xenstore_write_str(be, "params",          filename);
    xenstore_write_str(be, "mode",            mode);

    /* common stuff */
    return xen_config_dev_all(fe, be);
}

int xen_config_dev_nic(NICInfo *nic)
{
    char fe[256], be[256];
    char mac[20];
    int vlan_id = -1;

    net_hub_id_for_client(nic->netdev, &vlan_id);
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             nic->macaddr.a[0], nic->macaddr.a[1], nic->macaddr.a[2],
             nic->macaddr.a[3], nic->macaddr.a[4], nic->macaddr.a[5]);
    xen_pv_printf(NULL, 1, "config nic %d: mac=\"%s\"\n", vlan_id, mac);
    xen_config_dev_dirs("vif", "qnic", vlan_id, fe, be, sizeof(fe));

    /* frontend */
    xenstore_write_int(fe, "handle",     vlan_id);
    xenstore_write_str(fe, "mac",        mac);

    /* backend */
    xenstore_write_int(be, "handle",     vlan_id);
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
