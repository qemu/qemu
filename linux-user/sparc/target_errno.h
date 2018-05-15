#ifndef SPARC_TARGET_ERRNO_H
#define SPARC_TARGET_ERRNO_H

/* Target errno definitions taken from asm-sparc/errno.h */
#undef TARGET_EWOULDBLOCK
#define TARGET_EWOULDBLOCK     TARGET_EAGAIN /* Operation would block */
#undef TARGET_EINPROGRESS
#define TARGET_EINPROGRESS     36 /* Operation now in progress */
#undef TARGET_EALREADY
#define TARGET_EALREADY        37 /* Operation already in progress */
#undef TARGET_ENOTSOCK
#define TARGET_ENOTSOCK        38 /* Socket operation on non-socket */
#undef TARGET_EDESTADDRREQ
#define TARGET_EDESTADDRREQ    39 /* Destination address required */
#undef TARGET_EMSGSIZE
#define TARGET_EMSGSIZE        40 /* Message too long */
#undef TARGET_EPROTOTYPE
#define TARGET_EPROTOTYPE      41 /* Protocol wrong type for socket */
#undef TARGET_ENOPROTOOPT
#define TARGET_ENOPROTOOPT     42 /* Protocol not available */
#undef TARGET_EPROTONOSUPPORT
#define TARGET_EPROTONOSUPPORT  43 /* Protocol not supported */
#undef TARGET_ESOCKTNOSUPPORT
#define TARGET_ESOCKTNOSUPPORT  44 /* Socket type not supported */
#undef TARGET_EOPNOTSUPP
#define TARGET_EOPNOTSUPP      45 /* Op not supported on transport endpoint */
#undef TARGET_EPFNOSUPPORT
#define TARGET_EPFNOSUPPORT    46 /* Protocol family not supported */
#undef TARGET_EAFNOSUPPORT
#define TARGET_EAFNOSUPPORT    47 /* Address family not supported by protocol */
#undef TARGET_EADDRINUSE
#define TARGET_EADDRINUSE      48 /* Address already in use */
#undef TARGET_EADDRNOTAVAIL
#define TARGET_EADDRNOTAVAIL   49 /* Cannot assign requested address */
#undef TARGET_ENETDOWN
#define TARGET_ENETDOWN        50 /* Network is down */
#undef TARGET_ENETUNREACH
#define TARGET_ENETUNREACH     51 /* Network is unreachable */
#undef TARGET_ENETRESET
#define TARGET_ENETRESET       52 /* Net dropped connection because of reset */
#undef TARGET_ECONNABORTED
#define TARGET_ECONNABORTED    53 /* Software caused connection abort */
#undef TARGET_ECONNRESET
#define TARGET_ECONNRESET      54 /* Connection reset by peer */
#undef TARGET_ENOBUFS
#define TARGET_ENOBUFS         55 /* No buffer space available */
#undef TARGET_EISCONN
#define TARGET_EISCONN         56 /* Transport endpoint is already connected */
#undef TARGET_ENOTCONN
#define TARGET_ENOTCONN        57 /* Transport endpoint is not connected */
#undef TARGET_ESHUTDOWN
#define TARGET_ESHUTDOWN       58 /* No send after transport endpoint shutdown*/
#undef TARGET_ETOOMANYREFS
#define TARGET_ETOOMANYREFS    59 /* Too many references: cannot splice */
#undef TARGET_ETIMEDOUT
#define TARGET_ETIMEDOUT       60 /* Connection timed out */
#undef TARGET_ECONNREFUSED
#define TARGET_ECONNREFUSED    61 /* Connection refused */
#undef TARGET_ELOOP
#define TARGET_ELOOP           62 /* Too many symbolic links encountered */
#undef TARGET_ENAMETOOLONG
#define TARGET_ENAMETOOLONG    63 /* File name too long */
#undef TARGET_EHOSTDOWN
#define TARGET_EHOSTDOWN       64 /* Host is down */
#undef TARGET_EHOSTUNREACH
#define TARGET_EHOSTUNREACH    65 /* No route to host */
#undef TARGET_ENOTEMPTY
#define TARGET_ENOTEMPTY       66 /* Directory not empty */
#undef TARGET_EPROCLIM
#define TARGET_EPROCLIM        67 /* SUNOS: Too many processes */
#undef TARGET_EUSERS
#define TARGET_EUSERS          68 /* Too many users */
#undef TARGET_EDQUOT
#define TARGET_EDQUOT          69 /* Quota exceeded */
#undef TARGET_ESTALE
#define TARGET_ESTALE          70 /* Stale file handle */
#undef TARGET_EREMOTE
#define TARGET_EREMOTE         71 /* Object is remote */
#undef TARGET_ENOSTR
#define TARGET_ENOSTR          72 /* Device not a stream */
#undef TARGET_ETIME
#define TARGET_ETIME           73 /* Timer expired */
#undef TARGET_ENOSR
#define TARGET_ENOSR           74 /* Out of streams resources */
#undef TARGET_ENOMSG
#define TARGET_ENOMSG          75 /* No message of desired type */
#undef TARGET_EBADMSG
#define TARGET_EBADMSG         76 /* Not a data message */
#undef TARGET_EIDRM
#define TARGET_EIDRM           77 /* Identifier removed */
#undef TARGET_EDEADLK
#define TARGET_EDEADLK         78 /* Resource deadlock would occur */
#undef TARGET_ENOLCK
#define TARGET_ENOLCK          79 /* No record locks available */
#undef TARGET_ENONET
#define TARGET_ENONET          80 /* Machine is not on the network */
#undef TARGET_ERREMOTE
#define TARGET_ERREMOTE        81 /* SunOS: Too many lvls of remote in path */
#undef TARGET_ENOLINK
#define TARGET_ENOLINK         82 /* Link has been severed */
#undef TARGET_EADV
#define TARGET_EADV            83 /* Advertise error */
#undef TARGET_ESRMNT
#define TARGET_ESRMNT          84 /* Srmount error */
#undef TARGET_ECOMM
#define TARGET_ECOMM           85 /* Communication error on send */
#undef TARGET_EPROTO
#define TARGET_EPROTO          86 /* Protocol error */
#undef TARGET_EMULTIHOP
#define TARGET_EMULTIHOP       87 /* Multihop attempted */
#undef TARGET_EDOTDOT
#define TARGET_EDOTDOT         88 /* RFS specific error */
#undef TARGET_EREMCHG
#define TARGET_EREMCHG         89 /* Remote address changed */
#undef TARGET_ENOSYS
#define TARGET_ENOSYS          90 /* Function not implemented */
#undef TARGET_ESTRPIPE
#define TARGET_ESTRPIPE        91 /* Streams pipe error */
#undef TARGET_EOVERFLOW
#define TARGET_EOVERFLOW       92 /* Value too large for defined data type */
#undef TARGET_EBADFD
#define TARGET_EBADFD          93 /* File descriptor in bad state */
#undef TARGET_ECHRNG
#define TARGET_ECHRNG          94 /* Channel number out of range */
#undef TARGET_EL2NSYNC
#define TARGET_EL2NSYNC        95 /* Level 2 not synchronized */
#undef TARGET_EL3HLT
#define TARGET_EL3HLT          96 /* Level 3 halted */
#undef TARGET_EL3RST
#define TARGET_EL3RST          97 /* Level 3 reset */
#undef TARGET_ELNRNG
#define TARGET_ELNRNG          98 /* Link number out of range */
#undef TARGET_EUNATCH
#define TARGET_EUNATCH         99 /* Protocol driver not attached */
#undef TARGET_ENOCSI
#define TARGET_ENOCSI          100 /* No CSI structure available */
#undef TARGET_EL2HLT
#define TARGET_EL2HLT          101 /* Level 2 halted */
#undef TARGET_EBADE
#define TARGET_EBADE           102 /* Invalid exchange */
#undef TARGET_EBADR
#define TARGET_EBADR           103 /* Invalid request descriptor */
#undef TARGET_EXFULL
#define TARGET_EXFULL          104 /* Exchange full */
#undef TARGET_ENOANO
#define TARGET_ENOANO          105 /* No anode */
#undef TARGET_EBADRQC
#define TARGET_EBADRQC         106 /* Invalid request code */
#undef TARGET_EBADSLT
#define TARGET_EBADSLT         107 /* Invalid slot */
#undef TARGET_EDEADLOCK
#define TARGET_EDEADLOCK       108 /* File locking deadlock error */
#undef TARGET_EBFONT
#define TARGET_EBFONT          109 /* Bad font file format */
#undef TARGET_ELIBEXEC
#define TARGET_ELIBEXEC        110 /* Cannot exec a shared library directly */
#undef TARGET_ENODATA
#define TARGET_ENODATA         111 /* No data available */
#undef TARGET_ELIBBAD
#define TARGET_ELIBBAD         112 /* Accessing a corrupted shared library */
#undef TARGET_ENOPKG
#define TARGET_ENOPKG          113 /* Package not installed */
#undef TARGET_ELIBACC
#define TARGET_ELIBACC         114 /* Can not access a needed shared library */
#undef TARGET_ENOTUNIQ
#define TARGET_ENOTUNIQ        115 /* Name not unique on network */
#undef TARGET_ERESTART
#define TARGET_ERESTART        116 /* Interrupted syscall should be restarted */
#undef TARGET_EUCLEAN
#define TARGET_EUCLEAN         117 /* Structure needs cleaning */
#undef TARGET_ENOTNAM
#define TARGET_ENOTNAM         118 /* Not a XENIX named type file */
#undef TARGET_ENAVAIL
#define TARGET_ENAVAIL         119 /* No XENIX semaphores available */
#undef TARGET_EISNAM
#define TARGET_EISNAM          120 /* Is a named type file */
#undef TARGET_EREMOTEIO
#define TARGET_EREMOTEIO       121 /* Remote I/O error */
#undef TARGET_EILSEQ
#define TARGET_EILSEQ          122 /* Illegal byte sequence */
#undef TARGET_ELIBMAX
#define TARGET_ELIBMAX         123 /* Atmpt to link in too many shared libs */
#undef TARGET_ELIBSCN
#define TARGET_ELIBSCN         124 /* .lib section in a.out corrupted */
#undef TARGET_ENOMEDIUM
#define TARGET_ENOMEDIUM       125 /* No medium found */
#undef TARGET_EMEDIUMTYPE
#define TARGET_EMEDIUMTYPE     126 /* Wrong medium type */
#undef TARGET_ECANCELED
#define TARGET_ECANCELED       127 /* Operation Cancelled */
#undef TARGET_ENOKEY
#define TARGET_ENOKEY          128 /* Required key not available */
#undef TARGET_EKEYEXPIRED
#define TARGET_EKEYEXPIRED     129 /* Key has expired */
#undef TARGET_EKEYREVOKED
#define TARGET_EKEYREVOKED     130 /* Key has been revoked */
#undef TARGET_EKEYREJECTED
#define TARGET_EKEYREJECTED    131 /* Key was rejected by service */
#undef TARGET_EOWNERDEAD
#define TARGET_EOWNERDEAD      132 /* Owner died */
#undef TARGET_ENOTRECOVERABLE
#define TARGET_ENOTRECOVERABLE  133 /* State not recoverable */
#undef TARGET_ERFKILL
#define TARGET_ERFKILL         134 /* Operation not possible due to RF-kill */
#undef TARGET_EHWPOISON
#define TARGET_EHWPOISON       135 /* Memory page has hardware error */
#endif
