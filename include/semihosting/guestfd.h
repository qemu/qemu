/*
 * Hosted file support for semihosting syscalls.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SEMIHOSTING_GUESTFD_H
#define SEMIHOSTING_GUESTFD_H

typedef enum GuestFDType {
    GuestFDUnused = 0,
    GuestFDHost,
    GuestFDGDB,
    GuestFDStatic,
    GuestFDConsole,
} GuestFDType;

/*
 * Guest file descriptors are integer indexes into an array of
 * these structures (we will dynamically resize as necessary).
 */
typedef struct GuestFD {
    GuestFDType type;
    union {
        int hostfd;
        struct {
            const uint8_t *data;
            size_t len;
            size_t off;
        } staticfile;
    };
} GuestFD;

/**
 * alloc_guestfd:
 *
 * Allocate an unused GuestFD index.  The associated guestfd index
 * will still be GuestFDUnused until it is initialized.
 */
int alloc_guestfd(void);

/**
 * dealloc_guestfd:
 * @guestfd: GuestFD index
 *
 * Deallocate a GuestFD index.  The associated GuestFD structure
 * will be recycled for a subsequent allocation.
 */
void dealloc_guestfd(int guestfd);

/**
 * get_guestfd:
 * @guestfd: GuestFD index
 *
 * Return the GuestFD structure associated with an initialized @guestfd,
 * or NULL if it has not been allocated, or hasn't been initialized.
 */
GuestFD *get_guestfd(int guestfd);

/**
 * associate_guestfd:
 * @guestfd: GuestFD index
 * @hostfd: host file descriptor
 *
 * Initialize the GuestFD for @guestfd to GuestFDHost using @hostfd.
 */
void associate_guestfd(int guestfd, int hostfd);

/**
 * staticfile_guestfd:
 * @guestfd: GuestFD index
 * @data: data to be read
 * @len: length of @data
 *
 * Initialize the GuestFD for @guestfd to GuestFDStatic.
 * The @len bytes at @data will be returned to the guest on reads.
 */
void staticfile_guestfd(int guestfd, const uint8_t *data, size_t len);

#endif /* SEMIHOSTING_GUESTFD_H */
