#ifndef KVM_SECURITY_LAYER
#define KVM_SECURITY_LAYER

#include <time.h>

#define HYPERCALL_OFFSET 0x80

#define AGENT_HYPERCALL             1   /* DEPRECATED HYPERCALL*/

/* Protect a memory area */
#define PROTECT_MEMORY_HYPERCALL    2   

/* Save a memory area. It could be for automatic injection or later comparison */
#define SAVE_MEMORY_HYPERCALL       3   

/* Compare a previously saved memory area */
#define COMPARE_MEMORY_HYPERCALL    4   

/* Used by the module when it has finished its initialization. It allows set irq hook */
#define SET_IRQ_LINE_HYPERCALL      5   

/* Start monitoring kernel invariants */
#define START_MONITOR_HYPERCALL     6   

/* End the recording of accessed pages */
#define END_RECORDING_HYPERCALL     7   

/* setting the address of the page containing the list of the processes */
#define SET_PROCESS_LIST_HYPERCALL  8

/* used as notification, the list was updater */
#define PROCESS_LIST_HYPERCALL      9

/* Call clear access log, testing experiment */
/* #define CLEAR_ACCESS_LOG_HYPERCALL  8 */

/* Performance measurments */
#define START_TIMER_HYPERCALL       10
#define EMPTY_HYPERCALL             11
#define STOP_TIMER_HYPERCALL        12


typedef enum recording_state {
    PRE_RECORDING, /* initial state */
    RECORDING, /* when the device driver is configured */
    POST_RECORDING /* reloading state */
} KVMRecordingState;
KVMRecordingState recording_state = PRE_RECORDING;
struct kvm_access_log kvm_access_log;

static void reload_saved_memory_chunks(void);

MemoryRegion *fx_mr = NULL;
int fx_irq_line = -1;
bool start_monitor = false;

#define NOT_IN_SLOT 0
#define IN_SLOT     1
#define IN_PMC      2

typedef struct protected_memory_chunk {
    KVMSlot *slot; /* if write is outside chunk, hypervisor will complete it */
    struct protected_memory_chunk *next;
    hwaddr addr;
    hwaddr size;
    const char *name;
} ProtectedMemoryChunk;

typedef struct saved_memory_chunk {
    bool inject_before_interrupt;
    bool access_log; /* chunks deriving from access log */
    void *hva;
    hwaddr size;
    void *saved;
    struct saved_memory_chunk *next;
} SavedMemoryChunk;

ProtectedMemoryChunk *pmc_head = NULL;
SavedMemoryChunk *smc_head = NULL;

/* Not useful anymore. */
struct kernel_invariants {
    hwaddr idt_physical_addr;
    hwaddr gdt_physical_addr; /* ? */
} kernel_invariants;

static void *process_list;

/* page table monitor */
#define PT_MONITOR_INTERVAL 1
QemuThread pt_monitor;
QemuMutex pt_mutex;

typedef struct monitored_pt_entry {
    unsigned long *entry;
    struct monitored_pt_entry *next;
} MonitoredPageTableEntry;

MonitoredPageTableEntry *pt_head;

/* Performance measurments */
FILE *perf_fd, *hypercall_fd;
struct timespec begin, end;
struct timespec begin_hypercall, end_hypercall;

#endif