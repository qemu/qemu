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

#ifndef QEMU_GUEST_RANDOM_H
#define QEMU_GUEST_RANDOM_H

/**
 * qemu_guest_random_seed_main(const char *optarg, Error **errp)
 * @optarg: a non-NULL pointer to a C string
 * @errp: an error indicator
 *
 * The @optarg value is that which accompanies the -seed argument.
 * This forces qemu_guest_getrandom into deterministic mode.
 *
 * Returns 0 on success, < 0 on failure while setting *errp.
 */
int qemu_guest_random_seed_main(const char *optarg, Error **errp);

/**
 * qemu_guest_random_seed_thread_part1(void)
 *
 * If qemu_getrandom is in deterministic mode, returns an
 * independent seed for the new thread.  Otherwise returns 0.
 */
uint64_t qemu_guest_random_seed_thread_part1(void);

/**
 * qemu_guest_random_seed_thread_part2(uint64_t seed)
 * @seed: a value for the new thread.
 *
 * If qemu_guest_getrandom is in deterministic mode, this stores an
 * independent seed for the new thread.  Otherwise a no-op.
 */
void qemu_guest_random_seed_thread_part2(uint64_t seed);

/**
 * qemu_guest_getrandom(void *buf, size_t len, Error **errp)
 * @buf: a buffer of bytes to be written
 * @len: the number of bytes in @buf
 * @errp: an error indicator
 *
 * Fills len bytes in buf with random data.  This should only be used
 * for data presented to the guest.  Host-side crypto services should
 * use qcrypto_random_bytes.
 *
 * Returns 0 on success, < 0 on failure while setting *errp.
 */
int qemu_guest_getrandom(void *buf, size_t len, Error **errp);

/**
 * qemu_guest_getrandom_nofail(void *buf, size_t len)
 * @buf: a buffer of bytes to be written
 * @len: the number of bytes in @buf
 *
 * Like qemu_guest_getrandom, but will assert for failure.
 * Use this when there is no reasonable recovery.
 */
void qemu_guest_getrandom_nofail(void *buf, size_t len);

#endif /* QEMU_GUEST_RANDOM_H */
