/*
 * QEMU translation definitions for SPARC
 *
 * Copyright (c) 2024 Linaro, Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SPARC_TRANSLATION_H
#define SPARC_TRANSLATION_H

/* Dynamic PC, must exit to main loop. */
#define DYNAMIC_PC         1
/* Dynamic PC, one of two values according to jump_pc[T2]. */
#define JUMP_PC            2
/* Dynamic PC, may lookup next TB. */
#define DYNAMIC_PC_LOOKUP  3

#endif
