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

struct subchannel_id blk_schid;
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
    struct schib schib;
    int i;
    int r;
    bool found = false;
    bool check_devno = false;
    uint16_t dev_no = -1;
    blk_schid.one = 1;

    if (dev_info != -1) {
        check_devno = true;
        dev_no = dev_info & 0xffff;
        debug_print_int("device no. ", dev_no);
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
}

int main(void)
{
    sclp_setup();
    debug_print_int("boot reg[7] ", boot_value);
    virtio_setup(boot_value);

    if (zipl_load() < 0)
        sclp_print("Failed to load OS from hard disk\n");
    disabled_wait();
    while (1) { }
}
