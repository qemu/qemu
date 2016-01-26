/*
 * Convenience functions for bluetooth.
 *
 * Copyright (C) 2008 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/bt.h"
#include "hw/bt.h"

/* Slave implementations can ignore this */
static void bt_dummy_lmp_mode_change(struct bt_link_s *link)
{
}

/* Slaves should never receive these PDUs */
static void bt_dummy_lmp_connection_complete(struct bt_link_s *link)
{
    if (link->slave->reject_reason)
        fprintf(stderr, "%s: stray LMP_not_accepted received, fixme\n",
                        __FUNCTION__);
    else
        fprintf(stderr, "%s: stray LMP_accepted received, fixme\n",
                        __FUNCTION__);
    exit(-1);
}

static void bt_dummy_lmp_disconnect_master(struct bt_link_s *link)
{
    fprintf(stderr, "%s: stray LMP_detach received, fixme\n", __FUNCTION__);
    exit(-1);
}

static void bt_dummy_lmp_acl_resp(struct bt_link_s *link,
                const uint8_t *data, int start, int len)
{
    fprintf(stderr, "%s: stray ACL response PDU, fixme\n", __FUNCTION__);
    exit(-1);
}

/* Slaves that don't hold any additional per link state can use these */
static void bt_dummy_lmp_connection_request(struct bt_link_s *req)
{
    struct bt_link_s *link = g_malloc0(sizeof(struct bt_link_s));

    link->slave = req->slave;
    link->host = req->host;

    req->host->reject_reason = 0;
    req->host->lmp_connection_complete(link);
}

static void bt_dummy_lmp_disconnect_slave(struct bt_link_s *link)
{
    g_free(link);
}

static void bt_dummy_destroy(struct bt_device_s *device)
{
    bt_device_done(device);
    g_free(device);
}

static int bt_dev_idx = 0;

void bt_device_init(struct bt_device_s *dev, struct bt_scatternet_s *net)
{
    memset(dev, 0, sizeof(*dev));
    dev->inquiry_scan = 1;
    dev->page_scan = 1;

    dev->bd_addr.b[0] = bt_dev_idx & 0xff;
    dev->bd_addr.b[1] = bt_dev_idx >> 8;
    dev->bd_addr.b[2] = 0xd0;
    dev->bd_addr.b[3] = 0xba;
    dev->bd_addr.b[4] = 0xbe;
    dev->bd_addr.b[5] = 0xba;
    bt_dev_idx ++;

    /* Simple slave-only devices need to implement only .lmp_acl_data */
    dev->lmp_connection_complete = bt_dummy_lmp_connection_complete;
    dev->lmp_disconnect_master = bt_dummy_lmp_disconnect_master;
    dev->lmp_acl_resp = bt_dummy_lmp_acl_resp;
    dev->lmp_mode_change = bt_dummy_lmp_mode_change;
    dev->lmp_connection_request = bt_dummy_lmp_connection_request;
    dev->lmp_disconnect_slave = bt_dummy_lmp_disconnect_slave;

    dev->handle_destroy = bt_dummy_destroy;

    dev->net = net;
    dev->next = net->slave;
    net->slave = dev;
}

void bt_device_done(struct bt_device_s *dev)
{
    struct bt_device_s **p = &dev->net->slave;

    while (*p && *p != dev)
        p = &(*p)->next;
    if (*p != dev) {
        fprintf(stderr, "%s: bad bt device \"%s\"\n", __FUNCTION__,
                        dev->lmp_name ?: "(null)");
        exit(-1);
    }

    *p = dev->next;
}

static struct bt_vlan_s {
    struct bt_scatternet_s net;
    int id;
    struct bt_vlan_s *next;
} *first_bt_vlan;

/* find or alloc a new bluetooth "VLAN" */
struct bt_scatternet_s *qemu_find_bt_vlan(int id)
{
    struct bt_vlan_s **pvlan, *vlan;
    for (vlan = first_bt_vlan; vlan != NULL; vlan = vlan->next) {
        if (vlan->id == id)
            return &vlan->net;
    }
    vlan = g_malloc0(sizeof(struct bt_vlan_s));
    vlan->id = id;
    pvlan = &first_bt_vlan;
    while (*pvlan != NULL)
        pvlan = &(*pvlan)->next;
    *pvlan = vlan;
    return &vlan->net;
}
