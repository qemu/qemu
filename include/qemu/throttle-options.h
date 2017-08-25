/*
 * QEMU throttling command line options
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 *
 */
#ifndef THROTTLE_OPTIONS_H
#define THROTTLE_OPTIONS_H

#define QEMU_OPT_IOPS_TOTAL "iops-total"
#define QEMU_OPT_IOPS_TOTAL_MAX "iops-total-max"
#define QEMU_OPT_IOPS_TOTAL_MAX_LENGTH "iops-total-max-length"
#define QEMU_OPT_IOPS_READ "iops-read"
#define QEMU_OPT_IOPS_READ_MAX "iops-read-max"
#define QEMU_OPT_IOPS_READ_MAX_LENGTH "iops-read-max-length"
#define QEMU_OPT_IOPS_WRITE "iops-write"
#define QEMU_OPT_IOPS_WRITE_MAX "iops-write-max"
#define QEMU_OPT_IOPS_WRITE_MAX_LENGTH "iops-write-max-length"
#define QEMU_OPT_BPS_TOTAL "bps-total"
#define QEMU_OPT_BPS_TOTAL_MAX "bps-total-max"
#define QEMU_OPT_BPS_TOTAL_MAX_LENGTH "bps-total-max-length"
#define QEMU_OPT_BPS_READ "bps-read"
#define QEMU_OPT_BPS_READ_MAX "bps-read-max"
#define QEMU_OPT_BPS_READ_MAX_LENGTH "bps-read-max-length"
#define QEMU_OPT_BPS_WRITE "bps-write"
#define QEMU_OPT_BPS_WRITE_MAX "bps-write-max"
#define QEMU_OPT_BPS_WRITE_MAX_LENGTH "bps-write-max-length"
#define QEMU_OPT_IOPS_SIZE "iops-size"
#define QEMU_OPT_THROTTLE_GROUP_NAME "throttle-group"

#define THROTTLE_OPT_PREFIX "throttling."
#define THROTTLE_OPTS \
          { \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_TOTAL,\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit total I/O operations per second",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_READ,\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit read operations per second",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_WRITE,\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit write operations per second",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_TOTAL,\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit total bytes per second",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_READ,\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit read bytes per second",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_WRITE,\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit write bytes per second",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_TOTAL_MAX,\
            .type = QEMU_OPT_NUMBER,\
            .help = "I/O operations burst",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_READ_MAX,\
            .type = QEMU_OPT_NUMBER,\
            .help = "I/O operations read burst",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_WRITE_MAX,\
            .type = QEMU_OPT_NUMBER,\
            .help = "I/O operations write burst",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_TOTAL_MAX,\
            .type = QEMU_OPT_NUMBER,\
            .help = "total bytes burst",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_READ_MAX,\
            .type = QEMU_OPT_NUMBER,\
            .help = "total bytes read burst",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_WRITE_MAX,\
            .type = QEMU_OPT_NUMBER,\
            .help = "total bytes write burst",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_TOTAL_MAX_LENGTH,\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the iops-total-max burst period, in seconds",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_READ_MAX_LENGTH,\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the iops-read-max burst period, in seconds",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_WRITE_MAX_LENGTH,\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the iops-write-max burst period, in seconds",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_TOTAL_MAX_LENGTH,\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the bps-total-max burst period, in seconds",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_READ_MAX_LENGTH,\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the bps-read-max burst period, in seconds",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_BPS_WRITE_MAX_LENGTH,\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the bps-write-max burst period, in seconds",\
        },{ \
            .name = THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_SIZE,\
            .type = QEMU_OPT_NUMBER,\
            .help = "when limiting by iops max size of an I/O in bytes",\
        }

#endif
