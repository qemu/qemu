#ifndef ALPHA_TARGET_ERRNO_DEFS_H
#define ALPHA_TARGET_ERRNO_DEFS_H

#include "../generic/target_errno_defs.h"

/*
 * Generic target errno overridden with definitions taken
 * from asm-alpha/errno.h
 */
#undef TARGET_EWOULDBLOCK
#define TARGET_EWOULDBLOCK      TARGET_EAGAIN
#undef TARGET_EDEADLK
#define TARGET_EDEADLK          11
#undef TARGET_EAGAIN
#define TARGET_EAGAIN           35
#undef TARGET_EINPROGRESS
#define TARGET_EINPROGRESS      36
#undef TARGET_EALREADY
#define TARGET_EALREADY         37
#undef TARGET_ENOTSOCK
#define TARGET_ENOTSOCK         38
#undef TARGET_EDESTADDRREQ
#define TARGET_EDESTADDRREQ     39
#undef TARGET_EMSGSIZE
#define TARGET_EMSGSIZE         40
#undef TARGET_EPROTOTYPE
#define TARGET_EPROTOTYPE       41
#undef TARGET_ENOPROTOOPT
#define TARGET_ENOPROTOOPT      42
#undef TARGET_EPROTONOSUPPORT
#define TARGET_EPROTONOSUPPORT  43
#undef TARGET_ESOCKTNOSUPPORT
#define TARGET_ESOCKTNOSUPPORT  44
#undef TARGET_EOPNOTSUPP
#define TARGET_EOPNOTSUPP       45
#undef TARGET_EPFNOSUPPORT
#define TARGET_EPFNOSUPPORT     46
#undef TARGET_EAFNOSUPPORT
#define TARGET_EAFNOSUPPORT     47
#undef TARGET_EADDRINUSE
#define TARGET_EADDRINUSE       48
#undef TARGET_EADDRNOTAVAIL
#define TARGET_EADDRNOTAVAIL    49
#undef TARGET_ENETDOWN
#define TARGET_ENETDOWN         50
#undef TARGET_ENETUNREACH
#define TARGET_ENETUNREACH      51
#undef TARGET_ENETRESET
#define TARGET_ENETRESET        52
#undef TARGET_ECONNABORTED
#define TARGET_ECONNABORTED     53
#undef TARGET_ECONNRESET
#define TARGET_ECONNRESET       54
#undef TARGET_ENOBUFS
#define TARGET_ENOBUFS          55
#undef TARGET_EISCONN
#define TARGET_EISCONN          56
#undef TARGET_ENOTCONN
#define TARGET_ENOTCONN         57
#undef TARGET_ESHUTDOWN
#define TARGET_ESHUTDOWN        58
#undef TARGET_ETOOMANYREFS
#define TARGET_ETOOMANYREFS     59
#undef TARGET_ETIMEDOUT
#define TARGET_ETIMEDOUT        60
#undef TARGET_ECONNREFUSED
#define TARGET_ECONNREFUSED     61
#undef TARGET_ELOOP
#define TARGET_ELOOP            62
#undef TARGET_ENAMETOOLONG
#define TARGET_ENAMETOOLONG     63
#undef TARGET_EHOSTDOWN
#define TARGET_EHOSTDOWN        64
#undef TARGET_EHOSTUNREACH
#define TARGET_EHOSTUNREACH     65
#undef TARGET_ENOTEMPTY
#define TARGET_ENOTEMPTY        66
/* Unused                       67 */
#undef TARGET_EUSERS
#define TARGET_EUSERS           68
#undef TARGET_EDQUOT
#define TARGET_EDQUOT           69
#undef TARGET_ESTALE
#define TARGET_ESTALE           70
#undef TARGET_EREMOTE
#define TARGET_EREMOTE          71
/* Unused                       72-76 */
#undef TARGET_ENOLCK
#define TARGET_ENOLCK           77
#undef TARGET_ENOSYS
#define TARGET_ENOSYS           78
/* Unused                       79 */
#undef TARGET_ENOMSG
#define TARGET_ENOMSG           80
#undef TARGET_EIDRM
#define TARGET_EIDRM            81
#undef TARGET_ENOSR
#define TARGET_ENOSR            82
#undef TARGET_ETIME
#define TARGET_ETIME            83
#undef TARGET_EBADMSG
#define TARGET_EBADMSG          84
#undef TARGET_EPROTO
#define TARGET_EPROTO           85
#undef TARGET_ENODATA
#define TARGET_ENODATA          86
#undef TARGET_ENOSTR
#define TARGET_ENOSTR           87
#undef TARGET_ECHRNG
#define TARGET_ECHRNG           88
#undef TARGET_EL2NSYNC
#define TARGET_EL2NSYNC         89
#undef TARGET_EL3HLT
#define TARGET_EL3HLT           90
#undef TARGET_EL3RST
#define TARGET_EL3RST           91
#undef TARGET_ENOPKG
#define TARGET_ENOPKG           92
#undef TARGET_ELNRNG
#define TARGET_ELNRNG           93
#undef TARGET_EUNATCH
#define TARGET_EUNATCH          94
#undef TARGET_ENOCSI
#define TARGET_ENOCSI           95
#undef TARGET_EL2HLT
#define TARGET_EL2HLT           96
#undef TARGET_EBADE
#define TARGET_EBADE            97
#undef TARGET_EBADR
#define TARGET_EBADR            98
#undef TARGET_EXFULL
#define TARGET_EXFULL           99
#undef TARGET_ENOANO
#define TARGET_ENOANO           100
#undef TARGET_EBADRQC
#define TARGET_EBADRQC          101
#undef TARGET_EBADSLT
#define TARGET_EBADSLT          102
/* Unused                       103 */
#undef TARGET_EBFONT
#define TARGET_EBFONT           104
#undef TARGET_ENONET
#define TARGET_ENONET           105
#undef TARGET_ENOLINK
#define TARGET_ENOLINK          106
#undef TARGET_EADV
#define TARGET_EADV             107
#undef TARGET_ESRMNT
#define TARGET_ESRMNT           108
#undef TARGET_ECOMM
#define TARGET_ECOMM            109
#undef TARGET_EMULTIHOP
#define TARGET_EMULTIHOP        110
#undef TARGET_EDOTDOT
#define TARGET_EDOTDOT          111
#undef TARGET_EOVERFLOW
#define TARGET_EOVERFLOW        112
#undef TARGET_ENOTUNIQ
#define TARGET_ENOTUNIQ         113
#undef TARGET_EBADFD
#define TARGET_EBADFD           114
#undef TARGET_EREMCHG
#define TARGET_EREMCHG          115
#undef TARGET_EILSEQ
#define TARGET_EILSEQ           116
/* Same as default              117-121 */
#undef TARGET_ELIBACC
#define TARGET_ELIBACC          122
#undef TARGET_ELIBBAD
#define TARGET_ELIBBAD          123
#undef TARGET_ELIBSCN
#define TARGET_ELIBSCN          124
#undef TARGET_ELIBMAX
#define TARGET_ELIBMAX          125
#undef TARGET_ELIBEXEC
#define TARGET_ELIBEXEC         126
#undef TARGET_ERESTART
#define TARGET_ERESTART         127
#undef TARGET_ESTRPIPE
#define TARGET_ESTRPIPE         128
#undef TARGET_ENOMEDIUM
#define TARGET_ENOMEDIUM        129
#undef TARGET_EMEDIUMTYPE
#define TARGET_EMEDIUMTYPE      130
#undef TARGET_ECANCELED
#define TARGET_ECANCELED        131
#undef TARGET_ENOKEY
#define TARGET_ENOKEY           132
#undef TARGET_EKEYEXPIRED
#define TARGET_EKEYEXPIRED      133
#undef TARGET_EKEYREVOKED
#define TARGET_EKEYREVOKED      134
#undef TARGET_EKEYREJECTED
#define TARGET_EKEYREJECTED     135
#undef TARGET_EOWNERDEAD
#define TARGET_EOWNERDEAD       136
#undef TARGET_ENOTRECOVERABLE
#define TARGET_ENOTRECOVERABLE  137
#undef TARGET_ERFKILL
#define TARGET_ERFKILL          138
#undef TARGET_EHWPOISON
#define TARGET_EHWPOISON        139

#endif
