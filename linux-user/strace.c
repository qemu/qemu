#include <stdio.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sched.h>
#include "qemu.h"

int do_strace=0;

struct syscallname {
    int nr;
    const char *name;
    const char *format;
    void (*call)(const struct syscallname *,
                 abi_long, abi_long, abi_long,
                 abi_long, abi_long, abi_long);
    void (*result)(const struct syscallname *, abi_long);
};

#ifdef __GNUC__
/*
 * It is possible that target doesn't have syscall that uses
 * following flags but we don't want the compiler to warn
 * us about them being unused.  Same applies to utility print
 * functions.  It is ok to keep them while not used.
 */
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif

/*
 * Structure used to translate flag values into strings.  This is
 * similar that is in the actual strace tool.
 */
struct flags {
    abi_long    f_value;  /* flag */
    const char  *f_string; /* stringified flag */
};

/* common flags for all architectures */
#define FLAG_GENERIC(name) { name, #name }
/* target specific flags (syscall_defs.h has TARGET_<flag>) */
#define FLAG_TARGET(name)  { TARGET_ ## name, #name }
/* end of flags array */
#define FLAG_END           { 0, NULL }

UNUSED static const char *get_comma(int);
UNUSED static void print_pointer(abi_long, int);
UNUSED static void print_flags(const struct flags *, abi_long, int);
UNUSED static void print_at_dirfd(abi_long, int);
UNUSED static void print_file_mode(abi_long, int);
UNUSED static void print_open_flags(abi_long, int);
UNUSED static void print_syscall_prologue(const struct syscallname *);
UNUSED static void print_syscall_epilogue(const struct syscallname *);
UNUSED static void print_string(abi_long, int);
UNUSED static void print_raw_param(const char *, abi_long, int);
UNUSED static void print_timeval(abi_ulong, int);
UNUSED static void print_number(abi_long, int);
UNUSED static void print_signal(abi_ulong, int);

/*
 * Utility functions
 */
static void
print_ipc_cmd(int cmd)
{
#define output_cmd(val) \
if( cmd == val ) { \
    gemu_log(#val); \
    return; \
}

    cmd &= 0xff;

    /* General IPC commands */
    output_cmd( IPC_RMID );
    output_cmd( IPC_SET );
    output_cmd( IPC_STAT );
    output_cmd( IPC_INFO );
    /* msgctl() commands */
    #ifdef __USER_MISC
    output_cmd( MSG_STAT );
    output_cmd( MSG_INFO );
    #endif
    /* shmctl() commands */
    output_cmd( SHM_LOCK );
    output_cmd( SHM_UNLOCK );
    output_cmd( SHM_STAT );
    output_cmd( SHM_INFO );
    /* semctl() commands */
    output_cmd( GETPID );
    output_cmd( GETVAL );
    output_cmd( GETALL );
    output_cmd( GETNCNT );
    output_cmd( GETZCNT );
    output_cmd( SETVAL );
    output_cmd( SETALL );
    output_cmd( SEM_STAT );
    output_cmd( SEM_INFO );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );
    output_cmd( IPC_RMID );

    /* Some value we don't recognize */
    gemu_log("%d",cmd);
}

static void
print_signal(abi_ulong arg, int last)
{
    const char *signal_name = NULL;
    switch(arg) {
    case TARGET_SIGHUP: signal_name = "SIGHUP"; break;
    case TARGET_SIGINT: signal_name = "SIGINT"; break;
    case TARGET_SIGQUIT: signal_name = "SIGQUIT"; break;
    case TARGET_SIGILL: signal_name = "SIGILL"; break;
    case TARGET_SIGABRT: signal_name = "SIGABRT"; break;
    case TARGET_SIGFPE: signal_name = "SIGFPE"; break;
    case TARGET_SIGKILL: signal_name = "SIGKILL"; break;
    case TARGET_SIGSEGV: signal_name = "SIGSEGV"; break;
    case TARGET_SIGPIPE: signal_name = "SIGPIPE"; break;
    case TARGET_SIGALRM: signal_name = "SIGALRM"; break;
    case TARGET_SIGTERM: signal_name = "SIGTERM"; break;
    case TARGET_SIGUSR1: signal_name = "SIGUSR1"; break;
    case TARGET_SIGUSR2: signal_name = "SIGUSR2"; break;
    case TARGET_SIGCHLD: signal_name = "SIGCHLD"; break;
    case TARGET_SIGCONT: signal_name = "SIGCONT"; break;
    case TARGET_SIGSTOP: signal_name = "SIGSTOP"; break;
    case TARGET_SIGTTIN: signal_name = "SIGTTIN"; break;
    case TARGET_SIGTTOU: signal_name = "SIGTTOU"; break;
    }
    if (signal_name == NULL) {
        print_raw_param("%ld", arg, 1);
        return;
    }
    gemu_log("%s%s", signal_name, get_comma(last));
}

#ifdef TARGET_NR__newselect
static void
print_fdset(int n, abi_ulong target_fds_addr)
{
    int i;

    gemu_log("[");
    if( target_fds_addr ) {
        abi_long *target_fds;

        target_fds = lock_user(VERIFY_READ,
                               target_fds_addr,
                               sizeof(*target_fds)*(n / TARGET_ABI_BITS + 1),
                               1);

        if (!target_fds)
            return;

        for (i=n; i>=0; i--) {
            if ((tswapl(target_fds[i / TARGET_ABI_BITS]) >> (i & (TARGET_ABI_BITS - 1))) & 1)
                gemu_log("%d,", i );
            }
        unlock_user(target_fds, target_fds_addr, 0);
    }
    gemu_log("]");
}
#endif

/*
 * Sysycall specific output functions
 */

/* select */
#ifdef TARGET_NR__newselect
static long newselect_arg1 = 0;
static long newselect_arg2 = 0;
static long newselect_arg3 = 0;
static long newselect_arg4 = 0;
static long newselect_arg5 = 0;

static void
print_newselect(const struct syscallname *name,
                abi_long arg1, abi_long arg2, abi_long arg3,
                abi_long arg4, abi_long arg5, abi_long arg6)
{
    gemu_log("%s(" TARGET_ABI_FMT_ld ",", name->name, arg1);
    print_fdset(arg1, arg2);
    gemu_log(",");
    print_fdset(arg1, arg3);
    gemu_log(",");
    print_fdset(arg1, arg4);
    gemu_log(",");
    print_timeval(arg5, 1);
    gemu_log(")");

    /* save for use in the return output function below */
    newselect_arg1=arg1;
    newselect_arg2=arg2;
    newselect_arg3=arg3;
    newselect_arg4=arg4;
    newselect_arg5=arg5;
}
#endif

#ifdef TARGET_NR_semctl
static void
print_semctl(const struct syscallname *name,
             abi_long arg1, abi_long arg2, abi_long arg3,
             abi_long arg4, abi_long arg5, abi_long arg6)
{
    gemu_log("%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ",", name->name, arg1, arg2);
    print_ipc_cmd(arg3);
    gemu_log(",0x" TARGET_ABI_FMT_lx ")", arg4);
}
#endif

static void
print_execve(const struct syscallname *name,
             abi_long arg1, abi_long arg2, abi_long arg3,
             abi_long arg4, abi_long arg5, abi_long arg6)
{
    abi_ulong arg_ptr_addr;
    char *s;

    if (!(s = lock_user_string(arg1)))
        return;
    gemu_log("%s(\"%s\",{", name->name, s);
    unlock_user(s, arg1, 0);

    for (arg_ptr_addr = arg2; ; arg_ptr_addr += sizeof(abi_ulong)) {
        abi_ulong *arg_ptr, arg_addr;

	arg_ptr = lock_user(VERIFY_READ, arg_ptr_addr, sizeof(abi_ulong), 1);
        if (!arg_ptr)
            return;
	arg_addr = tswapl(*arg_ptr);
	unlock_user(arg_ptr, arg_ptr_addr, 0);
        if (!arg_addr)
            break;
        if ((s = lock_user_string(arg_addr))) {
            gemu_log("\"%s\",", s);
            unlock_user(s, arg_addr, 0);
        }
    }

    gemu_log("NULL})");
}

#ifdef TARGET_NR_ipc
static void
print_ipc(const struct syscallname *name,
          abi_long arg1, abi_long arg2, abi_long arg3,
          abi_long arg4, abi_long arg5, abi_long arg6)
{
    switch(arg1) {
    case IPCOP_semctl:
        gemu_log("semctl(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ",", arg1, arg2);
        print_ipc_cmd(arg3);
        gemu_log(",0x" TARGET_ABI_FMT_lx ")", arg4);
        break;
    default:
        gemu_log("%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ")",
                 name->name, arg1, arg2, arg3, arg4);
    }
}
#endif

/*
 * Variants for the return value output function
 */

static void
print_syscall_ret_addr(const struct syscallname *name, abi_long ret)
{
if( ret == -1 ) {
        gemu_log(" = -1 errno=%d (%s)\n", errno, target_strerror(errno));
    } else {
        gemu_log(" = 0x" TARGET_ABI_FMT_lx "\n", ret);
    }
}

#if 0 /* currently unused */
static void
print_syscall_ret_raw(struct syscallname *name, abi_long ret)
{
        gemu_log(" = 0x" TARGET_ABI_FMT_lx "\n", ret);
}
#endif

#ifdef TARGET_NR__newselect
static void
print_syscall_ret_newselect(const struct syscallname *name, abi_long ret)
{
    gemu_log(" = 0x" TARGET_ABI_FMT_lx " (", ret);
    print_fdset(newselect_arg1,newselect_arg2);
    gemu_log(",");
    print_fdset(newselect_arg1,newselect_arg3);
    gemu_log(",");
    print_fdset(newselect_arg1,newselect_arg4);
    gemu_log(",");
    print_timeval(newselect_arg5, 1);
    gemu_log(")\n");
}
#endif

UNUSED static struct flags access_flags[] = {
    FLAG_GENERIC(F_OK),
    FLAG_GENERIC(R_OK),
    FLAG_GENERIC(W_OK),
    FLAG_GENERIC(X_OK),
    FLAG_END,
};

UNUSED static struct flags at_file_flags[] = {
#ifdef AT_EACCESS
    FLAG_GENERIC(AT_EACCESS),
#endif
#ifdef AT_SYMLINK_NOFOLLOW
    FLAG_GENERIC(AT_SYMLINK_NOFOLLOW),
#endif
    FLAG_END,
};

UNUSED static struct flags unlinkat_flags[] = {
#ifdef AT_REMOVEDIR
    FLAG_GENERIC(AT_REMOVEDIR),
#endif
    FLAG_END,
};

UNUSED static struct flags mode_flags[] = {
    FLAG_GENERIC(S_IFSOCK),
    FLAG_GENERIC(S_IFLNK),
    FLAG_GENERIC(S_IFREG),
    FLAG_GENERIC(S_IFBLK),
    FLAG_GENERIC(S_IFDIR),
    FLAG_GENERIC(S_IFCHR),
    FLAG_GENERIC(S_IFIFO),
    FLAG_END,
};

UNUSED static struct flags open_access_flags[] = {
    FLAG_TARGET(O_RDONLY),
    FLAG_TARGET(O_WRONLY),
    FLAG_TARGET(O_RDWR),
    FLAG_END,
};

UNUSED static struct flags open_flags[] = {
    FLAG_TARGET(O_APPEND),
    FLAG_TARGET(O_CREAT),
    FLAG_TARGET(O_DIRECTORY),
    FLAG_TARGET(O_EXCL),
    FLAG_TARGET(O_LARGEFILE),
    FLAG_TARGET(O_NOCTTY),
    FLAG_TARGET(O_NOFOLLOW),
    FLAG_TARGET(O_NONBLOCK),      /* also O_NDELAY */
    FLAG_TARGET(O_SYNC),
    FLAG_TARGET(O_TRUNC),
#ifdef O_DIRECT
    FLAG_TARGET(O_DIRECT),
#endif
    FLAG_END,
};

UNUSED static struct flags mount_flags[] = {
#ifdef MS_BIND
    FLAG_GENERIC(MS_BIND),
#endif
#ifdef MS_DIRSYNC
    FLAG_GENERIC(MS_DIRSYNC),
#endif
    FLAG_GENERIC(MS_MANDLOCK),
#ifdef MS_MOVE
    FLAG_GENERIC(MS_MOVE),
#endif
    FLAG_GENERIC(MS_NOATIME),
    FLAG_GENERIC(MS_NODEV),
    FLAG_GENERIC(MS_NODIRATIME),
    FLAG_GENERIC(MS_NOEXEC),
    FLAG_GENERIC(MS_NOSUID),
    FLAG_GENERIC(MS_RDONLY),
#ifdef MS_RELATIME
    FLAG_GENERIC(MS_RELATIME),
#endif
    FLAG_GENERIC(MS_REMOUNT),
    FLAG_GENERIC(MS_SYNCHRONOUS),
    FLAG_END,
};

UNUSED static struct flags umount2_flags[] = {
#ifdef MNT_FORCE
    FLAG_GENERIC(MNT_FORCE),
#endif
#ifdef MNT_DETACH
    FLAG_GENERIC(MNT_DETACH),
#endif
#ifdef MNT_EXPIRE
    FLAG_GENERIC(MNT_EXPIRE),
#endif
    FLAG_END,
};

UNUSED static struct flags mmap_prot_flags[] = {
    FLAG_GENERIC(PROT_NONE),
    FLAG_GENERIC(PROT_EXEC),
    FLAG_GENERIC(PROT_READ),
    FLAG_GENERIC(PROT_WRITE),
    FLAG_TARGET(PROT_SEM),
    FLAG_GENERIC(PROT_GROWSDOWN),
    FLAG_GENERIC(PROT_GROWSUP),
    FLAG_END,
};

UNUSED static struct flags mmap_flags[] = {
    FLAG_TARGET(MAP_SHARED),
    FLAG_TARGET(MAP_PRIVATE),
    FLAG_TARGET(MAP_ANONYMOUS),
    FLAG_TARGET(MAP_DENYWRITE),
    FLAG_TARGET(MAP_FIXED),
    FLAG_TARGET(MAP_GROWSDOWN),
    FLAG_TARGET(MAP_EXECUTABLE),
#ifdef MAP_LOCKED
    FLAG_TARGET(MAP_LOCKED),
#endif
#ifdef MAP_NONBLOCK
    FLAG_TARGET(MAP_NONBLOCK),
#endif
    FLAG_TARGET(MAP_NORESERVE),
#ifdef MAP_POPULATE
    FLAG_TARGET(MAP_POPULATE),
#endif
#ifdef TARGET_MAP_UNINITIALIZED
    FLAG_TARGET(MAP_UNINITIALIZED),
#endif
    FLAG_END,
};

UNUSED static struct flags fcntl_flags[] = {
    FLAG_TARGET(F_DUPFD),
    FLAG_TARGET(F_GETFD),
    FLAG_TARGET(F_SETFD),
    FLAG_TARGET(F_GETFL),
    FLAG_TARGET(F_SETFL),
    FLAG_TARGET(F_GETLK),
    FLAG_TARGET(F_SETLK),
    FLAG_TARGET(F_SETLKW),
    FLAG_END,
};

UNUSED static struct flags clone_flags[] = {
    FLAG_GENERIC(CLONE_VM),
    FLAG_GENERIC(CLONE_FS),
    FLAG_GENERIC(CLONE_FILES),
    FLAG_GENERIC(CLONE_SIGHAND),
    FLAG_GENERIC(CLONE_PTRACE),
    FLAG_GENERIC(CLONE_VFORK),
    FLAG_GENERIC(CLONE_PARENT),
    FLAG_GENERIC(CLONE_THREAD),
    FLAG_GENERIC(CLONE_NEWNS),
    FLAG_GENERIC(CLONE_SYSVSEM),
    FLAG_GENERIC(CLONE_SETTLS),
    FLAG_GENERIC(CLONE_PARENT_SETTID),
    FLAG_GENERIC(CLONE_CHILD_CLEARTID),
    FLAG_GENERIC(CLONE_DETACHED),
    FLAG_GENERIC(CLONE_UNTRACED),
    FLAG_GENERIC(CLONE_CHILD_SETTID),
#if defined(CLONE_NEWUTS)
    FLAG_GENERIC(CLONE_NEWUTS),
#endif
#if defined(CLONE_NEWIPC)
    FLAG_GENERIC(CLONE_NEWIPC),
#endif
#if defined(CLONE_NEWUSER)
    FLAG_GENERIC(CLONE_NEWUSER),
#endif
#if defined(CLONE_NEWPID)
    FLAG_GENERIC(CLONE_NEWPID),
#endif
#if defined(CLONE_NEWNET)
    FLAG_GENERIC(CLONE_NEWNET),
#endif
#if defined(CLONE_IO)
    FLAG_GENERIC(CLONE_IO),
#endif
    FLAG_END,
};

/*
 * print_xxx utility functions.  These are used to print syscall
 * parameters in certain format.  All of these have parameter
 * named 'last'.  This parameter is used to add comma to output
 * when last == 0.
 */

static const char *
get_comma(int last)
{
    return ((last) ? "" : ",");
}

static void
print_flags(const struct flags *f, abi_long flags, int last)
{
    const char *sep = "";
    int n;

    if ((flags == 0) && (f->f_value == 0)) {
        gemu_log("%s%s", f->f_string, get_comma(last));
        return;
    }
    for (n = 0; f->f_string != NULL; f++) {
        if ((f->f_value != 0) && ((flags & f->f_value) == f->f_value)) {
            gemu_log("%s%s", sep, f->f_string);
            flags &= ~f->f_value;
            sep = "|";
            n++;
        }
    }

    if (n > 0) {
        /* print rest of the flags as numeric */
        if (flags != 0) {
            gemu_log("%s%#x%s", sep, (unsigned int)flags, get_comma(last));
        } else {
            gemu_log("%s", get_comma(last));
        }
    } else {
        /* no string version of flags found, print them in hex then */
        gemu_log("%#x%s", (unsigned int)flags, get_comma(last));
    }
}

static void
print_at_dirfd(abi_long dirfd, int last)
{
#ifdef AT_FDCWD
    if (dirfd == AT_FDCWD) {
        gemu_log("AT_FDCWD%s", get_comma(last));
        return;
    }
#endif
    gemu_log("%d%s", (int)dirfd, get_comma(last));
}

static void
print_file_mode(abi_long mode, int last)
{
    const char *sep = "";
    const struct flags *m;

    for (m = &mode_flags[0]; m->f_string != NULL; m++) {
        if ((m->f_value & mode) == m->f_value) {
            gemu_log("%s%s", m->f_string, sep);
            sep = "|";
            mode &= ~m->f_value;
            break;
        }
    }

    mode &= ~S_IFMT;
    /* print rest of the mode as octal */
    if (mode != 0)
        gemu_log("%s%#o", sep, (unsigned int)mode);

    gemu_log("%s", get_comma(last));
}

static void
print_open_flags(abi_long flags, int last)
{
    print_flags(open_access_flags, flags & TARGET_O_ACCMODE, 1);
    flags &= ~TARGET_O_ACCMODE;
    if (flags == 0) {
        gemu_log("%s", get_comma(last));
        return;
    }
    gemu_log("|");
    print_flags(open_flags, flags, last);
}

static void
print_syscall_prologue(const struct syscallname *sc)
{
    gemu_log("%s(", sc->name);
}

/*ARGSUSED*/
static void
print_syscall_epilogue(const struct syscallname *sc)
{
    (void)sc;
    gemu_log(")");
}

static void
print_string(abi_long addr, int last)
{
    char *s;

    if ((s = lock_user_string(addr)) != NULL) {
        gemu_log("\"%s\"%s", s, get_comma(last));
        unlock_user(s, addr, 0);
    } else {
        /* can't get string out of it, so print it as pointer */
        print_pointer(addr, last);
    }
}

/*
 * Prints out raw parameter using given format.  Caller needs
 * to do byte swapping if needed.
 */
static void
print_raw_param(const char *fmt, abi_long param, int last)
{
    char format[64];

    (void) snprintf(format, sizeof (format), "%s%s", fmt, get_comma(last));
    gemu_log(format, param);
}

static void
print_pointer(abi_long p, int last)
{
    if (p == 0)
        gemu_log("NULL%s", get_comma(last));
    else
        gemu_log("0x" TARGET_ABI_FMT_lx "%s", p, get_comma(last));
}

/*
 * Reads 32-bit (int) number from guest address space from
 * address 'addr' and prints it.
 */
static void
print_number(abi_long addr, int last)
{
    if (addr == 0) {
        gemu_log("NULL%s", get_comma(last));
    } else {
        int num;

        get_user_s32(num, addr);
        gemu_log("[%d]%s", num, get_comma(last));
    }
}

static void
print_timeval(abi_ulong tv_addr, int last)
{
    if( tv_addr ) {
        struct target_timeval *tv;

        tv = lock_user(VERIFY_READ, tv_addr, sizeof(*tv), 1);
        if (!tv)
            return;
        gemu_log("{" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "}%s",
            tv->tv_sec, tv->tv_usec, get_comma(last));
        unlock_user(tv, tv_addr, 0);
    } else
        gemu_log("NULL%s", get_comma(last));
}

#undef UNUSED

#ifdef TARGET_NR_accept
static void
print_accept(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_pointer(arg1, 0);
    print_number(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_access
static void
print_access(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_flags(access_flags, arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_brk
static void
print_brk(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_pointer(arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_chdir
static void
print_chdir(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_chmod
static void
print_chmod(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_file_mode(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_clone
static void
print_clone(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
#if defined(TARGET_M68K)
    print_flags(clone_flags, arg0, 0);
    print_raw_param("newsp=0x" TARGET_ABI_FMT_lx, arg1, 1);
#elif defined(TARGET_SH4) || defined(TARGET_ALPHA)
    print_flags(clone_flags, arg0, 0);
    print_raw_param("child_stack=0x" TARGET_ABI_FMT_lx, arg1, 0);
    print_raw_param("parent_tidptr=0x" TARGET_ABI_FMT_lx, arg2, 0);
    print_raw_param("child_tidptr=0x" TARGET_ABI_FMT_lx, arg3, 0);
    print_raw_param("tls=0x" TARGET_ABI_FMT_lx, arg4, 1);
#elif defined(TARGET_CRIS)
    print_raw_param("child_stack=0x" TARGET_ABI_FMT_lx, arg0, 0);
    print_flags(clone_flags, arg1, 0);
    print_raw_param("parent_tidptr=0x" TARGET_ABI_FMT_lx, arg2, 0);
    print_raw_param("tls=0x" TARGET_ABI_FMT_lx, arg3, 0);
    print_raw_param("child_tidptr=0x" TARGET_ABI_FMT_lx, arg4, 1);
#else
    print_flags(clone_flags, arg0, 0);
    print_raw_param("child_stack=0x" TARGET_ABI_FMT_lx, arg1, 0);
    print_raw_param("parent_tidptr=0x" TARGET_ABI_FMT_lx, arg2, 0);
    print_raw_param("tls=0x" TARGET_ABI_FMT_lx, arg3, 0);
    print_raw_param("child_tidptr=0x" TARGET_ABI_FMT_lx, arg4, 1);
#endif
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_creat
static void
print_creat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_file_mode(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_execv
static void
print_execv(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_raw_param("0x" TARGET_ABI_FMT_lx, arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_faccessat
static void
print_faccessat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_flags(access_flags, arg2, 0);
    print_flags(at_file_flags, arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_fchmodat
static void
print_fchmodat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_file_mode(arg2, 0);
    print_flags(at_file_flags, arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_fchownat
static void
print_fchownat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_raw_param("%d", arg2, 0);
    print_raw_param("%d", arg3, 0);
    print_flags(at_file_flags, arg4, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_fcntl) || defined(TARGET_NR_fcntl64)
static void
print_fcntl(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_flags(fcntl_flags, arg1, 0);
    /*
     * TODO: check flags and print following argument only
     *       when needed.
     */
    print_pointer(arg2, 1);
    print_syscall_epilogue(name);
}
#define print_fcntl64   print_fcntl
#endif


#ifdef TARGET_NR_futimesat
static void
print_futimesat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_timeval(arg2, 0);
    print_timeval(arg2 + sizeof (struct target_timeval), 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_link
static void
print_link(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_string(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_linkat
static void
print_linkat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_at_dirfd(arg2, 0);
    print_string(arg3, 0);
    print_flags(at_file_flags, arg4, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR__llseek
static void
print__llseek(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    const char *whence = "UNKNOWN";
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_raw_param("%ld", arg1, 0);
    print_raw_param("%ld", arg2, 0);
    print_pointer(arg3, 0);
    switch(arg4) {
    case SEEK_SET: whence = "SEEK_SET"; break;
    case SEEK_CUR: whence = "SEEK_CUR"; break;
    case SEEK_END: whence = "SEEK_END"; break;
    }
    gemu_log("%s",whence);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_stat) || defined(TARGET_NR_stat64) || \
    defined(TARGET_NR_lstat) || defined(TARGET_NR_lstat64)
static void
print_stat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#define print_lstat     print_stat
#define print_stat64	print_stat
#define print_lstat64   print_stat
#endif

#if defined(TARGET_NR_fstat) || defined(TARGET_NR_fstat64)
static void
print_fstat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#define print_fstat64     print_fstat
#endif

#ifdef TARGET_NR_mkdir
static void
print_mkdir(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_file_mode(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mkdirat
static void
print_mkdirat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_file_mode(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_rmdir
static void
print_rmdir(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_rt_sigaction
static void
print_rt_sigaction(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_signal(arg0, 0);
    print_pointer(arg1, 0);
    print_pointer(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_rt_sigprocmask
static void
print_rt_sigprocmask(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    const char *how = "UNKNOWN";
    print_syscall_prologue(name);
    switch(arg0) {
    case TARGET_SIG_BLOCK: how = "SIG_BLOCK"; break;
    case TARGET_SIG_UNBLOCK: how = "SIG_UNBLOCK"; break;
    case TARGET_SIG_SETMASK: how = "SIG_SETMASK"; break;
    }
    gemu_log("%s,",how);
    print_pointer(arg1, 0);
    print_pointer(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mknod
static void
print_mknod(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    int hasdev = (arg1 & (S_IFCHR|S_IFBLK));

    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_file_mode(arg1, (hasdev == 0));
    if (hasdev) {
        print_raw_param("makedev(%d", major(arg2), 0);
        print_raw_param("%d)", minor(arg2), 1);
    }
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mknodat
static void
print_mknodat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    int hasdev = (arg2 & (S_IFCHR|S_IFBLK));

    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_file_mode(arg2, (hasdev == 0));
    if (hasdev) {
        print_raw_param("makedev(%d", major(arg3), 0);
        print_raw_param("%d)", minor(arg3), 1);
    }
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mq_open
static void
print_mq_open(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    int is_creat = (arg1 & TARGET_O_CREAT);

    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_open_flags(arg1, (is_creat == 0));
    if (is_creat) {
        print_file_mode(arg2, 0);
        print_pointer(arg3, 1);
    }
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_open
static void
print_open(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    int is_creat = (arg1 & TARGET_O_CREAT);

    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_open_flags(arg1, (is_creat == 0));
    if (is_creat)
        print_file_mode(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_openat
static void
print_openat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    int is_creat = (arg2 & TARGET_O_CREAT);

    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_open_flags(arg2, (is_creat == 0));
    if (is_creat)
        print_file_mode(arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mq_unlink
static void
print_mq_unlink(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_fstatat64) || defined(TARGET_NR_newfstatat)
static void
print_fstatat64(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_pointer(arg2, 0);
    print_flags(at_file_flags, arg3, 1);
    print_syscall_epilogue(name);
}
#define print_newfstatat    print_fstatat64
#endif

#ifdef TARGET_NR_readlink
static void
print_readlink(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 0);
    print_raw_param("%u", arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_readlinkat
static void
print_readlinkat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_pointer(arg2, 0);
    print_raw_param("%u", arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_rename
static void
print_rename(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_string(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_renameat
static void
print_renameat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_at_dirfd(arg2, 0);
    print_string(arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_statfs
static void
print_statfs(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#define print_statfs64  print_statfs
#endif

#ifdef TARGET_NR_symlink
static void
print_symlink(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_string(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_symlinkat
static void
print_symlinkat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_at_dirfd(arg1, 0);
    print_string(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mount
static void
print_mount(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_string(arg1, 0);
    print_string(arg2, 0);
    print_flags(mount_flags, arg3, 0);
    print_pointer(arg4, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_umount
static void
print_umount(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_umount2
static void
print_umount2(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_flags(umount2_flags, arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_unlink
static void
print_unlink(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_unlinkat
static void
print_unlinkat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_flags(unlinkat_flags, arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_utime
static void
print_utime(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_utimes
static void
print_utimes(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_utimensat
static void
print_utimensat(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_pointer(arg2, 0);
    print_flags(at_file_flags, arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_mmap) || defined(TARGET_NR_mmap2)
static void
print_mmap(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_flags(mmap_prot_flags, arg2, 0);
    print_flags(mmap_flags, arg3, 0);
    print_raw_param("%d", arg4, 0);
    print_raw_param("%#x", arg5, 1);
    print_syscall_epilogue(name);
}
#define print_mmap2     print_mmap
#endif

#ifdef TARGET_NR_mprotect
static void
print_mprotect(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_flags(mmap_prot_flags, arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_munmap
static void
print_munmap(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_raw_param("%d", arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_futex
static void print_futex_op(abi_long tflag, int last)
{
#define print_op(val) \
if( cmd == val ) { \
    gemu_log(#val); \
    return; \
}

    int cmd = (int)tflag;
#ifdef FUTEX_PRIVATE_FLAG
    if (cmd & FUTEX_PRIVATE_FLAG) {
        gemu_log("FUTEX_PRIVATE_FLAG|");
        cmd &= ~FUTEX_PRIVATE_FLAG;
    }
#endif
    print_op(FUTEX_WAIT)
    print_op(FUTEX_WAKE)
    print_op(FUTEX_FD)
    print_op(FUTEX_REQUEUE)
    print_op(FUTEX_CMP_REQUEUE)
    print_op(FUTEX_WAKE_OP)
    print_op(FUTEX_LOCK_PI)
    print_op(FUTEX_UNLOCK_PI)
    print_op(FUTEX_TRYLOCK_PI)
#ifdef FUTEX_WAIT_BITSET
    print_op(FUTEX_WAIT_BITSET)
#endif
#ifdef FUTEX_WAKE_BITSET
    print_op(FUTEX_WAKE_BITSET)
#endif
    /* unknown values */
    gemu_log("%d",cmd);
}

static void
print_futex(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_futex_op(arg1, 0);
    print_raw_param(",%d", arg2, 0);
    print_pointer(arg3, 0); /* struct timespec */
    print_pointer(arg4, 0);
    print_raw_param("%d", arg4, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_kill
static void
print_kill(const struct syscallname *name,
    abi_long arg0, abi_long arg1, abi_long arg2,
    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_signal(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

/*
 * An array of all of the syscalls we know about
 */

static const struct syscallname scnames[] = {
#include "strace.list"
};

static int nsyscalls = ARRAY_SIZE(scnames);

/*
 * The public interface to this module.
 */
void
print_syscall(int num,
              abi_long arg1, abi_long arg2, abi_long arg3,
              abi_long arg4, abi_long arg5, abi_long arg6)
{
    int i;
    const char *format="%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ")";

    gemu_log("%d ", getpid() );

    for(i=0;i<nsyscalls;i++)
        if( scnames[i].nr == num ) {
            if( scnames[i].call != NULL ) {
                scnames[i].call(&scnames[i],arg1,arg2,arg3,arg4,arg5,arg6);
            } else {
                /* XXX: this format system is broken because it uses
                   host types and host pointers for strings */
                if( scnames[i].format != NULL )
                    format = scnames[i].format;
                gemu_log(format,scnames[i].name, arg1,arg2,arg3,arg4,arg5,arg6);
            }
            return;
        }
    gemu_log("Unknown syscall %d\n", num);
}


void
print_syscall_ret(int num, abi_long ret)
{
    int i;

    for(i=0;i<nsyscalls;i++)
        if( scnames[i].nr == num ) {
            if( scnames[i].result != NULL ) {
                scnames[i].result(&scnames[i],ret);
            } else {
                if( ret < 0 ) {
                    gemu_log(" = -1 errno=" TARGET_ABI_FMT_ld " (%s)\n", -ret, target_strerror(-ret));
                } else {
                    gemu_log(" = " TARGET_ABI_FMT_ld "\n", ret);
                }
            }
            break;
        }
}
