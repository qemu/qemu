/*
 * QEMU Character device internals
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef CHARDEV_INTERNAL_H
#define CHARDEV_INTERNAL_H

#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define MAX_HUB 4
#define MAX_MUX 4
#define MUX_BUFFER_SIZE 32 /* Must be a power of 2.  */
#define MUX_BUFFER_MASK (MUX_BUFFER_SIZE - 1)

struct MuxChardev {
    Chardev parent;
    /* Linked frontends */
    CharFrontend *frontends[MAX_MUX];
    /* frontend of the underlying muxed chardev */
    CharFrontend chr;
    unsigned long mux_bitset;
    int focus;
    bool term_got_escape;
    /* Intermediate input buffer catches escape sequences even if the
       currently active device is not accepting any input - but only until it
       is full as well. */
    unsigned char buffer[MAX_MUX][MUX_BUFFER_SIZE];
    unsigned int prod[MAX_MUX];
    unsigned int cons[MAX_MUX];
    int timestamps;

    /* Protected by the Chardev chr_write_lock.  */
    bool linestart;
    int64_t timestamps_start;
};
typedef struct MuxChardev MuxChardev;
typedef struct HubChardev HubChardev;
typedef struct HubCharBackend HubCharBackend;

/*
 * Back-pointer on a hub, actual backend and its index in
 * `hub->backends` array
 */
struct HubCharBackend {
    HubChardev *hub;
    CharFrontend fe;
    unsigned int be_ind;
};

struct HubChardev {
    Chardev parent;
    /* Linked backends */
    HubCharBackend backends[MAX_HUB];
    /*
     * Number of backends attached to this hub. Once attached, a
     * backend can't be detached, so the counter is only increasing.
     * To safely remove a backend, hub has to be removed first.
     */
    unsigned int be_cnt;
    /*
     * Number of CHR_EVEN_OPENED events from all backends. Needed to
     * send CHR_EVEN_CLOSED only when counter goes to zero.
     */
    unsigned int be_event_opened_cnt;
    /*
     * Counters of written bytes from a single frontend device
     * to multiple backend devices.
     */
    unsigned int be_written[MAX_HUB];
    unsigned int be_min_written;
    /*
     * Index of a backend device which got EAGAIN on last write,
     * -1 is invalid index.
     */
    int be_eagain_ind;
};
typedef struct HubChardev HubChardev;

DECLARE_INSTANCE_CHECKER(MuxChardev, MUX_CHARDEV,
                         TYPE_CHARDEV_MUX)
DECLARE_INSTANCE_CHECKER(HubChardev, HUB_CHARDEV,
                         TYPE_CHARDEV_HUB)

#define CHARDEV_IS_MUX(chr)                                \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_MUX)
#define CHARDEV_IS_HUB(chr)                                \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_HUB)

bool mux_chr_attach_frontend(MuxChardev *d, CharFrontend *c,
                             unsigned int *tag, Error **errp);
bool mux_chr_detach_frontend(MuxChardev *d, unsigned int tag);
void mux_set_focus(Chardev *chr, unsigned int focus);
void mux_chr_send_all_event(Chardev *chr, QEMUChrEvent event);

Object *get_chardevs_root(void);

#endif /* CHARDEV_INTERNAL_H */
