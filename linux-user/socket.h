
#if defined(TARGET_MIPS)
    /* MIPS special values for constants */

    /*
     * For setsockopt(2)
     *
     * This defines are ABI conformant as far as Linux supports these ...
     */
    #define TARGET_SOL_SOCKET      0xffff

    #define TARGET_SO_DEBUG        0x0001  /* Record debugging information. */
    #define TARGET_SO_REUSEADDR    0x0004  /* Allow reuse of local addresses. */
    #define TARGET_SO_KEEPALIVE    0x0008  /* Keep connections alive and send
                                              SIGPIPE when they die. */
    #define TARGET_SO_DONTROUTE    0x0010  /* Don't do local routing. */
    #define TARGET_SO_BROADCAST    0x0020  /* Allow transmission of
                                              broadcast messages. */
    #define TARGET_SO_LINGER       0x0080  /* Block on close of a reliable
                                            * socket to transmit pending data.
                                            */
    #define TARGET_SO_OOBINLINE 0x0100     /* Receive out-of-band data in-band.
                                            */
    #if 0
    /* To add: Allow local address and port reuse. */
    #define TARGET_SO_REUSEPORT 0x0200
    #endif

    #define TARGET_SO_TYPE         0x1008  /* Compatible name for SO_STYLE. */
    #define TARGET_SO_STYLE        SO_TYPE /* Synonym */
    #define TARGET_SO_ERROR        0x1007  /* get error status and clear */
    #define TARGET_SO_SNDBUF       0x1001  /* Send buffer size. */
    #define TARGET_SO_RCVBUF       0x1002  /* Receive buffer. */
    #define TARGET_SO_SNDLOWAT     0x1003  /* send low-water mark */
    #define TARGET_SO_RCVLOWAT     0x1004  /* receive low-water mark */
    #define TARGET_SO_SNDTIMEO     0x1005  /* send timeout */
    #define TARGET_SO_RCVTIMEO     0x1006  /* receive timeout */
    #define TARGET_SO_ACCEPTCONN   0x1009

    /* linux-specific, might as well be the same as on i386 */
    #define TARGET_SO_NO_CHECK     11
    #define TARGET_SO_PRIORITY     12
    #define TARGET_SO_BSDCOMPAT    14

    #define TARGET_SO_PASSCRED     17
    #define TARGET_SO_PEERCRED     18

    /* Security levels - as per NRL IPv6 - don't actually do anything */
    #define TARGET_SO_SECURITY_AUTHENTICATION              22
    #define TARGET_SO_SECURITY_ENCRYPTION_TRANSPORT        23
    #define TARGET_SO_SECURITY_ENCRYPTION_NETWORK          24

    #define TARGET_SO_BINDTODEVICE         25

    /* Socket filtering */
    #define TARGET_SO_ATTACH_FILTER        26
    #define TARGET_SO_DETACH_FILTER        27

    #define TARGET_SO_PEERNAME             28
    #define TARGET_SO_TIMESTAMP            29
    #define SCM_TIMESTAMP          SO_TIMESTAMP

    #define TARGET_SO_PEERSEC              30
    #define TARGET_SO_SNDBUFFORCE          31
    #define TARGET_SO_RCVBUFFORCE          33

    /** sock_type - Socket types
     *
     * Please notice that for binary compat reasons MIPS has to
     * override the enum sock_type in include/linux/net.h, so
     * we define ARCH_HAS_SOCKET_TYPES here.
     *
     * @SOCK_DGRAM - datagram (conn.less) socket
     * @SOCK_STREAM - stream (connection) socket
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

    #define ARCH_HAS_SOCKET_TYPES          1

    enum sock_type {
           TARGET_SOCK_DGRAM       = 1,
           TARGET_SOCK_STREAM      = 2,
           TARGET_SOCK_RAW         = 3,
           TARGET_SOCK_RDM         = 4,
           TARGET_SOCK_SEQPACKET   = 5,
           TARGET_SOCK_DCCP        = 6,
           TARGET_SOCK_PACKET      = 10,
           TARGET_SOCK_CLOEXEC     = 02000000,
           TARGET_SOCK_NONBLOCK    = 0200,
    };

    #define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
    #define TARGET_SOCK_TYPE_MASK    0xf  /* Covers up to TARGET_SOCK_MAX-1. */

#elif defined(TARGET_ALPHA)

    /* For setsockopt(2) */
    #define TARGET_SOL_SOCKET   0xffff

    #define TARGET_SO_DEBUG 0x0001
    #define TARGET_SO_REUSEADDR 0x0004
    #define TARGET_SO_KEEPALIVE 0x0008
    #define TARGET_SO_DONTROUTE 0x0010
    #define TARGET_SO_BROADCAST 0x0020
    #define TARGET_SO_LINGER    0x0080
    #define TARGET_SO_OOBINLINE 0x0100
    /* To add :#define TARGET_SO_REUSEPORT 0x0200 */

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

    /** sock_type - Socket types
     *
     * Please notice that for binary compat reasons ALPHA has to
     * override the enum sock_type in include/linux/net.h, so
     * we define ARCH_HAS_SOCKET_TYPES here.
     *
     * @SOCK_DGRAM - datagram (conn.less) socket
     * @SOCK_STREAM - stream (connection) socket
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

    #define ARCH_HAS_SOCKET_TYPES          1

    enum sock_type {
           TARGET_SOCK_STREAM      = 1,
           TARGET_SOCK_DGRAM       = 2,
           TARGET_SOCK_RAW         = 3,
           TARGET_SOCK_RDM         = 4,
           TARGET_SOCK_SEQPACKET   = 5,
           TARGET_SOCK_DCCP        = 6,
           TARGET_SOCK_PACKET      = 10,
           TARGET_SOCK_CLOEXEC     = 010000000,
           TARGET_SOCK_NONBLOCK    = 010000000000,
    };

    #define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
    #define TARGET_SOCK_TYPE_MASK    0xf  /* Covers up to TARGET_SOCK_MAX-1. */
#else

#if defined(TARGET_SPARC)
    /** sock_type - Socket types
     *
     * Please notice that for binary compat reasons SPARC has to
     * override the enum sock_type in include/linux/net.h, so
     * we define ARCH_HAS_SOCKET_TYPES here.
     *
     * @SOCK_DGRAM - datagram (conn.less) socket
     * @SOCK_STREAM - stream (connection) socket
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

    #define ARCH_HAS_SOCKET_TYPES          1

    enum sock_type {
           TARGET_SOCK_STREAM      = 1,
           TARGET_SOCK_DGRAM       = 2,
           TARGET_SOCK_RAW         = 3,
           TARGET_SOCK_RDM         = 4,
           TARGET_SOCK_SEQPACKET   = 5,
           TARGET_SOCK_DCCP        = 6,
           TARGET_SOCK_PACKET      = 10,
           TARGET_SOCK_CLOEXEC     = 020000000,
           TARGET_SOCK_NONBLOCK    = 040000,
    };

    #define TARGET_SOCK_MAX (TARGET_SOCK_PACKET + 1)
    #define TARGET_SOCK_TYPE_MASK    0xf  /* Covers up to TARGET_SOCK_MAX-1. */
#endif

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
    /* To add :#define TARGET_SO_REUSEPORT 15 */
#if defined(TARGET_PPC)
    #define TARGET_SO_RCVLOWAT     16
    #define TARGET_SO_SNDLOWAT     17
    #define TARGET_SO_RCVTIMEO     18
    #define TARGET_SO_SNDTIMEO     19
    #define TARGET_SO_PASSCRED     20
    #define TARGET_SO_PEERCRED     21
#else
    #define TARGET_SO_PASSCRED     16
    #define TARGET_SO_PEERCRED     17
    #define TARGET_SO_RCVLOWAT     18
    #define TARGET_SO_SNDLOWAT     19
    #define TARGET_SO_RCVTIMEO     20
    #define TARGET_SO_SNDTIMEO     21
#endif

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

#endif

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
