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
static SubChannelId blk_schid = { .one = 1 };
IplParameterBlock iplb __attribute__((__aligned__(PAGE_SIZE)));

/*
 * Priniciples of Operations (SA22-7832-09) chapter 17 requires that
 * a subsystem-identification is at 184-187 and bytes 188-191 are zero
 * after list-directed-IPL and ccw-IPL.
 */
void write_subsystem_identification(void)
{
    SubChannelId *schid = (SubChannelId *) 184;
    uint32_t *zeroes = (uint32_t *) 188;

    *schid = blk_schid;
    *zeroes = 0;
}


void panic(const char *string)
{
    sclp_print(string);
    disabled_wait();
    while (1) { }
}

static bool find_dev(Schib *schib, int dev_no)
{
    int i, r;

    for (i = 0; i < 0x10000; i++) {
        blk_schid.sch_no = i;
        r = stsch_err(blk_schid, schib);
        if ((r == 3) || (r == -EIO)) {
            break;
        }
        if (!schib->pmcw.dnv) {
            continue;
        }
        if (!virtio_is_supported(blk_schid)) {
            continue;
        }
        if ((dev_no < 0) || (schib->pmcw.dev == dev_no)) {
            return true;
        }
    }

    return false;
}

static void virtio_setup(void)
{
    Schib schib;
    int ssid;
    bool found = false;
    uint16_t dev_no;

    /*
     * We unconditionally enable mss support. In every sane configuration,
     * this will succeed; and even if it doesn't, stsch_err() can deal
     * with the consequences.
     */
    enable_mss_facility();

    if (store_iplb(&iplb)) {
        switch (iplb.pbt) {
        case S390_IPL_TYPE_CCW:
            dev_no = iplb.ccw.devno;
            debug_print_int("device no. ", dev_no);
            blk_schid.ssid = iplb.ccw.ssid & 0x3;
            debug_print_int("ssid ", blk_schid.ssid);
            found = find_dev(&schib, dev_no);
            break;
        default:
            panic("List-directed IPL not supported yet!\n");
        }
    } else {
        for (ssid = 0; ssid < 0x3; ssid++) {
            blk_schid.ssid = ssid;
            found = find_dev(&schib, -1);
            if (found) {
                break;
            }
        }
    }

    IPL_assert(found, "No virtio device found");

    virtio_setup_device(blk_schid);

    IPL_assert(virtio_ipl_disk_is_valid(), "No valid IPL device detected");
}

int main(void)
{
    sclp_setup();
    virtio_setup();

    zipl_load(); /* no return */

    panic("Failed to load OS from hard disk\n");
    return 0; /* make compiler happy */
}
