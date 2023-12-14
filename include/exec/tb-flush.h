/*
 * tb-flush prototype for use by the rest of the system.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _TB_FLUSH_H_
#define _TB_FLUSH_H_

/**
 * tb_flush() - flush all translation blocks
 * @cs: CPUState (must be valid, but treated as anonymous pointer)
 *
 * Used to flush all the translation blocks in the system. Sometimes
 * it is simpler to flush everything than work out which individual
 * translations are now invalid and ensure they are not called
 * anymore.
 *
 * tb_flush() takes care of running the flush in an exclusive context
 * if it is not already running in one. This means no guest code will
 * run until this complete.
 */
void tb_flush(CPUState *cs);

void tcg_flush_jmp_cache(CPUState *cs);

#endif /* _TB_FLUSH_H_ */
