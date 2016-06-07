/*
 * QEMU Guest Agent channel declarations
 *
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QGA_CHANNEL_H
#define QGA_CHANNEL_H


typedef struct GAChannel GAChannel;

typedef enum {
    GA_CHANNEL_VIRTIO_SERIAL,
    GA_CHANNEL_ISA_SERIAL,
    GA_CHANNEL_UNIX_LISTEN,
} GAChannelMethod;

typedef gboolean (*GAChannelCallback)(GIOCondition condition, gpointer opaque);

GAChannel *ga_channel_new(GAChannelMethod method, const gchar *path,
                          GAChannelCallback cb, gpointer opaque);
void ga_channel_free(GAChannel *c);
GIOStatus ga_channel_read(GAChannel *c, gchar *buf, gsize size, gsize *count);
GIOStatus ga_channel_write_all(GAChannel *c, const gchar *buf, gsize size);

#endif
