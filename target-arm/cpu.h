/*
 * ARM virtual CPU header
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CPU_ARM_H
#define CPU_ARM_H

#define TARGET_LONG_BITS 32

#include "cpu-defs.h"

#define EXCP_UDEF       1   /* undefined instruction */
#define EXCP_SWI        2   /* software interrupt */

typedef struct CPUARMState {
    uint32_t regs[16];
    uint32_t cpsr;
    
    /* cpsr flag cache for faster execution */
    uint32_t CF; /* 0 or 1 */
    uint32_t VF; /* V is the bit 31. All other bits are undefined */
    uint32_t NZF; /* N is bit 31. Z is computed from NZF */

    /* exception/interrupt handling */
    jmp_buf jmp_env;
    int exception_index;
    int interrupt_request;
    struct TranslationBlock *current_tb;
    int user_mode_only;

    /* user data */
    void *opaque;
} CPUARMState;

CPUARMState *cpu_arm_init(void);
int cpu_arm_exec(CPUARMState *s);
void cpu_arm_close(CPUARMState *s);
/* you can call this signal handler from your SIGBUS and SIGSEGV
   signal handlers to inform the virtual CPU of exceptions. non zero
   is returned if the signal was handled by the virtual CPU.  */
struct siginfo;
int cpu_arm_signal_handler(int host_signum, struct siginfo *info, 
                           void *puc);

void cpu_arm_dump_state(CPUARMState *env, FILE *f, int flags);

#define TARGET_PAGE_BITS 12
#include "cpu-all.h"

#endif
