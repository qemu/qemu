#ifndef QEMU_AUDIO_PT_INT_H
#define QEMU_AUDIO_PT_INT_H

#include <pthread.h>

struct audio_pt {
    const char *drv;
    pthread_t thread;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
};

int audio_pt_init (struct audio_pt *, void *(*) (void *), void *,
                   const char *, const char *);
int audio_pt_fini (struct audio_pt *, const char *);
int audio_pt_lock (struct audio_pt *, const char *);
int audio_pt_unlock (struct audio_pt *, const char *);
int audio_pt_wait (struct audio_pt *, const char *);
int audio_pt_unlock_and_signal (struct audio_pt *, const char *);
int audio_pt_join (struct audio_pt *, void **, const char *);

#endif /* QEMU_AUDIO_PT_INT_H */
