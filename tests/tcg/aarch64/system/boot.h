/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *
 * Copyright (c) 2026 Linaro Ltd
 *
 */


/* Global variables exported in boot.S */
extern volatile uint64_t exception_fault_address; /* Updated by ISR */
extern volatile uint64_t exception_type_code; /* Updated by ISR */
extern uint64_t realms_gpt0[];
extern uint64_t realms_gpt1[];
