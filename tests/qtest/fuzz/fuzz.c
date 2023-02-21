/*
 * fuzzing driver
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include <wordexp.h>

#include "qemu/cutils.h"
#include "qemu/datadir.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "sysemu/runstate.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/qgraph.h"
#include "fuzz.h"

#define MAX_EVENT_LOOPS 10

typedef struct FuzzTargetState {
        FuzzTarget *target;
        QSLIST_ENTRY(FuzzTargetState) target_list;
} FuzzTargetState;

typedef QSLIST_HEAD(, FuzzTargetState) FuzzTargetList;

static const char *fuzz_arch = TARGET_NAME;

static FuzzTargetList *fuzz_target_list;
static FuzzTarget *fuzz_target;
static QTestState *fuzz_qts;



void flush_events(QTestState *s)
{
    int i = MAX_EVENT_LOOPS;
    while (g_main_context_pending(NULL) && i-- > 0) {
        main_loop_wait(false);
    }
}

void fuzz_reset(QTestState *s)
{
    qemu_system_reset(SHUTDOWN_CAUSE_GUEST_RESET);
    main_loop_wait(true);
}

static QTestState *qtest_setup(void)
{
    qtest_server_set_send_handler(&qtest_client_inproc_recv, &fuzz_qts);
    return qtest_inproc_init(&fuzz_qts, false, fuzz_arch,
            &qtest_server_inproc_recv);
}

void fuzz_add_target(const FuzzTarget *target)
{
    FuzzTargetState *tmp;
    FuzzTargetState *target_state;
    if (!fuzz_target_list) {
        fuzz_target_list = g_new0(FuzzTargetList, 1);
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (g_strcmp0(tmp->target->name, target->name) == 0) {
            fprintf(stderr, "Error: Fuzz target name %s already in use\n",
                    target->name);
            abort();
        }
    }
    target_state = g_new0(FuzzTargetState, 1);
    target_state->target = g_new0(FuzzTarget, 1);
    *(target_state->target) = *target;
    QSLIST_INSERT_HEAD(fuzz_target_list, target_state, target_list);
}



static void usage(char *path)
{
    printf("Usage: %s --fuzz-target=FUZZ_TARGET [LIBFUZZER ARGUMENTS]\n", path);
    printf("where FUZZ_TARGET is one of:\n");
    FuzzTargetState *tmp;
    if (!fuzz_target_list) {
        fprintf(stderr, "Fuzz target list not initialized\n");
        abort();
    }
    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        printf(" * %s  : %s\n", tmp->target->name,
                tmp->target->description);
    }
    printf("Alternatively, add -target-FUZZ_TARGET to the executable name\n\n"
           "Set the environment variable FUZZ_SERIALIZE_QTEST=1 to serialize\n"
           "QTest commands into an ASCII protocol. Useful for building crash\n"
           "reproducers, but slows down execution.\n\n"
           "Set the environment variable QTEST_LOG=1 to log all qtest commands"
           "\n");
    exit(0);
}

static FuzzTarget *fuzz_get_target(char* name)
{
    FuzzTargetState *tmp;
    if (!fuzz_target_list) {
        fprintf(stderr, "Fuzz target list not initialized\n");
        abort();
    }

    QSLIST_FOREACH(tmp, fuzz_target_list, target_list) {
        if (strcmp(tmp->target->name, name) == 0) {
            return tmp->target;
        }
    }
    return NULL;
}


/* Sometimes called by libfuzzer to mutate two inputs into one */
size_t LLVMFuzzerCustomCrossOver(const uint8_t *data1, size_t size1,
                                 const uint8_t *data2, size_t size2,
                                 uint8_t *out, size_t max_out_size,
                                 unsigned int seed)
{
    if (fuzz_target->crossover) {
        return fuzz_target->crossover(data1, size1, data2, size2, out,
                                      max_out_size, seed);
    }
    return 0;
}

/* Executed for each fuzzing-input */
int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size)
{
    /*
     * Do the pre-fuzz-initialization before the first fuzzing iteration,
     * instead of before the actual fuzz loop. This is needed since libfuzzer
     * may fork off additional workers, prior to the fuzzing loop, and if
     * pre_fuzz() sets up e.g. shared memory, this should be done for the
     * individual worker processes
     */
    static int pre_fuzz_done;
    if (!pre_fuzz_done && fuzz_target->pre_fuzz) {
        fuzz_target->pre_fuzz(fuzz_qts);
        pre_fuzz_done = true;
    }

    fuzz_target->fuzz(fuzz_qts, Data, Size);
    return 0;
}

/* Executed once, prior to fuzzing */
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp)
{

    char *target_name;
    GString *cmd_line;
    gchar *pretty_cmd_line;
    bool serialize = false;

    /* Initialize qgraph and modules */
    qos_graph_init();
    module_call_init(MODULE_INIT_FUZZ_TARGET);
    module_call_init(MODULE_INIT_QOM);
    module_call_init(MODULE_INIT_LIBQOS);

    qemu_init_exec_dir(**argv);
    target_name = strstr(**argv, "-target-");
    if (target_name) {        /* The binary name specifies the target */
        target_name += strlen("-target-");
    } else if (*argc > 1) {  /* The target is specified as an argument */
        target_name = (*argv)[1];
        if (!strstr(target_name, "--fuzz-target=")) {
            usage(**argv);
        }
        target_name += strlen("--fuzz-target=");
    } else {
        usage(**argv);
    }

    /* Should we always serialize qtest commands? */
    if (getenv("FUZZ_SERIALIZE_QTEST")) {
        serialize = true;
    }

    fuzz_qtest_set_serialize(serialize);

    /* Identify the fuzz target */
    fuzz_target = fuzz_get_target(target_name);
    if (!fuzz_target) {
        usage(**argv);
    }

    fuzz_qts = qtest_setup();

    if (fuzz_target->pre_vm_init) {
        fuzz_target->pre_vm_init();
    }

    /* Run QEMU's softmmu main with the fuzz-target dependent arguments */
    cmd_line = fuzz_target->get_init_cmdline(fuzz_target);
    g_string_append_printf(cmd_line, " %s -qtest /dev/null ",
                           getenv("QTEST_LOG") ? "" : "-qtest-log none");

    /* Split the runcmd into an argv and argc */
    wordexp_t result;
    wordexp(cmd_line->str, &result, 0);
    g_string_free(cmd_line, true);

    if (getenv("QTEST_LOG")) {
        pretty_cmd_line  = g_strjoinv(" ", result.we_wordv + 1);
        printf("Starting %s with Arguments: %s\n",
                result.we_wordv[0], pretty_cmd_line);
        g_free(pretty_cmd_line);
    }

    qemu_init(result.we_wordc, result.we_wordv);

    /* re-enable the rcu atfork, which was previously disabled in qemu_init */
    rcu_enable_atfork();

    /*
     * Disable QEMU's signal handlers, since we manually control the main_loop,
     * and don't check for main_loop_should_exit
     */
    signal(SIGINT, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    return 0;
}
