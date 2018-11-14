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

/* Define if the machine is big endian */
//#undef HOST_WORDS_BIGENDIAN

/* Define to sizeof(char *) */
#define SIZEOF_CHAR_P (HOST_LONG_BITS / 8)

/* Define if you have inet_aton */
#undef HAVE_INET_ATON
#ifndef _WIN32
#define HAVE_INET_ATON
#endif
