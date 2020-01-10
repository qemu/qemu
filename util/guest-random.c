/*
 * QEMU guest-visible random functions
 *
 * Copyright 2019 Linaro, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/guest-random.h"
#include "crypto/random.h"
#include "sysemu/replay.h"


static __thread GRand *thread_rand;
static bool deterministic;


static int glib_random_bytes(void *buf, size_t len)
{
    GRand *rand = thread_rand;
    size_t i;
    uint32_t x;

    if (unlikely(rand == NULL)) {
        /* Thread not initialized for a cpu, or main w/o -seed.  */
        thread_rand = rand = g_rand_new();
    }

    for (i = 0; i + 4 <= len; i += 4) {
        x = g_rand_int(rand);
        __builtin_memcpy(buf + i, &x, 4);
    }
    if (i < len) {
        x = g_rand_int(rand);
        __builtin_memcpy(buf + i, &x, i - len);
    }
    return 0;
}

int qemu_guest_getrandom(void *buf, size_t len, Error **errp)
{
    int ret;
    if (replay_mode == REPLAY_MODE_PLAY) {
        return replay_read_random(buf, len);
    }
    if (unlikely(deterministic)) {
        /* Deterministic implementation using Glib's Mersenne Twister.  */
        ret = glib_random_bytes(buf, len);
    } else {
        /* Non-deterministic implementation using crypto routines.  */
        ret = qcrypto_random_bytes(buf, len, errp);
    }
    if (replay_mode == REPLAY_MODE_RECORD) {
        replay_save_random(ret, buf, len);
    }
    return ret;
}

void qemu_guest_getrandom_nofail(void *buf, size_t len)
{
    (void)qemu_guest_getrandom(buf, len, &error_fatal);
}

uint64_t qemu_guest_random_seed_thread_part1(void)
{
    if (deterministic) {
        uint64_t ret;
        glib_random_bytes(&ret, sizeof(ret));
        return ret;
    }
    return 0;
}

void qemu_guest_random_seed_thread_part2(uint64_t seed)
{
    g_assert(thread_rand == NULL);
    if (deterministic) {
        thread_rand =
            g_rand_new_with_seed_array((const guint32 *)&seed,
                                       sizeof(seed) / sizeof(guint32));
    }
}

int qemu_guest_random_seed_main(const char *optarg, Error **errp)
{
    unsigned long long seed;
    if (parse_uint_full(optarg, &seed, 0)) {
        error_setg(errp, "Invalid seed number: %s", optarg);
        return -1;
    } else {
        deterministic = true;
        qemu_guest_random_seed_thread_part2(seed);
        return 0;
    }
}
