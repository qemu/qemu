/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef PPC_SOCKBITS_H
#define PPC_SOCKBITS_H

#include "../generic/sockbits.h"

#undef  TARGET_SO_RCVLOWAT
#define TARGET_SO_RCVLOWAT     16
#undef  TARGET_SO_SNDLOWAT
#define TARGET_SO_SNDLOWAT     17
#undef  TARGET_SO_RCVTIMEO
#define TARGET_SO_RCVTIMEO     18
#undef  TARGET_SO_SNDTIMEO
#define TARGET_SO_SNDTIMEO     19
#undef  TARGET_SO_PASSCRED
#define TARGET_SO_PASSCRED     20
#undef  TARGET_SO_PEERCRED
#define TARGET_SO_PEERCRED     21

#endif
