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

#define THROTTLE_OPTS \
          { \
            .name = "throttling.iops-total",\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit total I/O operations per second",\
        },{ \
            .name = "throttling.iops-read",\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit read operations per second",\
        },{ \
            .name = "throttling.iops-write",\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit write operations per second",\
        },{ \
            .name = "throttling.bps-total",\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit total bytes per second",\
        },{ \
            .name = "throttling.bps-read",\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit read bytes per second",\
        },{ \
            .name = "throttling.bps-write",\
            .type = QEMU_OPT_NUMBER,\
            .help = "limit write bytes per second",\
        },{ \
            .name = "throttling.iops-total-max",\
            .type = QEMU_OPT_NUMBER,\
            .help = "I/O operations burst",\
        },{ \
            .name = "throttling.iops-read-max",\
            .type = QEMU_OPT_NUMBER,\
            .help = "I/O operations read burst",\
        },{ \
            .name = "throttling.iops-write-max",\
            .type = QEMU_OPT_NUMBER,\
            .help = "I/O operations write burst",\
        },{ \
            .name = "throttling.bps-total-max",\
            .type = QEMU_OPT_NUMBER,\
            .help = "total bytes burst",\
        },{ \
            .name = "throttling.bps-read-max",\
            .type = QEMU_OPT_NUMBER,\
            .help = "total bytes read burst",\
        },{ \
            .name = "throttling.bps-write-max",\
            .type = QEMU_OPT_NUMBER,\
            .help = "total bytes write burst",\
        },{ \
            .name = "throttling.iops-total-max-length",\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the iops-total-max burst period, in seconds",\
        },{ \
            .name = "throttling.iops-read-max-length",\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the iops-read-max burst period, in seconds",\
        },{ \
            .name = "throttling.iops-write-max-length",\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the iops-write-max burst period, in seconds",\
        },{ \
            .name = "throttling.bps-total-max-length",\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the bps-total-max burst period, in seconds",\
        },{ \
            .name = "throttling.bps-read-max-length",\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the bps-read-max burst period, in seconds",\
        },{ \
            .name = "throttling.bps-write-max-length",\
            .type = QEMU_OPT_NUMBER,\
            .help = "length of the bps-write-max burst period, in seconds",\
        },{ \
            .name = "throttling.iops-size",\
            .type = QEMU_OPT_NUMBER,\
            .help = "when limiting by iops max size of an I/O in bytes",\
        }

#endif
