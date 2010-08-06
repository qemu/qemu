#include "qemu-common.h"
#include "audio.h"

#define AUDIO_CAP "audio-pt"

#include "audio_int.h"
#include "audio_pt_int.h"

#include <signal.h>

static void logerr (struct audio_pt *pt, int err, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    AUD_vlog (pt->drv, fmt, ap);
    va_end (ap);

    AUD_log (NULL, "\n");
    AUD_log (pt->drv, "Reason: %s\n", strerror (err));
}

int audio_pt_init (struct audio_pt *p, void *(*func) (void *),
                   void *opaque, const char *drv, const char *cap)
{
    int err, err2;
    const char *efunc;
    sigset_t set, old_set;

    p->drv = drv;

    err = sigfillset (&set);
    if (err) {
        logerr (p, errno, "%s(%s): sigfillset failed", cap, AUDIO_FUNC);
        return -1;
    }

    err = pthread_mutex_init (&p->mutex, NULL);
    if (err) {
        efunc = "pthread_mutex_init";
        goto err0;
    }

    err = pthread_cond_init (&p->cond, NULL);
    if (err) {
        efunc = "pthread_cond_init";
        goto err1;
    }

    err = pthread_sigmask (SIG_BLOCK, &set, &old_set);
    if (err) {
        efunc = "pthread_sigmask";
        goto err2;
    }

    err = pthread_create (&p->thread, NULL, func, opaque);

    err2 = pthread_sigmask (SIG_SETMASK, &old_set, NULL);
    if (err2) {
        logerr (p, err2, "%s(%s): pthread_sigmask (restore) failed",
                cap, AUDIO_FUNC);
        /* We have failed to restore original signal mask, all bets are off,
           so terminate the process */
        exit (EXIT_FAILURE);
    }

    if (err) {
        efunc = "pthread_create";
        goto err2;
    }

    return 0;

 err2:
    err2 = pthread_cond_destroy (&p->cond);
    if (err2) {
        logerr (p, err2, "%s(%s): pthread_cond_destroy failed", cap, AUDIO_FUNC);
    }

 err1:
    err2 = pthread_mutex_destroy (&p->mutex);
    if (err2) {
        logerr (p, err2, "%s(%s): pthread_mutex_destroy failed", cap, AUDIO_FUNC);
    }

 err0:
    logerr (p, err, "%s(%s): %s failed", cap, AUDIO_FUNC, efunc);
    return -1;
}

int audio_pt_fini (struct audio_pt *p, const char *cap)
{
    int err, ret = 0;

    err = pthread_cond_destroy (&p->cond);
    if (err) {
        logerr (p, err, "%s(%s): pthread_cond_destroy failed", cap, AUDIO_FUNC);
        ret = -1;
    }

    err = pthread_mutex_destroy (&p->mutex);
    if (err) {
        logerr (p, err, "%s(%s): pthread_mutex_destroy failed", cap, AUDIO_FUNC);
        ret = -1;
    }
    return ret;
}

int audio_pt_lock (struct audio_pt *p, const char *cap)
{
    int err;

    err = pthread_mutex_lock (&p->mutex);
    if (err) {
        logerr (p, err, "%s(%s): pthread_mutex_lock failed", cap, AUDIO_FUNC);
        return -1;
    }
    return 0;
}

int audio_pt_unlock (struct audio_pt *p, const char *cap)
{
    int err;

    err = pthread_mutex_unlock (&p->mutex);
    if (err) {
        logerr (p, err, "%s(%s): pthread_mutex_unlock failed", cap, AUDIO_FUNC);
        return -1;
    }
    return 0;
}

int audio_pt_wait (struct audio_pt *p, const char *cap)
{
    int err;

    err = pthread_cond_wait (&p->cond, &p->mutex);
    if (err) {
        logerr (p, err, "%s(%s): pthread_cond_wait failed", cap, AUDIO_FUNC);
        return -1;
    }
    return 0;
}

int audio_pt_unlock_and_signal (struct audio_pt *p, const char *cap)
{
    int err;

    err = pthread_mutex_unlock (&p->mutex);
    if (err) {
        logerr (p, err, "%s(%s): pthread_mutex_unlock failed", cap, AUDIO_FUNC);
        return -1;
    }
    err = pthread_cond_signal (&p->cond);
    if (err) {
        logerr (p, err, "%s(%s): pthread_cond_signal failed", cap, AUDIO_FUNC);
        return -1;
    }
    return 0;
}

int audio_pt_join (struct audio_pt *p, void **arg, const char *cap)
{
    int err;
    void *ret;

    err = pthread_join (p->thread, &ret);
    if (err) {
        logerr (p, err, "%s(%s): pthread_join failed", cap, AUDIO_FUNC);
        return -1;
    }
    *arg = ret;
    return 0;
}
