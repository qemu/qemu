/*
 * Helper functions for guest memory tracing
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TRACE__MEM_H
#define TRACE__MEM_H

#include "tcg/tcg.h"


/**
 * trace_mem_get_info:
 *
 * Return a value for the 'info' argument in guest memory access traces.
 */
static uint8_t trace_mem_get_info(TCGMemOp op, bool store);

/**
 * trace_mem_build_info:
 *
 * Return a value for the 'info' argument in guest memory access traces.
 */
static uint8_t trace_mem_build_info(int size_shift, bool sign_extend,
                                    TCGMemOp endianness, bool store);


#include "trace/mem-internal.h"

#endif /* TRACE__MEM_H */
