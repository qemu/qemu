/* from asm/termbits.h */

#define TARGET_NCCS 19

struct target_termios {
    unsigned int c_iflag;               /* input mode flags */
    unsigned int c_oflag;               /* output mode flags */
    unsigned int c_cflag;               /* control mode flags */
    unsigned int c_lflag;               /* local mode flags */
    unsigned char c_cc[TARGET_NCCS];                /* control characters */
    unsigned char c_line;                    /* line discipline */
    unsigned int c_ispeed;		/* input speed */
    unsigned int c_ospeed;		/* output speed */
};

/* c_cc character offsets */
#define TARGET_VINTR 	0
#define TARGET_VQUIT 	1
#define TARGET_VERASE 	2
#define TARGET_VKILL	3
#define TARGET_VEOF	4
#define TARGET_VMIN	5
#define TARGET_VEOL	6
#define TARGET_VTIME	7
#define TARGET_VEOL2	8
#define TARGET_VSWTC	9

#define TARGET_VWERASE 	10
#define TARGET_VREPRINT	11
#define TARGET_VSUSP 		12
#define TARGET_VSTART		13
#define TARGET_VSTOP		14
#define TARGET_VLNEXT		15
#define TARGET_VDISCARD	16

#define TARGET_IGNBRK	0000001
#define TARGET_BRKINT	0000002
#define TARGET_IGNPAR	0000004
#define TARGET_PARMRK	0000010
#define TARGET_INPCK	0000020
#define TARGET_ISTRIP	0000040
#define TARGET_INLCR	0000100
#define TARGET_IGNCR	0000200
#define TARGET_ICRNL	0000400
#define TARGET_IXON	0001000
#define TARGET_IXOFF	0002000
#define TARGET_IXANY	0004000
#define TARGET_IUCLC	0010000
#define TARGET_IMAXBEL	0020000
#define	TARGET_IUTF8	0040000

/* c_oflag bits */
#define TARGET_OPOST	0000001
#define TARGET_ONLCR	0000002
#define TARGET_OLCUC	0000004

#define TARGET_OCRNL	0000010
#define TARGET_ONOCR	0000020
#define TARGET_ONLRET	0000040

#define TARGET_OFILL	00000100
#define TARGET_OFDEL	00000200
#define TARGET_NLDLY	00001400
#define   TARGET_NL0	00000000
#define   TARGET_NL1	00000400
#define   TARGET_NL2	00001000
#define   TARGET_NL3	00001400
#define TARGET_TABDLY	00006000
#define   TARGET_TAB0	00000000
#define   TARGET_TAB1	00002000
#define   TARGET_TAB2	00004000
#define   TARGET_TAB3	00006000
#define   TARGET_XTABS	00006000	/* required by POSIX to == TAB3 */
#define TARGET_CRDLY	00030000
#define   TARGET_CR0	00000000
#define   TARGET_CR1	00010000
#define   TARGET_CR2	00020000
#define   TARGET_CR3	00030000
#define TARGET_FFDLY	00040000
#define   TARGET_FF0	00000000
#define   TARGET_FF1	00040000
#define TARGET_BSDLY	00100000
#define   TARGET_BS0	00000000
#define   TARGET_BS1	00100000
#define TARGET_VTDLY	00200000
#define   TARGET_VT0	00000000
#define   TARGET_VT1	00200000

/* c_cflag bit meaning */
#define TARGET_CBAUD	0000377
#define  TARGET_B0	0000000		/* hang up */
#define  TARGET_B50	0000001
#define  TARGET_B75	0000002
#define  TARGET_B110	0000003
#define  TARGET_B134	0000004
#define  TARGET_B150	0000005
#define  TARGET_B200	0000006
#define  TARGET_B300	0000007
#define  TARGET_B600	0000010
#define  TARGET_B1200	0000011
#define  TARGET_B1800	0000012
#define  TARGET_B2400	0000013
#define  TARGET_B4800	0000014
#define  TARGET_B9600	0000015
#define  TARGET_B19200	0000016
#define  TARGET_B38400	0000017
#define TARGET_EXTA B19200
#define TARGET_EXTB B38400
#define TARGET_CBAUDEX 0000000
#define  TARGET_B57600   00020
#define  TARGET_B115200  00021
#define  TARGET_B230400  00022
#define  TARGET_B460800  00023
#define  TARGET_B500000  00024
#define  TARGET_B576000  00025
#define  TARGET_B921600  00026
#define TARGET_B1000000  00027
#define TARGET_B1152000  00030
#define TARGET_B1500000  00031
#define TARGET_B2000000  00032
#define TARGET_B2500000  00033
#define TARGET_B3000000  00034
#define TARGET_B3500000  00035
#define TARGET_B4000000  00036

#define TARGET_CSIZE	00001400
#define   TARGET_CS5	00000000
#define   TARGET_CS6	00000400
#define   TARGET_CS7	00001000
#define   TARGET_CS8	00001400

#define TARGET_CSTOPB	00002000
#define TARGET_CREAD	00004000
#define TARGET_PARENB	00010000
#define TARGET_PARODD	00020000
#define TARGET_HUPCL	00040000

#define TARGET_CLOCAL	00100000
#define TARGET_CMSPAR	010000000000		/* mark or space (stick) parity */
#define TARGET_CRTSCTS	020000000000		/* flow control */

/* c_lflag bits */
#define TARGET_ISIG	0x00000080
#define TARGET_ICANON	0x00000100
#define TARGET_XCASE	0x00004000
#define TARGET_ECHO	0x00000008
#define TARGET_ECHOE	0x00000002
#define TARGET_ECHOK	0x00000004
#define TARGET_ECHONL	0x00000010
#define TARGET_NOFLSH	0x80000000
#define TARGET_TOSTOP	0x00400000
#define TARGET_ECHOCTL	0x00000040
#define TARGET_ECHOPRT	0x00000020
#define TARGET_ECHOKE	0x00000001
#define TARGET_FLUSHO	0x00800000
#define TARGET_PENDIN	0x20000000
#define TARGET_IEXTEN	0x00000400

/* ioctls */

#define TARGET_FIOCLEX		TARGET_IO('f', 1)
#define TARGET_FIONCLEX	        TARGET_IO('f', 2)
#define TARGET_FIOASYNC	        TARGET_IOW('f', 125, int)
#define TARGET_FIONBIO		TARGET_IOW('f', 126, int)
#define TARGET_FIONREAD	        TARGET_IOR('f', 127, int)
#define TARGET_TIOCINQ		TARGET_FIONREAD
//#define TARGET_FIOQSIZE	        TARGET_IOR('f', 128, loff_t)

#define TARGET_TCGETS		TARGET_IOR('t', 19, struct target_termios)
#define TARGET_TCSETS		TARGET_IOW('t', 20, struct target_termios)
#define TARGET_TCSETSW		TARGET_IOW('t', 21, struct target_termios)
#define TARGET_TCSETSF		TARGET_IOW('t', 22, struct target_termios)

#define TARGET_TCGETA		TARGET_IOR('t', 23, struct target_termio)
#define TARGET_TCSETA		TARGET_IOW('t', 24, struct target_termio)
#define TARGET_TCSETAW		TARGET_IOW('t', 25, struct target_termio)
#define TARGET_TCSETAF		TARGET_IOW('t', 28, struct target_termio)

#define TARGET_TCSBRK		TARGET_IO('t', 29)
#define TARGET_TCXONC		TARGET_IO('t', 30)
#define TARGET_TCFLSH		TARGET_IO('t', 31)

#define TARGET_TIOCSWINSZ	TARGET_IOW('t', 103, struct target_winsize)
#define TARGET_TIOCGWINSZ	TARGET_IOR('t', 104, struct target_winsize)
#define	TARGET_TIOCSTART	TARGET_IO('t', 110)		/* start output, like ^Q */
#define	TARGET_TIOCSTOP	TARGET_IO('t', 111)		/* stop output, like ^S */
#define TARGET_TIOCOUTQ        TARGET_IOR('t', 115, int)     /* output queue size */

#define TARGET_TIOCGLTC	TARGET_IOR('t', 116, struct target_ltchars)
#define TARGET_TIOCSLTC	TARGET_IOW('t', 117, struct target_ltchars)
#define TARGET_TIOCSPGRP	TARGET_IOW('t', 118, int)
#define TARGET_TIOCGPGRP	TARGET_IOR('t', 119, int)

#define TARGET_TIOCEXCL	0x540C
#define TARGET_TIOCNXCL	0x540D
#define TARGET_TIOCSCTTY	0x540E

#define TARGET_TIOCSTI		0x5412
#define TARGET_TIOCMGET	0x5415
#define TARGET_TIOCMBIS	0x5416
#define TARGET_TIOCMBIC	0x5417
#define TARGET_TIOCMSET	0x5418

#define TARGET_TIOCGSOFTCAR	0x5419
#define TARGET_TIOCSSOFTCAR	0x541A
#define TARGET_TIOCLINUX	0x541C
#define TARGET_TIOCCONS	0x541D
#define TARGET_TIOCGSERIAL	0x541E
#define TARGET_TIOCSSERIAL	0x541F
#define TARGET_TIOCPKT		0x5420

#define TARGET_TIOCNOTTY	0x5422
#define TARGET_TIOCSETD	0x5423
#define TARGET_TIOCGETD	0x5424
#define TARGET_TCSBRKP		0x5425	/* Needed for POSIX tcsendbreak() */
#define TARGET_TIOCTTYGSTRUCT	0x5426  /* For debugging only */
#define TARGET_TIOCSBRK	0x5427  /* BSD compatibility */
#define TARGET_TIOCCBRK	0x5428  /* BSD compatibility */
#define TARGET_TIOCGSID	0x5429  /* Return the session ID of FD */
#define TARGET_TIOCGPTN	TARGET_IOR('T',0x30, unsigned int) /* Get Pty Number (of pty-mux device) */
#define TARGET_TIOCSPTLCK	TARGET_IOW('T',0x31, int)  /* Lock/unlock Pty */

#define TARGET_TIOCSERCONFIG	0x5453
#define TARGET_TIOCSERGWILD	0x5454
#define TARGET_TIOCSERSWILD	0x5455
#define TARGET_TIOCGLCKTRMIOS	0x5456
#define TARGET_TIOCSLCKTRMIOS	0x5457
#define TARGET_TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TARGET_TIOCSERGETLSR   0x5459 /* Get line status register */
  /* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */
# define TARGET_TIOCSER_TEMT    0x01	/* Transmitter physically empty */
#define TARGET_TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TARGET_TIOCSERSETMULTI 0x545B /* Set multiport config */

#define TARGET_TIOCMIWAIT	0x545C	/* wait for a change on serial input line(s) */
#define TARGET_TIOCGICOUNT	0x545D	/* read serial port inline interrupt counts */
