/*
 *  qemu bsd user main
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *  Copyright (c) 2013-14 Stacey Son
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <sys/resource.h>
#include <sys/sysctl.h>

#include "qemu/help-texts.h"
#include "qemu/units.h"
#include "qemu/accel.h"
#include "qemu-version.h"
#include <machine/trap.h>

#include "qapi/error.h"
#include "qemu.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/path.h"
#include "qemu/help_option.h"
#include "qemu/module.h"
#include "qemu/plugin.h"
#include "user/guest-base.h"
#include "user/page-protection.h"
#include "accel/accel-ops.h"
#include "tcg/startup.h"
#include "qemu/timer.h"
#include "qemu/envlist.h"
#include "qemu/cutils.h"
#include "exec/log.h"
#include "trace/control.h"
#include "crypto/init.h"
#include "qemu/guest-random.h"
#include "gdbstub/user.h"
#include "exec/page-vary.h"

#include "host-os.h"
#include "target_arch_cpu.h"


/*
 * TODO: Remove these and rely only on qemu_real_host_page_size().
 */
uintptr_t qemu_host_page_size;
intptr_t qemu_host_page_mask;

static bool opt_one_insn_per_tb;
static unsigned long opt_tb_size;
uintptr_t guest_base;
bool have_guest_base;
/*
 * When running 32-on-64 we should make sure we can fit all of the possible
 * guest address space into a contiguous chunk of virtual host memory.
 *
 * This way we will never overlap with our own libraries or binaries or stack
 * or anything else that QEMU maps.
 *
 * Many cpus reserve the high bit (or more than one for some 64-bit cpus)
 * of the address for the kernel.  Some cpus rely on this and user space
 * uses the high bit(s) for pointer tagging and the like.  For them, we
 * must preserve the expected address space.
 */
#ifndef MAX_RESERVED_VA
# if HOST_LONG_BITS > TARGET_VIRT_ADDR_SPACE_BITS
#  if TARGET_VIRT_ADDR_SPACE_BITS == 32 && \
      (TARGET_LONG_BITS == 32 || defined(TARGET_ABI32))
#   define MAX_RESERVED_VA(CPU)  0xfffffffful
#  else
#   define MAX_RESERVED_VA(CPU)  ((1ul << TARGET_VIRT_ADDR_SPACE_BITS) - 1)
#  endif
# else
#  define MAX_RESERVED_VA(CPU)  0
# endif
#endif

unsigned long reserved_va;
unsigned long guest_addr_max;

const char *interp_prefix = CONFIG_QEMU_INTERP_PREFIX;
const char *qemu_uname_release;

unsigned long target_maxtsiz = TARGET_MAXTSIZ;   /* max text size */
unsigned long target_dfldsiz = TARGET_DFLDSIZ;   /* initial data size limit */
unsigned long target_maxdsiz = TARGET_MAXDSIZ;   /* max data size */
unsigned long target_dflssiz = TARGET_DFLSSIZ;   /* initial data size limit */
unsigned long target_maxssiz = TARGET_MAXSSIZ;   /* max stack size */
unsigned long target_sgrowsiz = TARGET_SGROWSIZ; /* amount to grow stack */

/* Helper routines for implementing atomic operations. */

void fork_start(void)
{
    start_exclusive();
    mmap_fork_start();
    cpu_list_lock();
    qemu_plugin_user_prefork_lock();
    gdbserver_fork_start();
}

void fork_end(pid_t pid)
{
    bool child = pid == 0;

    qemu_plugin_user_postfork(child);
    mmap_fork_end(child);
    if (child) {
        CPUState *cpu, *next_cpu;
        /*
         * Child processes created by fork() only have a single thread.
         * Discard information about the parent threads.
         */
        CPU_FOREACH_SAFE(cpu, next_cpu) {
            if (cpu != thread_cpu) {
                QTAILQ_REMOVE_RCU(&cpus_queue, cpu, node);
            }
        }
        qemu_init_cpu_list();
        get_task_state(thread_cpu)->ts_tid = qemu_get_thread_id();
    } else {
        cpu_list_unlock();
    }
    gdbserver_fork_end(thread_cpu, pid);
    /*
     * qemu_init_cpu_list() reinitialized the child exclusive state, but we
     * also need to keep current_cpu consistent, so call end_exclusive() for
     * both child and parent.
     */
    end_exclusive();
}

void cpu_loop(CPUArchState *env)
{
    target_cpu_loop(env);
}

static void usage(void)
{
    printf("qemu-" TARGET_NAME " version " QEMU_FULL_VERSION
           "\n" QEMU_COPYRIGHT "\n"
           "usage: qemu-" TARGET_NAME " [options] program [arguments...]\n"
           "BSD CPU emulator (compiled for %s emulation)\n"
           "\n"
           "Standard options:\n"
           "-h                print this help\n"
           "-g port           wait gdb connection to port\n"
           "-L path           set the elf interpreter prefix (default=%s)\n"
           "-s size           set the stack size in bytes (default=%ld)\n"
           "-cpu model        select CPU (-cpu help for list)\n"
           "-drop-ld-preload  drop LD_PRELOAD for target process\n"
           "-E var=value      sets/modifies targets environment variable(s)\n"
           "-U var            unsets targets environment variable(s)\n"
           "-B address        set guest_base address to address\n"
           "\n"
           "Debug options:\n"
           "-d item1[,...]    enable logging of specified items\n"
           "                  (use '-d help' for a list of log items)\n"
           "-D logfile        write logs to 'logfile' (default stderr)\n"
           "-one-insn-per-tb  run with one guest instruction per emulated TB\n"
           "-tb-size size     TCG translation block cache size\n"
           "-strace           log system calls\n"
           "-trace            [[enable=]<pattern>][,events=<file>][,file=<file>]\n"
           "                  specify tracing options\n"
#ifdef CONFIG_PLUGIN
           "-plugin           [file=]<file>[,<argname>=<argvalue>]\n"
#endif
           "\n"
           "Environment variables:\n"
           "QEMU_STRACE       Print system calls and arguments similar to the\n"
           "                  'strace' program.  Enable by setting to any value.\n"
           "You can use -E and -U options to set/unset environment variables\n"
           "for target process.  It is possible to provide several variables\n"
           "by repeating the option.  For example:\n"
           "    -E var1=val2 -E var2=val2 -U LD_PRELOAD -U LD_DEBUG\n"
           "Note that if you provide several changes to single variable\n"
           "last change will stay in effect.\n"
           "\n"
           QEMU_HELP_BOTTOM "\n"
           ,
           TARGET_NAME,
           interp_prefix,
           target_dflssiz);
    exit(1);
}

__thread CPUState *thread_cpu;

void stop_all_tasks(void)
{
    /*
     * We trust when using NPTL (pthreads) start_exclusive() handles thread
     * stopping correctly.
     */
    start_exclusive();
}

bool qemu_cpu_is_self(CPUState *cpu)
{
    return thread_cpu == cpu;
}

/* Assumes contents are already zeroed.  */
static void init_task_state(TaskState *ts)
{
    ts->sigaltstack_used = (struct target_sigaltstack) {
        .ss_sp = 0,
        .ss_size = 0,
        .ss_flags = TARGET_SS_DISABLE,
    };
}

static QemuPluginList plugins = QTAILQ_HEAD_INITIALIZER(plugins);

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void
adjust_ssize(void)
{
    struct rlimit rl;

    if (getrlimit(RLIMIT_STACK, &rl) != 0) {
        return;
    }

    target_maxssiz = MIN(target_maxssiz, rl.rlim_max);
    target_dflssiz = MIN(MAX(target_dflssiz, rl.rlim_cur), target_maxssiz);

    rl.rlim_max = target_maxssiz;
    rl.rlim_cur = target_dflssiz;
    setrlimit(RLIMIT_STACK, &rl);
}

int main(int argc, char **argv)
{
    const char *filename;
    const char *cpu_model;
    const char *cpu_type;
    const char *log_file = NULL;
    const char *log_mask = NULL;
    const char *seed_optarg = NULL;
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    struct bsd_binprm bprm;
    TaskState *ts;
    CPUArchState *env;
    CPUState *cpu;
    int optind, rv;
    const char *r;
    const char *gdbstub = NULL;
    char **target_environ, **wrk;
    envlist_t *envlist = NULL;
    char *argv0 = NULL;
    int host_page_size;
    unsigned long max_reserved_va;

    adjust_ssize();

    if (argc <= 1) {
        usage();
    }


    error_init(argv[0]);
    module_call_init(MODULE_INIT_TRACE);
    qemu_init_cpu_list();
    module_call_init(MODULE_INIT_QOM);

    envlist = envlist_create();

    /*
     * add current environment into the list
     * envlist_setenv adds to the front of the list; to preserve environ
     * order add from back to front
     */
    for (wrk = environ; *wrk != NULL; wrk++) {
        continue;
    }
    while (wrk != environ) {
        wrk--;
        (void) envlist_setenv(envlist, *wrk);
    }

    qemu_host_page_size = getpagesize();
    qemu_host_page_size = MAX(qemu_host_page_size, TARGET_PAGE_SIZE);

    cpu_model = NULL;

    qemu_add_opts(&qemu_trace_opts);
    qemu_plugin_add_opts();

    optind = 1;
    for (;;) {
        if (optind >= argc) {
            break;
        }
        r = argv[optind];
        if (r[0] != '-') {
            break;
        }
        optind++;
        r++;
        if (!strcmp(r, "-")) {
            break;
        } else if (!strcmp(r, "d")) {
            if (optind >= argc) {
                break;
            }
            log_mask = argv[optind++];
        } else if (!strcmp(r, "D")) {
            if (optind >= argc) {
                break;
            }
            log_file = argv[optind++];
        } else if (!strcmp(r, "E")) {
            r = argv[optind++];
            if (envlist_setenv(envlist, r) != 0) {
                usage();
            }
        } else if (!strcmp(r, "ignore-environment")) {
            envlist_free(envlist);
            envlist = envlist_create();
        } else if (!strcmp(r, "U")) {
            r = argv[optind++];
            if (envlist_unsetenv(envlist, r) != 0) {
                usage();
            }
        } else if (!strcmp(r, "s")) {
            r = argv[optind++];
            rv = qemu_strtoul(r, &r, 0, &target_dflssiz);
            if (rv < 0 || target_dflssiz <= 0) {
                usage();
            }
            if (*r == 'M') {
                target_dflssiz *= 1024 * 1024;
            } else if (*r == 'k' || *r == 'K') {
                target_dflssiz *= 1024;
            }
            if (target_dflssiz > target_maxssiz) {
                usage();
            }
        } else if (!strcmp(r, "L")) {
            interp_prefix = argv[optind++];
        } else if (!strcmp(r, "g")) {
            gdbstub = g_strdup(argv[optind++]);
        } else if (!strcmp(r, "r")) {
            qemu_uname_release = argv[optind++];
        } else if (!strcmp(r, "cpu")) {
            cpu_model = argv[optind++];
            if (is_help_option(cpu_model)) {
                list_cpus();
                exit(1);
            }
        } else if (!strcmp(r, "B")) {
            rv = qemu_strtoul(argv[optind++], NULL, 0, &guest_base);
            if (rv < 0) {
                usage();
            }
            have_guest_base = true;
        } else if (!strcmp(r, "drop-ld-preload")) {
            (void) envlist_unsetenv(envlist, "LD_PRELOAD");
        } else if (!strcmp(r, "seed")) {
            seed_optarg = optarg;
        } else if (!strcmp(r, "one-insn-per-tb")) {
            opt_one_insn_per_tb = true;
        } else if (!strcmp(r, "tb-size")) {
            r = argv[optind++];
            if (qemu_strtoul(r, NULL, 0, &opt_tb_size)) {
                usage();
            }
        } else if (!strcmp(r, "strace")) {
            do_strace = 1;
        } else if (!strcmp(r, "trace")) {
            trace_opt_parse(optarg);
#ifdef CONFIG_PLUGIN
        } else if (!strcmp(r, "plugin")) {
            r = argv[optind++];
            qemu_plugin_opt_parse(r, &plugins);
#endif
        } else if (!strcmp(r, "0")) {
            argv0 = argv[optind++];
        } else {
            usage();
        }
    }

    qemu_host_page_mask = -qemu_host_page_size;

    /* init debug */
    {
        int mask = 0;
        if (log_mask) {
            mask = qemu_str_to_log_mask(log_mask);
            if (!mask) {
                qemu_print_log_usage(stdout);
                exit(1);
            }
        }
        qemu_set_log_filename_flags(log_file, mask, &error_fatal);
    }

    if (optind >= argc) {
        usage();
    }
    filename = argv[optind];
    if (argv0) {
        argv[optind] = argv0;
    }

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file();
    qemu_plugin_load_list(&plugins, &error_fatal);

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* Zero bsd params */
    memset(&bprm, 0, sizeof(bprm));

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    /* Scan interp_prefix dir for replacement files. */
    init_paths(interp_prefix);

    if (cpu_model == NULL) {
        cpu_model = TARGET_DEFAULT_CPU_MODEL;
    }

    cpu_type = parse_cpu_option(cpu_model);

    /* init tcg before creating CPUs and to get qemu_host_page_size */
    {
        AccelState *accel = current_accel();
        AccelClass *ac = ACCEL_GET_CLASS(accel);

        accel_init_interfaces(ac);
        object_property_set_bool(OBJECT(accel), "one-insn-per-tb",
                                 opt_one_insn_per_tb, &error_abort);
        object_property_set_int(OBJECT(accel), "tb-size",
                                opt_tb_size, &error_abort);
        ac->init_machine(accel, NULL);
    }

    /*
     * Finalize page size before creating CPUs.
     * This will do nothing if !TARGET_PAGE_BITS_VARY.
     * The most efficient setting is to match the host.
     */
    host_page_size = qemu_real_host_page_size();
    set_preferred_target_page_bits(ctz32(host_page_size));
    finalize_target_page_bits();

    cpu = cpu_create(cpu_type);
    env = cpu_env(cpu);
    cpu_reset(cpu);
    thread_cpu = cpu;

    /*
     * Reserving too much vm space via mmap can run into problems with rlimits,
     * oom due to page table creation, etc.  We will still try it, if directed
     * by the command-line option, but not by default. Unless we're running a
     * target address space of 32 or fewer bits on a host with 64 bits.
     */
    max_reserved_va = MAX_RESERVED_VA(cpu);
    if (reserved_va != 0) {
        if ((reserved_va + 1) % host_page_size) {
            char *s = size_to_str(host_page_size);
            fprintf(stderr, "Reserved virtual address not aligned mod %s\n", s);
            g_free(s);
            exit(EXIT_FAILURE);
        }
        if (max_reserved_va && reserved_va > max_reserved_va) {
            fprintf(stderr, "Reserved virtual address too big\n");
            exit(EXIT_FAILURE);
        }
    } else if (HOST_LONG_BITS == 64 && TARGET_VIRT_ADDR_SPACE_BITS <= 32) {
        /* MAX_RESERVED_VA + 1 is a large power of 2, so is aligned. */
        reserved_va = max_reserved_va;
    }
    if (reserved_va != 0) {
        guest_addr_max = reserved_va;
    } else if (MIN(TARGET_VIRT_ADDR_SPACE_BITS, TARGET_ABI_BITS) <= 32) {
        guest_addr_max = UINT32_MAX;
    } else {
        guest_addr_max = ~0ul;
    }

    if (getenv("QEMU_STRACE")) {
        do_strace = 1;
    }

    target_environ = envlist_to_environ(envlist, NULL);
    envlist_free(envlist);

    {
        Error *err = NULL;
        if (seed_optarg != NULL) {
            qemu_guest_random_seed_main(seed_optarg, &err);
        } else {
            qcrypto_init(&err);
        }
        if (err) {
            error_reportf_err(err, "cannot initialize crypto: ");
            exit(1);
        }
    }

    /*
     * Now that page sizes are configured we can do
     * proper page alignment for guest_base.
     */
    if (have_guest_base) {
        if (guest_base & ~qemu_host_page_mask) {
            error_report("Selected guest base not host page aligned");
            exit(1);
        }
    }

    /*
     * If reserving host virtual address space, do so now.
     * Combined with '-B', ensure that the chosen range is free.
     */
    if (reserved_va) {
        void *p;

        if (have_guest_base) {
            p = mmap((void *)guest_base, reserved_va + 1, PROT_NONE,
                     MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_EXCL, -1, 0);
        } else {
            p = mmap(NULL, reserved_va + 1, PROT_NONE,
                     MAP_ANON | MAP_PRIVATE, -1, 0);
        }
        if (p == MAP_FAILED) {
            const char *err = strerror(errno);
            char *sz = size_to_str(reserved_va + 1);

            if (have_guest_base) {
                error_report("Cannot allocate %s bytes at -B %p for guest "
                             "address space: %s", sz, (void *)guest_base, err);
            } else {
                error_report("Cannot allocate %s bytes for guest "
                             "address space: %s", sz, err);
            }
            exit(1);
        }
        guest_base = (uintptr_t)p;
        have_guest_base = true;

        /* Ensure that mmap_next_start is within range. */
        if (reserved_va <= mmap_next_start) {
            mmap_next_start = (reserved_va / 4 * 3)
                              & TARGET_PAGE_MASK & qemu_host_page_mask;
        }
    }

    if (loader_exec(filename, argv + optind, target_environ, regs, info,
                    &bprm) != 0) {
        printf("Error loading %s\n", filename);
        _exit(1);
    }

    for (wrk = target_environ; *wrk; wrk++) {
        g_free(*wrk);
    }

    g_free(target_environ);

    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        FILE *f = qemu_log_trylock();
        if (f) {
            fprintf(f, "guest_base  %p\n", (void *)guest_base);
            fprintf(f, "page layout changed following binary load\n");
            page_dump(f);

            fprintf(f, "end_code    0x" TARGET_ABI_FMT_lx "\n",
                    info->end_code);
            fprintf(f, "start_code  0x" TARGET_ABI_FMT_lx "\n",
                    info->start_code);
            fprintf(f, "start_data  0x" TARGET_ABI_FMT_lx "\n",
                    info->start_data);
            fprintf(f, "end_data    0x" TARGET_ABI_FMT_lx "\n",
                    info->end_data);
            fprintf(f, "start_stack 0x" TARGET_ABI_FMT_lx "\n",
                    info->start_stack);
            fprintf(f, "brk         0x" TARGET_ABI_FMT_lx "\n", info->brk);
            fprintf(f, "entry       0x" TARGET_ABI_FMT_lx "\n", info->entry);

            qemu_log_unlock(f);
        }
    }

    /* build Task State */
    ts = g_new0(TaskState, 1);
    init_task_state(ts);
    ts->info = info;
    ts->bprm = &bprm;
    ts->ts_tid = qemu_get_thread_id();
    cpu->opaque = ts;

    target_set_brk(info->brk);
    syscall_init();
    signal_init();

    /*
     * Now that we've loaded the binary, GUEST_BASE is fixed.  Delay
     * generating the prologue until now so that the prologue can take
     * the real value of GUEST_BASE into account.
     */
    tcg_prologue_init();

    target_cpu_init(env, regs);

    if (gdbstub) {
        gdbserver_start(gdbstub, &error_fatal);
    }
    cpu_loop(env);
    /* never exits */
    return 0;
}
