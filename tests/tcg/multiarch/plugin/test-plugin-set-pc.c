/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2026, Florian Hofhammer <florian.hofhammer@epfl.ch>
 *
 * This test set exercises the qemu_plugin_set_pc() function in four different
 * contexts:
 * 1. in an instruction callback during normal execution,
 * 2. in an instruction callback during signal handling,
 * 3. in a memory access callback.
 * 4. in a syscall callback,
 */
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* If we issue this magic syscall, ... */
#define MAGIC_SYSCALL 4096
/* ... the plugin either jumps directly to the target address ... */
#define SETPC 0
/* ... or just updates the target address for future use in callbacks. */
#define SETTARGET 1

static int signal_handled;

void panic(const char *msg)
{
    fprintf(stderr, "Panic: %s\n", msg);
    abort();
}

/*
 * This test executes a magic syscall which communicates two addresses to the
 * plugin via the syscall arguments. Whenever we reach the "bad" instruction
 * during normal execution, the plugin should redirect control flow to the
 * "good" instruction instead.
 */
void test_insn(void)
{
    long ret = syscall(MAGIC_SYSCALL, SETTARGET, &&bad_insn, &&good_insn,
                       NULL);
    assert(ret == 0 && "Syscall filter did not return expected value");
bad_insn:
    panic("PC redirection in instruction callback failed");
good_insn:
    puts("PC redirection in instruction callback succeeded");
}

/*
 * This signal handler communicates a "bad" and a "good" address to the plugin
 * similar to the previous test, and skips to the "good" address when the "bad"
 * one is reached. This serves to test whether PC redirection via
 * qemu_plugin_set_pc() also works properly in a signal handler context.
 */
void usr1_handler(int signum)
{
    long ret = syscall(MAGIC_SYSCALL, SETTARGET, &&bad_signal, &&good_signal,
                       NULL);
    assert(ret == 0 && "Syscall filter did not return expected value");
bad_signal:
    panic("PC redirection in instruction callback failed");
good_signal:
    signal_handled = 1;
    puts("PC redirection in instruction callback succeeded");
}

/*
 * This test sends a signal to the process, which should trigger the above
 * signal handler. The signal handler should then exercise the PC redirection
 * functionality in the context of a signal handler, which behaves a bit
 * differently from normal execution.
 */
void test_sighandler(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = usr1_handler;
    sigaction(SIGUSR1, &sa, NULL);
    pid_t pid = getpid();
    kill(pid, SIGUSR1);
    assert(signal_handled == 1 && "Signal handler was not executed properly");
}

/*
 * This test communicates a "good" address and the address of a local variable
 * to the plugin. Upon accessing the local variable, the plugin should then
 * redirect control flow to the "good" address via qemu_plugin_set_pc().
 */
void test_mem(void)
{
    static uint32_t test = 1;
    long ret = syscall(MAGIC_SYSCALL, SETTARGET, NULL, &&good_mem, &test);
    assert(ret == 0 && "Syscall filter did not return expected value");
    /* Ensure read access to the variable to trigger the plugin callback */
    assert(test == 1);
    panic("PC redirection in memory access callback failed");
good_mem:
    puts("PC redirection in memory access callback succeeded");
}

/*
 * This test executes a magic syscall which is intercepted and its actual
 * execution skipped via the qemu_plugin_set_pc() API. In a proper plugin,
 * syscall skipping would rather be implemented via the syscall filtering
 * callback, but we want to make sure qemu_plugin_set_pc() works in different
 * contexts.
 */
__attribute__((noreturn))
void test_syscall(void)
{
    syscall(MAGIC_SYSCALL, SETPC, &&good_syscall);
    panic("PC redirection in syscall callback failed");
good_syscall:
    /*
     * Note: we execute this test last and exit straight from here because when
     * the plugin redirects control flow upon syscall, the stack frame for the
     * syscall function (and potential other functions in the call chain in
     * libc) is still live and the stack is not unwound properly. Thus,
     * returning from here is risky and breaks on some architectures, so we
     * just exit directly from this test.
     */
    _exit(EXIT_SUCCESS);
}


int main(int argc, char *argv[])
{
    test_insn();
    test_sighandler();
    test_mem();
    test_syscall();
}
