/*
 * QEMU VNC display driver
 *
 * From libvncserver/rfb/rfbproto.h
 * Copyright (C) 2005 Rohit Kumar, Johannes E. Schindelin
 * Copyright (C) 2000-2002 Constantin Kaplinsky.  All Rights Reserved.
 * Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
 * Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
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

#ifndef VNC_JOBS_H
#define VNC_JOBS_H

/* Jobs */
VncJob *vnc_job_new(VncState *vs);
int vnc_job_add_rect(VncJob *job, int x, int y, int w, int h);
void vnc_job_push(VncJob *job);
bool vnc_has_job(VncState *vs);
void vnc_jobs_clear(VncState *vs);
void vnc_jobs_join(VncState *vs);

#ifdef CONFIG_VNC_THREAD

void vnc_jobs_consume_buffer(VncState *vs);
void vnc_start_worker_thread(void);
bool vnc_worker_thread_running(void);
void vnc_stop_worker_thread(void);

#endif /* CONFIG_VNC_THREAD */

/* Locks */
static inline int vnc_trylock_display(VncDisplay *vd)
{
#ifdef CONFIG_VNC_THREAD
    return qemu_mutex_trylock(&vd->mutex);
#else
    return 0;
#endif
}

static inline void vnc_lock_display(VncDisplay *vd)
{
#ifdef CONFIG_VNC_THREAD
    qemu_mutex_lock(&vd->mutex);
#endif
}

static inline void vnc_unlock_display(VncDisplay *vd)
{
#ifdef CONFIG_VNC_THREAD
    qemu_mutex_unlock(&vd->mutex);
#endif
}

static inline void vnc_lock_output(VncState *vs)
{
#ifdef CONFIG_VNC_THREAD
    qemu_mutex_lock(&vs->output_mutex);
#endif
}

static inline void vnc_unlock_output(VncState *vs)
{
#ifdef CONFIG_VNC_THREAD
    qemu_mutex_unlock(&vs->output_mutex);
#endif
}

#endif /* VNC_JOBS_H */
