/*
 * User definable configuration options
 */

/* Define to 1 if you want KEEPALIVE timers */
#define DO_KEEPALIVE 0

/*********************************************************/
/*
 * Autoconf defined configuration options
 * You shouldn't need to touch any of these
 */

/* Define if you have sys/filio.h */
#undef HAVE_SYS_FILIO_H
#ifdef __APPLE__
#define HAVE_SYS_FILIO_H
#endif

/* Define if the machine is big endian */
//#undef HOST_WORDS_BIGENDIAN

/* Define if iovec needs to be declared */
#undef DECLARE_IOVEC
#ifdef _WIN32
#define DECLARE_IOVEC
#endif

/* Define to sizeof(char *) */
#define SIZEOF_CHAR_P (HOST_LONG_BITS / 8)

/* Define if you have inet_aton */
#undef HAVE_INET_ATON
#ifndef _WIN32
#define HAVE_INET_ATON
#endif
