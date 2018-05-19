
#include "sockbits.h"

#ifndef ARCH_HAS_SOCKET_TYPES
    /** sock_type - Socket types - default values
     *
     *
     * @SOCK_STREAM - stream (connection) socket
     * @SOCK_DGRAM - datagram (conn.less) socket
     * @SOCK_RAW - raw socket
     * @SOCK_RDM - reliably-delivered message
     * @SOCK_SEQPACKET - sequential packet socket
     * @SOCK_DCCP - Datagram Congestion Control Protocol socket
     * @SOCK_PACKET - linux specific way of getting packets at the dev level.
     *                For writing rarp and other similar things on the user
     *                level.
     * @SOCK_CLOEXEC - sets the close-on-exec (FD_CLOEXEC) flag.
     * @SOCK_NONBLOCK - sets the O_NONBLOCK file status flag.
     */
    enum sock_type {
           TARGET_SOCK_STREAM      = 1,
           TARGET_SOCK_DGRAM       = 2,
           TARGET_SOCK_RAW         = 3,
           TARGET_SOCK_RDM         = 4,
           TARGET_SOCK_SEQPACKET   = 5,
           TARGET_SOCK_DCCP        = 6,
           TARGET_SOCK_PACKET      = 10,
           TARGET_SOCK_CLOEXEC     = 02000000,
           TARGET_SOCK_NONBLOCK    = 04000,
    };

    #define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
    #define TARGET_SOCK_TYPE_MASK    0xf  /* Covers up to TARGET_SOCK_MAX-1. */

#endif
