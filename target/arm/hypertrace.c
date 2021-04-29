#include "hypercall.h"
#include "hypertrace.h"
#include <sys/mman.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

static uint64_t *cursor = NULL;
static uint64_t *tracebuf = NULL;
static int tracing_enabled = false;
static uint64_t last_trace_len = 0;

char next_testcase[TEST_CASE_LEN];

/*
 * talk_to_server
 * Issue a reply to the server and receive our next assignment
 */
void talk_to_server(char cmd, char *buf, size_t buf_len) {
    struct sockaddr_in server_address;
    int clientsocket = socket(AF_INET, SOCK_STREAM, 0);

    if (clientsocket < 0) {
        perror("socket");
        qemu_log("Socket couldn't be created\n");
    }

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(FUZZ_PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0) {
        perror("inet_pton");
    }

    if (connect(clientsocket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        qemu_log("Couldn't connect to server\n");
        perror("connect");
    }
    else {
        char cmdbuf[64];
        cmdbuf[0] = cmd;
        cmdbuf[1] = '\x00';
        int bytes_written = write(clientsocket, cmdbuf, sizeof(cmdbuf));
        int bytes_read = read(clientsocket, buf, buf_len);
        bytes_written = bytes_read + 1; // get compiler to stop complaining
        bytes_read = bytes_written + 1; // get compiler to stop complaining
    }

    close(clientsocket);
}

/*
 * test_pass_nochange
 * Same state as last test case
 */
void test_pass_nochange(void) {
    talk_to_server('R', next_testcase, sizeof(next_testcase));
}

/*
 * test_crash
 * This test crashed the VM
 */
void test_crash(void) {
    talk_to_server('C', next_testcase, sizeof(next_testcase));
}

/*
 * test_pass_change
 * Test passed and we uncovered new state
 */
void test_pass_change(void) {
    talk_to_server('U', next_testcase, sizeof(next_testcase));
}

/*
 * complete_testcase
 * This current test is complete- we can compare what's in the trace buffer
 * with the previous state and decide accordingly what's up
 */
void complete_testcase(void) {
    // We use the length of the trace buffer to determine coverage
    uint64_t trace_len = cursor - tracebuf;
    if (trace_len != last_trace_len) {
        last_trace_len = trace_len;
        test_pass_change();
    }
    else {
        test_pass_nochange();
    }
}

void start_hypertrace(void) {
    if (NULL == tracebuf) {
        tracebuf = mmap(NULL, 0x1000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

        if (MAP_FAILED == tracebuf) {
            qemu_log("FAILED TO MMAP\n");
            exit(0);
        }
        else {
            qemu_log("mmap success! tracebuf is at %p\n", tracebuf);
        }
    }

    cursor = tracebuf;
    *cursor = 0;
    tracing_enabled = true;
}

void stop_hypertrace(void) {
    tracing_enabled = false;
    /*uint64_t *tmp_cursor = tracebuf;
    while (*tmp_cursor) {
        qemu_log("%lx\n", *tmp_cursor);
        tmp_cursor++;
    }*/
}

void submit_pc(uint64_t pc_val) {
    if (tracing_enabled) {
        *cursor = pc_val;
        cursor++;
        *cursor = 0;
    }
}
