#include "qemu/osdep.h"

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/select.h>
#include <sys/mount.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <linux/if_packet.h>
#include <linux/in6.h>
#include <linux/netlink.h>
#ifdef HAVE_OPENAT2_H
#include <linux/openat2.h>
#endif
#include <sched.h>
#include "qemu.h"
#include "user-internals.h"
#include "strace.h"
#include "signal-common.h"
#include "target_mman.h"

struct syscallname {
    int nr;
    const char *name;
    const char *format;
    void (*call)(CPUArchState *, const struct syscallname *,
                 abi_long, abi_long, abi_long,
                 abi_long, abi_long, abi_long);
    void (*result)(CPUArchState *, const struct syscallname *, abi_long,
                   abi_long, abi_long, abi_long,
                   abi_long, abi_long, abi_long);
};

/*
 * It is possible that target doesn't have syscall that uses
 * following flags but we don't want the compiler to warn
 * us about them being unused.  Same applies to utility print
 * functions.  It is ok to keep them while not used.
 */
#define UNUSED __attribute__ ((unused))

/*
 * Structure used to translate flag values into strings.  This is
 * similar that is in the actual strace tool.
 */
struct flags {
    abi_long    f_value;  /* flag */
    abi_long    f_mask;   /* mask */
    const char  *f_string; /* stringified flag */
};

/* No 'struct flags' element should have a zero mask. */
#define FLAG_BASIC(V, M, N)      { V, M | QEMU_BUILD_BUG_ON_ZERO((M) == 0), N }

/* common flags for all architectures */
#define FLAG_GENERIC_MASK(V, M)  FLAG_BASIC(V, M, #V)
#define FLAG_GENERIC(V)          FLAG_BASIC(V, V, #V)
/* target specific flags (syscall_defs.h has TARGET_<flag>) */
#define FLAG_TARGET_MASK(V, M)   FLAG_BASIC(TARGET_##V, TARGET_##M, #V)
#define FLAG_TARGET(V)           FLAG_BASIC(TARGET_##V, TARGET_##V, #V)
/* end of flags array */
#define FLAG_END           { 0, 0, NULL }

/* Structure used to translate enumerated values into strings */
struct enums {
    abi_long    e_value;   /* enum value */
    const char  *e_string; /* stringified enum */
};

/* common enums for all architectures */
#define ENUM_GENERIC(name) { name, #name }
/* target specific enums */
#define ENUM_TARGET(name)  { TARGET_ ## name, #name }
/* end of enums array */
#define ENUM_END           { 0, NULL }

UNUSED static const char *get_comma(int);
UNUSED static void print_pointer(abi_long, int);
UNUSED static void print_flags(const struct flags *, abi_long, int);
UNUSED static void print_enums(const struct enums *, abi_long, int);
UNUSED static void print_at_dirfd(abi_long, int);
UNUSED static void print_file_mode(abi_long, int);
UNUSED static void print_open_flags(abi_long, int);
UNUSED static void print_syscall_prologue(const struct syscallname *);
UNUSED static void print_syscall_epilogue(const struct syscallname *);
UNUSED static void print_string(abi_long, int);
UNUSED static void print_buf(abi_long addr, abi_long len, int last);
UNUSED static void print_raw_param(const char *, abi_long, int);
UNUSED static void print_raw_param64(const char *, long long, int last);
UNUSED static void print_timeval(abi_ulong, int);
UNUSED static void print_timespec(abi_ulong, int);
UNUSED static void print_timespec64(abi_ulong, int);
UNUSED static void print_timezone(abi_ulong, int);
UNUSED static void print_itimerval(abi_ulong, int);
UNUSED static void print_number(abi_long, int);
UNUSED static void print_signal(abi_ulong, int);
UNUSED static void print_sockaddr(abi_ulong, abi_long, int);
UNUSED static void print_socket_domain(int domain);
UNUSED static void print_socket_type(int type);
UNUSED static void print_socket_protocol(int domain, int type, int protocol);

/*
 * Utility functions
 */
static void
print_ipc_cmd(int cmd)
{
#define output_cmd(val) \
if( cmd == val ) { \
    qemu_log(#val); \
    return; \
}

    cmd &= 0xff;

    /* General IPC commands */
    output_cmd( IPC_RMID );
    output_cmd( IPC_SET );
    output_cmd( IPC_STAT );
    output_cmd( IPC_INFO );
    /* msgctl() commands */
    output_cmd( MSG_STAT );
    output_cmd( MSG_INFO );
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
    qemu_log("%d", cmd);
}

static const char * const target_signal_name[] = {
#define MAKE_SIG_ENTRY(sig)     [TARGET_##sig] = #sig,
        MAKE_SIGNAL_LIST
#undef MAKE_SIG_ENTRY
};

static void
print_signal_1(abi_ulong arg)
{
    if (arg < ARRAY_SIZE(target_signal_name)) {
        qemu_log("%s", target_signal_name[arg]);
    } else {
        qemu_log(TARGET_ABI_FMT_lu, arg);
    }
}

static void
print_signal(abi_ulong arg, int last)
{
    print_signal_1(arg);
    qemu_log("%s", get_comma(last));
}

static void print_si_code(int arg)
{
    const char *codename = NULL;

    switch (arg) {
    case SI_USER:
        codename = "SI_USER";
        break;
    case SI_KERNEL:
        codename = "SI_KERNEL";
        break;
    case SI_QUEUE:
        codename = "SI_QUEUE";
        break;
    case SI_TIMER:
        codename = "SI_TIMER";
        break;
    case SI_MESGQ:
        codename = "SI_MESGQ";
        break;
    case SI_ASYNCIO:
        codename = "SI_ASYNCIO";
        break;
    case SI_SIGIO:
        codename = "SI_SIGIO";
        break;
    case SI_TKILL:
        codename = "SI_TKILL";
        break;
    default:
        qemu_log("%d", arg);
        return;
    }
    qemu_log("%s", codename);
}

static void get_target_siginfo(target_siginfo_t *tinfo,
                                const target_siginfo_t *info)
{
    abi_ulong sival_ptr;

    int sig;
    int si_errno;
    int si_code;
    int si_type;

    __get_user(sig, &info->si_signo);
    __get_user(si_errno, &tinfo->si_errno);
    __get_user(si_code, &info->si_code);

    tinfo->si_signo = sig;
    tinfo->si_errno = si_errno;
    tinfo->si_code = si_code;

    /* Ensure we don't leak random junk to the guest later */
    memset(tinfo->_sifields._pad, 0, sizeof(tinfo->_sifields._pad));

    /* This is awkward, because we have to use a combination of
     * the si_code and si_signo to figure out which of the union's
     * members are valid. (Within the host kernel it is always possible
     * to tell, but the kernel carefully avoids giving userspace the
     * high 16 bits of si_code, so we don't have the information to
     * do this the easy way...) We therefore make our best guess,
     * bearing in mind that a guest can spoof most of the si_codes
     * via rt_sigqueueinfo() if it likes.
     *
     * Once we have made our guess, we record it in the top 16 bits of
     * the si_code, so that print_siginfo() later can use it.
     * print_siginfo() will strip these top bits out before printing
     * the si_code.
     */

    switch (si_code) {
    case SI_USER:
    case SI_TKILL:
    case SI_KERNEL:
        /* Sent via kill(), tkill() or tgkill(), or direct from the kernel.
         * These are the only unspoofable si_code values.
         */
        __get_user(tinfo->_sifields._kill._pid, &info->_sifields._kill._pid);
        __get_user(tinfo->_sifields._kill._uid, &info->_sifields._kill._uid);
        si_type = QEMU_SI_KILL;
        break;
    default:
        /* Everything else is spoofable. Make best guess based on signal */
        switch (sig) {
        case TARGET_SIGCHLD:
            __get_user(tinfo->_sifields._sigchld._pid,
                       &info->_sifields._sigchld._pid);
            __get_user(tinfo->_sifields._sigchld._uid,
                       &info->_sifields._sigchld._uid);
            __get_user(tinfo->_sifields._sigchld._status,
                       &info->_sifields._sigchld._status);
            __get_user(tinfo->_sifields._sigchld._utime,
                       &info->_sifields._sigchld._utime);
            __get_user(tinfo->_sifields._sigchld._stime,
                       &info->_sifields._sigchld._stime);
            si_type = QEMU_SI_CHLD;
            break;
        case TARGET_SIGIO:
            __get_user(tinfo->_sifields._sigpoll._band,
                       &info->_sifields._sigpoll._band);
            __get_user(tinfo->_sifields._sigpoll._fd,
                       &info->_sifields._sigpoll._fd);
            si_type = QEMU_SI_POLL;
            break;
        default:
            /* Assume a sigqueue()/mq_notify()/rt_sigqueueinfo() source. */
            __get_user(tinfo->_sifields._rt._pid, &info->_sifields._rt._pid);
            __get_user(tinfo->_sifields._rt._uid, &info->_sifields._rt._uid);
            /* XXX: potential problem if 64 bit */
            __get_user(sival_ptr, &info->_sifields._rt._sigval.sival_ptr);
            tinfo->_sifields._rt._sigval.sival_ptr = sival_ptr;

            si_type = QEMU_SI_RT;
            break;
        }
        break;
    }

    tinfo->si_code = deposit32(si_code, 16, 16, si_type);
}

static void print_siginfo(const target_siginfo_t *tinfo)
{
    /* Print a target_siginfo_t in the format desired for printing
     * signals being taken. We assume the target_siginfo_t is in the
     * internal form where the top 16 bits of si_code indicate which
     * part of the union is valid, rather than in the guest-visible
     * form where the bottom 16 bits are sign-extended into the top 16.
     */
    int si_type = extract32(tinfo->si_code, 16, 16);
    int si_code = sextract32(tinfo->si_code, 0, 16);

    qemu_log("{si_signo=");
    print_signal(tinfo->si_signo, 1);
    qemu_log(", si_code=");
    print_si_code(si_code);

    switch (si_type) {
    case QEMU_SI_KILL:
        qemu_log(", si_pid=%u, si_uid=%u",
                 (unsigned int)tinfo->_sifields._kill._pid,
                 (unsigned int)tinfo->_sifields._kill._uid);
        break;
    case QEMU_SI_TIMER:
        qemu_log(", si_timer1=%u, si_timer2=%u",
                 tinfo->_sifields._timer._timer1,
                 tinfo->_sifields._timer._timer2);
        break;
    case QEMU_SI_POLL:
        qemu_log(", si_band=%d, si_fd=%d",
                 tinfo->_sifields._sigpoll._band,
                 tinfo->_sifields._sigpoll._fd);
        break;
    case QEMU_SI_FAULT:
        qemu_log(", si_addr=");
        print_pointer(tinfo->_sifields._sigfault._addr, 1);
        break;
    case QEMU_SI_CHLD:
        qemu_log(", si_pid=%u, si_uid=%u, si_status=%d"
                 ", si_utime=" TARGET_ABI_FMT_ld
                 ", si_stime=" TARGET_ABI_FMT_ld,
                 (unsigned int)(tinfo->_sifields._sigchld._pid),
                 (unsigned int)(tinfo->_sifields._sigchld._uid),
                 tinfo->_sifields._sigchld._status,
                 tinfo->_sifields._sigchld._utime,
                 tinfo->_sifields._sigchld._stime);
        break;
    case QEMU_SI_RT:
        qemu_log(", si_pid=%u, si_uid=%u, si_sigval=" TARGET_ABI_FMT_ld,
                 (unsigned int)tinfo->_sifields._rt._pid,
                 (unsigned int)tinfo->_sifields._rt._uid,
                 tinfo->_sifields._rt._sigval.sival_ptr);
        break;
    default:
        g_assert_not_reached();
    }
    qemu_log("}");
}

static void
print_sockaddr(abi_ulong addr, abi_long addrlen, int last)
{
    struct target_sockaddr *sa;
    int i;
    int sa_family;

    sa = lock_user(VERIFY_READ, addr, addrlen, 1);
    if (sa) {
        sa_family = tswap16(sa->sa_family);
        switch (sa_family) {
        case AF_UNIX: {
            struct target_sockaddr_un *un = (struct target_sockaddr_un *)sa;
            qemu_log("{sun_family=AF_UNIX,sun_path=\"");
            for (i = 0; i < addrlen -
                            offsetof(struct target_sockaddr_un, sun_path) &&
                 un->sun_path[i]; i++) {
                qemu_log("%c", un->sun_path[i]);
            }
            qemu_log("\"},");
            break;
        }
        case AF_INET: {
            struct target_sockaddr_in *in = (struct target_sockaddr_in *)sa;
            uint8_t *c = (uint8_t *)&in->sin_addr.s_addr;
            qemu_log("{sin_family=AF_INET,sin_port=htons(%d),",
                     ntohs(in->sin_port));
            qemu_log("sin_addr=inet_addr(\"%d.%d.%d.%d\")",
                     c[0], c[1], c[2], c[3]);
            qemu_log("},");
            break;
        }
        case AF_PACKET: {
            struct target_sockaddr_ll *ll = (struct target_sockaddr_ll *)sa;
            uint8_t *c = (uint8_t *)&ll->sll_addr;
            qemu_log("{sll_family=AF_PACKET,"
                     "sll_protocol=htons(0x%04x),if%d,pkttype=",
                     ntohs(ll->sll_protocol), ll->sll_ifindex);
            switch (ll->sll_pkttype) {
            case PACKET_HOST:
                qemu_log("PACKET_HOST");
                break;
            case PACKET_BROADCAST:
                qemu_log("PACKET_BROADCAST");
                break;
            case PACKET_MULTICAST:
                qemu_log("PACKET_MULTICAST");
                break;
            case PACKET_OTHERHOST:
                qemu_log("PACKET_OTHERHOST");
                break;
            case PACKET_OUTGOING:
                qemu_log("PACKET_OUTGOING");
                break;
            default:
                qemu_log("%d", ll->sll_pkttype);
                break;
            }
            qemu_log(",sll_addr=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                     c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
            qemu_log("},");
            break;
        }
        case AF_NETLINK: {
            struct target_sockaddr_nl *nl = (struct target_sockaddr_nl *)sa;
            qemu_log("{nl_family=AF_NETLINK,nl_pid=%u,nl_groups=%u},",
                     tswap32(nl->nl_pid), tswap32(nl->nl_groups));
            break;
        }
        default:
            qemu_log("{sa_family=%d, sa_data={", sa->sa_family);
            for (i = 0; i < 13; i++) {
                qemu_log("%02x, ", sa->sa_data[i]);
            }
            qemu_log("%02x}", sa->sa_data[i]);
            qemu_log("},");
            break;
        }
        unlock_user(sa, addr, 0);
    } else {
        print_pointer(addr, 0);
    }
    qemu_log(TARGET_ABI_FMT_ld"%s", addrlen, get_comma(last));
}

static void
print_socket_domain(int domain)
{
    switch (domain) {
    case PF_UNIX:
        qemu_log("PF_UNIX");
        break;
    case PF_INET:
        qemu_log("PF_INET");
        break;
    case PF_NETLINK:
        qemu_log("PF_NETLINK");
        break;
    case PF_PACKET:
        qemu_log("PF_PACKET");
        break;
    default:
        qemu_log("%d", domain);
        break;
    }
}

static void
print_socket_type(int type)
{
    switch (type & TARGET_SOCK_TYPE_MASK) {
    case TARGET_SOCK_DGRAM:
        qemu_log("SOCK_DGRAM");
        break;
    case TARGET_SOCK_STREAM:
        qemu_log("SOCK_STREAM");
        break;
    case TARGET_SOCK_RAW:
        qemu_log("SOCK_RAW");
        break;
    case TARGET_SOCK_RDM:
        qemu_log("SOCK_RDM");
        break;
    case TARGET_SOCK_SEQPACKET:
        qemu_log("SOCK_SEQPACKET");
        break;
    case TARGET_SOCK_PACKET:
        qemu_log("SOCK_PACKET");
        break;
    }
    if (type & TARGET_SOCK_CLOEXEC) {
        qemu_log("|SOCK_CLOEXEC");
    }
    if (type & TARGET_SOCK_NONBLOCK) {
        qemu_log("|SOCK_NONBLOCK");
    }
}

static void
print_socket_protocol(int domain, int type, int protocol)
{
    const char *name = NULL;

    switch (domain) {
    case AF_PACKET:
        switch (protocol) {
        case 3:
            name = "ETH_P_ALL";
            break;
        }
        break;

    case PF_NETLINK:
        switch (protocol) {
        case NETLINK_ROUTE:
            name = "NETLINK_ROUTE";
            break;
        case NETLINK_UNUSED:
            name = "NETLINK_UNUSED";
            break;
        case NETLINK_USERSOCK:
            name = "NETLINK_USERSOCK";
            break;
        case NETLINK_FIREWALL:
            name = "NETLINK_FIREWALL";
            break;
        case NETLINK_SOCK_DIAG:
            name = "NETLINK_SOCK_DIAG";
            break;
        case NETLINK_NFLOG:
            name = "NETLINK_NFLOG";
            break;
        case NETLINK_XFRM:
            name = "NETLINK_XFRM";
            break;
        case NETLINK_SELINUX:
            name = "NETLINK_SELINUX";
            break;
        case NETLINK_ISCSI:
            name = "NETLINK_ISCSI";
            break;
        case NETLINK_AUDIT:
            name = "NETLINK_AUDIT";
            break;
        case NETLINK_FIB_LOOKUP:
            name = "NETLINK_FIB_LOOKUP";
            break;
        case NETLINK_CONNECTOR:
            name = "NETLINK_CONNECTOR";
            break;
        case NETLINK_NETFILTER:
            name = "NETLINK_NETFILTER";
            break;
        case NETLINK_IP6_FW:
            name = "NETLINK_IP6_FW";
            break;
        case NETLINK_DNRTMSG:
            name = "NETLINK_DNRTMSG";
            break;
        case NETLINK_KOBJECT_UEVENT:
            name = "NETLINK_KOBJECT_UEVENT";
            break;
        case NETLINK_GENERIC:
            name = "NETLINK_GENERIC";
            break;
        case NETLINK_SCSITRANSPORT:
            name = "NETLINK_SCSITRANSPORT";
            break;
        case NETLINK_ECRYPTFS:
            name = "NETLINK_ECRYPTFS";
            break;
        case NETLINK_RDMA:
            name = "NETLINK_RDMA";
            break;
        case NETLINK_CRYPTO:
            name = "NETLINK_CRYPTO";
            break;
        case NETLINK_SMC:
            name = "NETLINK_SMC";
            break;
        }
        break;

    case AF_INET:
    case AF_INET6:
        switch (protocol) {
        case 3:
            if (domain == AF_INET && type == TARGET_SOCK_PACKET) {
                name = "ETH_P_ALL";
            }
            break;
        case IPPROTO_IP:
            name = "IPPROTO_IP";
            break;
        case IPPROTO_TCP:
            name = "IPPROTO_TCP";
            break;
        case IPPROTO_UDP:
            name = "IPPROTO_UDP";
            break;
        case IPPROTO_RAW:
            name = "IPPROTO_RAW";
            break;
        }
        break;
    }

    if (name) {
        qemu_log("%s", name);
    } else {
        qemu_log("%d", protocol);
    }
}

#ifdef TARGET_NR__newselect
static void
print_fdset(int n, abi_ulong target_fds_addr)
{
    int i;
    int first = 1;

    qemu_log("[");
    if( target_fds_addr ) {
        abi_long *target_fds;

        target_fds = lock_user(VERIFY_READ,
                               target_fds_addr,
                               sizeof(*target_fds)*(n / TARGET_ABI_BITS + 1),
                               1);

        if (!target_fds)
            return;

        for (i=n; i>=0; i--) {
            if ((tswapal(target_fds[i / TARGET_ABI_BITS]) >>
                (i & (TARGET_ABI_BITS - 1))) & 1) {
                qemu_log("%s%d", get_comma(first), i);
                first = 0;
            }
        }
        unlock_user(target_fds, target_fds_addr, 0);
    }
    qemu_log("]");
}
#endif

/*
 * Sysycall specific output functions
 */

/* select */
#ifdef TARGET_NR__newselect
static void
print_newselect(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg1, abi_long arg2, abi_long arg3,
                abi_long arg4, abi_long arg5, abi_long arg6)
{
    print_syscall_prologue(name);
    print_fdset(arg1, arg2);
    qemu_log(",");
    print_fdset(arg1, arg3);
    qemu_log(",");
    print_fdset(arg1, arg4);
    qemu_log(",");
    print_timeval(arg5, 1);
    print_syscall_epilogue(name);
}
#endif

static void
print_semctl(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg1, abi_long arg2, abi_long arg3,
             abi_long arg4, abi_long arg5, abi_long arg6)
{
    qemu_log("%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ",",
             name->name, arg1, arg2);
    print_ipc_cmd(arg3);
    qemu_log(",0x" TARGET_ABI_FMT_lx ")", arg4);
}

static void
print_shmat(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    static const struct flags shmat_flags[] = {
        FLAG_GENERIC(SHM_RND),
        FLAG_GENERIC(SHM_REMAP),
        FLAG_GENERIC(SHM_RDONLY),
        FLAG_GENERIC(SHM_EXEC),
        FLAG_END
    };

    print_syscall_prologue(name);
    print_raw_param(TARGET_ABI_FMT_ld, arg0, 0);
    print_pointer(arg1, 0);
    print_flags(shmat_flags, arg2, 1);
    print_syscall_epilogue(name);
}

#ifdef TARGET_NR_ipc
static void
print_ipc(CPUArchState *cpu_env, const struct syscallname *name,
          abi_long arg1, abi_long arg2, abi_long arg3,
          abi_long arg4, abi_long arg5, abi_long arg6)
{
    switch(arg1) {
    case IPCOP_semctl:
        print_semctl(cpu_env, &(const struct syscallname){ .name = "semctl" },
                     arg2, arg3, arg4, arg5, 0, 0);
        break;
    case IPCOP_shmat:
        print_shmat(cpu_env, &(const struct syscallname){ .name = "shmat" },
                    arg2, arg5, arg3, 0, 0, 0);
        break;
    default:
        qemu_log(("%s("
                  TARGET_ABI_FMT_ld ","
                  TARGET_ABI_FMT_ld ","
                  TARGET_ABI_FMT_ld ","
                  TARGET_ABI_FMT_ld
                  ")"),
                 name->name, arg1, arg2, arg3, arg4);
    }
}
#endif

#ifdef TARGET_NR_rt_sigprocmask
static void print_target_sigset_t_1(target_sigset_t *set, int last)
{
    bool first = true;
    int i, sig = 1;

    qemu_log("[");
    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        abi_ulong bits = 0;
        int j;

        __get_user(bits, &set->sig[i]);
        for (j = 0; j < sizeof(bits) * 8; j++) {
            if (bits & ((abi_ulong)1 << j)) {
                if (first) {
                    first = false;
                } else {
                    qemu_log(" ");
                }
                print_signal_1(sig);
            }
            sig++;
        }
    }
    qemu_log("]%s", get_comma(last));
}

static void print_target_sigset_t(abi_ulong addr, abi_ulong size, int last)
{
    if (addr && size == sizeof(target_sigset_t)) {
        target_sigset_t *set;

        set = lock_user(VERIFY_READ, addr, sizeof(target_sigset_t), 1);
        if (set) {
            print_target_sigset_t_1(set, last);
            unlock_user(set, addr, 0);
        } else {
            print_pointer(addr, last);
        }
    } else {
        print_pointer(addr, last);
    }
}
#endif

/*
 * Variants for the return value output function
 */

static bool
print_syscall_err(abi_long ret)
{
    const char *errstr;

    qemu_log(" = ");
    if (is_error(ret)) {
        errstr = target_strerror(-ret);
        if (errstr) {
            qemu_log("-1 errno=%d (%s)", (int)-ret, errstr);
            return true;
        }
    }
    return false;
}

static void
print_syscall_ret_addr(CPUArchState *cpu_env, const struct syscallname *name,
                       abi_long ret, abi_long arg0, abi_long arg1,
                       abi_long arg2, abi_long arg3, abi_long arg4,
                       abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log("0x" TARGET_ABI_FMT_lx, ret);
    }
    qemu_log("\n");
}

#if 0 /* currently unused */
static void
print_syscall_ret_raw(struct syscallname *name, abi_long ret)
{
        qemu_log(" = 0x" TARGET_ABI_FMT_lx "\n", ret);
}
#endif

#ifdef TARGET_NR__newselect
static void
print_syscall_ret_newselect(CPUArchState *cpu_env, const struct syscallname *name,
                            abi_long ret, abi_long arg0, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(" = 0x" TARGET_ABI_FMT_lx " (", ret);
        print_fdset(arg0, arg1);
        qemu_log(",");
        print_fdset(arg0, arg2);
        qemu_log(",");
        print_fdset(arg0, arg3);
        qemu_log(",");
        print_timeval(arg4, 1);
        qemu_log(")");
    }

    qemu_log("\n");
}
#endif

/* special meanings of adjtimex()' non-negative return values */
#define TARGET_TIME_OK       0   /* clock synchronized, no leap second */
#define TARGET_TIME_INS      1   /* insert leap second */
#define TARGET_TIME_DEL      2   /* delete leap second */
#define TARGET_TIME_OOP      3   /* leap second in progress */
#define TARGET_TIME_WAIT     4   /* leap second has occurred */
#define TARGET_TIME_ERROR    5   /* clock not synchronized */
#ifdef TARGET_NR_adjtimex
static void
print_syscall_ret_adjtimex(CPUArchState *cpu_env, const struct syscallname *name,
                           abi_long ret, abi_long arg0, abi_long arg1,
                           abi_long arg2, abi_long arg3, abi_long arg4,
                           abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        switch (ret) {
        case TARGET_TIME_OK:
            qemu_log(" TIME_OK (clock synchronized, no leap second)");
            break;
        case TARGET_TIME_INS:
            qemu_log(" TIME_INS (insert leap second)");
            break;
        case TARGET_TIME_DEL:
            qemu_log(" TIME_DEL (delete leap second)");
            break;
        case TARGET_TIME_OOP:
            qemu_log(" TIME_OOP (leap second in progress)");
            break;
        case TARGET_TIME_WAIT:
            qemu_log(" TIME_WAIT (leap second has occurred)");
            break;
        case TARGET_TIME_ERROR:
            qemu_log(" TIME_ERROR (clock not synchronized)");
            break;
        }
    }

    qemu_log("\n");
}
#endif

#if defined(TARGET_NR_clock_gettime) || defined(TARGET_NR_clock_getres)
static void
print_syscall_ret_clock_gettime(CPUArchState *cpu_env, const struct syscallname *name,
                                abi_long ret, abi_long arg0, abi_long arg1,
                                abi_long arg2, abi_long arg3, abi_long arg4,
                                abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        qemu_log(" (");
        print_timespec(arg1, 1);
        qemu_log(")");
    }

    qemu_log("\n");
}
#define print_syscall_ret_clock_getres     print_syscall_ret_clock_gettime
#endif

#if defined(TARGET_NR_clock_gettime64)
static void
print_syscall_ret_clock_gettime64(CPUArchState *cpu_env, const struct syscallname *name,
                                abi_long ret, abi_long arg0, abi_long arg1,
                                abi_long arg2, abi_long arg3, abi_long arg4,
                                abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        qemu_log(" (");
        print_timespec64(arg1, 1);
        qemu_log(")");
    }

    qemu_log("\n");
}
#endif

#ifdef TARGET_NR_gettimeofday
static void
print_syscall_ret_gettimeofday(CPUArchState *cpu_env, const struct syscallname *name,
                               abi_long ret, abi_long arg0, abi_long arg1,
                               abi_long arg2, abi_long arg3, abi_long arg4,
                               abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        qemu_log(" (");
        print_timeval(arg0, 0);
        print_timezone(arg1, 1);
        qemu_log(")");
    }

    qemu_log("\n");
}
#endif

#ifdef TARGET_NR_getitimer
static void
print_syscall_ret_getitimer(CPUArchState *cpu_env, const struct syscallname *name,
                            abi_long ret, abi_long arg0, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        qemu_log(" (");
        print_itimerval(arg1, 1);
        qemu_log(")");
    }

    qemu_log("\n");
}
#endif


#ifdef TARGET_NR_getitimer
static void
print_syscall_ret_setitimer(CPUArchState *cpu_env, const struct syscallname *name,
                            abi_long ret, abi_long arg0, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        qemu_log(" (old_value = ");
        print_itimerval(arg2, 1);
        qemu_log(")");
    }

    qemu_log("\n");
}
#endif

#if defined(TARGET_NR_listxattr) || defined(TARGET_NR_llistxattr) \
 || defined(TARGGET_NR_flistxattr)
static void
print_syscall_ret_listxattr(CPUArchState *cpu_env, const struct syscallname *name,
                            abi_long ret, abi_long arg0, abi_long arg1,
                            abi_long arg2, abi_long arg3, abi_long arg4,
                            abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        qemu_log(" (list = ");
        if (arg1 != 0) {
            abi_long attr = arg1;
            while (ret) {
                if (attr != arg1) {
                    qemu_log(",");
                }
                print_string(attr, 1);
                ret -= target_strlen(attr) + 1;
                attr += target_strlen(attr) + 1;
            }
        } else {
            qemu_log("NULL");
        }
        qemu_log(")");
    }

    qemu_log("\n");
}
#define print_syscall_ret_llistxattr     print_syscall_ret_listxattr
#define print_syscall_ret_flistxattr     print_syscall_ret_listxattr
#endif

#ifdef TARGET_NR_ioctl
static void
print_syscall_ret_ioctl(CPUArchState *cpu_env, const struct syscallname *name,
                        abi_long ret, abi_long arg0, abi_long arg1,
                        abi_long arg2, abi_long arg3, abi_long arg4,
                        abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);

        const IOCTLEntry *ie;
        const argtype *arg_type;
        void *argptr;
        int target_size;

        for (ie = ioctl_entries; ie->target_cmd != 0; ie++) {
            if (ie->target_cmd == arg1) {
                break;
            }
        }

        if (ie->target_cmd == arg1 &&
           (ie->access == IOC_R || ie->access == IOC_RW)) {
            arg_type = ie->arg_type;
            qemu_log(" (");
            arg_type++;
            target_size = thunk_type_size(arg_type, 0);
            argptr = lock_user(VERIFY_READ, arg2, target_size, 1);
            if (argptr) {
                thunk_print(argptr, arg_type);
                unlock_user(argptr, arg2, target_size);
            } else {
                print_pointer(arg2, 1);
            }
            qemu_log(")");
        }
    }
    qemu_log("\n");
}
#endif

UNUSED static const struct flags access_flags[] = {
    FLAG_GENERIC_MASK(F_OK, R_OK | W_OK | X_OK),
    FLAG_GENERIC(R_OK),
    FLAG_GENERIC(W_OK),
    FLAG_GENERIC(X_OK),
    FLAG_END,
};

UNUSED static const struct flags at_file_flags[] = {
#ifdef AT_EACCESS
    FLAG_GENERIC(AT_EACCESS),
#endif
#ifdef AT_SYMLINK_NOFOLLOW
    FLAG_GENERIC(AT_SYMLINK_NOFOLLOW),
#endif
    FLAG_END,
};

UNUSED static const struct flags unlinkat_flags[] = {
#ifdef AT_REMOVEDIR
    FLAG_GENERIC(AT_REMOVEDIR),
#endif
    FLAG_END,
};

UNUSED static const struct flags mode_flags[] = {
    FLAG_GENERIC(S_IFSOCK),
    FLAG_GENERIC(S_IFLNK),
    FLAG_GENERIC(S_IFREG),
    FLAG_GENERIC(S_IFBLK),
    FLAG_GENERIC(S_IFDIR),
    FLAG_GENERIC(S_IFCHR),
    FLAG_GENERIC(S_IFIFO),
    FLAG_END,
};

UNUSED static const struct flags open_access_flags[] = {
    FLAG_TARGET_MASK(O_RDONLY, O_ACCMODE),
    FLAG_TARGET_MASK(O_WRONLY, O_ACCMODE),
    FLAG_TARGET_MASK(O_RDWR, O_ACCMODE),
    FLAG_END,
};

UNUSED static const struct flags open_flags[] = {
    FLAG_TARGET(O_APPEND),
    FLAG_TARGET(O_CREAT),
    FLAG_TARGET(O_DIRECTORY),
    FLAG_TARGET(O_EXCL),
#if TARGET_O_LARGEFILE != 0
    FLAG_TARGET(O_LARGEFILE),
#endif
    FLAG_TARGET(O_NOCTTY),
    FLAG_TARGET(O_NOFOLLOW),
    FLAG_TARGET(O_NONBLOCK),      /* also O_NDELAY */
    FLAG_TARGET(O_DSYNC),
    FLAG_TARGET(__O_SYNC),
    FLAG_TARGET(O_TRUNC),
#ifdef O_DIRECT
    FLAG_TARGET(O_DIRECT),
#endif
#ifdef O_NOATIME
    FLAG_TARGET(O_NOATIME),
#endif
#ifdef O_CLOEXEC
    FLAG_TARGET(O_CLOEXEC),
#endif
#ifdef O_PATH
    FLAG_TARGET(O_PATH),
#endif
#ifdef O_TMPFILE
    FLAG_TARGET(O_TMPFILE),
    FLAG_TARGET(__O_TMPFILE),
#endif
    FLAG_END,
};

UNUSED static const struct flags openat2_resolve_flags[] = {
#ifdef HAVE_OPENAT2_H
    FLAG_GENERIC(RESOLVE_NO_XDEV),
    FLAG_GENERIC(RESOLVE_NO_MAGICLINKS),
    FLAG_GENERIC(RESOLVE_NO_SYMLINKS),
    FLAG_GENERIC(RESOLVE_BENEATH),
    FLAG_GENERIC(RESOLVE_IN_ROOT),
    FLAG_GENERIC(RESOLVE_CACHED),
#endif
    FLAG_END,
};

UNUSED static const struct flags mount_flags[] = {
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

UNUSED static const struct flags umount2_flags[] = {
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

UNUSED static const struct flags mmap_prot_flags[] = {
    FLAG_GENERIC_MASK(PROT_NONE, PROT_READ | PROT_WRITE | PROT_EXEC),
    FLAG_GENERIC(PROT_EXEC),
    FLAG_GENERIC(PROT_READ),
    FLAG_GENERIC(PROT_WRITE),
    FLAG_TARGET(PROT_SEM),
    FLAG_GENERIC(PROT_GROWSDOWN),
    FLAG_GENERIC(PROT_GROWSUP),
    FLAG_END,
};

UNUSED static const struct flags mmap_flags[] = {
    FLAG_TARGET_MASK(MAP_SHARED, MAP_TYPE),
    FLAG_TARGET_MASK(MAP_PRIVATE, MAP_TYPE),
    FLAG_TARGET_MASK(MAP_SHARED_VALIDATE, MAP_TYPE),
    FLAG_TARGET(MAP_ANONYMOUS),
    FLAG_TARGET(MAP_DENYWRITE),
    FLAG_TARGET(MAP_EXECUTABLE),
    FLAG_TARGET(MAP_FIXED),
    FLAG_TARGET(MAP_FIXED_NOREPLACE),
    FLAG_TARGET(MAP_GROWSDOWN),
    FLAG_TARGET(MAP_HUGETLB),
    FLAG_TARGET(MAP_LOCKED),
    FLAG_TARGET(MAP_NONBLOCK),
    FLAG_TARGET(MAP_NORESERVE),
    FLAG_TARGET(MAP_POPULATE),
    FLAG_TARGET(MAP_STACK),
    FLAG_TARGET(MAP_SYNC),
#if TARGET_MAP_UNINITIALIZED != 0
    FLAG_TARGET(MAP_UNINITIALIZED),
#endif
    FLAG_END,
};

#ifndef CLONE_PIDFD
# define CLONE_PIDFD 0x00001000
#endif

UNUSED static const struct flags clone_flags[] = {
    FLAG_GENERIC(CLONE_VM),
    FLAG_GENERIC(CLONE_FS),
    FLAG_GENERIC(CLONE_FILES),
    FLAG_GENERIC(CLONE_SIGHAND),
    FLAG_GENERIC(CLONE_PIDFD),
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
#if defined(CLONE_NEWCGROUP)
    FLAG_GENERIC(CLONE_NEWCGROUP),
#endif
#if defined(CLONE_NEWTIME)
    FLAG_GENERIC(CLONE_NEWTIME),
#endif
#if defined(CLONE_IO)
    FLAG_GENERIC(CLONE_IO),
#endif
    FLAG_END,
};

UNUSED static const struct flags execveat_flags[] = {
#ifdef AT_EMPTY_PATH
    FLAG_GENERIC(AT_EMPTY_PATH),
#endif
#ifdef AT_SYMLINK_NOFOLLOW
    FLAG_GENERIC(AT_SYMLINK_NOFOLLOW),
#endif
    FLAG_END,
};

UNUSED static const struct flags msg_flags[] = {
    /* send */
    FLAG_GENERIC(MSG_CONFIRM),
    FLAG_GENERIC(MSG_DONTROUTE),
    FLAG_GENERIC(MSG_DONTWAIT),
    FLAG_GENERIC(MSG_EOR),
    FLAG_GENERIC(MSG_MORE),
    FLAG_GENERIC(MSG_NOSIGNAL),
    FLAG_GENERIC(MSG_OOB),
    /* recv */
    FLAG_GENERIC(MSG_CMSG_CLOEXEC),
    FLAG_GENERIC(MSG_ERRQUEUE),
    FLAG_GENERIC(MSG_PEEK),
    FLAG_GENERIC(MSG_TRUNC),
    FLAG_GENERIC(MSG_WAITALL),
    /* recvmsg */
    FLAG_GENERIC(MSG_CTRUNC),
    FLAG_END,
};

UNUSED static const struct flags statx_flags[] = {
#ifdef AT_EMPTY_PATH
    FLAG_GENERIC(AT_EMPTY_PATH),
#endif
#ifdef AT_NO_AUTOMOUNT
    FLAG_GENERIC(AT_NO_AUTOMOUNT),
#endif
#ifdef AT_SYMLINK_NOFOLLOW
    FLAG_GENERIC(AT_SYMLINK_NOFOLLOW),
#endif
#ifdef AT_STATX_SYNC_AS_STAT
    FLAG_GENERIC_MASK(AT_STATX_SYNC_AS_STAT, AT_STATX_SYNC_TYPE),
#endif
#ifdef AT_STATX_FORCE_SYNC
    FLAG_GENERIC_MASK(AT_STATX_FORCE_SYNC, AT_STATX_SYNC_TYPE),
#endif
#ifdef AT_STATX_DONT_SYNC
    FLAG_GENERIC_MASK(AT_STATX_DONT_SYNC, AT_STATX_SYNC_TYPE),
#endif
    FLAG_END,
};

UNUSED static const struct flags statx_mask[] = {
/* This must come first, because it includes everything.  */
#ifdef STATX_ALL
    FLAG_GENERIC(STATX_ALL),
#endif
/* This must come second; it includes everything except STATX_BTIME.  */
#ifdef STATX_BASIC_STATS
    FLAG_GENERIC(STATX_BASIC_STATS),
#endif
#ifdef STATX_TYPE
    FLAG_GENERIC(STATX_TYPE),
#endif
#ifdef STATX_MODE
    FLAG_GENERIC(STATX_MODE),
#endif
#ifdef STATX_NLINK
    FLAG_GENERIC(STATX_NLINK),
#endif
#ifdef STATX_UID
    FLAG_GENERIC(STATX_UID),
#endif
#ifdef STATX_GID
    FLAG_GENERIC(STATX_GID),
#endif
#ifdef STATX_ATIME
    FLAG_GENERIC(STATX_ATIME),
#endif
#ifdef STATX_MTIME
    FLAG_GENERIC(STATX_MTIME),
#endif
#ifdef STATX_CTIME
    FLAG_GENERIC(STATX_CTIME),
#endif
#ifdef STATX_INO
    FLAG_GENERIC(STATX_INO),
#endif
#ifdef STATX_SIZE
    FLAG_GENERIC(STATX_SIZE),
#endif
#ifdef STATX_BLOCKS
    FLAG_GENERIC(STATX_BLOCKS),
#endif
#ifdef STATX_BTIME
    FLAG_GENERIC(STATX_BTIME),
#endif
    FLAG_END,
};

UNUSED static const struct flags falloc_flags[] = {
    FLAG_GENERIC(FALLOC_FL_KEEP_SIZE),
    FLAG_GENERIC(FALLOC_FL_PUNCH_HOLE),
#ifdef FALLOC_FL_NO_HIDE_STALE
    FLAG_GENERIC(FALLOC_FL_NO_HIDE_STALE),
#endif
#ifdef FALLOC_FL_COLLAPSE_RANGE
    FLAG_GENERIC(FALLOC_FL_COLLAPSE_RANGE),
#endif
#ifdef FALLOC_FL_ZERO_RANGE
    FLAG_GENERIC(FALLOC_FL_ZERO_RANGE),
#endif
#ifdef FALLOC_FL_INSERT_RANGE
    FLAG_GENERIC(FALLOC_FL_INSERT_RANGE),
#endif
#ifdef FALLOC_FL_UNSHARE_RANGE
    FLAG_GENERIC(FALLOC_FL_UNSHARE_RANGE),
#endif
};

UNUSED static const struct flags termios_iflags[] = {
    FLAG_TARGET(IGNBRK),
    FLAG_TARGET(BRKINT),
    FLAG_TARGET(IGNPAR),
    FLAG_TARGET(PARMRK),
    FLAG_TARGET(INPCK),
    FLAG_TARGET(ISTRIP),
    FLAG_TARGET(INLCR),
    FLAG_TARGET(IGNCR),
    FLAG_TARGET(ICRNL),
    FLAG_TARGET(IUCLC),
    FLAG_TARGET(IXON),
    FLAG_TARGET(IXANY),
    FLAG_TARGET(IXOFF),
    FLAG_TARGET(IMAXBEL),
    FLAG_TARGET(IUTF8),
    FLAG_END,
};

UNUSED static const struct flags termios_oflags[] = {
    FLAG_TARGET(OPOST),
    FLAG_TARGET(OLCUC),
    FLAG_TARGET(ONLCR),
    FLAG_TARGET(OCRNL),
    FLAG_TARGET(ONOCR),
    FLAG_TARGET(ONLRET),
    FLAG_TARGET(OFILL),
    FLAG_TARGET(OFDEL),
    FLAG_END,
};

UNUSED static struct enums termios_oflags_NLDLY[] = {
    ENUM_TARGET(NL0),
    ENUM_TARGET(NL1),
    ENUM_END,
};

UNUSED static struct enums termios_oflags_CRDLY[] = {
    ENUM_TARGET(CR0),
    ENUM_TARGET(CR1),
    ENUM_TARGET(CR2),
    ENUM_TARGET(CR3),
    ENUM_END,
};

UNUSED static struct enums termios_oflags_TABDLY[] = {
    ENUM_TARGET(TAB0),
    ENUM_TARGET(TAB1),
    ENUM_TARGET(TAB2),
    ENUM_TARGET(TAB3),
    ENUM_END,
};

UNUSED static struct enums termios_oflags_VTDLY[] = {
    ENUM_TARGET(VT0),
    ENUM_TARGET(VT1),
    ENUM_END,
};

UNUSED static struct enums termios_oflags_FFDLY[] = {
    ENUM_TARGET(FF0),
    ENUM_TARGET(FF1),
    ENUM_END,
};

UNUSED static struct enums termios_oflags_BSDLY[] = {
    ENUM_TARGET(BS0),
    ENUM_TARGET(BS1),
    ENUM_END,
};

UNUSED static struct enums termios_cflags_CBAUD[] = {
    ENUM_TARGET(B0),
    ENUM_TARGET(B50),
    ENUM_TARGET(B75),
    ENUM_TARGET(B110),
    ENUM_TARGET(B134),
    ENUM_TARGET(B150),
    ENUM_TARGET(B200),
    ENUM_TARGET(B300),
    ENUM_TARGET(B600),
    ENUM_TARGET(B1200),
    ENUM_TARGET(B1800),
    ENUM_TARGET(B2400),
    ENUM_TARGET(B4800),
    ENUM_TARGET(B9600),
    ENUM_TARGET(B19200),
    ENUM_TARGET(B38400),
    ENUM_TARGET(B57600),
    ENUM_TARGET(B115200),
    ENUM_TARGET(B230400),
    ENUM_TARGET(B460800),
    ENUM_END,
};

UNUSED static struct enums termios_cflags_CSIZE[] = {
    ENUM_TARGET(CS5),
    ENUM_TARGET(CS6),
    ENUM_TARGET(CS7),
    ENUM_TARGET(CS8),
    ENUM_END,
};

UNUSED static const struct flags termios_cflags[] = {
    FLAG_TARGET(CSTOPB),
    FLAG_TARGET(CREAD),
    FLAG_TARGET(PARENB),
    FLAG_TARGET(PARODD),
    FLAG_TARGET(HUPCL),
    FLAG_TARGET(CLOCAL),
    FLAG_TARGET(CRTSCTS),
    FLAG_END,
};

UNUSED static const struct flags termios_lflags[] = {
    FLAG_TARGET(ISIG),
    FLAG_TARGET(ICANON),
    FLAG_TARGET(XCASE),
    FLAG_TARGET(ECHO),
    FLAG_TARGET(ECHOE),
    FLAG_TARGET(ECHOK),
    FLAG_TARGET(ECHONL),
    FLAG_TARGET(NOFLSH),
    FLAG_TARGET(TOSTOP),
    FLAG_TARGET(ECHOCTL),
    FLAG_TARGET(ECHOPRT),
    FLAG_TARGET(ECHOKE),
    FLAG_TARGET(FLUSHO),
    FLAG_TARGET(PENDIN),
    FLAG_TARGET(IEXTEN),
    FLAG_TARGET(EXTPROC),
    FLAG_END,
};

#ifdef TARGET_NR_mlockall
static const struct flags mlockall_flags[] = {
    FLAG_TARGET(MCL_CURRENT),
    FLAG_TARGET(MCL_FUTURE),
#ifdef MCL_ONFAULT
    FLAG_TARGET(MCL_ONFAULT),
#endif
    FLAG_END,
};
#endif

/* IDs of the various system clocks */
#define TARGET_CLOCK_REALTIME              0
#define TARGET_CLOCK_MONOTONIC             1
#define TARGET_CLOCK_PROCESS_CPUTIME_ID    2
#define TARGET_CLOCK_THREAD_CPUTIME_ID     3
#define TARGET_CLOCK_MONOTONIC_RAW         4
#define TARGET_CLOCK_REALTIME_COARSE       5
#define TARGET_CLOCK_MONOTONIC_COARSE      6
#define TARGET_CLOCK_BOOTTIME              7
#define TARGET_CLOCK_REALTIME_ALARM        8
#define TARGET_CLOCK_BOOTTIME_ALARM        9
#define TARGET_CLOCK_SGI_CYCLE             10
#define TARGET_CLOCK_TAI                   11

UNUSED static struct enums clockids[] = {
    ENUM_TARGET(CLOCK_REALTIME),
    ENUM_TARGET(CLOCK_MONOTONIC),
    ENUM_TARGET(CLOCK_PROCESS_CPUTIME_ID),
    ENUM_TARGET(CLOCK_THREAD_CPUTIME_ID),
    ENUM_TARGET(CLOCK_MONOTONIC_RAW),
    ENUM_TARGET(CLOCK_REALTIME_COARSE),
    ENUM_TARGET(CLOCK_MONOTONIC_COARSE),
    ENUM_TARGET(CLOCK_BOOTTIME),
    ENUM_TARGET(CLOCK_REALTIME_ALARM),
    ENUM_TARGET(CLOCK_BOOTTIME_ALARM),
    ENUM_TARGET(CLOCK_SGI_CYCLE),
    ENUM_TARGET(CLOCK_TAI),
    ENUM_END,
};

UNUSED static struct enums itimer_types[] = {
    ENUM_GENERIC(ITIMER_REAL),
    ENUM_GENERIC(ITIMER_VIRTUAL),
    ENUM_GENERIC(ITIMER_PROF),
    ENUM_END,
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

    for (n = 0; f->f_string != NULL; f++) {
        if ((flags & f->f_mask) == f->f_value) {
            qemu_log("%s%s", sep, f->f_string);
            flags &= ~f->f_mask;
            sep = "|";
            n++;
        }
    }

    if (n > 0) {
        /* print rest of the flags as numeric */
        if (flags != 0) {
            qemu_log("%s%#x%s", sep, (unsigned int)flags, get_comma(last));
        } else {
            qemu_log("%s", get_comma(last));
        }
    } else {
        /* no string version of flags found, print them in hex then */
        qemu_log("%#x%s", (unsigned int)flags, get_comma(last));
    }
}

static void
print_enums(const struct enums *e, abi_long enum_arg, int last)
{
    for (; e->e_string != NULL; e++) {
        if (e->e_value == enum_arg) {
            qemu_log("%s", e->e_string);
            break;
        }
    }

    if (e->e_string == NULL) {
        qemu_log("%#x", (unsigned int)enum_arg);
    }

    qemu_log("%s", get_comma(last));
}

static void
print_at_dirfd(abi_long dirfd, int last)
{
#ifdef AT_FDCWD
    if (dirfd == AT_FDCWD) {
        qemu_log("AT_FDCWD%s", get_comma(last));
        return;
    }
#endif
    qemu_log("%d%s", (int)dirfd, get_comma(last));
}

static void
print_file_mode(abi_long mode, int last)
{
    const char *sep = "";
    const struct flags *m;

    if (mode == 0) {
        qemu_log("000%s", get_comma(last));
        return;
    }

    for (m = &mode_flags[0]; m->f_string != NULL; m++) {
        if ((m->f_value & mode) == m->f_value) {
            qemu_log("%s%s", m->f_string, sep);
            sep = "|";
            mode &= ~m->f_value;
            break;
        }
    }

    mode &= ~S_IFMT;
    /* print rest of the mode as octal */
    if (mode != 0)
        qemu_log("%s%#o", sep, (unsigned int)mode);

    qemu_log("%s", get_comma(last));
}

static void
print_open_flags(abi_long flags, int last)
{
    print_flags(open_access_flags, flags & TARGET_O_ACCMODE, 1);
    flags &= ~TARGET_O_ACCMODE;
    if (flags == 0) {
        qemu_log("%s", get_comma(last));
        return;
    }
    qemu_log("|");
    print_flags(open_flags, flags, last);
}

static void
print_syscall_prologue(const struct syscallname *sc)
{
    qemu_log("%s(", sc->name);
}

/*ARGSUSED*/
static void
print_syscall_epilogue(const struct syscallname *sc)
{
    (void)sc;
    qemu_log(")");
}

static void
print_string(abi_long addr, int last)
{
    char *s;

    if ((s = lock_user_string(addr)) != NULL) {
        qemu_log("\"%s\"%s", s, get_comma(last));
        unlock_user(s, addr, 0);
    } else {
        /* can't get string out of it, so print it as pointer */
        print_pointer(addr, last);
    }
}

#define MAX_PRINT_BUF 40
static void
print_buf(abi_long addr, abi_long len, int last)
{
    uint8_t *s;
    int i;

    s = lock_user(VERIFY_READ, addr, len, 1);
    if (s) {
        qemu_log("\"");
        for (i = 0; i < MAX_PRINT_BUF && i < len; i++) {
            if (isprint(s[i])) {
                qemu_log("%c", s[i]);
            } else {
                qemu_log("\\%o", s[i]);
            }
        }
        qemu_log("\"");
        if (i != len) {
            qemu_log("...");
        }
        if (!last) {
            qemu_log(",");
        }
        unlock_user(s, addr, 0);
    } else {
        print_pointer(addr, last);
    }
}

static void
print_buf_len(abi_long addr, abi_long len, int last)
{
    print_buf(addr, len, 0);
    print_raw_param(TARGET_ABI_FMT_ld, len, last);
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
    qemu_log(format, param);
}

/*
 * Same as print_raw_param() but prints out raw 64-bit parameter.
 */
static void
print_raw_param64(const char *fmt, long long param, int last)
{
    char format[64];

    (void)snprintf(format, sizeof(format), "%s%s", fmt, get_comma(last));
    qemu_log(format, param);
}


static void
print_pointer(abi_long p, int last)
{
    if (p == 0)
        qemu_log("NULL%s", get_comma(last));
    else
        qemu_log("0x" TARGET_ABI_FMT_lx "%s", p, get_comma(last));
}

/*
 * Reads 32-bit (int) number from guest address space from
 * address 'addr' and prints it.
 */
static void
print_number(abi_long addr, int last)
{
    if (addr == 0) {
        qemu_log("NULL%s", get_comma(last));
    } else {
        int num;

        get_user_s32(num, addr);
        qemu_log("[%d]%s", num, get_comma(last));
    }
}

static void
print_timeval(abi_ulong tv_addr, int last)
{
    if( tv_addr ) {
        struct target_timeval *tv;

        tv = lock_user(VERIFY_READ, tv_addr, sizeof(*tv), 1);
        if (!tv) {
            print_pointer(tv_addr, last);
            return;
        }
        qemu_log("{tv_sec = " TARGET_ABI_FMT_ld
                 ",tv_usec = " TARGET_ABI_FMT_ld "}%s",
                 tswapal(tv->tv_sec), tswapal(tv->tv_usec), get_comma(last));
        unlock_user(tv, tv_addr, 0);
    } else
        qemu_log("NULL%s", get_comma(last));
}

static void
print_timespec(abi_ulong ts_addr, int last)
{
    if (ts_addr) {
        struct target_timespec *ts;

        ts = lock_user(VERIFY_READ, ts_addr, sizeof(*ts), 1);
        if (!ts) {
            print_pointer(ts_addr, last);
            return;
        }
        qemu_log("{tv_sec = " TARGET_ABI_FMT_ld
                 ",tv_nsec = " TARGET_ABI_FMT_ld "}%s",
                 tswapal(ts->tv_sec), tswapal(ts->tv_nsec), get_comma(last));
        unlock_user(ts, ts_addr, 0);
    } else {
        qemu_log("NULL%s", get_comma(last));
    }
}

static void
print_timespec64(abi_ulong ts_addr, int last)
{
    if (ts_addr) {
        struct target__kernel_timespec *ts;

        ts = lock_user(VERIFY_READ, ts_addr, sizeof(*ts), 1);
        if (!ts) {
            print_pointer(ts_addr, last);
            return;
        }
        print_raw_param64("{tv_sec=%" PRId64, tswap64(ts->tv_sec), 0);
        print_raw_param64("tv_nsec=%" PRId64 "}", tswap64(ts->tv_nsec), last);
        unlock_user(ts, ts_addr, 0);
    } else {
        qemu_log("NULL%s", get_comma(last));
    }
}

static void
print_timezone(abi_ulong tz_addr, int last)
{
    if (tz_addr) {
        struct target_timezone *tz;

        tz = lock_user(VERIFY_READ, tz_addr, sizeof(*tz), 1);
        if (!tz) {
            print_pointer(tz_addr, last);
            return;
        }
        qemu_log("{%d,%d}%s", tswap32(tz->tz_minuteswest),
                 tswap32(tz->tz_dsttime), get_comma(last));
        unlock_user(tz, tz_addr, 0);
    } else {
        qemu_log("NULL%s", get_comma(last));
    }
}

static void
print_itimerval(abi_ulong it_addr, int last)
{
    if (it_addr) {
        qemu_log("{it_interval=");
        print_timeval(it_addr +
                      offsetof(struct target_itimerval, it_interval), 0);
        qemu_log("it_value=");
        print_timeval(it_addr +
                      offsetof(struct target_itimerval, it_value), 0);
        qemu_log("}%s", get_comma(last));
    } else {
        qemu_log("NULL%s", get_comma(last));
    }
}

void
print_termios(void *arg)
{
    const struct target_termios *target = arg;

    target_tcflag_t iflags = tswap32(target->c_iflag);
    target_tcflag_t oflags = tswap32(target->c_oflag);
    target_tcflag_t cflags = tswap32(target->c_cflag);
    target_tcflag_t lflags = tswap32(target->c_lflag);

    qemu_log("{");

    qemu_log("c_iflag = ");
    print_flags(termios_iflags, iflags, 0);

    qemu_log("c_oflag = ");
    target_tcflag_t oflags_clean =  oflags & ~(TARGET_NLDLY | TARGET_CRDLY |
                                               TARGET_TABDLY | TARGET_BSDLY |
                                               TARGET_VTDLY | TARGET_FFDLY);
    print_flags(termios_oflags, oflags_clean, 0);
    if (oflags & TARGET_NLDLY) {
        print_enums(termios_oflags_NLDLY, oflags & TARGET_NLDLY, 0);
    }
    if (oflags & TARGET_CRDLY) {
        print_enums(termios_oflags_CRDLY, oflags & TARGET_CRDLY, 0);
    }
    if (oflags & TARGET_TABDLY) {
        print_enums(termios_oflags_TABDLY, oflags & TARGET_TABDLY, 0);
    }
    if (oflags & TARGET_BSDLY) {
        print_enums(termios_oflags_BSDLY, oflags & TARGET_BSDLY, 0);
    }
    if (oflags & TARGET_VTDLY) {
        print_enums(termios_oflags_VTDLY, oflags & TARGET_VTDLY, 0);
    }
    if (oflags & TARGET_FFDLY) {
        print_enums(termios_oflags_FFDLY, oflags & TARGET_FFDLY, 0);
    }

    qemu_log("c_cflag = ");
    if (cflags & TARGET_CBAUD) {
        print_enums(termios_cflags_CBAUD, cflags & TARGET_CBAUD, 0);
    }
    if (cflags & TARGET_CSIZE) {
        print_enums(termios_cflags_CSIZE, cflags & TARGET_CSIZE, 0);
    }
    target_tcflag_t cflags_clean = cflags & ~(TARGET_CBAUD | TARGET_CSIZE);
    print_flags(termios_cflags, cflags_clean, 0);

    qemu_log("c_lflag = ");
    print_flags(termios_lflags, lflags, 0);

    qemu_log("c_cc = ");
    qemu_log("\"%s\",", target->c_cc);

    qemu_log("c_line = ");
    print_raw_param("\'%c\'", target->c_line, 1);

    qemu_log("}");
}

#undef UNUSED

#ifdef TARGET_NR_accept
static void
print_accept(CPUArchState *cpu_env, const struct syscallname *name,
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
print_access(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg0, abi_long arg1, abi_long arg2,
             abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_flags(access_flags, arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_acct
static void
print_acct(CPUArchState *cpu_env, const struct syscallname *name,
           abi_long arg0, abi_long arg1, abi_long arg2,
           abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_brk
static void
print_brk(CPUArchState *cpu_env, const struct syscallname *name,
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
print_chdir(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_chroot
static void
print_chroot(CPUArchState *cpu_env, const struct syscallname *name,
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
print_chmod(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_file_mode(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_chown) || defined(TARGET_NR_lchown)
static void
print_chown(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_raw_param("%d", arg2, 1);
    print_syscall_epilogue(name);
}
#define print_lchown     print_chown
#endif

#ifdef TARGET_NR_clock_adjtime
static void
print_clock_adjtime(CPUArchState *cpu_env, const struct syscallname *name,
                    abi_long arg0, abi_long arg1, abi_long arg2,
                    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_enums(clockids, arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_clone
static void do_print_clone(unsigned int flags, abi_ulong newsp,
                           abi_ulong parent_tidptr, target_ulong newtls,
                           abi_ulong child_tidptr)
{
    print_flags(clone_flags, flags, 0);
    print_raw_param("child_stack=0x" TARGET_ABI_FMT_lx, newsp, 0);
    print_raw_param("parent_tidptr=0x" TARGET_ABI_FMT_lx, parent_tidptr, 0);
    print_raw_param("tls=0x" TARGET_ABI_FMT_lx, newtls, 0);
    print_raw_param("child_tidptr=0x" TARGET_ABI_FMT_lx, child_tidptr, 1);
}

static void
print_clone(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg1, abi_long arg2, abi_long arg3,
            abi_long arg4, abi_long arg5, abi_long arg6)
{
    print_syscall_prologue(name);
#if defined(TARGET_MICROBLAZE)
    do_print_clone(arg1, arg2, arg4, arg6, arg5);
#elif defined(TARGET_CLONE_BACKWARDS)
    do_print_clone(arg1, arg2, arg3, arg4, arg5);
#elif defined(TARGET_CLONE_BACKWARDS2)
    do_print_clone(arg2, arg1, arg3, arg5, arg4);
#else
    do_print_clone(arg1, arg2, arg3, arg5, arg4);
#endif
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_creat
static void
print_creat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_execv(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_raw_param("0x" TARGET_ABI_FMT_lx, arg1, 1);
    print_syscall_epilogue(name);
}
#endif

static void
print_execve_argv(abi_long argv, int last)
{
    abi_ulong arg_ptr_addr;
    char *s;

    qemu_log("{");
    for (arg_ptr_addr = argv; ; arg_ptr_addr += sizeof(abi_ulong)) {
        abi_ulong *arg_ptr, arg_addr;

        arg_ptr = lock_user(VERIFY_READ, arg_ptr_addr, sizeof(abi_ulong), 1);
        if (!arg_ptr) {
            return;
        }
        arg_addr = tswapal(*arg_ptr);
        unlock_user(arg_ptr, arg_ptr_addr, 0);
        if (!arg_addr) {
            break;
        }
        s = lock_user_string(arg_addr);
        if (s) {
            qemu_log("\"%s\",", s);
            unlock_user(s, arg_addr, 0);
        }
    }
    qemu_log("NULL}%s", get_comma(last));
}

static void
print_execve(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg1, abi_long arg2, abi_long arg3,
             abi_long arg4, abi_long arg5, abi_long arg6)
{
    print_syscall_prologue(name);
    print_string(arg1, 0);
    print_execve_argv(arg2, 1);
    print_syscall_epilogue(name);
}

static void
print_execveat(CPUArchState *cpu_env, const struct syscallname *name,
               abi_long arg1, abi_long arg2, abi_long arg3,
               abi_long arg4, abi_long arg5, abi_long arg6)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg1, 0);
    print_string(arg2, 0);
    print_execve_argv(arg3, 0);
    print_flags(execveat_flags, arg5, 1);
    print_syscall_epilogue(name);
}

#if defined(TARGET_NR_faccessat) || defined(TARGET_NR_faccessat2)
static void
print_faccessat(CPUArchState *cpu_env, const struct syscallname *name,
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

#ifdef TARGET_NR_fallocate
static void
print_fallocate(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg0, abi_long arg1, abi_long arg2,
                abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_flags(falloc_flags, arg1, 0);
#if TARGET_ABI_BITS == 32
    print_raw_param("%" PRIu64, target_offset64(arg2, arg3), 0);
    print_raw_param("%" PRIu64, target_offset64(arg4, arg5), 1);
#else
    print_raw_param(TARGET_ABI_FMT_ld, arg2, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg3, 1);
#endif
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_fchmodat
static void
print_fchmodat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_fchownat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_fcntl(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    switch(arg1) {
    case TARGET_F_DUPFD:
        qemu_log("F_DUPFD,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 1);
        break;
    case TARGET_F_GETFD:
        qemu_log("F_GETFD");
        break;
    case TARGET_F_SETFD:
        qemu_log("F_SETFD,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 1);
        break;
    case TARGET_F_GETFL:
        qemu_log("F_GETFL");
        break;
    case TARGET_F_SETFL:
        qemu_log("F_SETFL,");
        print_open_flags(arg2, 1);
        break;
    case TARGET_F_GETLK:
        qemu_log("F_GETLK,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_SETLK:
        qemu_log("F_SETLK,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_SETLKW:
        qemu_log("F_SETLKW,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_GETOWN:
        qemu_log("F_GETOWN");
        break;
    case TARGET_F_SETOWN:
        qemu_log("F_SETOWN,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 0);
        break;
    case TARGET_F_GETSIG:
        qemu_log("F_GETSIG");
        break;
    case TARGET_F_SETSIG:
        qemu_log("F_SETSIG,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 0);
        break;
#if TARGET_ABI_BITS == 32
    case TARGET_F_GETLK64:
        qemu_log("F_GETLK64,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_SETLK64:
        qemu_log("F_SETLK64,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_SETLKW64:
        qemu_log("F_SETLKW64,");
        print_pointer(arg2, 1);
        break;
#endif
    case TARGET_F_OFD_GETLK:
        qemu_log("F_OFD_GETLK,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_OFD_SETLK:
        qemu_log("F_OFD_SETLK,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_OFD_SETLKW:
        qemu_log("F_OFD_SETLKW,");
        print_pointer(arg2, 1);
        break;
    case TARGET_F_SETLEASE:
        qemu_log("F_SETLEASE,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 1);
        break;
    case TARGET_F_GETLEASE:
        qemu_log("F_GETLEASE");
        break;
#ifdef F_DUPFD_CLOEXEC
    case TARGET_F_DUPFD_CLOEXEC:
        qemu_log("F_DUPFD_CLOEXEC,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 1);
        break;
#endif
    case TARGET_F_NOTIFY:
        qemu_log("F_NOTIFY,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 1);
        break;
#ifdef F_GETOWN_EX
    case TARGET_F_GETOWN_EX:
        qemu_log("F_GETOWN_EX,");
        print_pointer(arg2, 1);
        break;
#endif
#ifdef F_SETOWN_EX
    case TARGET_F_SETOWN_EX:
        qemu_log("F_SETOWN_EX,");
        print_pointer(arg2, 1);
        break;
#endif
#ifdef F_SETPIPE_SZ
    case TARGET_F_SETPIPE_SZ:
        qemu_log("F_SETPIPE_SZ,");
        print_raw_param(TARGET_ABI_FMT_ld, arg2, 1);
        break;
    case TARGET_F_GETPIPE_SZ:
        qemu_log("F_GETPIPE_SZ");
        break;
#endif
#ifdef F_ADD_SEALS
    case TARGET_F_ADD_SEALS:
        qemu_log("F_ADD_SEALS,");
        print_raw_param("0x"TARGET_ABI_FMT_lx, arg2, 1);
        break;
    case TARGET_F_GET_SEALS:
        qemu_log("F_GET_SEALS");
        break;
#endif
    default:
        print_raw_param(TARGET_ABI_FMT_ld, arg1, 0);
        print_pointer(arg2, 1);
        break;
    }
    print_syscall_epilogue(name);
}
#define print_fcntl64   print_fcntl
#endif

#ifdef TARGET_NR_fgetxattr
static void
print_fgetxattr(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg0, abi_long arg1, abi_long arg2,
                abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_string(arg1, 0);
    print_pointer(arg2, 0);
    print_raw_param(TARGET_FMT_lu, arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_flistxattr
static void
print_flistxattr(CPUArchState *cpu_env, const struct syscallname *name,
                 abi_long arg0, abi_long arg1, abi_long arg2,
                 abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_pointer(arg1, 0);
    print_raw_param(TARGET_FMT_lu, arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_getxattr) || defined(TARGET_NR_lgetxattr)
static void
print_getxattr(CPUArchState *cpu_env, const struct syscallname *name,
               abi_long arg0, abi_long arg1, abi_long arg2,
               abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_string(arg1, 0);
    print_pointer(arg2, 0);
    print_raw_param(TARGET_FMT_lu, arg3, 1);
    print_syscall_epilogue(name);
}
#define print_lgetxattr     print_getxattr
#endif

#if defined(TARGET_NR_listxattr) || defined(TARGET_NR_llistxattr)
static void
print_listxattr(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg0, abi_long arg1, abi_long arg2,
                abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 0);
    print_raw_param(TARGET_FMT_lu, arg2, 1);
    print_syscall_epilogue(name);
}
#define print_llistxattr     print_listxattr
#endif

#if defined(TARGET_NR_fremovexattr)
static void
print_fremovexattr(CPUArchState *cpu_env, const struct syscallname *name,
                   abi_long arg0, abi_long arg1, abi_long arg2,
                   abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_string(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_removexattr) || defined(TARGET_NR_lremovexattr)
static void
print_removexattr(CPUArchState *cpu_env, const struct syscallname *name,
                  abi_long arg0, abi_long arg1, abi_long arg2,
                  abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_string(arg1, 1);
    print_syscall_epilogue(name);
}
#define print_lremovexattr     print_removexattr
#endif

#ifdef TARGET_NR_futimesat
static void
print_futimesat(CPUArchState *cpu_env, const struct syscallname *name,
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

#ifdef TARGET_NR_gettimeofday
static void
print_gettimeofday(CPUArchState *cpu_env, const struct syscallname *name,
                   abi_long arg0, abi_long arg1, abi_long arg2,
                   abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_settimeofday
static void
print_settimeofday(CPUArchState *cpu_env, const struct syscallname *name,
                   abi_long arg0, abi_long arg1, abi_long arg2,
                   abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_timeval(arg0, 0);
    print_timezone(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_clock_gettime) || defined(TARGET_NR_clock_getres)
static void
print_clock_gettime(CPUArchState *cpu_env, const struct syscallname *name,
                    abi_long arg0, abi_long arg1, abi_long arg2,
                    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_enums(clockids, arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#define print_clock_getres     print_clock_gettime
#endif

#if defined(TARGET_NR_clock_gettime64)
static void
print_clock_gettime64(CPUArchState *cpu_env, const struct syscallname *name,
                    abi_long arg0, abi_long arg1, abi_long arg2,
                    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_enums(clockids, arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_clock_settime
static void
print_clock_settime(CPUArchState *cpu_env, const struct syscallname *name,
                    abi_long arg0, abi_long arg1, abi_long arg2,
                    abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_enums(clockids, arg0, 0);
    print_timespec(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_getitimer
static void
print_getitimer(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg0, abi_long arg1, abi_long arg2,
                abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_enums(itimer_types, arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_setitimer
static void
print_setitimer(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg0, abi_long arg1, abi_long arg2,
                abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_enums(itimer_types, arg0, 0);
    print_itimerval(arg1, 0);
    print_pointer(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_link
static void
print_link(CPUArchState *cpu_env, const struct syscallname *name,
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
print_linkat(CPUArchState *cpu_env, const struct syscallname *name,
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

#if defined(TARGET_NR__llseek) || defined(TARGET_NR_llseek)
static void
print__llseek(CPUArchState *cpu_env, const struct syscallname *name,
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
    qemu_log("%s", whence);
    print_syscall_epilogue(name);
}
#define print_llseek print__llseek
#endif

#ifdef TARGET_NR_lseek
static void
print_lseek(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg1, 0);
    switch (arg2) {
    case SEEK_SET:
        qemu_log("SEEK_SET"); break;
    case SEEK_CUR:
        qemu_log("SEEK_CUR"); break;
    case SEEK_END:
        qemu_log("SEEK_END"); break;
#ifdef SEEK_DATA
    case SEEK_DATA:
        qemu_log("SEEK_DATA"); break;
#endif
#ifdef SEEK_HOLE
    case SEEK_HOLE:
        qemu_log("SEEK_HOLE"); break;
#endif
    default:
        print_raw_param("%#x", arg2, 1);
    }
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_truncate
static void
print_truncate(CPUArchState *cpu_env, const struct syscallname *name,
               abi_long arg0, abi_long arg1, abi_long arg2,
               abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_truncate64
static void
print_truncate64(CPUArchState *cpu_env, const struct syscallname *name,
                 abi_long arg0, abi_long arg1, abi_long arg2,
                 abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    if (regpairs_aligned(cpu_env, TARGET_NR_truncate64)) {
        arg1 = arg2;
        arg2 = arg3;
    }
    print_raw_param("%" PRIu64, target_offset64(arg1, arg2), 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_ftruncate64
static void
print_ftruncate64(CPUArchState *cpu_env, const struct syscallname *name,
                  abi_long arg0, abi_long arg1, abi_long arg2,
                  abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    if (regpairs_aligned(cpu_env, TARGET_NR_ftruncate64)) {
        arg1 = arg2;
        arg2 = arg3;
    }
    print_raw_param("%" PRIu64, target_offset64(arg1, arg2), 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mlockall
static void
print_mlockall(CPUArchState *cpu_env, const struct syscallname *name,
               abi_long arg0, abi_long arg1, abi_long arg2,
               abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_flags(mlockall_flags, arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_socket)
static void
print_socket(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg0, abi_long arg1, abi_long arg2,
             abi_long arg3, abi_long arg4, abi_long arg5)
{
    abi_ulong domain = arg0, type = arg1, protocol = arg2;

    print_syscall_prologue(name);
    print_socket_domain(domain);
    qemu_log(",");
    print_socket_type(type);
    qemu_log(",");
    if (domain == AF_PACKET ||
        (domain == AF_INET && type == TARGET_SOCK_PACKET)) {
        protocol = tswap16(protocol);
    }
    print_socket_protocol(domain, type, protocol);
    print_syscall_epilogue(name);
}

#endif

#if defined(TARGET_NR_socketcall) || defined(TARGET_NR_bind)

static void print_sockfd(abi_long sockfd, int last)
{
    print_raw_param(TARGET_ABI_FMT_ld, sockfd, last);
}

#endif

#if defined(TARGET_NR_socketcall)

#define get_user_ualx(x, gaddr, idx) \
        get_user_ual(x, (gaddr) + (idx) * sizeof(abi_long))

static void do_print_socket(const char *name, abi_long arg1)
{
    abi_ulong domain, type, protocol;

    get_user_ualx(domain, arg1, 0);
    get_user_ualx(type, arg1, 1);
    get_user_ualx(protocol, arg1, 2);
    qemu_log("%s(", name);
    print_socket_domain(domain);
    qemu_log(",");
    print_socket_type(type);
    qemu_log(",");
    if (domain == AF_PACKET ||
        (domain == AF_INET && type == TARGET_SOCK_PACKET)) {
        protocol = tswap16(protocol);
    }
    print_socket_protocol(domain, type, protocol);
    qemu_log(")");
}

static void do_print_sockaddr(const char *name, abi_long arg1)
{
    abi_ulong sockfd, addr, addrlen;

    get_user_ualx(sockfd, arg1, 0);
    get_user_ualx(addr, arg1, 1);
    get_user_ualx(addrlen, arg1, 2);

    qemu_log("%s(", name);
    print_sockfd(sockfd, 0);
    print_sockaddr(addr, addrlen, 0);
    qemu_log(")");
}

static void do_print_listen(const char *name, abi_long arg1)
{
    abi_ulong sockfd, backlog;

    get_user_ualx(sockfd, arg1, 0);
    get_user_ualx(backlog, arg1, 1);

    qemu_log("%s(", name);
    print_sockfd(sockfd, 0);
    print_raw_param(TARGET_ABI_FMT_ld, backlog, 1);
    qemu_log(")");
}

static void do_print_socketpair(const char *name, abi_long arg1)
{
    abi_ulong domain, type, protocol, tab;

    get_user_ualx(domain, arg1, 0);
    get_user_ualx(type, arg1, 1);
    get_user_ualx(protocol, arg1, 2);
    get_user_ualx(tab, arg1, 3);

    qemu_log("%s(", name);
    print_socket_domain(domain);
    qemu_log(",");
    print_socket_type(type);
    qemu_log(",");
    print_socket_protocol(domain, type, protocol);
    qemu_log(",");
    print_raw_param(TARGET_ABI_FMT_lx, tab, 1);
    qemu_log(")");
}

static void do_print_sendrecv(const char *name, abi_long arg1)
{
    abi_ulong sockfd, msg, len, flags;

    get_user_ualx(sockfd, arg1, 0);
    get_user_ualx(msg, arg1, 1);
    get_user_ualx(len, arg1, 2);
    get_user_ualx(flags, arg1, 3);

    qemu_log("%s(", name);
    print_sockfd(sockfd, 0);
    print_buf_len(msg, len, 0);
    print_flags(msg_flags, flags, 1);
    qemu_log(")");
}

static void do_print_msgaddr(const char *name, abi_long arg1)
{
    abi_ulong sockfd, msg, len, flags, addr, addrlen;

    get_user_ualx(sockfd, arg1, 0);
    get_user_ualx(msg, arg1, 1);
    get_user_ualx(len, arg1, 2);
    get_user_ualx(flags, arg1, 3);
    get_user_ualx(addr, arg1, 4);
    get_user_ualx(addrlen, arg1, 5);

    qemu_log("%s(", name);
    print_sockfd(sockfd, 0);
    print_buf_len(msg, len, 0);
    print_flags(msg_flags, flags, 0);
    print_sockaddr(addr, addrlen, 0);
    qemu_log(")");
}

static void do_print_shutdown(const char *name, abi_long arg1)
{
    abi_ulong sockfd, how;

    get_user_ualx(sockfd, arg1, 0);
    get_user_ualx(how, arg1, 1);

    qemu_log("shutdown(");
    print_sockfd(sockfd, 0);
    switch (how) {
    case SHUT_RD:
        qemu_log("SHUT_RD");
        break;
    case SHUT_WR:
        qemu_log("SHUT_WR");
        break;
    case SHUT_RDWR:
        qemu_log("SHUT_RDWR");
        break;
    default:
        print_raw_param(TARGET_ABI_FMT_ld, how, 1);
        break;
    }
    qemu_log(")");
}

static void do_print_msg(const char *name, abi_long arg1)
{
    abi_ulong sockfd, msg, flags;

    get_user_ualx(sockfd, arg1, 0);
    get_user_ualx(msg, arg1, 1);
    get_user_ualx(flags, arg1, 2);

    qemu_log("%s(", name);
    print_sockfd(sockfd, 0);
    print_pointer(msg, 0);
    print_flags(msg_flags, flags, 1);
    qemu_log(")");
}

static void do_print_sockopt(const char *name, abi_long arg1)
{
    abi_ulong sockfd, level, optname, optval, optlen;

    get_user_ualx(sockfd, arg1, 0);
    get_user_ualx(level, arg1, 1);
    get_user_ualx(optname, arg1, 2);
    get_user_ualx(optval, arg1, 3);
    get_user_ualx(optlen, arg1, 4);

    qemu_log("%s(", name);
    print_sockfd(sockfd, 0);
    switch (level) {
    case SOL_TCP:
        qemu_log("SOL_TCP,");
        print_raw_param(TARGET_ABI_FMT_ld, optname, 0);
        print_pointer(optval, 0);
        break;
    case SOL_UDP:
        qemu_log("SOL_UDP,");
        print_raw_param(TARGET_ABI_FMT_ld, optname, 0);
        print_pointer(optval, 0);
        break;
    case SOL_IP:
        qemu_log("SOL_IP,");
        print_raw_param(TARGET_ABI_FMT_ld, optname, 0);
        print_pointer(optval, 0);
        break;
    case SOL_RAW:
        qemu_log("SOL_RAW,");
        print_raw_param(TARGET_ABI_FMT_ld, optname, 0);
        print_pointer(optval, 0);
        break;
    case TARGET_SOL_SOCKET:
        qemu_log("SOL_SOCKET,");
        switch (optname) {
        case TARGET_SO_DEBUG:
            qemu_log("SO_DEBUG,");
print_optint:
            print_number(optval, 0);
            break;
        case TARGET_SO_REUSEADDR:
            qemu_log("SO_REUSEADDR,");
            goto print_optint;
        case TARGET_SO_REUSEPORT:
            qemu_log("SO_REUSEPORT,");
            goto print_optint;
        case TARGET_SO_TYPE:
            qemu_log("SO_TYPE,");
            goto print_optint;
        case TARGET_SO_ERROR:
            qemu_log("SO_ERROR,");
            goto print_optint;
        case TARGET_SO_DONTROUTE:
            qemu_log("SO_DONTROUTE,");
            goto print_optint;
        case TARGET_SO_BROADCAST:
            qemu_log("SO_BROADCAST,");
            goto print_optint;
        case TARGET_SO_SNDBUF:
            qemu_log("SO_SNDBUF,");
            goto print_optint;
        case TARGET_SO_RCVBUF:
            qemu_log("SO_RCVBUF,");
            goto print_optint;
        case TARGET_SO_KEEPALIVE:
            qemu_log("SO_KEEPALIVE,");
            goto print_optint;
        case TARGET_SO_OOBINLINE:
            qemu_log("SO_OOBINLINE,");
            goto print_optint;
        case TARGET_SO_NO_CHECK:
            qemu_log("SO_NO_CHECK,");
            goto print_optint;
        case TARGET_SO_PRIORITY:
            qemu_log("SO_PRIORITY,");
            goto print_optint;
        case TARGET_SO_BSDCOMPAT:
            qemu_log("SO_BSDCOMPAT,");
            goto print_optint;
        case TARGET_SO_PASSCRED:
            qemu_log("SO_PASSCRED,");
            goto print_optint;
        case TARGET_SO_TIMESTAMP:
            qemu_log("SO_TIMESTAMP,");
            goto print_optint;
        case TARGET_SO_RCVLOWAT:
            qemu_log("SO_RCVLOWAT,");
            goto print_optint;
        case TARGET_SO_RCVTIMEO:
            qemu_log("SO_RCVTIMEO,");
            print_timeval(optval, 0);
            break;
        case TARGET_SO_SNDTIMEO:
            qemu_log("SO_SNDTIMEO,");
            print_timeval(optval, 0);
            break;
        case TARGET_SO_ATTACH_FILTER: {
            struct target_sock_fprog *fprog;

            qemu_log("SO_ATTACH_FILTER,");

            if (lock_user_struct(VERIFY_READ, fprog, optval,  0)) {
                struct target_sock_filter *filter;
                qemu_log("{");
                if (lock_user_struct(VERIFY_READ, filter,
                                     tswapal(fprog->filter),  0)) {
                    int i;
                    for (i = 0; i < tswap16(fprog->len) - 1; i++) {
                        qemu_log("[%d]{0x%x,%d,%d,0x%x},",
                                 i, tswap16(filter[i].code),
                                 filter[i].jt, filter[i].jf,
                                 tswap32(filter[i].k));
                    }
                    qemu_log("[%d]{0x%x,%d,%d,0x%x}",
                             i, tswap16(filter[i].code),
                             filter[i].jt, filter[i].jf,
                             tswap32(filter[i].k));
                } else {
                    qemu_log(TARGET_ABI_FMT_lx, tswapal(fprog->filter));
                }
                qemu_log(",%d},", tswap16(fprog->len));
                unlock_user(fprog, optval, 0);
            } else {
                print_pointer(optval, 0);
            }
            break;
        }
        default:
            print_raw_param(TARGET_ABI_FMT_ld, optname, 0);
            print_pointer(optval, 0);
            break;
        }
        break;
    case SOL_IPV6:
        qemu_log("SOL_IPV6,");
        switch (optname) {
        case IPV6_MTU_DISCOVER:
            qemu_log("IPV6_MTU_DISCOVER,");
            goto print_optint;
        case IPV6_MTU:
            qemu_log("IPV6_MTU,");
            goto print_optint;
        case IPV6_V6ONLY:
            qemu_log("IPV6_V6ONLY,");
            goto print_optint;
        case IPV6_RECVPKTINFO:
            qemu_log("IPV6_RECVPKTINFO,");
            goto print_optint;
        case IPV6_UNICAST_HOPS:
            qemu_log("IPV6_UNICAST_HOPS,");
            goto print_optint;
        case IPV6_MULTICAST_HOPS:
            qemu_log("IPV6_MULTICAST_HOPS,");
            goto print_optint;
        case IPV6_MULTICAST_LOOP:
            qemu_log("IPV6_MULTICAST_LOOP,");
            goto print_optint;
        case IPV6_RECVERR:
            qemu_log("IPV6_RECVERR,");
            goto print_optint;
        case IPV6_RECVHOPLIMIT:
            qemu_log("IPV6_RECVHOPLIMIT,");
            goto print_optint;
        case IPV6_2292HOPLIMIT:
            qemu_log("IPV6_2292HOPLIMIT,");
            goto print_optint;
        case IPV6_CHECKSUM:
            qemu_log("IPV6_CHECKSUM,");
            goto print_optint;
        case IPV6_ADDRFORM:
            qemu_log("IPV6_ADDRFORM,");
            goto print_optint;
        case IPV6_2292PKTINFO:
            qemu_log("IPV6_2292PKTINFO,");
            goto print_optint;
        case IPV6_RECVTCLASS:
            qemu_log("IPV6_RECVTCLASS,");
            goto print_optint;
        case IPV6_RECVRTHDR:
            qemu_log("IPV6_RECVRTHDR,");
            goto print_optint;
        case IPV6_2292RTHDR:
            qemu_log("IPV6_2292RTHDR,");
            goto print_optint;
        case IPV6_RECVHOPOPTS:
            qemu_log("IPV6_RECVHOPOPTS,");
            goto print_optint;
        case IPV6_2292HOPOPTS:
            qemu_log("IPV6_2292HOPOPTS,");
            goto print_optint;
        case IPV6_RECVDSTOPTS:
            qemu_log("IPV6_RECVDSTOPTS,");
            goto print_optint;
        case IPV6_2292DSTOPTS:
            qemu_log("IPV6_2292DSTOPTS,");
            goto print_optint;
        case IPV6_TCLASS:
            qemu_log("IPV6_TCLASS,");
            goto print_optint;
        case IPV6_ADDR_PREFERENCES:
            qemu_log("IPV6_ADDR_PREFERENCES,");
            goto print_optint;
#ifdef IPV6_RECVPATHMTU
        case IPV6_RECVPATHMTU:
            qemu_log("IPV6_RECVPATHMTU,");
            goto print_optint;
#endif
#ifdef IPV6_TRANSPARENT
        case IPV6_TRANSPARENT:
            qemu_log("IPV6_TRANSPARENT,");
            goto print_optint;
#endif
#ifdef IPV6_FREEBIND
        case IPV6_FREEBIND:
            qemu_log("IPV6_FREEBIND,");
            goto print_optint;
#endif
#ifdef IPV6_RECVORIGDSTADDR
        case IPV6_RECVORIGDSTADDR:
            qemu_log("IPV6_RECVORIGDSTADDR,");
            goto print_optint;
#endif
        case IPV6_PKTINFO:
            qemu_log("IPV6_PKTINFO,");
            print_pointer(optval, 0);
            break;
        case IPV6_ADD_MEMBERSHIP:
            qemu_log("IPV6_ADD_MEMBERSHIP,");
            print_pointer(optval, 0);
            break;
        case IPV6_DROP_MEMBERSHIP:
            qemu_log("IPV6_DROP_MEMBERSHIP,");
            print_pointer(optval, 0);
            break;
        default:
            print_raw_param(TARGET_ABI_FMT_ld, optname, 0);
            print_pointer(optval, 0);
            break;
        }
        break;
    default:
        print_raw_param(TARGET_ABI_FMT_ld, level, 0);
        print_raw_param(TARGET_ABI_FMT_ld, optname, 0);
        print_pointer(optval, 0);
        break;
    }
    print_raw_param(TARGET_ABI_FMT_ld, optlen, 1);
    qemu_log(")");
}

#define PRINT_SOCKOP(name, func) \
    [TARGET_SYS_##name] = { #name, func }

static struct {
    const char *name;
    void (*print)(const char *, abi_long);
} scall[] = {
    PRINT_SOCKOP(SOCKET, do_print_socket),
    PRINT_SOCKOP(BIND, do_print_sockaddr),
    PRINT_SOCKOP(CONNECT, do_print_sockaddr),
    PRINT_SOCKOP(LISTEN, do_print_listen),
    PRINT_SOCKOP(ACCEPT, do_print_sockaddr),
    PRINT_SOCKOP(GETSOCKNAME, do_print_sockaddr),
    PRINT_SOCKOP(GETPEERNAME, do_print_sockaddr),
    PRINT_SOCKOP(SOCKETPAIR, do_print_socketpair),
    PRINT_SOCKOP(SEND, do_print_sendrecv),
    PRINT_SOCKOP(RECV, do_print_sendrecv),
    PRINT_SOCKOP(SENDTO, do_print_msgaddr),
    PRINT_SOCKOP(RECVFROM, do_print_msgaddr),
    PRINT_SOCKOP(SHUTDOWN, do_print_shutdown),
    PRINT_SOCKOP(SETSOCKOPT, do_print_sockopt),
    PRINT_SOCKOP(GETSOCKOPT, do_print_sockopt),
    PRINT_SOCKOP(SENDMSG, do_print_msg),
    PRINT_SOCKOP(RECVMSG, do_print_msg),
    PRINT_SOCKOP(ACCEPT4, NULL),
    PRINT_SOCKOP(RECVMMSG, NULL),
    PRINT_SOCKOP(SENDMMSG, NULL),
};

static void
print_socketcall(CPUArchState *cpu_env, const struct syscallname *name,
                 abi_long arg0, abi_long arg1, abi_long arg2,
                 abi_long arg3, abi_long arg4, abi_long arg5)
{
    if (arg0 >= 0 && arg0 < ARRAY_SIZE(scall) && scall[arg0].print) {
        scall[arg0].print(scall[arg0].name, arg1);
        return;
    }
    print_syscall_prologue(name);
    print_raw_param(TARGET_ABI_FMT_ld, arg0, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg1, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg2, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg3, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg4, 0);
    print_raw_param(TARGET_ABI_FMT_ld, arg5, 0);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_bind)
static void
print_bind(CPUArchState *cpu_env, const struct syscallname *name,
           abi_long arg0, abi_long arg1, abi_long arg2,
           abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_sockfd(arg0, 0);
    print_sockaddr(arg1, arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_recvfrom
static void
print_recvfrom(CPUArchState *cpu_env, const struct syscallname *name,
               abi_long arg0, abi_long arg1, abi_long arg2,
               abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_sockfd(arg0, 0);
    print_pointer(arg1, 0); /* output */
    print_raw_param(TARGET_ABI_FMT_ld, arg2, 0);
    print_flags(msg_flags, arg3, 0);
    print_pointer(arg4, 0); /* output */
    print_pointer(arg5, 1); /* in/out */
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_sendto
static void
print_sendto(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg0, abi_long arg1, abi_long arg2,
             abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_sockfd(arg0, 0);
    print_buf_len(arg1, arg2, 0);
    print_flags(msg_flags, arg3, 0);
    print_sockaddr(arg4, arg5, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_stat) || defined(TARGET_NR_stat64) || \
    defined(TARGET_NR_lstat) || defined(TARGET_NR_lstat64)
static void
print_stat(CPUArchState *cpu_env, const struct syscallname *name,
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

#if defined(TARGET_NR_madvise)
static struct enums madvise_advice[] = {
    ENUM_TARGET(MADV_NORMAL),
    ENUM_TARGET(MADV_RANDOM),
    ENUM_TARGET(MADV_SEQUENTIAL),
    ENUM_TARGET(MADV_WILLNEED),
    ENUM_TARGET(MADV_DONTNEED),
    ENUM_TARGET(MADV_FREE),
    ENUM_TARGET(MADV_REMOVE),
    ENUM_TARGET(MADV_DONTFORK),
    ENUM_TARGET(MADV_DOFORK),
    ENUM_TARGET(MADV_MERGEABLE),
    ENUM_TARGET(MADV_UNMERGEABLE),
    ENUM_TARGET(MADV_HUGEPAGE),
    ENUM_TARGET(MADV_NOHUGEPAGE),
    ENUM_TARGET(MADV_DONTDUMP),
    ENUM_TARGET(MADV_DODUMP),
    ENUM_TARGET(MADV_WIPEONFORK),
    ENUM_TARGET(MADV_KEEPONFORK),
    ENUM_TARGET(MADV_COLD),
    ENUM_TARGET(MADV_PAGEOUT),
    ENUM_TARGET(MADV_POPULATE_READ),
    ENUM_TARGET(MADV_POPULATE_WRITE),
    ENUM_TARGET(MADV_DONTNEED_LOCKED),
    ENUM_END,
};

static void
print_madvise(CPUArchState *cpu_env, const struct syscallname *name,
              abi_long arg0, abi_long arg1, abi_long arg2,
              abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_enums(madvise_advice, arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_fstat) || defined(TARGET_NR_fstat64)
static void
print_fstat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_mkdir(CPUArchState *cpu_env, const struct syscallname *name,
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
print_mkdirat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_rmdir(CPUArchState *cpu_env, const struct syscallname *name,
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
print_rt_sigaction(CPUArchState *cpu_env, const struct syscallname *name,
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
print_rt_sigprocmask(CPUArchState *cpu_env, const struct syscallname *name,
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
    qemu_log("%s,", how);
    print_target_sigset_t(arg1, arg3, 0);
    print_pointer(arg2, 0);
    print_raw_param("%u", arg3, 1);
    print_syscall_epilogue(name);
}

static void
print_rt_sigprocmask_ret(CPUArchState *cpu_env, const struct syscallname *name,
                         abi_long ret, abi_long arg0, abi_long arg1,
                         abi_long arg2, abi_long arg3, abi_long arg4,
                         abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        if (arg2) {
            qemu_log(" (oldset=");
            print_target_sigset_t(arg2, arg3, 1);
            qemu_log(")");
        }
    }

    qemu_log("\n");
}
#endif

#ifdef TARGET_NR_rt_sigqueueinfo
static void
print_rt_sigqueueinfo(CPUArchState *cpu_env, const struct syscallname *name,
                      abi_long arg0, abi_long arg1, abi_long arg2,
                      abi_long arg3, abi_long arg4, abi_long arg5)
{
    void *p;
    target_siginfo_t uinfo;

    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_signal(arg1, 0);
    p = lock_user(VERIFY_READ, arg2, sizeof(target_siginfo_t), 1);
    if (p) {
        get_target_siginfo(&uinfo, p);
        print_siginfo(&uinfo);

        unlock_user(p, arg2, 0);
    } else {
        print_pointer(arg2, 1);
    }
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_rt_tgsigqueueinfo
static void
print_rt_tgsigqueueinfo(CPUArchState *cpu_env, const struct syscallname *name,
                        abi_long arg0, abi_long arg1, abi_long arg2,
                        abi_long arg3, abi_long arg4, abi_long arg5)
{
    void *p;
    target_siginfo_t uinfo;

    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_signal(arg2, 0);
    p = lock_user(VERIFY_READ, arg3, sizeof(target_siginfo_t), 1);
    if (p) {
        get_target_siginfo(&uinfo, p);
        print_siginfo(&uinfo);

        unlock_user(p, arg3, 0);
    } else {
        print_pointer(arg3, 1);
    }
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_syslog
static void
print_syslog_action(abi_ulong arg, int last)
{
    const char *type;

    switch (arg) {
        case TARGET_SYSLOG_ACTION_CLOSE: {
            type = "SYSLOG_ACTION_CLOSE";
            break;
        }
        case TARGET_SYSLOG_ACTION_OPEN: {
            type = "SYSLOG_ACTION_OPEN";
            break;
        }
        case TARGET_SYSLOG_ACTION_READ: {
            type = "SYSLOG_ACTION_READ";
            break;
        }
        case TARGET_SYSLOG_ACTION_READ_ALL: {
            type = "SYSLOG_ACTION_READ_ALL";
            break;
        }
        case TARGET_SYSLOG_ACTION_READ_CLEAR: {
            type = "SYSLOG_ACTION_READ_CLEAR";
            break;
        }
        case TARGET_SYSLOG_ACTION_CLEAR: {
            type = "SYSLOG_ACTION_CLEAR";
            break;
        }
        case TARGET_SYSLOG_ACTION_CONSOLE_OFF: {
            type = "SYSLOG_ACTION_CONSOLE_OFF";
            break;
        }
        case TARGET_SYSLOG_ACTION_CONSOLE_ON: {
            type = "SYSLOG_ACTION_CONSOLE_ON";
            break;
        }
        case TARGET_SYSLOG_ACTION_CONSOLE_LEVEL: {
            type = "SYSLOG_ACTION_CONSOLE_LEVEL";
            break;
        }
        case TARGET_SYSLOG_ACTION_SIZE_UNREAD: {
            type = "SYSLOG_ACTION_SIZE_UNREAD";
            break;
        }
        case TARGET_SYSLOG_ACTION_SIZE_BUFFER: {
            type = "SYSLOG_ACTION_SIZE_BUFFER";
            break;
        }
        default: {
            print_raw_param("%ld", arg, last);
            return;
        }
    }
    qemu_log("%s%s", type, get_comma(last));
}

static void
print_syslog(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg0, abi_long arg1, abi_long arg2,
             abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_syslog_action(arg0, 0);
    print_pointer(arg1, 0);
    print_raw_param("%d", arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mknod
static void
print_mknod(CPUArchState *cpu_env, const struct syscallname *name,
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
print_mknodat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_mq_open(CPUArchState *cpu_env, const struct syscallname *name,
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
print_open(CPUArchState *cpu_env, const struct syscallname *name,
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
print_openat(CPUArchState *cpu_env, const struct syscallname *name,
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

#ifdef TARGET_NR_openat2
static void
print_openat2(CPUArchState *cpu_env, const struct syscallname *name,
              abi_long arg0, abi_long arg1, abi_long arg2,
              abi_long arg3, abi_long arg4, abi_long arg5)
{
    struct open_how_ver0 how;

    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);

    if ((abi_ulong)arg3 >= sizeof(struct target_open_how_ver0) &&
        copy_struct_from_user(&how, sizeof(how), arg2, arg3) == 0) {
        how.flags = tswap64(how.flags);
        how.mode = tswap64(how.mode);
        how.resolve = tswap64(how.resolve);
        qemu_log("{");
        print_open_flags(how.flags, 0);
        if (how.flags & TARGET_O_CREAT) {
            print_file_mode(how.mode, 0);
        }
        print_flags(openat2_resolve_flags, how.resolve, 1);
        qemu_log("},");
    } else {
        print_pointer(arg2, 0);
    }
    print_raw_param(TARGET_ABI_FMT_lu, arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_pidfd_send_signal
static void
print_pidfd_send_signal(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg0, abi_long arg1, abi_long arg2,
                abi_long arg3, abi_long arg4, abi_long arg5)
{
    void *p;
    target_siginfo_t uinfo;

    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_signal(arg1, 0);

    p = lock_user(VERIFY_READ, arg2, sizeof(target_siginfo_t), 1);
    if (p) {
        get_target_siginfo(&uinfo, p);
        print_siginfo(&uinfo);

        unlock_user(p, arg2, 0);
    } else {
        print_pointer(arg2, 0);
    }

    print_raw_param("%u", arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_mq_unlink
static void
print_mq_unlink(CPUArchState *cpu_env, const struct syscallname *name,
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
print_fstatat64(CPUArchState *cpu_env, const struct syscallname *name,
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
print_readlink(CPUArchState *cpu_env, const struct syscallname *name,
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
print_readlinkat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_rename(CPUArchState *cpu_env, const struct syscallname *name,
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
print_renameat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_statfs(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg0, abi_long arg1, abi_long arg2,
             abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_statfs64
static void
print_statfs64(CPUArchState *cpu_env, const struct syscallname *name,
               abi_long arg0, abi_long arg1, abi_long arg2,
               abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_string(arg0, 0);
    print_pointer(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_symlink
static void
print_symlink(CPUArchState *cpu_env, const struct syscallname *name,
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
print_symlinkat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_mount(CPUArchState *cpu_env, const struct syscallname *name,
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
print_umount(CPUArchState *cpu_env, const struct syscallname *name,
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
print_umount2(CPUArchState *cpu_env, const struct syscallname *name,
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
print_unlink(CPUArchState *cpu_env, const struct syscallname *name,
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
print_unlinkat(CPUArchState *cpu_env, const struct syscallname *name,
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

#ifdef TARGET_NR_unshare
static void
print_unshare(CPUArchState *cpu_env, const struct syscallname *name,
              abi_long arg0, abi_long arg1, abi_long arg2,
              abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_flags(clone_flags, arg0, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_clock_nanosleep
static void
print_clock_nanosleep(CPUArchState *cpu_env, const struct syscallname *name,
                abi_long arg0, abi_long arg1, abi_long arg2,
                abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_enums(clockids, arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_timespec(arg2, 0);
    print_timespec(arg3, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_utime
static void
print_utime(CPUArchState *cpu_env, const struct syscallname *name,
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
print_utimes(CPUArchState *cpu_env, const struct syscallname *name,
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
print_utimensat(CPUArchState *cpu_env, const struct syscallname *name,
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
print_mmap_both(CPUArchState *cpu_env, const struct syscallname *name,
           abi_long arg0, abi_long arg1, abi_long arg2,
           abi_long arg3, abi_long arg4, abi_long arg5,
           bool is_old_mmap)
{
    if (is_old_mmap) {
            abi_ulong *v;
            abi_ulong argp = arg0;
            if (!(v = lock_user(VERIFY_READ, argp, 6 * sizeof(abi_ulong), 1)))
                return;
            arg0 = tswapal(v[0]);
            arg1 = tswapal(v[1]);
            arg2 = tswapal(v[2]);
            arg3 = tswapal(v[3]);
            arg4 = tswapal(v[4]);
            arg5 = tswapal(v[5]);
            unlock_user(v, argp, 0);
        }
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_flags(mmap_prot_flags, arg2, 0);
    print_flags(mmap_flags, arg3, 0);
    print_raw_param("%d", arg4, 0);
    print_raw_param("%#x", arg5, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_mmap)
static void
print_mmap(CPUArchState *cpu_env, const struct syscallname *name,
           abi_long arg0, abi_long arg1, abi_long arg2,
           abi_long arg3, abi_long arg4, abi_long arg5)
{
    return print_mmap_both(cpu_env, name, arg0, arg1, arg2, arg3,
                           arg4, arg5,
#ifdef TARGET_ARCH_WANT_SYS_OLD_MMAP
                            true
#else
                            false
#endif
                            );
}
#endif

#if defined(TARGET_NR_mmap2)
static void
print_mmap2(CPUArchState *cpu_env, const struct syscallname *name,
           abi_long arg0, abi_long arg1, abi_long arg2,
           abi_long arg3, abi_long arg4, abi_long arg5)
{
    return print_mmap_both(cpu_env, name, arg0, arg1, arg2, arg3,
                           arg4, arg5, false);
}
#endif

#ifdef TARGET_NR_mprotect
static void
print_mprotect(CPUArchState *cpu_env, const struct syscallname *name,
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
print_munmap(CPUArchState *cpu_env, const struct syscallname *name,
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
static void print_futex_op(int cmd, int last)
{
    static const char * const futex_names[] = {
#define NAME(X)  [X] = #X
        NAME(FUTEX_WAIT),
        NAME(FUTEX_WAKE),
        NAME(FUTEX_FD),
        NAME(FUTEX_REQUEUE),
        NAME(FUTEX_CMP_REQUEUE),
        NAME(FUTEX_WAKE_OP),
        NAME(FUTEX_LOCK_PI),
        NAME(FUTEX_UNLOCK_PI),
        NAME(FUTEX_TRYLOCK_PI),
        NAME(FUTEX_WAIT_BITSET),
        NAME(FUTEX_WAKE_BITSET),
        NAME(FUTEX_WAIT_REQUEUE_PI),
        NAME(FUTEX_CMP_REQUEUE_PI),
        NAME(FUTEX_LOCK_PI2),
#undef NAME
    };

    unsigned base_cmd = cmd & FUTEX_CMD_MASK;

    if (base_cmd < ARRAY_SIZE(futex_names)) {
        qemu_log("%s%s%s",
                 (cmd & FUTEX_PRIVATE_FLAG ? "FUTEX_PRIVATE_FLAG|" : ""),
                 (cmd & FUTEX_CLOCK_REALTIME ? "FUTEX_CLOCK_REALTIME|" : ""),
                 futex_names[base_cmd]);
    } else {
        qemu_log("0x%x", cmd);
    }
}

static void
print_futex(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    abi_long op = arg1 & FUTEX_CMD_MASK;
    print_syscall_prologue(name);
    print_pointer(arg0, 0);
    print_futex_op(arg1, 0);
    print_raw_param(",%d", arg2, 0);
    switch (op) {
        case FUTEX_WAIT:
        case FUTEX_WAIT_BITSET:
        case FUTEX_LOCK_PI:
        case FUTEX_LOCK_PI2:
        case FUTEX_WAIT_REQUEUE_PI:
            print_timespec(arg3, 0);
            break;
        default:
            print_pointer(arg3, 0);
            break;
    }
    print_pointer(arg4, 0);
    print_raw_param("%d", arg4, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_prlimit64
static const char *target_ressource_string(abi_ulong r)
{
    #define RET_RES_ENTRY(res) case TARGET_##res:  return #res;
    switch (r) {
    RET_RES_ENTRY(RLIMIT_AS);
    RET_RES_ENTRY(RLIMIT_CORE);
    RET_RES_ENTRY(RLIMIT_CPU);
    RET_RES_ENTRY(RLIMIT_DATA);
    RET_RES_ENTRY(RLIMIT_FSIZE);
    RET_RES_ENTRY(RLIMIT_LOCKS);
    RET_RES_ENTRY(RLIMIT_MEMLOCK);
    RET_RES_ENTRY(RLIMIT_MSGQUEUE);
    RET_RES_ENTRY(RLIMIT_NICE);
    RET_RES_ENTRY(RLIMIT_NOFILE);
    RET_RES_ENTRY(RLIMIT_NPROC);
    RET_RES_ENTRY(RLIMIT_RSS);
    RET_RES_ENTRY(RLIMIT_RTPRIO);
#ifdef RLIMIT_RTTIME
    RET_RES_ENTRY(RLIMIT_RTTIME);
#endif
    RET_RES_ENTRY(RLIMIT_SIGPENDING);
    RET_RES_ENTRY(RLIMIT_STACK);
    default:
        return NULL;
    }
    #undef RET_RES_ENTRY
}

static void
print_rlimit64(abi_ulong rlim_addr, int last)
{
    if (rlim_addr) {
        struct target_rlimit64 *rl;

        rl = lock_user(VERIFY_READ, rlim_addr, sizeof(*rl), 1);
        if (!rl) {
            print_pointer(rlim_addr, last);
            return;
        }
        print_raw_param64("{rlim_cur=%" PRId64, tswap64(rl->rlim_cur), 0);
        print_raw_param64("rlim_max=%" PRId64 "}", tswap64(rl->rlim_max),
                            last);
        unlock_user(rl, rlim_addr, 0);
    } else {
        qemu_log("NULL%s", get_comma(last));
    }
}

static void
print_prlimit64(CPUArchState *cpu_env, const struct syscallname *name,
           abi_long arg0, abi_long arg1, abi_long arg2,
           abi_long arg3, abi_long arg4, abi_long arg5)
{
    const char *rlim_name;

    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    rlim_name = target_ressource_string(arg1);
    if (rlim_name) {
        qemu_log("%s,", rlim_name);
    } else {
        print_raw_param("%d", arg1, 0);
    }
    print_rlimit64(arg2, 0);
    print_pointer(arg3, 1);
    print_syscall_epilogue(name);
}

static void
print_syscall_ret_prlimit64(CPUArchState *cpu_env,
                       const struct syscallname *name,
                       abi_long ret, abi_long arg0, abi_long arg1,
                       abi_long arg2, abi_long arg3, abi_long arg4,
                       abi_long arg5)
{
    if (!print_syscall_err(ret)) {
        qemu_log(TARGET_ABI_FMT_ld, ret);
        if (arg3) {
            qemu_log(" (");
            print_rlimit64(arg3, 1);
            qemu_log(")");
        }
    }
    qemu_log("\n");
}
#endif

#ifdef TARGET_NR_kill
static void
print_kill(CPUArchState *cpu_env, const struct syscallname *name,
           abi_long arg0, abi_long arg1, abi_long arg2,
           abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_signal(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_tkill
static void
print_tkill(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_signal(arg1, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_tgkill
static void
print_tgkill(CPUArchState *cpu_env, const struct syscallname *name,
             abi_long arg0, abi_long arg1, abi_long arg2,
             abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_raw_param("%d", arg1, 0);
    print_signal(arg2, 1);
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_pread64) || defined(TARGET_NR_pwrite64)
static void
print_pread64(CPUArchState *cpu_env, const struct syscallname *name,
        abi_long arg0, abi_long arg1, abi_long arg2,
        abi_long arg3, abi_long arg4, abi_long arg5)
{
    if (regpairs_aligned(cpu_env, TARGET_NR_pread64)) {
        arg3 = arg4;
        arg4 = arg5;
    }
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);
    print_pointer(arg1, 0);
    print_raw_param("%d", arg2, 0);
    print_raw_param("%" PRIu64, target_offset64(arg3, arg4), 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_statx
static void
print_statx(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_at_dirfd(arg0, 0);
    print_string(arg1, 0);
    print_flags(statx_flags, arg2, 0);
    print_flags(statx_mask, arg3, 0);
    print_pointer(arg4, 1);
    print_syscall_epilogue(name);
}
#endif

#ifdef TARGET_NR_ioctl
static void
print_ioctl(CPUArchState *cpu_env, const struct syscallname *name,
            abi_long arg0, abi_long arg1, abi_long arg2,
            abi_long arg3, abi_long arg4, abi_long arg5)
{
    print_syscall_prologue(name);
    print_raw_param("%d", arg0, 0);

    const IOCTLEntry *ie;
    const argtype *arg_type;
    void *argptr;
    int target_size;

    for (ie = ioctl_entries; ie->target_cmd != 0; ie++) {
        if (ie->target_cmd == arg1) {
            break;
        }
    }

    if (ie->target_cmd == 0) {
        print_raw_param("%#x", arg1, 0);
        print_raw_param("%#x", arg2, 1);
    } else {
        qemu_log("%s", ie->name);
        arg_type = ie->arg_type;

        if (arg_type[0] != TYPE_NULL) {
            qemu_log(",");

            switch (arg_type[0]) {
            case TYPE_PTRVOID:
                print_pointer(arg2, 1);
                break;
            case TYPE_CHAR:
            case TYPE_SHORT:
            case TYPE_INT:
                print_raw_param("%d", arg2, 1);
                break;
            case TYPE_LONG:
                print_raw_param(TARGET_ABI_FMT_ld, arg2, 1);
                break;
            case TYPE_ULONG:
                print_raw_param(TARGET_ABI_FMT_lu, arg2, 1);
                break;
            case TYPE_PTR:
                switch (ie->access) {
                case IOC_R:
                    print_pointer(arg2, 1);
                    break;
                case IOC_W:
                case IOC_RW:
                    arg_type++;
                    target_size = thunk_type_size(arg_type, 0);
                    argptr = lock_user(VERIFY_READ, arg2, target_size, 1);
                    if (argptr) {
                        thunk_print(argptr, arg_type);
                        unlock_user(argptr, arg2, target_size);
                    } else {
                        print_pointer(arg2, 1);
                    }
                    break;
                }
                break;
            default:
                g_assert_not_reached();
            }
        }
    }
    print_syscall_epilogue(name);
}
#endif

#if defined(TARGET_NR_wait4) || defined(TARGET_NR_waitpid)
static void print_wstatus(int wstatus)
{
    if (WIFSIGNALED(wstatus)) {
        qemu_log("{WIFSIGNALED(s) && WTERMSIG(s) == ");
        print_signal(WTERMSIG(wstatus), 1);
        if (WCOREDUMP(wstatus)) {
            qemu_log(" && WCOREDUMP(s)");
        }
        qemu_log("}");
    } else if (WIFEXITED(wstatus)) {
        qemu_log("{WIFEXITED(s) && WEXITSTATUS(s) == %d}",
                 WEXITSTATUS(wstatus));
    } else {
        print_number(wstatus, 1);
    }
}

static void print_ret_wstatus(abi_long ret, abi_long wstatus_addr)
{
    int wstatus;

    if (!print_syscall_err(ret)
        && wstatus_addr
        && get_user_s32(wstatus, wstatus_addr)) {
        qemu_log(TARGET_ABI_FMT_ld " (wstatus=", ret);
        print_wstatus(wstatus);
        qemu_log(")");
    }
    qemu_log("\n");
}
#endif

#ifdef TARGET_NR_wait4
static void
print_syscall_ret_wait4(CPUArchState *cpu_env,
                        const struct syscallname *name,
                        abi_long ret, abi_long arg0, abi_long arg1,
                        abi_long arg2, abi_long arg3, abi_long arg4,
                        abi_long arg5)
{
    print_ret_wstatus(ret, arg1);
}
#endif

#ifdef TARGET_NR_waitpid
static void
print_syscall_ret_waitpid(CPUArchState *cpu_env,
                          const struct syscallname *name,
                          abi_long ret, abi_long arg0, abi_long arg1,
                          abi_long arg2, abi_long arg3, abi_long arg4,
                          abi_long arg5)
{
    print_ret_wstatus(ret, arg1);
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
print_syscall(CPUArchState *cpu_env, int num,
              abi_long arg1, abi_long arg2, abi_long arg3,
              abi_long arg4, abi_long arg5, abi_long arg6)
{
    int i;
    FILE *f;
    const char *format = "%s(" TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ","
                               TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ","
                               TARGET_ABI_FMT_ld "," TARGET_ABI_FMT_ld ")";

    f = qemu_log_trylock();
    if (!f) {
        return;
    }
    fprintf(f, "%d ", get_task_state(env_cpu(cpu_env))->ts_tid);

    for (i = 0; i < nsyscalls; i++) {
        if (scnames[i].nr == num) {
            if (scnames[i].call != NULL) {
                scnames[i].call(cpu_env, &scnames[i], arg1, arg2, arg3,
                                arg4, arg5, arg6);
            } else {
                /* XXX: this format system is broken because it uses
                   host types and host pointers for strings */
                if (scnames[i].format != NULL) {
                    format = scnames[i].format;
                }
                fprintf(f, format, scnames[i].name, arg1, arg2,
                        arg3, arg4, arg5, arg6);
            }
            qemu_log_unlock(f);
            return;
        }
    }
    fprintf(f, "Unknown syscall %d\n", num);
    qemu_log_unlock(f);
}


void
print_syscall_ret(CPUArchState *cpu_env, int num, abi_long ret,
                  abi_long arg1, abi_long arg2, abi_long arg3,
                  abi_long arg4, abi_long arg5, abi_long arg6)
{
    int i;
    FILE *f;

    f = qemu_log_trylock();
    if (!f) {
        return;
    }

    for (i = 0; i < nsyscalls; i++) {
        if (scnames[i].nr == num) {
            if (scnames[i].result != NULL) {
                scnames[i].result(cpu_env, &scnames[i], ret,
                                  arg1, arg2, arg3,
                                  arg4, arg5, arg6);
            } else {
                if (!print_syscall_err(ret)) {
                    fprintf(f, TARGET_ABI_FMT_ld, ret);
                }
                fprintf(f, "\n");
            }
            break;
        }
    }
    qemu_log_unlock(f);
}

void print_taken_signal(int target_signum, const target_siginfo_t *tinfo)
{
    /* Print the strace output for a signal being taken:
     * --- SIGSEGV {si_signo=SIGSEGV, si_code=SI_KERNEL, si_addr=0} ---
     */
    FILE *f;

    f = qemu_log_trylock();
    if (!f) {
        return;
    }

    fprintf(f, "--- ");
    print_signal(target_signum, 1);
    fprintf(f, " ");
    print_siginfo(tinfo);
    fprintf(f, " ---\n");
    qemu_log_unlock(f);
}
