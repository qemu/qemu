/*
 * S390 virtio-ccw network boot loading program
 *
 * Copyright 2017 Thomas Huth, Red Hat Inc.
 *
 * Based on the S390 virtio-ccw loading program (main.c)
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "s390-ccw.h"
#include "virtio.h"

extern char _start[];

char stack[PAGE_SIZE * 8] __attribute__((aligned(PAGE_SIZE)));
IplParameterBlock iplb __attribute__((aligned(PAGE_SIZE)));

static SubChannelId net_schid = { .one = 1 };
static uint64_t dest_timer;

static uint64_t get_timer_ms(void)
{
    uint64_t clk;

    asm volatile(" stck %0 " : : "Q"(clk) : "memory");

    /* Bit 51 is incremented each microsecond */
    return (clk >> (63 - 51)) / 1000;
}

void set_timer(int val)
{
    dest_timer = get_timer_ms() + val;
}

int get_timer(void)
{
    return dest_timer - get_timer_ms();
}

int get_sec_ticks(void)
{
    return 1000;    /* number of ticks in 1 second */
}

void panic(const char *string)
{
    sclp_print(string);
    for (;;) {
        disabled_wait();
    }
}

static bool find_net_dev(Schib *schib, int dev_no)
{
    int i, r;

    for (i = 0; i < 0x10000; i++) {
        net_schid.sch_no = i;
        r = stsch_err(net_schid, schib);
        if (r == 3 || r == -EIO) {
            break;
        }
        if (!schib->pmcw.dnv) {
            continue;
        }
        if (!virtio_is_supported(net_schid)) {
            continue;
        }
        if (virtio_get_device_type() != VIRTIO_ID_NET) {
            continue;
        }
        if (dev_no < 0 || schib->pmcw.dev == dev_no) {
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
        IPL_assert(iplb.pbt == S390_IPL_TYPE_CCW, "IPL_TYPE_CCW expected");
        dev_no = iplb.ccw.devno;
        debug_print_int("device no. ", dev_no);
        net_schid.ssid = iplb.ccw.ssid & 0x3;
        debug_print_int("ssid ", net_schid.ssid);
        found = find_net_dev(&schib, dev_no);
    } else {
        for (ssid = 0; ssid < 0x3; ssid++) {
            net_schid.ssid = ssid;
            found = find_net_dev(&schib, -1);
            if (found) {
                break;
            }
        }
    }

    IPL_assert(found, "No virtio net device found");
}

void main(void)
{
    sclp_setup();
    sclp_print("Network boot starting...\n");

    virtio_setup();

    panic("Failed to load OS from network\n");
}
