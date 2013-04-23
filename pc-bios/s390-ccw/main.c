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

void virtio_panic(const char *string)
{
    sclp_print(string);
    disabled_wait();
    while (1) { }
}

static void virtio_setup(void)
{
    struct irb irb;
    int i;
    int r;
    bool found = false;

    blk_schid.one = 1;

    for (i = 0; i < 0x10000; i++) {
        blk_schid.sch_no = i;
        r = tsch(blk_schid, &irb);
        if (r != 3) {
            if (virtio_is_blk(blk_schid)) {
                found = true;
                break;
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
    virtio_setup();
    if (zipl_load() < 0)
        sclp_print("Failed to load OS from hard disk\n");
    disabled_wait();
    while (1) { }
}
