/*
 * User definable configuration options
 */

/* Define if you want the connection to be probed */
/* XXX Not working yet, so ignore this for now */
#undef PROBE_CONN

/* Define to 1 if you want KEEPALIVE timers */
#define DO_KEEPALIVE 0

/* Define this if you want slirp to write to the tty as fast as it can */
/* This should only be set if you are using load-balancing, slirp does a */
/* pretty good job on single modems already, and seting this will make */
/* interactive sessions less responsive */
/* XXXXX Talk about having fast modem as unit 0 */
#undef FULL_BOLT

/*********************************************************/
/*
 * Autoconf defined configuration options
 * You shouldn't need to touch any of these
 */

/* Define if you have sys/ioctl.h */
#undef HAVE_SYS_IOCTL_H
#ifndef _WIN32
#define HAVE_SYS_IOCTL_H
#endif

/* Define if you have sys/filio.h */
#undef HAVE_SYS_FILIO_H
#ifdef __APPLE__
#define HAVE_SYS_FILIO_H
#endif

/* Define if you have strerror */
#define HAVE_STRERROR

/* Define if you have sys/bitypes.h */
#undef HAVE_SYS_BITYPES_H

/* Define if the machine is big endian */
//#undef HOST_WORDS_BIGENDIAN

/* Define if you have readv */
#undef HAVE_READV

/* Define if iovec needs to be declared */
#undef DECLARE_IOVEC
#ifdef _WIN32
#define DECLARE_IOVEC
#endif

/* Define if you have a POSIX.1 sys/wait.h */
#undef HAVE_SYS_WAIT_H

/* Define if you have sys/select.h */
#undef HAVE_SYS_SELECT_H
#ifndef _WIN32
#define HAVE_SYS_SELECT_H
#endif

/* Define if you have arpa/inet.h */
#undef HAVE_ARPA_INET_H
#ifndef _WIN32
#define HAVE_ARPA_INET_H
#endif

/* Define if you have sys/signal.h */
#undef HAVE_SYS_SIGNAL_H

/* Define if you have sys/stropts.h */
#undef HAVE_SYS_STROPTS_H

/* Define to sizeof(char *) */
#define SIZEOF_CHAR_P (HOST_LONG_BITS / 8)

/* Define if you have inet_aton */
#undef HAVE_INET_ATON
#ifndef _WIN32
#define HAVE_INET_ATON
#endif

/* Define if you have index() */
#define HAVE_INDEX

/* Define if you have memmove */
#define HAVE_MEMMOVE

/* Define if you have gethostid */
#define HAVE_GETHOSTID

/* Define if you DON'T have unix-domain sockets */
#undef NO_UNIX_SOCKETS
#ifdef _WIN32
#define NO_UNIX_SOCKETS
#endif
