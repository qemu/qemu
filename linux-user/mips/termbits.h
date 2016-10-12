/* from asm/termbits.h */

#define TARGET_NCCS 23

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
#define  TARGET_BOTHER   0010000
#define  TARGET_B57600   0010001
#define  TARGET_B115200  0010002
#define  TARGET_B230400  0010003
#define  TARGET_B460800  0010004
#define  TARGET_B500000  0010005
#define  TARGET_B576000  0010006
#define  TARGET_B921600  0010007
#define  TARGET_B1000000 0010010
#define  TARGET_B1152000 0010011
#define  TARGET_B1500000 0010012
#define  TARGET_B2000000 0010013
#define  TARGET_B2500000 0010014
#define  TARGET_B3000000 0010015
#define  TARGET_B3500000 0010016
#define  TARGET_B4000000 0010017
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
#define TARGET_IEXTEN  0000400
#define TARGET_ECHOCTL 0001000
#define TARGET_ECHOPRT 0002000
#define TARGET_ECHOKE  0004000
#define TARGET_FLUSHO  0010000
#define TARGET_PENDIN  0040000
#define TARGET_TOSTOP  0100000
#define TARGET_ITOSTOP TARGET_TOSTOP

/* c_cc character offsets */
#define TARGET_VINTR	0
#define TARGET_VQUIT	1
#define TARGET_VERASE	2
#define TARGET_VKILL	3
#define TARGET_VMIN	4
#define TARGET_VTIME	5
#define TARGET_VEOL2	6
#define TARGET_VSWTC	7
#define TARGET_VSTART	8
#define TARGET_VSTOP	9
#define TARGET_VSUSP	10
/* VDSUSP not supported */
#define TARGET_VREPRINT	12
#define TARGET_VDISCARD	13
#define TARGET_VWERASE	14
#define TARGET_VLNEXT	15
#define TARGET_VEOF	16
#define TARGET_VEOL	17

/* ioctls */

#define TARGET_TCGETA		0x5401
#define TARGET_TCSETA		0x5402	/* Clashes with SNDCTL_TMR_START sound ioctl */
#define TARGET_TCSETAW		0x5403
#define TARGET_TCSETAF		0x5404

#define TARGET_TCSBRK		0x5405
#define TARGET_TCXONC		0x5406
#define TARGET_TCFLSH		0x5407

#define TARGET_TCGETS		0x540d
#define TARGET_TCSETS		0x540e
#define TARGET_TCSETSW		0x540f
#define TARGET_TCSETSF		0x5410

#define TARGET_TIOCEXCL	0x740d		/* set exclusive use of tty */
#define TARGET_TIOCNXCL	0x740e		/* reset exclusive use of tty */
#define TARGET_TIOCOUTQ	0x7472		/* output queue size */
#define TARGET_TIOCSTI	0x5472		/* simulate terminal input */
#define TARGET_TIOCMGET	0x741d		/* get all modem bits */
#define TARGET_TIOCMBIS	0x741b		/* bis modem bits */
#define TARGET_TIOCMBIC	0x741c		/* bic modem bits */
#define TARGET_TIOCMSET	0x741a		/* set all modem bits */
#define TARGET_TIOCPKT		0x5470		/* pty: set/clear packet mode */
#define	 TARGET_TIOCPKT_DATA		0x00	/* data packet */
#define	 TARGET_TIOCPKT_FLUSHREAD	0x01	/* flush packet */
#define	 TARGET_TIOCPKT_FLUSHWRITE	0x02	/* flush packet */
#define	 TARGET_TIOCPKT_STOP		0x04	/* stop output */
#define	 TARGET_TIOCPKT_START		0x08	/* start output */
#define	 TARGET_TIOCPKT_NOSTOP		0x10	/* no more ^S, ^Q */
#define	 TARGET_TIOCPKT_DOSTOP		0x20	/* now do ^S ^Q */
/* #define  TIOCPKT_IOCTL		0x40	state change of pty driver */
#define TARGET_TIOCSWINSZ	TARGET_IOW('t', 103, struct winsize)	/* set window size */
#define TARGET_TIOCGWINSZ	TARGET_IOR('t', 104, struct winsize)	/* get window size */
#define TARGET_TIOCNOTTY	0x5471		/* void tty association */
#define TARGET_TIOCSETD	0x7401
#define TARGET_TIOCGETD	0x7400

#define TARGET_FIOCLEX		0x6601
#define TARGET_FIONCLEX	0x6602
#define TARGET_FIOASYNC	0x667d
#define TARGET_FIONBIO		0x667e
#define TARGET_FIOQSIZE	0x667f

#define TARGET_TIOCGLTC	0x7474			/* get special local chars */
#define TARGET_TIOCSLTC	0x7475			/* set special local chars */
#define TARGET_TIOCSPGRP	TARGET_IOW('t', 118, int)	/* set pgrp of tty */
#define TARGET_TIOCGPGRP	TARGET_IOR('t', 119, int)	/* get pgrp of tty */
#define TARGET_TIOCCONS	TARGET_IOW('t', 120, int)	/* become virtual console */

#define TARGET_FIONREAD	0x467f
#define TARGET_TIOCINQ		TARGET_FIONREAD

#define TARGET_TIOCGETP        0x7408
#define TARGET_TIOCSETP        0x7409
#define TARGET_TIOCSETN        0x740a			/* TIOCSETP wo flush */

/* #define TARGET_TIOCSETA	TARGET_IOW('t', 20, struct termios) set termios struct */
/* #define TARGET_TIOCSETAW	TARGET_IOW('t', 21, struct termios) drain output, set */
/* #define TARGET_TIOCSETAF	TARGET_IOW('t', 22, struct termios) drn out, fls in, set */
/* #define TARGET_TIOCGETD	TARGET_IOR('t', 26, int)	get line discipline */
/* #define TARGET_TIOCSETD	TARGET_IOW('t', 27, int)	set line discipline */
						/* 127-124 compat */

#define TARGET_TIOCSBRK	0x5427  /* BSD compatibility */
#define TARGET_TIOCCBRK	0x5428  /* BSD compatibility */
#define TARGET_TIOCGSID	0x7416  /* Return the session ID of FD */
#define TARGET_TCGETS2          TARGET_IOR('T', 0x2A, struct termios2)
#define TARGET_TCSETS2          TARGET_IOW('T', 0x2B, struct termios2)
#define TARGET_TCSETSW2         TARGET_IOW('T', 0x2C, struct termios2)
#define TARGET_TCSETSF2         TARGET_IOW('T', 0x2D, struct termios2)
#define TARGET_TIOCGRS485       TARGET_IOR('T', 0x2E, struct serial_rs485)
#define TARGET_TIOCSRS485       TARGET_IOWR('T', 0x2F, struct serial_rs485)
#define TARGET_TIOCGPTN	TARGET_IOR('T',0x30, unsigned int) /* Get Pty Number (of pty-mux device) */
#define TARGET_TIOCSPTLCK	TARGET_IOW('T',0x31, int)  /* Lock/unlock Pty */
#define TARGET_TIOCGDEV         TARGET_IOR('T', 0x32, unsigned int)
#define TARGET_TIOCSIG          TARGET_IOW('T', 0x36, int)
#define TARGET_TIOCVHANGUP      0x5437
#define TARGET_TIOCGPKT         TARGET_IOR('T', 0x38, int)
#define TARGET_TIOCGPTLCK       TARGET_IOR('T', 0x39, int)
#define TARGET_TIOCGEXCL        TARGET_IOR('T', 0x40, int)

/* I hope the range from 0x5480 on is free ... */
#define TARGET_TIOCSCTTY	0x5480		/* become controlling tty */
#define TARGET_TIOCGSOFTCAR	0x5481
#define TARGET_TIOCSSOFTCAR	0x5482
#define TARGET_TIOCLINUX	0x5483
#define TARGET_TIOCGSERIAL	0x5484
#define TARGET_TIOCSSERIAL	0x5485
#define TARGET_TCSBRKP		0x5486	/* Needed for POSIX tcsendbreak() */
#define TARGET_TIOCSERCONFIG	0x5488
#define TARGET_TIOCSERGWILD	0x5489
#define TARGET_TIOCSERSWILD	0x548a
#define TARGET_TIOCGLCKTRMIOS	0x548b
#define TARGET_TIOCSLCKTRMIOS	0x548c
#define TARGET_TIOCSERGSTRUCT	0x548d /* For debugging only */
#define TARGET_TIOCSERGETLSR   0x548e /* Get line status register */
#define TARGET_TIOCSERGETMULTI 0x548f /* Get multiport config  */
#define TARGET_TIOCSERSETMULTI 0x5490 /* Set multiport config */
#define TARGET_TIOCMIWAIT      0x5491 /* wait for a change on serial input line(s) */
#define TARGET_TIOCGICOUNT     0x5492 /* read serial port inline interrupt counts */
#define TARGET_TIOCGHAYESESP	0x5493 /* Get Hayes ESP configuration */
#define TARGET_TIOCSHAYESESP	0x5494 /* Set Hayes ESP configuration */
