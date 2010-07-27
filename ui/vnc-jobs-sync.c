/*
 * QEMU VNC display driver
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2009 Red Hat, Inc
 * Copyright (C) 2010 Corentin Chary <corentin.chary@gmail.com>
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

#include "vnc.h"
#include "vnc-jobs.h"

void vnc_jobs_clear(VncState *vs)
{
}

void vnc_jobs_join(VncState *vs)
{
}

VncJob *vnc_job_new(VncState *vs)
{
    vs->job.vs = vs;
    vs->job.rectangles = 0;

    vnc_write_u8(vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(vs, 0);
    vs->job.saved_offset = vs->output.offset;
    vnc_write_u16(vs, 0);
    return &vs->job;
}

void vnc_job_push(VncJob *job)
{
    VncState *vs = job->vs;

    vs->output.buffer[job->saved_offset] = (job->rectangles >> 8) & 0xFF;
    vs->output.buffer[job->saved_offset + 1] = job->rectangles & 0xFF;
    vnc_flush(job->vs);
}

int vnc_job_add_rect(VncJob *job, int x, int y, int w, int h)
{
    int n;

    n = vnc_send_framebuffer_update(job->vs, x, y, w, h);
    if (n >= 0)
        job->rectangles += n;
    return n;
}

bool vnc_has_job(VncState *vs)
{
    return false;
}
