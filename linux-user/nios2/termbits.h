/* from asm/termbits.h */
/* NOTE: exactly the same as i386 */

#ifndef LINUX_USER_NIOS2_TERMBITS_H
#define LINUX_USER_NIOS2_TERMBITS_H

#define TARGET_NCCS 19

struct target_termios {
    unsigned int c_iflag;               /* input mode flags */
    unsigned int c_oflag;               /* output mode flags */
    unsigned int c_cflag;               /* control mode flags */
    unsigned int c_lflag;               /* local mode flags */
    unsigned char c_line;                    /* line discipline */
    unsigned char c_cc[TARGET_NCCS];                /* control characters */
};

/* c_iflag bits */
#define TARGET_IGNBRK  0000001
#define TARGET_BRKINT  0000002
#define TARGET_IGNPAR  0000004
#define TARGET_PARMRK  0000010
#define TARGET_INPCK   0000020
#define TARGET_ISTRIP  0000040
#define TARGET_INLCR   0000100
#define TARGET_IGNCR   0000200
#define TARGET_ICRNL   0000400
#define TARGET_IUCLC   0001000
#define TARGET_IXON    0002000
#define TARGET_IXANY   0004000
#define TARGET_IXOFF   0010000
#define TARGET_IMAXBEL 0020000
#define TARGET_IUTF8   0040000

/* c_oflag bits */
#define TARGET_OPOST   0000001
#define TARGET_OLCUC   0000002
#define TARGET_ONLCR   0000004
#define TARGET_OCRNL   0000010
#define TARGET_ONOCR   0000020
#define TARGET_ONLRET  0000040
#define TARGET_OFILL   0000100
#define TARGET_OFDEL   0000200
#define TARGET_NLDLY   0000400
#define   TARGET_NL0   0000000
#define   TARGET_NL1   0000400
#define TARGET_CRDLY   0003000
#define   TARGET_CR0   0000000
#define   TARGET_CR1   0001000
#define   TARGET_CR2   0002000
#define   TARGET_CR3   0003000
#define TARGET_TABDLY  0014000
#define   TARGET_TAB0  0000000
#define   TARGET_TAB1  0004000
#define   TARGET_TAB2  0010000
#define   TARGET_TAB3  0014000
#define   TARGET_XTABS 0014000
#define TARGET_BSDLY   0020000
#define   TARGET_BS0   0000000
#define   TARGET_BS1   0020000
#define TARGET_VTDLY   0040000
#define   TARGET_VT0   0000000
#define   TARGET_VT1   0040000
#define TARGET_FFDLY   0100000
#define   TARGET_FF0   0000000
#define   TARGET_FF1   0100000

/* c_cflag bit meaning */
#define TARGET_CBAUD   0010017
#define  TARGET_B0     0000000         /* hang up */
#define  TARGET_B50    0000001
#define  TARGET_B75    0000002
#define  TARGET_B110   0000003
#define  TARGET_B134   0000004
#define  TARGET_B150   0000005
#define  TARGET_B200   0000006
#define  TARGET_B300   0000007
#define  TARGET_B600   0000010
#define  TARGET_B1200  0000011
#define  TARGET_B1800  0000012
#define  TARGET_B2400  0000013
#define  TARGET_B4800  0000014
#define  TARGET_B9600  0000015
#define  TARGET_B19200 0000016
#define  TARGET_B38400 0000017
#define TARGET_EXTA B19200
#define TARGET_EXTB B38400
#define TARGET_CSIZE   0000060
#define   TARGET_CS5   0000000
#define   TARGET_CS6   0000020
#define   TARGET_CS7   0000040
#define   TARGET_CS8   0000060
#define TARGET_CSTOPB  0000100
#define TARGET_CREAD   0000200
#define TARGET_PARENB  0000400
#define TARGET_PARODD  0001000
#define TARGET_HUPCL   0002000
#define TARGET_CLOCAL  0004000
#define TARGET_CBAUDEX 0010000
#define  TARGET_B57600  0010001
#define  TARGET_B115200 0010002
#define  TARGET_B230400 0010003
#define  TARGET_B460800 0010004
#define TARGET_CIBAUD    002003600000  /* input baud rate (not used) */
#define TARGET_CMSPAR    010000000000  /* mark or space (stick) parity */
#define TARGET_CRTSCTS   020000000000  /* flow control */

/* c_lflag bits */
#define TARGET_ISIG    0000001
#define TARGET_ICANON  0000002
#define TARGET_XCASE   0000004
#define TARGET_ECHO    0000010
#define TARGET_ECHOE   0000020
#define TARGET_ECHOK   0000040
#define TARGET_ECHONL  0000100
#define TARGET_NOFLSH  0000200
#define TARGET_TOSTOP  0000400
#define TARGET_ECHOCTL 0001000
#define TARGET_ECHOPRT 0002000
#define TARGET_ECHOKE  0004000
#define TARGET_FLUSHO  0010000
#define TARGET_PENDIN  0040000
#define TARGET_IEXTEN  0100000

/* c_cc character offsets */
#define TARGET_VINTR    0
#define TARGET_VQUIT    1
#define TARGET_VERASE   2
#define TARGET_VKILL    3
#define TARGET_VEOF     4
#define TARGET_VTIME    5
#define TARGET_VMIN     6
#define TARGET_VSWTC    7
#define TARGET_VSTART   8
#define TARGET_VSTOP    9
#define TARGET_VSUSP    10
#define TARGET_VEOL     11
#define TARGET_VREPRINT 12
#define TARGET_VDISCARD 13
#define TARGET_VWERASE  14
#define TARGET_VLNEXT   15
#define TARGET_VEOL2    16

/* ioctls */

#define TARGET_TCGETS           0x5401
#define TARGET_TCSETS           0x5402
#define TARGET_TCSETSW          0x5403
#define TARGET_TCSETSF          0x5404
#define TARGET_TCGETA           0x5405
#define TARGET_TCSETA           0x5406
#define TARGET_TCSETAW          0x5407
#define TARGET_TCSETAF          0x5408
#define TARGET_TCSBRK           0x5409
#define TARGET_TCXONC           0x540A
#define TARGET_TCFLSH           0x540B

#define TARGET_TIOCEXCL         0x540C
#define TARGET_TIOCNXCL         0x540D
#define TARGET_TIOCSCTTY        0x540E
#define TARGET_TIOCGPGRP        0x540F
#define TARGET_TIOCSPGRP        0x5410
#define TARGET_TIOCOUTQ         0x5411
#define TARGET_TIOCSTI          0x5412
#define TARGET_TIOCGWINSZ       0x5413
#define TARGET_TIOCSWINSZ       0x5414
#define TARGET_TIOCMGET         0x5415
#define TARGET_TIOCMBIS         0x5416
#define TARGET_TIOCMBIC         0x5417
#define TARGET_TIOCMSET         0x5418
#define TARGET_TIOCGSOFTCAR     0x5419
#define TARGET_TIOCSSOFTCAR     0x541A
#define TARGET_FIONREAD         0x541B
#define TARGET_TIOCINQ          TARGET_FIONREAD
#define TARGET_TIOCLINUX        0x541C
#define TARGET_TIOCCONS         0x541D
#define TARGET_TIOCGSERIAL      0x541E
#define TARGET_TIOCSSERIAL      0x541F
#define TARGET_TIOCPKT          0x5420
#define TARGET_FIONBIO          0x5421
#define TARGET_TIOCNOTTY        0x5422
#define TARGET_TIOCSETD         0x5423
#define TARGET_TIOCGETD         0x5424
#define TARGET_TCSBRKP          0x5425 /* Needed for POSIX tcsendbreak() */
#define TARGET_TIOCTTYGSTRUCT   0x5426 /* For debugging only */
#define TARGET_TIOCSBRK         0x5427 /* BSD compatibility */
#define TARGET_TIOCCBRK         0x5428 /* BSD compatibility */
#define TARGET_TIOCGSID         0x5429 /* Return the session ID of FD */
#define TARGET_TIOCGPTN         TARGET_IOR('T', 0x30, unsigned int)
        /* Get Pty Number (of pty-mux device) */
#define TARGET_TIOCSPTLCK       TARGET_IOW('T', 0x31, int)
        /* Lock/unlock Pty */
#define TARGET_TIOCGPTPEER      TARGET_IO('T', 0x41)
        /* Safely open the slave */

#define TARGET_FIONCLEX         0x5450  /* these numbers need to be adjusted. */
#define TARGET_FIOCLEX          0x5451
#define TARGET_FIOASYNC         0x5452
#define TARGET_TIOCSERCONFIG    0x5453
#define TARGET_TIOCSERGWILD     0x5454
#define TARGET_TIOCSERSWILD     0x5455
#define TARGET_TIOCGLCKTRMIOS   0x5456
#define TARGET_TIOCSLCKTRMIOS   0x5457
#define TARGET_TIOCSERGSTRUCT   0x5458 /* For debugging only */
#define TARGET_TIOCSERGETLSR    0x5459 /* Get line status register */
#define TARGET_TIOCSERGETMULTI  0x545A /* Get multiport config  */
#define TARGET_TIOCSERSETMULTI  0x545B /* Set multiport config */

#define TARGET_TIOCMIWAIT      0x545C
        /* wait for a change on serial input line(s) */
#define TARGET_TIOCGICOUNT     0x545D
        /* read serial port inline interrupt counts */
#define TARGET_TIOCGHAYESESP   0x545E  /* Get Hayes ESP configuration */
#define TARGET_TIOCSHAYESESP   0x545F  /* Set Hayes ESP configuration */

/* Used for packet mode */
#define TARGET_TIOCPKT_DATA              0
#define TARGET_TIOCPKT_FLUSHREAD         1
#define TARGET_TIOCPKT_FLUSHWRITE        2
#define TARGET_TIOCPKT_STOP              4
#define TARGET_TIOCPKT_START             8
#define TARGET_TIOCPKT_NOSTOP           16
#define TARGET_TIOCPKT_DOSTOP           32

#define TARGET_TIOCSER_TEMT    0x01 /* Transmitter physically empty */

#endif
