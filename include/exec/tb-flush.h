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
 * tb_flush__exclusive_or_serial()
 *
 * Used to flush all the translation blocks in the system.  Mostly this is
 * used to empty the code generation buffer after it is full.  Sometimes it
 * is used when it is simpler to flush everything than work out which
 * individual translations are now invalid.
 *
 * Must be called from an exclusive or serial context, e.g. start_exclusive,
 * vm_stop, or when there is only one vcpu.  Note that start_exclusive cannot
 * be called from within the cpu run loop, so this cannot be called from
 * within target code.
 */
void tb_flush__exclusive_or_serial(void);

/**
 * queue_tb_flush() - add flush to the cpu work queue
 * @cs: CPUState
 *
 * Flush all translation blocks the next time @cs processes the work queue.
 * This should generally be followed by cpu_loop_exit(), so that the work
 * queue is processed promptly.
 */
void queue_tb_flush(CPUState *cs);

void tcg_flush_jmp_cache(CPUState *cs);

#endif /* _TB_FLUSH_H_ */
