/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef GENERIC_SOCKBITS_H
#define GENERIC_SOCKBITS_H

#define TARGET_SO_PASSSEC        34

/* For setsockopt(2) */
#define TARGET_SOL_SOCKET      1

#define TARGET_SO_DEBUG        1
#define TARGET_SO_REUSEADDR    2
#define TARGET_SO_TYPE         3
#define TARGET_SO_ERROR        4
#define TARGET_SO_DONTROUTE    5
#define TARGET_SO_BROADCAST    6
#define TARGET_SO_SNDBUF       7
#define TARGET_SO_RCVBUF       8
#define TARGET_SO_SNDBUFFORCE  32
#define TARGET_SO_RCVBUFFORCE  33
#define TARGET_SO_KEEPALIVE    9
#define TARGET_SO_OOBINLINE    10
#define TARGET_SO_NO_CHECK     11
#define TARGET_SO_PRIORITY     12
#define TARGET_SO_LINGER       13
#define TARGET_SO_BSDCOMPAT    14
#define TARGET_SO_REUSEPORT    15
#define TARGET_SO_PASSCRED     16
#define TARGET_SO_PEERCRED     17
#define TARGET_SO_RCVLOWAT     18
#define TARGET_SO_SNDLOWAT     19
#define TARGET_SO_RCVTIMEO     20
#define TARGET_SO_SNDTIMEO     21

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define TARGET_SO_SECURITY_AUTHENTICATION              22
#define TARGET_SO_SECURITY_ENCRYPTION_TRANSPORT        23
#define TARGET_SO_SECURITY_ENCRYPTION_NETWORK          24

#define TARGET_SO_BINDTODEVICE 25

/* Socket filtering */
#define TARGET_SO_ATTACH_FILTER        26
#define TARGET_SO_DETACH_FILTER        27

#define TARGET_SO_PEERNAME             28
#define TARGET_SO_TIMESTAMP            29
#define TARGET_SCM_TIMESTAMP           TARGET_SO_TIMESTAMP

#define TARGET_SO_ACCEPTCONN           30

#define TARGET_SO_PEERSEC              31

#define TARGET_SO_PROTOCOL             38
#define TARGET_SO_DOMAIN               39
#endif
