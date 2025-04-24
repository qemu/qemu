/* SPDX-License-Identifier: MIT */
/*
 * Define TARGET_INSN_START_WORDS
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TARGET_INSN_START_WORDS

#include "cpu-param.h"

# define TARGET_INSN_START_WORDS (1 + TARGET_INSN_START_EXTRA_WORDS)

#endif /* TARGET_INSN_START_WORDS */
