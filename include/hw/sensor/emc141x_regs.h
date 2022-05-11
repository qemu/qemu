/*
 * SMSC EMC141X temperature sensor.
 *
 * Browse the data sheet:
 *
 *    http://ww1.microchip.com/downloads/en/DeviceDoc/20005274A.pdf
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#ifndef EMC141X_REGS_H
#define EMC141X_REGS_H

#define EMC1413_DEVICE_ID                0x21
#define EMC1414_DEVICE_ID                0x25
#define MANUFACTURER_ID                  0x5d
#define REVISION                         0x04

/* the EMC141X registers */
#define EMC141X_TEMP_HIGH0               0x00
#define EMC141X_TEMP_HIGH1               0x01
#define EMC141X_TEMP_HIGH2               0x23
#define EMC141X_TEMP_HIGH3               0x2a
#define EMC141X_TEMP_MAX_HIGH0           0x05
#define EMC141X_TEMP_MIN_HIGH0           0x06
#define EMC141X_TEMP_MAX_HIGH1           0x07
#define EMC141X_TEMP_MIN_HIGH1           0x08
#define EMC141X_TEMP_MAX_HIGH2           0x15
#define EMC141X_TEMP_MIN_HIGH2           0x16
#define EMC141X_TEMP_MAX_HIGH3           0x2c
#define EMC141X_TEMP_MIN_HIGH3           0x2d
#define EMC141X_DEVICE_ID                0xfd
#define EMC141X_MANUFACTURER_ID          0xfe
#define EMC141X_REVISION                 0xff

#endif
