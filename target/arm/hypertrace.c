#include "hypercall.h"
#include "hypertrace.h"
#include <sys/mman.h>

static uint64_t *cursor = NULL;
static uint64_t *tracebuf = NULL;
static int tracing_enabled = false;

void start_hypertrace(void) {
    if (NULL == tracebuf) {
        tracebuf = mmap(NULL, 0x1000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

        if (MAP_FAILED == tracebuf) {
            printf("FAILED TO MMAP\n");
            exit(0);
        }
        else {
            printf("mmap success! tracebuf is at %p\n", tracebuf);
        }
    }

    cursor = tracebuf;
    *cursor = 0;
    tracing_enabled = true;
}

void stop_hypertrace(void) {
    tracing_enabled = false;
    uint64_t *tmp_cursor = tracebuf;
    while (*tmp_cursor) {
        printf("%lx\n", *tmp_cursor);
        tmp_cursor++;
    }
}

void submit_pc(uint64_t pc_val) {
    if (tracing_enabled) {
        *cursor = pc_val;
        cursor++;
        *cursor = 0;
    }
}
