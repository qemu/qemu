/*
 * Common definitions for PPC sPAPR
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SPAPR_COMMON_H
#define HW_SPAPR_COMMON_H

/*
 * The maximum number of DIMM slots we can have for sPAPR guest.
 * This is not defined by sPAPR but we are defining it to 32 slots
 * based on default number of slots provided by PowerPC kernel.
 */
#define SPAPR_MAX_RAM_SLOTS     32

#endif /* HW_SPAPR_COMMON_H */
