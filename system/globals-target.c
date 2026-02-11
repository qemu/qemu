/*
 * Global variables that should not exist (target specific)
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "system/system.h"

#if defined(TARGET_SPARC) || defined(TARGET_M68K)
int graphic_width;
int graphic_height;
int graphic_depth;
#else
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 32;
#endif
