/*
 * QEMU "hardware version" machinery
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_HW_VERSION_H
#define QEMU_HW_VERSION_H

/*
 * Starting on QEMU 2.5, qemu_hw_version() returns "2.5+" by default
 * instead of QEMU_VERSION, so setting hw_version on MachineClass
 * is no longer mandatory.
 *
 * Do NOT change this string, or it will break compatibility on all
 * machine classes that don't set hw_version.
 */
#define QEMU_HW_VERSION "2.5+"

/* QEMU "hardware version" setting. Used to replace code that exposed
 * QEMU_VERSION to guests in the past and need to keep compatibility.
 * Do not use qemu_hw_version() in new code.
 */
void qemu_set_hw_version(const char *);
const char *qemu_hw_version(void);

#endif
