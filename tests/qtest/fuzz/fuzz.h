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

#ifndef FUZZER_H_
#define FUZZER_H_

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"

#include "tests/qtest/libqtest.h"

/**
 * A libfuzzer fuzzing target
 *
 * The QEMU fuzzing binary is built with all available targets, each
 * with a unique @name that can be specified on the command-line to
 * select which target should run.
 *
 * A target must implement ->fuzz() to process a random input.  If QEMU
 * crashes in ->fuzz() then libfuzzer will record a failure.
 *
 * Fuzzing targets are registered with fuzz_add_target():
 *
 *   static const FuzzTarget fuzz_target = {
 *       .name = "my-device-fifo",
 *       .description = "Fuzz the FIFO buffer registers of my-device",
 *       ...
 *   };
 *
 *   static void register_fuzz_target(void)
 *   {
 *       fuzz_add_target(&fuzz_target);
 *   }
 *   fuzz_target_init(register_fuzz_target);
 */
typedef struct FuzzTarget {
    const char *name;         /* target identifier (passed to --fuzz-target=)*/
    const char *description;  /* help text */


    /*
     * Returns the arguments that are passed to qemu/softmmu init(). Freed by
     * the caller.
     */
    GString *(*get_init_cmdline)(struct FuzzTarget *);

    /*
     * will run once, prior to running qemu/softmmu init.
     * eg: set up shared-memory for communication with the child-process
     * Can be NULL
     */
    void(*pre_vm_init)(void);

    /*
     * will run once, after QEMU has been initialized, prior to the fuzz-loop.
     * eg: detect the memory map
     * Can be NULL
     */
    void(*pre_fuzz)(QTestState *);

    /*
     * accepts and executes an input from libfuzzer. this is repeatedly
     * executed during the fuzzing loop. Its should handle setup, input
     * execution and cleanup.
     * Cannot be NULL
     */
    void(*fuzz)(QTestState *, const unsigned char *, size_t);

} FuzzTarget;

void flush_events(QTestState *);
void reboot(QTestState *);

/* Use the QTest ASCII protocol or call address_space API directly?*/
void fuzz_qtest_set_serialize(bool option);

/*
 * makes a copy of *target and adds it to the target-list.
 * i.e. fine to set up target on the caller's stack
 */
void fuzz_add_target(const FuzzTarget *target);

int LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size);
int LLVMFuzzerInitialize(int *argc, char ***argv, char ***envp);

#endif

