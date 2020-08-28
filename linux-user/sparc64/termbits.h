/* from asm/termbits.h */

#ifndef LINUX_USER_SPARC64_TERMBITS_H
#define LINUX_USER_SPARC64_TERMBITS_H

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


/* c_cc characters */
#define TARGET_VINTR    0
#define TARGET_VQUIT    1
#define TARGET_VERASE   2
#define TARGET_VKILL    3
#define TARGET_VEOF     4
#define TARGET_VEOL     5
#define TARGET_VEOL2    6
#define TARGET_VSWTC    7
#define TARGET_VSTART   8
#define TARGET_VSTOP    9

#define TARGET_VSUSP    10
#define TARGET_VDSUSP   11  /* SunOS POSIX nicety I do believe... */
#define TARGET_VREPRINT 12
#define TARGET_VDISCARD 13
#define TARGET_VWERASE  14
#define TARGET_VLNEXT   15

/* Kernel keeps vmin/vtime separated, user apps assume vmin/vtime is
 * shared with eof/eol
 */
#define TARGET_VMIN     TARGET_VEOF
#define TARGET_VTIME    TARGET_VEOL

/* c_iflag bits */
#define TARGET_IGNBRK	0x00000001
#define TARGET_BRKINT	0x00000002
#define TARGET_IGNPAR	0x00000004
#define TARGET_PARMRK	0x00000008
#define TARGET_INPCK	0x00000010
#define TARGET_ISTRIP	0x00000020
#define TARGET_INLCR	0x00000040
#define TARGET_IGNCR	0x00000080
#define TARGET_ICRNL	0x00000100
#define TARGET_IUCLC	0x00000200
#define TARGET_IXON	0x00000400
#define TARGET_IXANY	0x00000800
#define TARGET_IXOFF	0x00001000
#define TARGET_IMAXBEL	0x00002000
#define TARGET_IUTF8	0x00004000

/* c_oflag bits */
#define TARGET_OPOST	0x00000001
#define TARGET_OLCUC	0x00000002
#define TARGET_ONLCR	0x00000004
#define TARGET_OCRNL	0x00000008
#define TARGET_ONOCR	0x00000010
#define TARGET_ONLRET	0x00000020
#define TARGET_OFILL	0x00000040
#define TARGET_OFDEL	0x00000080
#define TARGET_NLDLY	0x00000100
#define   TARGET_NL0	0x00000000
#define   TARGET_NL1	0x00000100
#define TARGET_CRDLY	0x00000600
#define   TARGET_CR0	0x00000000
#define   TARGET_CR1	0x00000200
#define   TARGET_CR2	0x00000400
#define   TARGET_CR3	0x00000600
#define TARGET_TABDLY	0x00001800
#define   TARGET_TAB0	0x00000000
#define   TARGET_TAB1	0x00000800
#define   TARGET_TAB2	0x00001000
#define   TARGET_TAB3	0x00001800
#define   TARGET_XTABS	0x00001800
#define TARGET_BSDLY	0x00002000
#define   TARGET_BS0	0x00000000
#define   TARGET_BS1	0x00002000
#define TARGET_VTDLY	0x00004000
#define   TARGET_VT0	0x00000000
#define   TARGET_VT1	0x00004000
#define TARGET_FFDLY	0x00008000
#define   TARGET_FF0	0x00000000
#define   TARGET_FF1	0x00008000
#define TARGET_PAGEOUT 0x00010000  /* SUNOS specific */
#define TARGET_WRAP    0x00020000  /* SUNOS specific */

/* c_cflag bit meaning */
#define TARGET_CBAUD	  0x0000100f
#define  TARGET_B0	  0x00000000   /* hang up */
#define  TARGET_B50	  0x00000001
#define  TARGET_B75	  0x00000002
#define  TARGET_B110	  0x00000003
#define  TARGET_B134	  0x00000004
#define  TARGET_B150	  0x00000005
#define  TARGET_B200	  0x00000006
#define  TARGET_B300	  0x00000007
#define  TARGET_B600	  0x00000008
#define  TARGET_B1200	  0x00000009
#define  TARGET_B1800	  0x0000000a
#define  TARGET_B2400	  0x0000000b
#define  TARGET_B4800	  0x0000000c
#define  TARGET_B9600	  0x0000000d
#define  TARGET_B19200	  0x0000000e
#define  TARGET_B38400	  0x0000000f
#define TARGET_EXTA      B19200
#define TARGET_EXTB      B38400
#define  TARGET_CSIZE    0x00000030
#define   TARGET_CS5	  0x00000000
#define   TARGET_CS6	  0x00000010
#define   TARGET_CS7	  0x00000020
#define   TARGET_CS8	  0x00000030
#define TARGET_CSTOPB	  0x00000040
#define TARGET_CREAD	  0x00000080
#define TARGET_PARENB	  0x00000100
#define TARGET_PARODD	  0x00000200
#define TARGET_HUPCL	  0x00000400
#define TARGET_CLOCAL	  0x00000800
#define TARGET_CBAUDEX   0x00001000
/* We'll never see these speeds with the Zilogs, but for completeness... */
#define  TARGET_B57600   0x00001001
#define  TARGET_B115200  0x00001002
#define  TARGET_B230400  0x00001003
#define  TARGET_B460800  0x00001004
/* This is what we can do with the Zilogs. */
#define  TARGET_B76800   0x00001005
/* This is what we can do with the SAB82532. */
#define  TARGET_B153600  0x00001006
#define  TARGET_B307200  0x00001007
#define  TARGET_B614400  0x00001008
#define  TARGET_B921600  0x00001009
/* And these are the rest... */
#define  TARGET_B500000  0x0000100a
#define  TARGET_B576000  0x0000100b
#define TARGET_B1000000  0x0000100c
#define TARGET_B1152000  0x0000100d
#define TARGET_B1500000  0x0000100e
#define TARGET_B2000000  0x0000100f
/* These have totally bogus values and nobody uses them
   so far. Later on we'd have to use say 0x10000x and
   adjust CBAUD constant and drivers accordingly.
#define B2500000  0x00001010
#define B3000000  0x00001011
#define B3500000  0x00001012
#define B4000000  0x00001013  */
#define TARGET_CIBAUD	  0x100f0000  /* input baud rate (not used) */
#define TARGET_CMSPAR	  0x40000000  /* mark or space (stick) parity */
#define TARGET_CRTSCTS	  0x80000000  /* flow control */

/* c_lflag bits */
#define TARGET_ISIG	0x00000001
#define TARGET_ICANON	0x00000002
#define TARGET_XCASE	0x00000004
#define TARGET_ECHO	0x00000008
#define TARGET_ECHOE	0x00000010
#define TARGET_ECHOK	0x00000020
#define TARGET_ECHONL	0x00000040
#define TARGET_NOFLSH	0x00000080
#define TARGET_TOSTOP	0x00000100
#define TARGET_ECHOCTL	0x00000200
#define TARGET_ECHOPRT	0x00000400
#define TARGET_ECHOKE	0x00000800
#define TARGET_DEFECHO  0x00001000  /* SUNOS thing, what is it? */
#define TARGET_FLUSHO	0x00002000
#define TARGET_PENDIN	0x00004000
#define TARGET_IEXTEN	0x00008000
#define TARGET_EXTPROC  0x00010000

/* ioctls */

/* Big T */
#define TARGET_TCGETA		TARGET_IOR('T', 1, struct target_termio)
#define TARGET_TCSETA		TARGET_IOW('T', 2, struct target_termio)
#define TARGET_TCSETAW		TARGET_IOW('T', 3, struct target_termio)
#define TARGET_TCSETAF		TARGET_IOW('T', 4, struct target_termio)
#define TARGET_TCSBRK		TARGET_IO('T', 5)
#define TARGET_TCXONC		TARGET_IO('T', 6)
#define TARGET_TCFLSH		TARGET_IO('T', 7)
#define TARGET_TCGETS		TARGET_IOR('T', 8, struct target_termios)
#define TARGET_TCSETS		TARGET_IOW('T', 9, struct target_termios)
#define TARGET_TCSETSW		TARGET_IOW('T', 10, struct target_termios)
#define TARGET_TCSETSF		TARGET_IOW('T', 11, struct target_termios)

/* Note that all the ioctls that are not available in Linux have a
 * double underscore on the front to: a) avoid some programs to
 * thing we support some ioctls under Linux (autoconfiguration stuff)
 */
/* Little t */
#define TARGET_TIOCGETD	TARGET_IOR('t', 0, int)
#define TARGET_TIOCSETD	TARGET_IOW('t', 1, int)
//#define __TIOCHPCL        _IO('t', 2) /* SunOS Specific */
//#define __TIOCMODG        _IOR('t', 3, int) /* SunOS Specific */
//#define __TIOCMODS        _IOW('t', 4, int) /* SunOS Specific */
//#define __TIOCGETP        _IOR('t', 8, struct sgttyb) /* SunOS Specific */
//#define __TIOCSETP        _IOW('t', 9, struct sgttyb) /* SunOS Specific */
//#define __TIOCSETN        _IOW('t', 10, struct sgttyb) /* SunOS Specific */
#define TARGET_TIOCEXCL	TARGET_IO('t', 13)
#define TARGET_TIOCNXCL	TARGET_IO('t', 14)
//#define __TIOCFLUSH       _IOW('t', 16, int) /* SunOS Specific */
//#define __TIOCSETC        _IOW('t', 17, struct tchars) /* SunOS Specific */
//#define __TIOCGETC        _IOR('t', 18, struct tchars) /* SunOS Specific */
//#define __TIOCTCNTL       _IOW('t', 32, int) /* SunOS Specific */
//#define __TIOCSIGNAL      _IOW('t', 33, int) /* SunOS Specific */
//#define __TIOCSETX        _IOW('t', 34, int) /* SunOS Specific */
//#define __TIOCGETX        _IOR('t', 35, int) /* SunOS Specific */
#define TARGET_TIOCCONS	TARGET_IO('t', 36)
//#define __TIOCSSIZE     _IOW('t', 37, struct sunos_ttysize) /* SunOS Specific */
//#define __TIOCGSIZE     _IOR('t', 38, struct sunos_ttysize) /* SunOS Specific */
#define TARGET_TIOCGSOFTCAR	TARGET_IOR('t', 100, int)
#define TARGET_TIOCSSOFTCAR	TARGET_IOW('t', 101, int)
//#define __TIOCUCNTL       _IOW('t', 102, int) /* SunOS Specific */
#define TARGET_TIOCSWINSZ	TARGET_IOW('t', 103, struct winsize)
#define TARGET_TIOCGWINSZ	TARGET_IOR('t', 104, struct winsize)
//#define __TIOCREMOTE      _IOW('t', 105, int) /* SunOS Specific */
#define TARGET_TIOCMGET	TARGET_IOR('t', 106, int)
#define TARGET_TIOCMBIC	TARGET_IOW('t', 107, int)
#define TARGET_TIOCMBIS	TARGET_IOW('t', 108, int)
#define TARGET_TIOCMSET	TARGET_IOW('t', 109, int)
#define TARGET_TIOCSTART       TARGET_IO('t', 110)
#define TARGET_TIOCSTOP        TARGET_IO('t', 111)
#define TARGET_TIOCPKT		TARGET_IOW('t', 112, int)
#define TARGET_TIOCNOTTY	TARGET_IO('t', 113)
#define TARGET_TIOCSTI		TARGET_IOW('t', 114, char)
#define TARGET_TIOCOUTQ	TARGET_IOR('t', 115, int)
//#define __TIOCGLTC        _IOR('t', 116, struct ltchars) /* SunOS Specific */
//#define __TIOCSLTC        _IOW('t', 117, struct ltchars) /* SunOS Specific */
/* 118 is the non-posix setpgrp tty ioctl */
/* 119 is the non-posix getpgrp tty ioctl */
//#define __TIOCCDTR        TARGET_IO('t', 120) /* SunOS Specific */
//#define __TIOCSDTR        TARGET_IO('t', 121) /* SunOS Specific */
#define TARGET_TIOCCBRK        TARGET_IO('t', 122)
#define TARGET_TIOCSBRK        TARGET_IO('t', 123)
//#define __TIOCLGET        TARGET_IOW('t', 124, int) /* SunOS Specific */
//#define __TIOCLSET        TARGET_IOW('t', 125, int) /* SunOS Specific */
//#define __TIOCLBIC        TARGET_IOW('t', 126, int) /* SunOS Specific */
//#define __TIOCLBIS        TARGET_IOW('t', 127, int) /* SunOS Specific */
//#define __TIOCISPACE      TARGET_IOR('t', 128, int) /* SunOS Specific */
//#define __TIOCISIZE       TARGET_IOR('t', 129, int) /* SunOS Specific */
#define TARGET_TIOCSPGRP	TARGET_IOW('t', 130, int)
#define TARGET_TIOCGPGRP	TARGET_IOR('t', 131, int)
#define TARGET_TIOCSCTTY	TARGET_IO('t', 132)
#define TARGET_TIOCGSID	TARGET_IOR('t', 133, int)
/* Get minor device of a pty master's FD -- Solaris equiv is ISPTM */
#define TARGET_TIOCGPTN	TARGET_IOR('t', 134, unsigned int) /* Get Pty Number */
#define TARGET_TIOCSPTLCK	TARGET_IOW('t', 135, int) /* Lock/unlock PTY */
#define TARGET_TIOCGPTPEER      TARGET_IO('t', 137) /* Safely open the slave */

/* Little f */
#define TARGET_FIOCLEX		TARGET_IO('f', 1)
#define TARGET_FIONCLEX	TARGET_IO('f', 2)
#define TARGET_FIOASYNC	TARGET_IOW('f', 125, int)
#define TARGET_FIONBIO		TARGET_IOW('f', 126, int)
#define TARGET_FIONREAD	TARGET_IOR('f', 127, int)
#define TARGET_TIOCINQ		TARGET_FIONREAD

/* SCARY Rutgers local SunOS kernel hackery, perhaps I will support it
 * someday.  This is completely bogus, I know...
 */
//#define __TCGETSTAT       TARGET_IO('T', 200) /* Rutgers specific */
//#define __TCSETSTAT       TARGET_IO('T', 201) /* Rutgers specific */

/* Linux specific, no SunOS equivalent. */
#define TARGET_TIOCLINUX	0x541C
#define TARGET_TIOCGSERIAL	0x541E
#define TARGET_TIOCSSERIAL	0x541F
#define TARGET_TCSBRKP		0x5425
#define TARGET_TIOCTTYGSTRUCT	0x5426
#define TARGET_TIOCSERCONFIG	0x5453
#define TARGET_TIOCSERGWILD	0x5454
#define TARGET_TIOCSERSWILD	0x5455
#define TARGET_TIOCGLCKTRMIOS	0x5456
#define TARGET_TIOCSLCKTRMIOS	0x5457
#define TARGET_TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TARGET_TIOCSERGETLSR   0x5459 /* Get line status register */
#define TARGET_TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TARGET_TIOCSERSETMULTI 0x545B /* Set multiport config */
#define TARGET_TIOCMIWAIT	0x545C /* Wait input */
#define TARGET_TIOCGICOUNT	0x545D /* Read serial port inline interrupt counts */

#endif
