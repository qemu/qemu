/*
 * GLIB Compatibility Functions
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Michael Tokarev   <mjt@tls.msk.ru>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_GLIB_COMPAT_H
#define QEMU_GLIB_COMPAT_H

#include <glib.h>

/* GLIB version compatibility flags */
#if !GLIB_CHECK_VERSION(2, 26, 0)
#define G_TIME_SPAN_SECOND              (G_GINT64_CONSTANT(1000000))
#endif

#if !GLIB_CHECK_VERSION(2, 14, 0)
static inline guint g_timeout_add_seconds(guint interval, GSourceFunc function,
                                          gpointer data)
{
    return g_timeout_add(interval * 1000, function, data);
}
#endif

#if !GLIB_CHECK_VERSION(2, 28, 0)
static inline gint64 g_get_monotonic_time(void)
{
    /* g_get_monotonic_time() is best-effort so we can use the wall clock as a
     * fallback.
     */

    GTimeVal time;
    g_get_current_time(&time);

    return time.tv_sec * G_TIME_SPAN_SECOND + time.tv_usec;
}
#endif

#if !GLIB_CHECK_VERSION(2, 16, 0)
static inline int g_strcmp0(const char *str1, const char *str2)
{
    int result;

    if (!str1) {
        result = -(str1 != str2);
    } else if (!str2) {
        result = (str1 != str2);
    } else {
        result = strcmp(str1, str2);
    }

    return result;
}
#endif

#ifdef _WIN32
/*
 * g_poll has a problem on Windows when using
 * timeouts < 10ms, so use wrapper.
 */
#define g_poll(fds, nfds, timeout) g_poll_fixed(fds, nfds, timeout)
gint g_poll_fixed(GPollFD *fds, guint nfds, gint timeout);
#elif !GLIB_CHECK_VERSION(2, 20, 0)
/*
 * Glib before 2.20.0 doesn't implement g_poll, so wrap it to compile properly
 * on older systems.
 */
static inline gint g_poll(GPollFD *fds, guint nfds, gint timeout)
{
    GMainContext *ctx = g_main_context_default();
    return g_main_context_get_poll_func(ctx)(fds, nfds, timeout);
}
#endif

#if !GLIB_CHECK_VERSION(2, 31, 0)
/* before glib-2.31, GMutex and GCond was dynamic-only (there was a separate
 * GStaticMutex, but it didn't work with condition variables).
 *
 * Our implementation uses GOnce to fake a static implementation that does
 * not require separate initialization.
 * We need to rename the types to avoid passing our CompatGMutex/CompatGCond
 * by mistake to a function that expects GMutex/GCond.  However, for ease
 * of use we keep the GLib function names.  GLib uses macros for the
 * implementation, we use inline functions instead and undefine the macros.
 */

typedef struct CompatGMutex {
    GOnce once;
} CompatGMutex;

typedef struct CompatGCond {
    GOnce once;
} CompatGCond;

static inline gpointer do_g_mutex_new(gpointer unused)
{
    return (gpointer) g_mutex_new();
}

static inline void g_mutex_init(CompatGMutex *mutex)
{
    mutex->once = (GOnce) G_ONCE_INIT;
}

static inline void g_mutex_clear(CompatGMutex *mutex)
{
    assert(mutex->once.status != G_ONCE_STATUS_PROGRESS);
    if (mutex->once.retval) {
        g_mutex_free((GMutex *) mutex->once.retval);
    }
    mutex->once = (GOnce) G_ONCE_INIT;
}

static inline void (g_mutex_lock)(CompatGMutex *mutex)
{
    g_once(&mutex->once, do_g_mutex_new, NULL);
    g_mutex_lock((GMutex *) mutex->once.retval);
}
#undef g_mutex_lock

static inline gboolean (g_mutex_trylock)(CompatGMutex *mutex)
{
    g_once(&mutex->once, do_g_mutex_new, NULL);
    return g_mutex_trylock((GMutex *) mutex->once.retval);
}
#undef g_mutex_trylock


static inline void (g_mutex_unlock)(CompatGMutex *mutex)
{
    g_mutex_unlock((GMutex *) mutex->once.retval);
}
#undef g_mutex_unlock

static inline gpointer do_g_cond_new(gpointer unused)
{
    return (gpointer) g_cond_new();
}

static inline void g_cond_init(CompatGCond *cond)
{
    cond->once = (GOnce) G_ONCE_INIT;
}

static inline void g_cond_clear(CompatGCond *cond)
{
    assert(cond->once.status != G_ONCE_STATUS_PROGRESS);
    if (cond->once.retval) {
        g_cond_free((GCond *) cond->once.retval);
    }
    cond->once = (GOnce) G_ONCE_INIT;
}

static inline void (g_cond_wait)(CompatGCond *cond, CompatGMutex *mutex)
{
    assert(mutex->once.status != G_ONCE_STATUS_PROGRESS);
    g_once(&cond->once, do_g_cond_new, NULL);
    g_cond_wait((GCond *) cond->once.retval, (GMutex *) mutex->once.retval);
}
#undef g_cond_wait

static inline void (g_cond_broadcast)(CompatGCond *cond)
{
    g_once(&cond->once, do_g_cond_new, NULL);
    g_cond_broadcast((GCond *) cond->once.retval);
}
#undef g_cond_broadcast

static inline void (g_cond_signal)(CompatGCond *cond)
{
    g_once(&cond->once, do_g_cond_new, NULL);
    g_cond_signal((GCond *) cond->once.retval);
}
#undef g_cond_signal


/* before 2.31 there was no g_thread_new() */
static inline GThread *g_thread_new(const char *name,
                                    GThreadFunc func, gpointer data)
{
    GThread *thread = g_thread_create(func, data, TRUE, NULL);
    if (!thread) {
        g_error("creating thread");
    }
    return thread;
}
#else
#define CompatGMutex GMutex
#define CompatGCond GCond
#endif /* glib 2.31 */

#endif
