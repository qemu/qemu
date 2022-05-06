/*
 *  sys/user.h definitions
 *
 *  Copyright (c) 2015 Stacey D. Son (sson at FreeBSD)
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

#ifndef TARGET_OS_USER_H
#define TARGET_OS_USER_H

/*
 * from sys/priority.h
 */
struct target_priority {
    uint8_t     pri_class;      /* Scheduling class. */
    uint8_t     pri_level;      /* Normal priority level. */
    uint8_t     pri_native;     /* Priority before propogation. */
    uint8_t     pri_user;       /* User priority based on p_cpu and p_nice. */
};

/*
 * sys/caprights.h
 */
#define TARGET_CAP_RIGHTS_VERSION  0

typedef struct target_cap_rights {
    uint64_t    cr_rights[TARGET_CAP_RIGHTS_VERSION + 2];
} target_cap_rights_t;

/*
 * From sys/_socketaddr_storage.h
 *
 */
#define TARGET_SS_MAXSIZE     128U
#define TARGET_SS_ALIGNSIZE   (sizeof(__int64_t))
#define TARGET_SS_PAD1SIZE    (TARGET_SS_ALIGNSIZE - sizeof(unsigned char) - \
        sizeof(uint8_t))
#define TARGET_SS_PAD2SIZE    (TARGET_SS_MAXSIZE - sizeof(unsigned char) - \
        sizeof(uint8_t) - TARGET_SS_PAD1SIZE - TARGET_SS_ALIGNSIZE)

struct target_sockaddr_storage {
    unsigned char   ss_len;         /* address length */
    uint8_t         ss_family;      /* address family */
    char            __ss_pad1[TARGET_SS_PAD1SIZE];
    __int64_t       __ss_align;     /* force desired struct alignment */
    char            __ss_pad2[TARGET_SS_PAD2SIZE];
};

/*
 * from sys/user.h
 */
#define TARGET_KI_NSPARE_INT        2
#define TARGET_KI_NSPARE_LONG       12
#define TARGET_KI_NSPARE_PTR        6

#define TARGET_WMESGLEN             8
#define TARGET_LOCKNAMELEN          8
#define TARGET_TDNAMLEN             16
#define TARGET_COMMLEN              19
#define TARGET_KI_EMULNAMELEN       16
#define TARGET_KI_NGROUPS           16
#define TARGET_LOGNAMELEN           17
#define TARGET_LOGINCLASSLEN        17

#define TARGET_KF_TYPE_NONE         0
#define TARGET_KF_TYPE_VNODE        1
#define TARGET_KF_TYPE_SOCKET       2
#define TARGET_KF_TYPE_PIPE         3
#define TARGET_KF_TYPE_FIFO         4
#define TARGET_KF_TYPE_KQUEUE       5
#define TARGET_KF_TYPE_CRYPTO       6
#define TARGET_KF_TYPE_MQUEUE       7
#define TARGET_KF_TYPE_SHM          8
#define TARGET_KF_TYPE_SEM          9
#define TARGET_KF_TYPE_PTS          10
#define TARGET_KF_TYPE_PROCDESC     11
#define TARGET_KF_TYPE_DEV          12
#define TARGET_KF_TYPE_UNKNOWN      255

struct target_kinfo_proc {
    int32_t     ki_structsize;      /* size of this structure */
    int32_t     ki_layout;          /* reserved: layout identifier */
    abi_ulong   ki_args;            /* address of command arguments */
    abi_ulong   ki_paddr;           /* address of proc */
    abi_ulong   ki_addr;            /* kernel virtual addr of u-area */
    abi_ulong   ki_tracep;          /* pointer to trace file */
    abi_ulong   ki_textvp;          /* pointer to executable file */
    abi_ulong   ki_fd;              /* pointer to open file info */
    abi_ulong   ki_vmspace;         /* pointer to kernel vmspace struct */
    abi_ulong   ki_wchan;           /* sleep address */
    int32_t     ki_pid;             /* Process identifier */
    int32_t     ki_ppid;            /* parent process id */
    int32_t     ki_pgid;            /* process group id */
    int32_t     ki_tpgid;           /* tty process group id */
    int32_t     ki_sid;             /* Process session ID */
    int32_t     ki_tsid;            /* Terminal session ID */
    int16_t     ki_jobc;            /* job control counter */
    int16_t     ki_spare_short1;    /* unused (just here for alignment) */
    int32_t     ki_tdev__freebsd11; /* controlling tty dev */
    target_sigset_t ki_siglist;     /* Signals arrived but not delivered */
    target_sigset_t ki_sigmask;     /* Current signal mask */
    target_sigset_t ki_sigignore;   /* Signals being ignored */
    target_sigset_t ki_sigcatch;    /* Signals being caught by user */

    int32_t     ki_uid;             /* effective user id */
    int32_t     ki_ruid;            /* Real user id */
    int32_t     ki_svuid;           /* Saved effective user id */
    int32_t     ki_rgid;            /* Real group id */
    int32_t     ki_svgid;           /* Saved effective group id */
    int16_t     ki_ngroups;         /* number of groups */
    int16_t     ki_spare_short2;    /* unused (just here for alignment) */
    int32_t     ki_groups[TARGET_KI_NGROUPS];  /* groups */

    abi_long    ki_size;            /* virtual size */

    abi_long    ki_rssize;          /* current resident set size in pages */
    abi_long    ki_swrss;           /* resident set size before last swap */
    abi_long    ki_tsize;           /* text size (pages) XXX */
    abi_long    ki_dsize;           /* data size (pages) XXX */
    abi_long    ki_ssize;           /* stack size (pages) */

    uint16_t    ki_xstat;           /* Exit status for wait & stop signal */
    uint16_t    ki_acflag;          /* Accounting flags */

    uint32_t    ki_pctcpu;          /* %cpu for process during ki_swtime */

    uint32_t    ki_estcpu;          /* Time averaged value of ki_cpticks */
    uint32_t    ki_slptime;         /* Time since last blocked */
    uint32_t    ki_swtime;          /* Time swapped in or out */
    uint32_t    ki_cow;             /* number of copy-on-write faults */
    uint64_t    ki_runtime;         /* Real time in microsec */

    struct  target_freebsd_timeval ki_start;  /* starting time */
    struct  target_freebsd_timeval ki_childtime; /* time used by process children */

    abi_long    ki_flag;            /* P_* flags */
    abi_long    ki_kiflag;          /* KI_* flags (below) */
    int32_t     ki_traceflag;       /* Kernel trace points */
    char        ki_stat;            /* S* process status */
    int8_t      ki_nice;            /* Process "nice" value */
    char        ki_lock;            /* Process lock (prevent swap) count */
    char        ki_rqindex;         /* Run queue index */
    u_char      ki_oncpu_old;       /* Which cpu we are on (legacy) */
    u_char      ki_lastcpu_old;     /* Last cpu we were on (legacy) */
    char        ki_tdname[TARGET_TDNAMLEN + 1];  /* thread name */
    char        ki_wmesg[TARGET_WMESGLEN + 1];   /* wchan message */
    char        ki_login[TARGET_LOGNAMELEN + 1]; /* setlogin name */
    char        ki_lockname[TARGET_LOCKNAMELEN + 1]; /* lock name */
    char        ki_comm[TARGET_COMMLEN + 1];     /* command name */
    char        ki_emul[TARGET_KI_EMULNAMELEN + 1];  /* emulation name */
    char        ki_loginclass[TARGET_LOGINCLASSLEN + 1]; /* login class */

    char        ki_sparestrings[50];    /* spare string space */
    int32_t     ki_spareints[TARGET_KI_NSPARE_INT]; /* spare room for growth */
    uint64_t ki_tdev;  /* controlling tty dev */
    int32_t     ki_oncpu;           /* Which cpu we are on */
    int32_t     ki_lastcpu;         /* Last cpu we were on */
    int32_t     ki_tracer;          /* Pid of tracing process */
    int32_t     ki_flag2;           /* P2_* flags */
    int32_t     ki_fibnum;          /* Default FIB number */
    uint32_t    ki_cr_flags;        /* Credential flags */
    int32_t     ki_jid;             /* Process jail ID */
    int32_t     ki_numthreads;      /* XXXKSE number of threads in total */

    int32_t     ki_tid;             /* XXXKSE thread id */

    struct  target_priority ki_pri; /* process priority */
    struct  target_freebsd_rusage ki_rusage;  /* process rusage statistics */
        /* XXX - most fields in ki_rusage_ch are not (yet) filled in */
    struct  target_freebsd_rusage ki_rusage_ch; /* rusage of children processes */


    abi_ulong   ki_pcb;             /* kernel virtual addr of pcb */
    abi_ulong   ki_kstack;          /* kernel virtual addr of stack */
    abi_ulong   ki_udata;           /* User convenience pointer */
    abi_ulong   ki_tdaddr;          /* address of thread */

    abi_ulong   ki_spareptrs[TARGET_KI_NSPARE_PTR];  /* spare room for growth */
    abi_long    ki_sparelongs[TARGET_KI_NSPARE_LONG];/* spare room for growth */
    abi_long    ki_sflag;           /* PS_* flags */
    abi_long    ki_tdflags;         /* XXXKSE kthread flag */
};

struct target_kinfo_file {
    int32_t  kf_structsize;  /* Variable size of record. */
    int32_t  kf_type;  /* Descriptor type. */
    int32_t  kf_fd;   /* Array index. */
    int32_t  kf_ref_count;  /* Reference count. */
    int32_t  kf_flags;  /* Flags. */
    int32_t  kf_pad0;  /* Round to 64 bit alignment. */
    int64_t  kf_offset;  /* Seek location. */
    union {
        struct {
            uint32_t kf_spareint;
            /* Socket domain. */
            int  kf_sock_domain0;
            /* Socket type. */
            int  kf_sock_type0;
            /* Socket protocol. */
            int  kf_sock_protocol0;
            /* Socket address. */
            struct sockaddr_storage kf_sa_local;
            /* Peer address. */
            struct sockaddr_storage kf_sa_peer;
            /* Address of so_pcb. */
            uint64_t kf_sock_pcb;
            /* Address of inp_ppcb. */
            uint64_t kf_sock_inpcb;
            /* Address of unp_conn. */
            uint64_t kf_sock_unpconn;
            /* Send buffer state. */
            uint16_t kf_sock_snd_sb_state;
            /* Receive buffer state. */
            uint16_t kf_sock_rcv_sb_state;
            /* Round to 64 bit alignment. */
            uint32_t kf_sock_pad0;
        } kf_sock;
        struct {
            /* Vnode type. */
            int  kf_file_type;
            /* Space for future use */
            int  kf_spareint[3];
            uint64_t kf_spareint64[30];
            /* Vnode filesystem id. */
            uint64_t kf_file_fsid;
            /* File device. */
            uint64_t kf_file_rdev;
            /* Global file id. */
            uint64_t kf_file_fileid;
            /* File size. */
            uint64_t kf_file_size;
            /* Vnode filesystem id, FreeBSD 11 compat. */
            uint32_t kf_file_fsid_freebsd11;
            /* File device, FreeBSD 11 compat. */
            uint32_t kf_file_rdev_freebsd11;
            /* File mode. */
            uint16_t kf_file_mode;
            /* Round to 64 bit alignment. */
            uint16_t kf_file_pad0;
            uint32_t kf_file_pad1;
        } kf_file;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            uint32_t kf_sem_value;
            uint16_t kf_sem_mode;
        } kf_sem;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            uint64_t kf_pipe_addr;
            uint64_t kf_pipe_peer;
            uint32_t kf_pipe_buffer_cnt;
            /* Round to 64 bit alignment. */
            uint32_t kf_pipe_pad0[3];
        } kf_pipe;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            uint32_t kf_pts_dev_freebsd11;
            uint32_t kf_pts_pad0;
            uint64_t kf_pts_dev;
            /* Round to 64 bit alignment. */
            uint32_t kf_pts_pad1[4];
        } kf_pts;
        struct {
            uint32_t kf_spareint[4];
            uint64_t kf_spareint64[32];
            int32_t  kf_pid;
        } kf_proc;
    } kf_un;
    uint16_t kf_status;  /* Status flags. */
    uint16_t kf_pad1;  /* Round to 32 bit alignment. */
    int32_t  _kf_ispare0;  /* Space for more stuff. */
    target_cap_rights_t kf_cap_rights; /* Capability rights. */
    uint64_t _kf_cap_spare; /* Space for future cap_rights_t. */
    /* Truncated before copyout in sysctl */
    char  kf_path[PATH_MAX]; /* Path to file, if any. */
};

struct target_kinfo_vmentry {
    int32_t  kve_structsize;  /* Variable size of record. */
    int32_t  kve_type;   /* Type of map entry. */
    uint64_t kve_start;   /* Starting address. */
    uint64_t kve_end;   /* Finishing address. */
    uint64_t kve_offset;   /* Mapping offset in object */
    uint64_t kve_vn_fileid;   /* inode number if vnode */
    uint32_t kve_vn_fsid_freebsd11;  /* dev_t of vnode location */
    int32_t  kve_flags;   /* Flags on map entry. */
    int32_t  kve_resident;   /* Number of resident pages. */
    int32_t  kve_private_resident;  /* Number of private pages. */
    int32_t  kve_protection;  /* Protection bitmask. */
    int32_t  kve_ref_count;   /* VM obj ref count. */
    int32_t  kve_shadow_count;  /* VM obj shadow count. */
    int32_t  kve_vn_type;   /* Vnode type. */
    uint64_t kve_vn_size;   /* File size. */
    uint32_t kve_vn_rdev_freebsd11;  /* Device id if device. */
    uint16_t kve_vn_mode;   /* File mode. */
    uint16_t kve_status;   /* Status flags. */
#if (__FreeBSD_version >= 1300501 && __FreeBSD_version < 1400000) ||    \
    __FreeBSD_version >= 1400009
    union {
        uint64_t _kve_vn_fsid;  /* dev_t of vnode location */
        uint64_t _kve_obj;  /* handle of anon obj */
    } kve_type_spec;
#define kve_vn_fsid kve_type_spec._kve_vn_fsid
#define kve_obj  kve_type_spec._kve_obj
#else
    uint64_t kve_vn_fsid;   /* dev_t of vnode location */
#endif
    uint64_t kve_vn_rdev;   /* Device id if device. */
    int  _kve_ispare[8];  /* Space for more stuff. */
    /* Truncated before copyout in sysctl */
    char  kve_path[PATH_MAX];  /* Path to VM obj, if any. */
};

#endif /* TARGET_OS_USER_H */
