/*
 * QEMU Bluetooth HCI helpers.
 *
 * Copyright (C) 2007 OpenMoko, Inc.
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

/* BD Address */
typedef struct {
    uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

#define BDADDR_ANY	(&(bdaddr_t) {{0, 0, 0, 0, 0, 0}})
#define BDADDR_ALL	(&(bdaddr_t) {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}})
#define BDADDR_LOCAL	(&(bdaddr_t) {{0, 0, 0, 0xff, 0xff, 0xff}})

/* Copy, swap, convert BD Address */
static inline int bacmp(const bdaddr_t *ba1, const bdaddr_t *ba2)
{
    return memcmp(ba1, ba2, sizeof(bdaddr_t));
}
static inline void bacpy(bdaddr_t *dst, const bdaddr_t *src)
{
    memcpy(dst, src, sizeof(bdaddr_t));
}

#define BAINIT(orig)	{ .b = {		\
    (orig)->b[0], (orig)->b[1], (orig)->b[2],	\
    (orig)->b[3], (orig)->b[4], (orig)->b[5],	\
}, }

/* The twisted structures of a bluetooth environment */
struct bt_device_s;
struct bt_scatternet_s;
struct bt_piconet_s;
struct bt_link_s;

struct bt_scatternet_s {
    struct bt_device_s *slave;
};

struct bt_link_s {
    struct bt_device_s *slave, *host;
    uint16_t handle;		/* Master (host) side handle */
    uint16_t acl_interval;
    enum {
        acl_active,
        acl_hold,
        acl_sniff,
        acl_parked,
    } acl_mode;
};

struct bt_device_s {
    int lt_addr;
    bdaddr_t bd_addr;
    int mtu;
    int setup;
    struct bt_scatternet_s *net;

    uint8_t key[16];
    int key_present;
    uint8_t class[3];

    uint8_t reject_reason;

    uint64_t lmp_caps;
    const char *lmp_name;
    void (*lmp_connection_request)(struct bt_link_s *link);
    void (*lmp_connection_complete)(struct bt_link_s *link);
    void (*lmp_disconnect_master)(struct bt_link_s *link);
    void (*lmp_disconnect_slave)(struct bt_link_s *link);
    void (*lmp_acl_data)(struct bt_link_s *link, const uint8_t *data,
                    int start, int len);
    void (*lmp_acl_resp)(struct bt_link_s *link, const uint8_t *data,
                    int start, int len);
    void (*lmp_mode_change)(struct bt_link_s *link);

    void (*handle_destroy)(struct bt_device_s *device);
    struct bt_device_s *next;	/* Next in the piconet/scatternet */

    int inquiry_scan;
    int page_scan;

    uint16_t clkoff;	/* Note: Always little-endian */
};

/* bt.c */
void bt_device_init(struct bt_device_s *dev, struct bt_scatternet_s *net);
void bt_device_done(struct bt_device_s *dev);
