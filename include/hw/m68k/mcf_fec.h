/*
 * Definitions for the ColdFire Fast Ethernet Controller emulation.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef HW_M68K_MCF_FEC_H
#define HW_M68K_MCF_FEC_H

#define TYPE_MCF_FEC_NET "mcf-fec"
#define MCF_FEC_NET(obj) OBJECT_CHECK(mcf_fec_state, (obj), TYPE_MCF_FEC_NET)

#define FEC_NUM_IRQ 13

#endif
