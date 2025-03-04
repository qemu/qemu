/*
 * Global variables that should not exist (target specific)
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "system/system.h"

#ifdef TARGET_SPARC
int graphic_width = 1024;
int graphic_height = 768;
int graphic_depth = 8;
#elif defined(TARGET_M68K)
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 8;
#else
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 32;
#endif
