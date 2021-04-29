#ifndef HYPERVISOR_TRACE_H
#define HYPERVISOR_TRACE_H

/*
 * Launch PC tracing
 */
void start_hypertrace(void);

/*
 * Cease PC tracing
 */
void stop_hypertrace(void);

/*
 * Submit current PC at this step
 */
void submit_pc(uint64_t pc_val);

#endif // HYPERVISOR_TRACE_H
