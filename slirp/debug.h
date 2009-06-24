/*
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

//#define DEBUG 1

#ifdef DEBUG

#define DBG_CALL 0x1
#define DBG_MISC 0x2
#define DBG_ERROR 0x4

#define dfd stderr

extern int slirp_debug;

#define DEBUG_CALL(x) if (slirp_debug & DBG_CALL) { fprintf(dfd, "%s...\n", x); fflush(dfd); }
#define DEBUG_ARG(x, y) if (slirp_debug & DBG_CALL) { fputc(' ', dfd); fprintf(dfd, x, y); fputc('\n', dfd); fflush(dfd); }
#define DEBUG_ARGS(x) if (slirp_debug & DBG_CALL) { fprintf x ; fflush(dfd); }
#define DEBUG_MISC(x) if (slirp_debug & DBG_MISC) { fprintf x ; fflush(dfd); }
#define DEBUG_ERROR(x) if (slirp_debug & DBG_ERROR) {fprintf x ; fflush(dfd); }

#else

#define DEBUG_CALL(x)
#define DEBUG_ARG(x, y)
#define DEBUG_ARGS(x)
#define DEBUG_MISC(x)
#define DEBUG_ERROR(x)

#endif
