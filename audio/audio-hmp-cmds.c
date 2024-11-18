/*
 * HMP commands related to audio backends
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "audio/audio.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qobject/qdict.h"

static QLIST_HEAD (capture_list_head, CaptureState) capture_head;

void hmp_info_capture(Monitor *mon, const QDict *qdict)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        monitor_printf(mon, "[%d]: ", i);
        s->ops.info (s->opaque);
    }
}

void hmp_stopcapture(Monitor *mon, const QDict *qdict)
{
    int i;
    int n = qdict_get_int(qdict, "n");
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        if (i == n) {
            s->ops.destroy (s->opaque);
            QLIST_REMOVE (s, entries);
            g_free (s);
            return;
        }
    }
}

void hmp_wavcapture(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_str(qdict, "path");
    int freq = qdict_get_try_int(qdict, "freq", 44100);
    int bits = qdict_get_try_int(qdict, "bits", 16);
    int nchannels = qdict_get_try_int(qdict, "nchannels", 2);
    const char *audiodev = qdict_get_str(qdict, "audiodev");
    CaptureState *s;
    Error *local_err = NULL;
    AudioState *as = audio_state_by_name(audiodev, &local_err);

    if (!as) {
        error_report_err(local_err);
        return;
    }

    s = g_malloc0 (sizeof (*s));

    if (wav_start_capture(as, s, path, freq, bits, nchannels)) {
        monitor_printf(mon, "Failed to add wave capture\n");
        g_free (s);
        return;
    }
    QLIST_INSERT_HEAD (&capture_head, s, entries);
}
