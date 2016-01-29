/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_MBOX_DEFS_H
#define BCM2835_MBOX_DEFS_H

/* Constants shared with the ARM identifying separate mailbox channels */
#define MBOX_CHAN_POWER    0 /* for use by the power management interface */
#define MBOX_CHAN_FB       1 /* for use by the frame buffer */
#define MBOX_CHAN_VCHIQ    3 /* for use by the VCHIQ interface */
#define MBOX_CHAN_PROPERTY 8 /* for use by the property channel */
#define MBOX_CHAN_COUNT    9

#define MBOX_SIZE          32
#define MBOX_INVALID_DATA  0x0f

/* Layout of the private address space used for communication between
 * the mbox device emulation, and child devices: each channel occupies
 * 16 bytes of address space, but only two registers are presently defined.
 */
#define MBOX_AS_CHAN_SHIFT 4
#define MBOX_AS_DATA       0 /* request / response data (RW at offset 0) */
#define MBOX_AS_PENDING    4 /* pending response status (RO at offset 4) */

#endif /* BCM2835_MBOX_DEFS_H */
