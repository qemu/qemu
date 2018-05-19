/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef ALPHA_SOCKBITS_H
#define ALPHA_SOCKBITS_H

/* For setsockopt(2) */
#define TARGET_SOL_SOCKET   0xffff

#define TARGET_SO_DEBUG 0x0001
#define TARGET_SO_REUSEADDR 0x0004
#define TARGET_SO_KEEPALIVE 0x0008
#define TARGET_SO_DONTROUTE 0x0010
#define TARGET_SO_BROADCAST 0x0020
#define TARGET_SO_LINGER    0x0080
#define TARGET_SO_OOBINLINE 0x0100
#define TARGET_SO_REUSEPORT 0x0200

#define TARGET_SO_TYPE      0x1008
#define TARGET_SO_ERROR 0x1007
#define TARGET_SO_SNDBUF    0x1001
#define TARGET_SO_RCVBUF    0x1002
#define TARGET_SO_SNDBUFFORCE   0x100a
#define TARGET_SO_RCVBUFFORCE   0x100b
#define TARGET_SO_RCVLOWAT  0x1010
#define TARGET_SO_SNDLOWAT  0x1011
#define TARGET_SO_RCVTIMEO  0x1012
#define TARGET_SO_SNDTIMEO  0x1013
#define TARGET_SO_ACCEPTCONN    0x1014
#define TARGET_SO_PROTOCOL  0x1028
#define TARGET_SO_DOMAIN    0x1029

/* linux-specific, might as well be the same as on i386 */
#define TARGET_SO_NO_CHECK  11
#define TARGET_SO_PRIORITY  12
#define TARGET_SO_BSDCOMPAT 14

#define TARGET_SO_PASSCRED  17
#define TARGET_SO_PEERCRED  18
#define TARGET_SO_BINDTODEVICE 25

/* Socket filtering */
#define TARGET_SO_ATTACH_FILTER        26
#define TARGET_SO_DETACH_FILTER        27

#define TARGET_SO_PEERNAME      28
#define TARGET_SO_TIMESTAMP     29
#define TARGET_SCM_TIMESTAMP        TARGET_SO_TIMESTAMP

#define TARGET_SO_PEERSEC       30
#define TARGET_SO_PASSSEC       34
#define TARGET_SO_TIMESTAMPNS       35
#define TARGET_SCM_TIMESTAMPNS      TARGET_SO_TIMESTAMPNS

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define TARGET_SO_SECURITY_AUTHENTICATION       19
#define TARGET_SO_SECURITY_ENCRYPTION_TRANSPORT 20
#define TARGET_SO_SECURITY_ENCRYPTION_NETWORK       21

#define TARGET_SO_MARK          36

#define TARGET_SO_TIMESTAMPING      37
#define TARGET_SCM_TIMESTAMPING TARGET_SO_TIMESTAMPING

#define TARGET_SO_RXQ_OVFL             40

#define TARGET_SO_WIFI_STATUS       41
#define TARGET_SCM_WIFI_STATUS      TARGET_SO_WIFI_STATUS
#define TARGET_SO_PEEK_OFF      42

/* Instruct lower device to use last 4-bytes of skb data as FCS */
#define TARGET_SO_NOFCS     43

/* TARGET_O_NONBLOCK clashes with the bits used for socket types.  Therefore we
 * have to define SOCK_NONBLOCK to a different value here.
 */
#define TARGET_SOCK_NONBLOCK   0x40000000

#endif
