/*
 * include/asm-xtensa/termbits.h
 *
 * Copied from SH.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef XTENSA_TERMBITS_H
#define XTENSA_TERMBITS_H

#include <linux/posix_types.h>

#define TARGET_NCCS 19

typedef unsigned char   target_cc_t;        /* cc_t */
typedef unsigned int    target_speed_t;     /* speed_t */
typedef unsigned int    target_tcflag_t;    /* tcflag_t */

struct target_termios {
    target_tcflag_t c_iflag;               /* input mode flags */
    target_tcflag_t c_oflag;               /* output mode flags */
    target_tcflag_t c_cflag;               /* control mode flags */
    target_tcflag_t c_lflag;               /* local mode flags */
    target_cc_t c_line;                    /* line discipline */
    target_cc_t c_cc[TARGET_NCCS];         /* control characters */
};


struct target_termios2 {
    target_tcflag_t c_iflag;       /* input mode flags */
    target_tcflag_t c_oflag;       /* output mode flags */
    target_tcflag_t c_cflag;       /* control mode flags */
    target_tcflag_t c_lflag;       /* local mode flags */
    target_cc_t c_line;            /* line discipline */
    target_cc_t c_cc[TARGET_NCCS]; /* control characters */
    target_speed_t c_ispeed;       /* input speed */
    target_speed_t c_ospeed;       /* output speed */
};

struct target_ktermios {
    target_tcflag_t c_iflag;       /* input mode flags */
    target_tcflag_t c_oflag;       /* output mode flags */
    target_tcflag_t c_cflag;       /* control mode flags */
    target_tcflag_t c_lflag;       /* local mode flags */
    target_cc_t c_line;            /* line discipline */
    target_cc_t c_cc[TARGET_NCCS]; /* control characters */
    target_speed_t c_ispeed;       /* input speed */
    target_speed_t c_ospeed;       /* output speed */
};

/* c_cc characters */

#define TARGET_VINTR 0
#define TARGET_VQUIT 1
#define TARGET_VERASE 2
#define TARGET_VKILL 3
#define TARGET_VEOF 4
#define TARGET_VTIME 5
#define TARGET_VMIN 6
#define TARGET_VSWTC 7
#define TARGET_VSTART 8
#define TARGET_VSTOP 9
#define TARGET_VSUSP 10
#define TARGET_VEOL 11
#define TARGET_VREPRINT 12
#define TARGET_VDISCARD 13
#define TARGET_VWERASE 14
#define TARGET_VLNEXT 15
#define TARGET_VEOL2 16

/* c_iflag bits */

#define TARGET_IGNBRK   0000001
#define TARGET_BRKINT   0000002
#define TARGET_IGNPAR   0000004
#define TARGET_PARMRK   0000010
#define TARGET_INPCK    0000020
#define TARGET_ISTRIP   0000040
#define TARGET_INLCR    0000100
#define TARGET_IGNCR    0000200
#define TARGET_ICRNL    0000400
#define TARGET_IUCLC    0001000
#define TARGET_IXON 0002000
#define TARGET_IXANY    0004000
#define TARGET_IXOFF    0010000
#define TARGET_IMAXBEL  0020000
#define TARGET_IUTF8    0040000

/* c_oflag bits */

#define TARGET_OPOST    0000001
#define TARGET_OLCUC    0000002
#define TARGET_ONLCR    0000004
#define TARGET_OCRNL    0000010
#define TARGET_ONOCR    0000020
#define TARGET_ONLRET   0000040
#define TARGET_OFILL    0000100
#define TARGET_OFDEL    0000200
#define TARGET_NLDLY    0000400
#define   TARGET_NL0    0000000
#define   TARGET_NL1    0000400
#define TARGET_CRDLY    0003000
#define   TARGET_CR0    0000000
#define   TARGET_CR1    0001000
#define   TARGET_CR2    0002000
#define   TARGET_CR3    0003000
#define TARGET_TABDLY   0014000
#define   TARGET_TAB0   0000000
#define   TARGET_TAB1   0004000
#define   TARGET_TAB2   0010000
#define   TARGET_TAB3   0014000
#define   TARGET_XTABS  0014000
#define TARGET_BSDLY    0020000
#define   TARGET_BS0    0000000
#define   TARGET_BS1    0020000
#define TARGET_VTDLY    0040000
#define   TARGET_VT0    0000000
#define   TARGET_VT1    0040000
#define TARGET_FFDLY    0100000
#define   TARGET_FF0    0000000
#define   TARGET_FF1    0100000

/* c_cflag bit meaning */

#define TARGET_CBAUD    0010017
#define  TARGET_B0  0000000     /* hang up */
#define  TARGET_B50 0000001
#define  TARGET_B75 0000002
#define  TARGET_B110    0000003
#define  TARGET_B134    0000004
#define  TARGET_B150    0000005
#define  TARGET_B200    0000006
#define  TARGET_B300    0000007
#define  TARGET_B600    0000010
#define  TARGET_B1200   0000011
#define  TARGET_B1800   0000012
#define  TARGET_B2400   0000013
#define  TARGET_B4800   0000014
#define  TARGET_B9600   0000015
#define  TARGET_B19200  0000016
#define  TARGET_B38400  0000017
#define TARGET_EXTA B19200
#define TARGET_EXTB B38400
#define TARGET_CSIZE    0000060
#define   TARGET_CS5    0000000
#define   TARGET_CS6    0000020
#define   TARGET_CS7    0000040
#define   TARGET_CS8    0000060
#define TARGET_CSTOPB   0000100
#define TARGET_CREAD    0000200
#define TARGET_PARENB   0000400
#define TARGET_PARODD   0001000
#define TARGET_HUPCL    0002000
#define TARGET_CLOCAL   0004000
#define TARGET_CBAUDEX 0010000
#define    TARGET_BOTHER 0010000
#define    TARGET_B57600 0010001
#define   TARGET_B115200 0010002
#define   TARGET_B230400 0010003
#define   TARGET_B460800 0010004
#define   TARGET_B500000 0010005
#define   TARGET_B576000 0010006
#define   TARGET_B921600 0010007
#define  TARGET_B1000000 0010010
#define  TARGET_B1152000 0010011
#define  TARGET_B1500000 0010012
#define  TARGET_B2000000 0010013
#define  TARGET_B2500000 0010014
#define  TARGET_B3000000 0010015
#define  TARGET_B3500000 0010016
#define  TARGET_B4000000 0010017
#define TARGET_CIBAUD     002003600000      /* input baud rate */
#define TARGET_CMSPAR     010000000000      /* mark or space (stick) parity */
#define TARGET_CRTSCTS    020000000000      /* flow control */

#define TARGET_IBSHIFT  16      /* Shift from CBAUD to CIBAUD */

/* c_lflag bits */

#define TARGET_ISIG 0000001
#define TARGET_ICANON   0000002
#define TARGET_XCASE    0000004
#define TARGET_ECHO 0000010
#define TARGET_ECHOE    0000020
#define TARGET_ECHOK    0000040
#define TARGET_ECHONL   0000100
#define TARGET_NOFLSH   0000200
#define TARGET_TOSTOP   0000400
#define TARGET_ECHOCTL  0001000
#define TARGET_ECHOPRT  0002000
#define TARGET_ECHOKE   0004000
#define TARGET_FLUSHO   0010000
#define TARGET_PENDIN   0040000
#define TARGET_IEXTEN   0100000
#define TARGET_EXTPROC  0200000

/* tcflow() and TCXONC use these */

#define TARGET_TCOOFF       0
#define TARGET_TCOON        1
#define TARGET_TCIOFF       2
#define TARGET_TCION        3

/* tcflush() and TCFLSH use these */

#define TARGET_TCIFLUSH 0
#define TARGET_TCOFLUSH 1
#define TARGET_TCIOFLUSH    2

/* tcsetattr uses these */

#define TARGET_TCSANOW      0
#define TARGET_TCSADRAIN    1
#define TARGET_TCSAFLUSH    2

/* from arch/xtensa/include/uapi/asm/ioctls.h */

#define TARGET_FIOCLEX     _IO('f', 1)
#define TARGET_FIONCLEX    _IO('f', 2)
#define TARGET_FIOASYNC    _IOW('f', 125, int)
#define TARGET_FIONBIO     _IOW('f', 126, int)
#define TARGET_FIONREAD    _IOR('f', 127, int)
#define TARGET_TIOCINQ     FIONREAD
#define TARGET_FIOQSIZE    _IOR('f', 128, loff_t)

#define TARGET_TCGETS      0x5401
#define TARGET_TCSETS      0x5402
#define TARGET_TCSETSW     0x5403
#define TARGET_TCSETSF     0x5404

#define TARGET_TCGETA      0x80127417  /* _IOR('t', 23, struct termio) */
#define TARGET_TCSETA      0x40127418  /* _IOW('t', 24, struct termio) */
#define TARGET_TCSETAW     0x40127419  /* _IOW('t', 25, struct termio) */
#define TARGET_TCSETAF     0x4012741C  /* _IOW('t', 28, struct termio) */

#define TARGET_TCSBRK      _IO('t', 29)
#define TARGET_TCXONC      _IO('t', 30)
#define TARGET_TCFLSH      _IO('t', 31)

#define TARGET_TIOCSWINSZ  0x40087467  /* _IOW('t', 103, struct winsize) */
#define TARGET_TIOCGWINSZ  0x80087468  /* _IOR('t', 104, struct winsize) */
#define TARGET_TIOCSTART   _IO('t', 110)       /* start output, like ^Q */
#define TARGET_TIOCSTOP    _IO('t', 111)       /* stop output, like ^S */
#define TARGET_TIOCOUTQ        _IOR('t', 115, int)     /* output queue size */

#define TARGET_TIOCSPGRP   _IOW('t', 118, int)
#define TARGET_TIOCGPGRP   _IOR('t', 119, int)

#define TARGET_TIOCEXCL    _IO('T', 12)
#define TARGET_TIOCNXCL    _IO('T', 13)
#define TARGET_TIOCSCTTY   _IO('T', 14)

#define TARGET_TIOCSTI     _IOW('T', 18, char)
#define TARGET_TIOCMGET    _IOR('T', 21, unsigned int)
#define TARGET_TIOCMBIS    _IOW('T', 22, unsigned int)
#define TARGET_TIOCMBIC    _IOW('T', 23, unsigned int)
#define TARGET_TIOCMSET    _IOW('T', 24, unsigned int)
# define TARGET_TIOCM_LE   0x001
# define TARGET_TIOCM_DTR  0x002
# define TARGET_TIOCM_RTS  0x004
# define TARGET_TIOCM_ST   0x008
# define TARGET_TIOCM_SR   0x010
# define TARGET_TIOCM_CTS  0x020
# define TARGET_TIOCM_CAR  0x040
# define TARGET_TIOCM_RNG  0x080
# define TARGET_TIOCM_DSR  0x100
# define TARGET_TIOCM_CD   TIOCM_CAR
# define TARGET_TIOCM_RI   TIOCM_RNG

#define TARGET_TIOCGSOFTCAR    _IOR('T', 25, unsigned int)
#define TARGET_TIOCSSOFTCAR    _IOW('T', 26, unsigned int)
#define TARGET_TIOCLINUX   _IOW('T', 28, char)
#define TARGET_TIOCCONS    _IO('T', 29)
#define TARGET_TIOCGSERIAL 0x803C541E  /*_IOR('T', 30, struct serial_struct)*/
#define TARGET_TIOCSSERIAL 0x403C541F  /*_IOW('T', 31, struct serial_struct)*/
#define TARGET_TIOCPKT     _IOW('T', 32, int)
# define TARGET_TIOCPKT_DATA        0
# define TARGET_TIOCPKT_FLUSHREAD   1
# define TARGET_TIOCPKT_FLUSHWRITE  2
# define TARGET_TIOCPKT_STOP        4
# define TARGET_TIOCPKT_START       8
# define TARGET_TIOCPKT_NOSTOP     16
# define TARGET_TIOCPKT_DOSTOP     32
# define TARGET_TIOCPKT_IOCTL      64


#define TARGET_TIOCNOTTY   _IO('T', 34)
#define TARGET_TIOCSETD    _IOW('T', 35, int)
#define TARGET_TIOCGETD    _IOR('T', 36, int)
#define TARGET_TCSBRKP     _IOW('T', 37, int)   /* Needed for POSIX tcsendbreak()*/
#define TARGET_TIOCSBRK    _IO('T', 39)         /* BSD compatibility */
#define TARGET_TIOCCBRK    _IO('T', 40)         /* BSD compatibility */
#define TARGET_TIOCGSID    _IOR('T', 41, pid_t) /* Return the session ID of FD*/
#define TARGET_TCGETS2     _IOR('T', 42, struct termios2)
#define TARGET_TCSETS2     _IOW('T', 43, struct termios2)
#define TARGET_TCSETSW2    _IOW('T', 44, struct termios2)
#define TARGET_TCSETSF2    _IOW('T', 45, struct termios2)
#define TARGET_TIOCGRS485  _IOR('T', 46, struct serial_rs485)
#define TARGET_TIOCSRS485  _IOWR('T', 47, struct serial_rs485)
#define TARGET_TIOCGPTN    _IOR('T',0x30, unsigned int) /* Get Pty Number (of pty-mux device) */
#define TARGET_TIOCSPTLCK  _IOW('T',0x31, int)  /* Lock/unlock Pty */
#define TARGET_TIOCGDEV    _IOR('T',0x32, unsigned int) /* Get primary device node of /dev/console */
#define TARGET_TIOCSIG     _IOW('T',0x36, int)  /* Generate signal on Pty slave */
#define TARGET_TIOCVHANGUP _IO('T', 0x37)
#define TARGET_TIOCGPKT    _IOR('T', 0x38, int) /* Get packet mode state */
#define TARGET_TIOCGPTLCK  _IOR('T', 0x39, int) /* Get Pty lock state */
#define TARGET_TIOCGEXCL   _IOR('T', 0x40, int) /* Get exclusive mode state */
#define TARGET_TIOCGPTPEER _IO('T', 0x41) /* Safely open the slave */

#define TARGET_TIOCSERCONFIG   _IO('T', 83)
#define TARGET_TIOCSERGWILD    _IOR('T', 84,  int)
#define TARGET_TIOCSERSWILD    _IOW('T', 85,  int)
#define TARGET_TIOCGLCKTRMIOS  0x5456
#define TARGET_TIOCSLCKTRMIOS  0x5457
#define TARGET_TIOCSERGSTRUCT  0x5458           /* For debugging only */
#define TARGET_TIOCSERGETLSR   _IOR('T', 89, unsigned int) /* Get line status reg. */
/* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */
# define TARGET_TIOCSER_TEMT    0x01            /* Transmitter physically empty */
#define TARGET_TIOCSERGETMULTI 0x80a8545a /* Get multiport config  */
/* _IOR('T', 90, struct serial_multiport_struct) */
#define TARGET_TIOCSERSETMULTI 0x40a8545b /* Set multiport config */
/* _IOW('T', 91, struct serial_multiport_struct) */

#define TARGET_TIOCMIWAIT  _IO('T', 92) /* wait for a change on serial input line(s) */
#define TARGET_TIOCGICOUNT 0x545D  /* read serial port inline interrupt counts */
#endif /* XTENSA_TERMBITS_H */
