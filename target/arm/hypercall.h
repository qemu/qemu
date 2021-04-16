#ifndef HYPERCALL_H
#define HYPERCALL_H

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "translate.h"
#include "internals.h"
#include "qemu/host-utils.h"

#include "semihosting/semihost.h"
#include "exec/gen-icount.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"

#include "trace-tcg.h"
#include "translate-a64.h"
#include "qemu/atomic128.h"

#define FUZZER_MAGIC_HVC_IMM 0x1337

/*
 * intercept_hypercall
 * Intercepts a HVC call regardless of whether we came from EL0 or not.
 *
 * Inputs:
 *  cpu_env - Current CPU environment variable (global state to translate-a64)
 *
 * Outputs:
 *  None
 *
 * Side Effects:
 *  May log to the qemu logfile (Specified with -D argument)
 */
void intercept_hypercall(CPUARMState *cpu_env);

/*
 * hypervisor_read_from_virt_mem
 * Translates a virtual address to physical address using user ARM MMU, and reads from that address
 * into the specified buffer. Returns the number of bytes read.
 *
 * Inputs:
 *  - cpu_env: The CPU state
 *  - virt_addr: Virtual address to read from
 *  - buf: Buffer (hypervisor side) to read into
 *  - len: Number of bytes to read from guest ram
 *
 * Return Value:
 *  - 0 on success, negative error code on error.
 *
 * Side Effects:
 *  - Causes page table walk in Qemu internals, reads from guest physical memory.
 */
ssize_t hypervisor_read_from_virt_mem (CPUARMState *cpu_env, uint64_t virt_addr, void *buf, size_t len);

#endif // HYPERCALL_H
