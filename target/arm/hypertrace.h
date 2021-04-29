#ifndef HYPERVISOR_TRACE_H
#define HYPERVISOR_TRACE_H

#define FUZZ_PORT 59895

#define TEST_CASE_LEN 2048
extern char next_testcase[TEST_CASE_LEN];

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

/*
 * test_pass_nochange
 * Same state as last test case
 */
void test_pass_nochange(void);

/*
 * test_crash
 * This test crashed the VM
 */
void test_crash(void);

/*
 * test_pass_change
 * Test passed and we uncovered new state
 */
void test_pass_change(void);

/*
 * complete_testcase
 * This current test is complete- we can compare what's in the trace buffer
 * with the previous state and decide accordingly what's up
 */
void complete_testcase(void);

/*
 * talk_to_server
 * Issue a reply to the server and receive our next assignment
 */
void talk_to_server(char cmd, char *buf, size_t buf_len);

#endif // HYPERVISOR_TRACE_H
