#ifndef TARGET_SYSCALL_H
#define TARGET_SYSCALL_H

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct target_pt_regs {
        /* Saved main processor registers. */
        target_ulong regs[32];

        /* Saved special registers. */
        target_ulong cp0_status;
        target_ulong lo;
        target_ulong hi;
        target_ulong cp0_badvaddr;
        target_ulong cp0_cause;
        target_ulong cp0_epc;
};

/* Target errno definitions taken from asm-mips/errno.h */
#undef TARGET_ENOMSG
#define TARGET_ENOMSG          35      /* Identifier removed */
#undef TARGET_EIDRM
#define TARGET_EIDRM           36      /* Identifier removed */
#undef TARGET_ECHRNG
#define TARGET_ECHRNG          37      /* Channel number out of range */
#undef TARGET_EL2NSYNC
#define TARGET_EL2NSYNC        38      /* Level 2 not synchronized */
#undef TARGET_EL3HLT
#define TARGET_EL3HLT          39      /* Level 3 halted */
#undef TARGET_EL3RST
#define TARGET_EL3RST          40      /* Level 3 reset */
#undef TARGET_ELNRNG
#define TARGET_ELNRNG          41      /* Link number out of range */
#undef TARGET_EUNATCH
#define TARGET_EUNATCH         42      /* Protocol driver not attached */
#undef TARGET_ENOCSI
#define TARGET_ENOCSI          43      /* No CSI structure available */
#undef TARGET_EL2HLT
#define TARGET_EL2HLT          44      /* Level 2 halted */
#undef TARGET_EDEADLK
#define TARGET_EDEADLK         45      /* Resource deadlock would occur */
#undef TARGET_ENOLCK
#define TARGET_ENOLCK          46      /* No record locks available */
#undef TARGET_EBADE
#define TARGET_EBADE           50      /* Invalid exchange */
#undef TARGET_EBADR
#define TARGET_EBADR           51      /* Invalid request descriptor */
#undef TARGET_EXFULL
#define TARGET_EXFULL          52      /* TARGET_Exchange full */
#undef TARGET_ENOANO
#define TARGET_ENOANO          53      /* No anode */
#undef TARGET_EBADRQC
#define TARGET_EBADRQC         54      /* Invalid request code */
#undef TARGET_EBADSLT
#define TARGET_EBADSLT         55      /* Invalid slot */
#undef TARGET_EDEADLOCK
#define TARGET_EDEADLOCK       56      /* File locking deadlock error */
#undef TARGET_EBFONT
#define TARGET_EBFONT          59      /* Bad font file format */
#undef TARGET_ENOSTR
#define TARGET_ENOSTR          60      /* Device not a stream */
#undef TARGET_ENODATA
#define TARGET_ENODATA         61      /* No data available */
#undef TARGET_ETIME
#define TARGET_ETIME           62      /* Timer expired */
#undef TARGET_ENOSR
#define TARGET_ENOSR           63      /* Out of streams resources */
#undef TARGET_ENONET
#define TARGET_ENONET          64      /* Machine is not on the network */
#undef TARGET_ENOPKG
#define TARGET_ENOPKG          65      /* Package not installed */
#undef TARGET_EREMOTE
#define TARGET_EREMOTE         66      /* Object is remote */
#undef TARGET_ENOLINK
#define TARGET_ENOLINK         67      /* Link has been severed */
#undef TARGET_EADV
#define TARGET_EADV            68      /* Advertise error */
#undef TARGET_ESRMNT
#define TARGET_ESRMNT          69      /* Srmount error */
#undef TARGET_ECOMM
#define TARGET_ECOMM           70      /* Communication error on send */
#undef TARGET_EPROTO
#define TARGET_EPROTO          71      /* Protocol error */
#undef TARGET_EDOTDOT
#define TARGET_EDOTDOT         73      /* RFS specific error */
#undef TARGET_EMULTIHOP
#define TARGET_EMULTIHOP       74      /* Multihop attempted */
#undef TARGET_EBADMSG
#define TARGET_EBADMSG         77      /* Not a data message */
#undef TARGET_ENAMETOOLONG
#define TARGET_ENAMETOOLONG    78      /* File name too long */
#undef TARGET_EOVERFLOW
#define TARGET_EOVERFLOW       79      /* Value too large for defined data type */
#undef TARGET_ENOTUNIQ
#define TARGET_ENOTUNIQ        80      /* Name not unique on network */
#undef TARGET_EBADFD
#define TARGET_EBADFD          81      /* File descriptor in bad state */
#undef TARGET_EREMCHG
#define TARGET_EREMCHG         82      /* Remote address changed */
#undef TARGET_ELIBACC
#define TARGET_ELIBACC         83      /* Can not access a needed shared library */
#undef TARGET_ELIBBAD
#define TARGET_ELIBBAD         84      /* Accessing a corrupted shared library */
#undef TARGET_ELIBSCN
#define TARGET_ELIBSCN         85      /* .lib section in a.out corrupted */
#undef TARGET_ELIBMAX
#define TARGET_ELIBMAX         86      /* Attempting to link in too many shared libraries */
#undef TARGET_ELIBEXEC
#define TARGET_ELIBEXEC        87      /* Cannot exec a shared library directly */
#undef TARGET_EILSEQ
#define TARGET_EILSEQ          88      /* Illegal byte sequence */
#undef TARGET_ENOSYS
#define TARGET_ENOSYS          89      /* Function not implemented */
#undef TARGET_ELOOP
#define TARGET_ELOOP           90      /* Too many symbolic links encountered */
#undef TARGET_ERESTART
#define TARGET_ERESTART        91      /* Interrupted system call should be restarted */
#undef TARGET_ESTRPIPE
#define TARGET_ESTRPIPE        92      /* Streams pipe error */
#undef TARGET_ENOTEMPTY
#define TARGET_ENOTEMPTY       93      /* Directory not empty */
#undef TARGET_EUSERS
#define TARGET_EUSERS          94      /* Too many users */
#undef TARGET_ENOTSOCK
#define TARGET_ENOTSOCK        95      /* Socket operation on non-socket */
#undef TARGET_EDESTADDRREQ
#define TARGET_EDESTADDRREQ    96      /* Destination address required */
#undef TARGET_EMSGSIZE
#define TARGET_EMSGSIZE        97      /* Message too long */
#undef TARGET_EPROTOTYPE
#define TARGET_EPROTOTYPE      98      /* Protocol wrong type for socket */
#undef TARGET_ENOPROTOOPT
#define TARGET_ENOPROTOOPT     99      /* Protocol not available */
#undef TARGET_EPROTONOSUPPORT
#define TARGET_EPROTONOSUPPORT 120     /* Protocol not supported */
#undef TARGET_ESOCKTNOSUPPORT
#define TARGET_ESOCKTNOSUPPORT 121     /* Socket type not supported */
#undef TARGET_EOPNOTSUPP
#define TARGET_EOPNOTSUPP      122     /* Operation not supported on transport endpoint */
#undef TARGET_EPFNOSUPPORT
#define TARGET_EPFNOSUPPORT    123     /* Protocol family not supported */
#undef TARGET_EAFNOSUPPORT
#define TARGET_EAFNOSUPPORT    124     /* Address family not supported by protocol */
#undef TARGET_EADDRINUSE
#define TARGET_EADDRINUSE      125     /* Address already in use */
#undef TARGET_EADDRNOTAVAIL
#define TARGET_EADDRNOTAVAIL   126     /* Cannot assign requested address */
#undef TARGET_ENETDOWN
#define TARGET_ENETDOWN        127     /* Network is down */
#undef TARGET_ENETUNREACH
#define TARGET_ENETUNREACH     128     /* Network is unreachable */
#undef TARGET_ENETRESET
#define TARGET_ENETRESET       129     /* Network dropped connection because of reset */
#undef TARGET_ECONNABORTED
#define TARGET_ECONNABORTED    130     /* Software caused connection abort */
#undef TARGET_ECONNRESET
#define TARGET_ECONNRESET      131     /* Connection reset by peer */
#undef TARGET_ENOBUFS
#define TARGET_ENOBUFS         132     /* No buffer space available */
#undef TARGET_EISCONN
#define TARGET_EISCONN         133     /* Transport endpoint is already connected */
#undef TARGET_ENOTCONN
#define TARGET_ENOTCONN        134     /* Transport endpoint is not connected */
#undef TARGET_EUCLEAN
#define TARGET_EUCLEAN         135     /* Structure needs cleaning */
#undef TARGET_ENOTNAM
#define TARGET_ENOTNAM         137     /* Not a XENIX named type file */
#undef TARGET_ENAVAIL
#define TARGET_ENAVAIL         138     /* No XENIX semaphores available */
#undef TARGET_EISNAM
#define TARGET_EISNAM          139     /* Is a named type file */
#undef TARGET_EREMOTEIO
#define TARGET_EREMOTEIO       140     /* Remote I/O error */
#undef TARGET_EINIT
#define TARGET_EINIT           141     /* Reserved */
#undef TARGET_EREMDEV
#define TARGET_EREMDEV         142     /* TARGET_Error 142 */
#undef TARGET_ESHUTDOWN
#define TARGET_ESHUTDOWN       143     /* Cannot send after transport endpoint shutdown */
#undef TARGET_ETOOMANYREFS
#define TARGET_ETOOMANYREFS    144     /* Too many references: cannot splice */
#undef TARGET_ETIMEDOUT
#define TARGET_ETIMEDOUT       145     /* Connection timed out */
#undef TARGET_ECONNREFUSED
#define TARGET_ECONNREFUSED    146     /* Connection refused */
#undef TARGET_EHOSTDOWN
#define TARGET_EHOSTDOWN       147     /* Host is down */
#undef TARGET_EHOSTUNREACH
#define TARGET_EHOSTUNREACH    148     /* No route to host */
#undef TARGET_EALREADY
#define TARGET_EALREADY        149     /* Operation already in progress */
#undef TARGET_EINPROGRESS
#define TARGET_EINPROGRESS     150     /* Operation now in progress */
#undef TARGET_ESTALE
#define TARGET_ESTALE          151     /* Stale NFS file handle */
#undef TARGET_ECANCELED
#define TARGET_ECANCELED       158     /* AIO operation canceled */
/*
 * These error are Linux extensions.
 */
#undef TARGET_ENOMEDIUM
#define TARGET_ENOMEDIUM       159     /* No medium found */
#undef TARGET_EMEDIUMTYPE
#define TARGET_EMEDIUMTYPE     160     /* Wrong medium type */
#undef TARGET_ENOKEY
#define TARGET_ENOKEY          161     /* Required key not available */
#undef TARGET_EKEYEXPIRED
#define TARGET_EKEYEXPIRED     162     /* Key has expired */
#undef TARGET_EKEYREVOKED
#define TARGET_EKEYREVOKED     163     /* Key has been revoked */
#undef TARGET_EKEYREJECTED
#define TARGET_EKEYREJECTED    164     /* Key was rejected by service */

/* for robust mutexes */
#undef TARGET_EOWNERDEAD
#define TARGET_EOWNERDEAD      165     /* Owner died */
#undef TARGET_ENOTRECOVERABLE
#define TARGET_ENOTRECOVERABLE 166     /* State not recoverable */


#define UNAME_MACHINE "mips64"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS
#define TARGET_MINSIGSTKSZ      2048
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#endif  /* TARGET_SYSCALL_H */
