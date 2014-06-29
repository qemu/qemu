/*
 * S390 virtio-ccw loading program
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "s390-ccw.h"
#include "virtio.h"

char stack[PAGE_SIZE * 8] __attribute__((__aligned__(PAGE_SIZE)));
uint64_t boot_value;

void virtio_panic(const char *string)
{
    sclp_print(string);
    disabled_wait();
    while (1) { }
}

static void virtio_setup(uint64_t dev_info)
{
    struct subchannel_id blk_schid = { .one = 1 };
    struct schib schib;
    int i;
    int r;
    bool found = false;
    bool check_devno = false;
    uint16_t dev_no = -1;

    if (dev_info != -1) {
        check_devno = true;
        dev_no = dev_info & 0xffff;
        debug_print_int("device no. ", dev_no);
        blk_schid.ssid = (dev_info >> 16) & 0x3;
        if (blk_schid.ssid != 0) {
            debug_print_int("ssid ", blk_schid.ssid);
            if (enable_mss_facility() != 0) {
                virtio_panic("Failed to enable mss facility\n");
            }
        }
    }

    for (i = 0; i < 0x10000; i++) {
        blk_schid.sch_no = i;
        r = stsch_err(blk_schid, &schib);
        if (r == 3) {
            break;
        }
        if (schib.pmcw.dnv) {
            if (!check_devno || (schib.pmcw.dev == dev_no)) {
                if (virtio_is_blk(blk_schid)) {
                    found = true;
                    break;
                }
            }
        }
    }

    if (!found) {
        virtio_panic("No virtio-blk device found!\n");
    }

    virtio_setup_block(blk_schid);

    if (!virtio_ipl_disk_is_valid()) {
        virtio_panic("No valid hard disk detected.\n");
    }
}

int main(void)
{
    sclp_setup();
    debug_print_int("boot reg[7] ", boot_value);
    virtio_setup(boot_value);

    zipl_load(); /* no return */

    virtio_panic("Failed to load OS from hard disk\n");
    return 0; /* make compiler happy */
}
