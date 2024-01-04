#ifndef _ASMARM64_PSCI_H_
#define _ASMARM64_PSCI_H_
#include <libcflat.h>
#include <asm/uapi-psci.h>

#define PSCI_INVOKE_ARG_TYPE	u64
#define PSCI_FN_CPU_ON		PSCI_0_2_FN64_CPU_ON

extern int psci_invoke(u64 function_id, u64 arg0, u64 arg1, u64 arg2);
extern int psci_cpu_on(unsigned long cpuid, unsigned long entry_point);
extern void psci_sys_reset(void);
extern int cpu_psci_cpu_boot(unsigned int cpu);
extern void cpu_psci_cpu_die(unsigned int cpu);

#endif /* _ASMARM64_PSCI_H_ */
