#ifndef TILEGX_TERMBITS_H
#define TILEGX_TERMBITS_H

/* From asm-generic/termbits.h, which is used by tilegx */

#define TARGET_NCCS 19
struct target_termios {
    unsigned int c_iflag;             /* input mode flags */
    unsigned int c_oflag;             /* output mode flags */
    unsigned int c_cflag;             /* control mode flags */
    unsigned int c_lflag;             /* local mode flags */
    unsigned char c_line;             /* line discipline */
    unsigned char c_cc[TARGET_NCCS];  /* control characters */
};

struct target_termios2 {
    unsigned int c_iflag;             /* input mode flags */
    unsigned int c_oflag;             /* output mode flags */
    unsigned int c_cflag;             /* control mode flags */
    unsigned int c_lflag;             /* local mode flags */
    unsigned char c_line;             /* line discipline */
    unsigned char c_cc[TARGET_NCCS];  /* control characters */
    unsigned int c_ispeed;            /* input speed */
    unsigned int c_ospeed;            /* output speed */
};

/* c_cc characters */
#define TARGET_VINTR     0
#define TARGET_VQUIT     1
#define TARGET_VERASE    2
#define TARGET_VKILL     3
#define TARGET_VEOF      4
#define TARGET_VTIME     5
#define TARGET_VMIN      6
#define TARGET_VSWTC     7
#define TARGET_VSTART    8
#define TARGET_VSTOP     9
#define TARGET_VSUSP     10
#define TARGET_VEOL      11
#define TARGET_VREPRINT  12
#define TARGET_VDISCARD  13
#define TARGET_VWERASE   14
#define TARGET_VLNEXT    15
#define TARGET_VEOL2     16

/* c_iflag bits */
#define TARGET_IGNBRK    0000001
#define TARGET_BRKINT    0000002
#define TARGET_IGNPAR    0000004
#define TARGET_PARMRK    0000010
#define TARGET_INPCK     0000020
#define TARGET_ISTRIP    0000040
#define TARGET_INLCR     0000100
#define TARGET_IGNCR     0000200
#define TARGET_ICRNL     0000400
#define TARGET_IUCLC     0001000
#define TARGET_IXON      0002000
#define TARGET_IXANY     0004000
#define TARGET_IXOFF     0010000
#define TARGET_IMAXBEL   0020000
#define TARGET_IUTF8     0040000

/* c_oflag bits */
#define TARGET_OPOST     0000001
#define TARGET_OLCUC     0000002
#define TARGET_ONLCR     0000004
#define TARGET_OCRNL     0000010
#define TARGET_ONOCR     0000020
#define TARGET_ONLRET    0000040
#define TARGET_OFILL     0000100
#define TARGET_OFDEL     0000200
#define TARGET_NLDLY     0000400
#define   TARGET_NL0     0000000
#define   TARGET_NL1     0000400
#define TARGET_CRDLY     0003000
#define   TARGET_CR0     0000000
#define   TARGET_CR1     0001000
#define   TARGET_CR2     0002000
#define   TARGET_CR3     0003000
#define TARGET_TABDLY    0014000
#define   TARGET_TAB0    0000000
#define   TARGET_TAB1    0004000
#define   TARGET_TAB2    0010000
#define   TARGET_TAB3    0014000
#define   TARGET_XTABS   0014000
#define TARGET_BSDLY     0020000
#define   TARGET_BS0     0000000
#define   TARGET_BS1     0020000
#define TARGET_VTDLY     0040000
#define   TARGET_VT0     0000000
#define   TARGET_VT1     0040000
#define TARGET_FFDLY     0100000
#define   TARGET_FF0     0000000
#define   TARGET_FF1     0100000

/* c_cflag bit meaning */
#define TARGET_CBAUD     0010017
#define  TARGET_B0       0000000        /* hang up */
#define  TARGET_B50      0000001
#define  TARGET_B75      0000002
#define  TARGET_B110     0000003
#define  TARGET_B134     0000004
#define  TARGET_B150     0000005
#define  TARGET_B200     0000006
#define  TARGET_B300     0000007
#define  TARGET_B600     0000010
#define  TARGET_B1200    0000011
#define  TARGET_B1800    0000012
#define  TARGET_B2400    0000013
#define  TARGET_B4800    0000014
#define  TARGET_B9600    0000015
#define  TARGET_B19200   0000016
#define  TARGET_B38400   0000017
#define TARGET_EXTA      TARGET_B19200
#define TARGET_EXTB      TARGET_B38400
#define TARGET_CSIZE     0000060
#define   TARGET_CS5     0000000
#define   TARGET_CS6     0000020
#define   TARGET_CS7     0000040
#define   TARGET_CS8     0000060
#define TARGET_CSTOPB    0000100
#define TARGET_CREAD     0000200
#define TARGET_PARENB    0000400
#define TARGET_PARODD    0001000
#define TARGET_HUPCL     0002000
#define TARGET_CLOCAL    0004000
#define TARGET_CBAUDEX   0010000
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
#define TARGET_CIBAUD    002003600000    /* input baud rate */
#define TARGET_CMSPAR    010000000000    /* mark or space (stick) parity */
#define TARGET_CRTSCTS   020000000000    /* flow control */

#define TARGET_IBSHIFT   16        /* Shift from CBAUD to CIBAUD */

/* c_lflag bits */
#define TARGET_ISIG      0000001
#define TARGET_ICANON    0000002
#define TARGET_XCASE     0000004
#define TARGET_ECHO      0000010
#define TARGET_ECHOE     0000020
#define TARGET_ECHOK     0000040
#define TARGET_ECHONL    0000100
#define TARGET_NOFLSH    0000200
#define TARGET_TOSTOP    0000400
#define TARGET_ECHOCTL   0001000
#define TARGET_ECHOPRT   0002000
#define TARGET_ECHOKE    0004000
#define TARGET_FLUSHO    0010000
#define TARGET_PENDIN    0040000
#define TARGET_IEXTEN    0100000
#define TARGET_EXTPROC   0200000

/* tcflow() and TCXONC use these */
#define TARGET_TCOOFF    0
#define TARGET_TCOON     1
#define TARGET_TCIOFF    2
#define TARGET_TCION     3

/* tcflush() and TCFLSH use these */
#define TARGET_TCIFLUSH  0
#define TARGET_TCOFLUSH  1
#define TARGET_TCIOFLUSH 2

/* tcsetattr uses these */
#define TARGET_TCSANOW   0
#define TARGET_TCSADRAIN 1
#define TARGET_TCSAFLUSH 2

/* From asm-generic/ioctls.h, which is used by tilegx */

#define TARGET_TCGETS                   0x5401
#define TARGET_TCSETS                   0x5402
#define TARGET_TCSETSW                  0x5403
#define TARGET_TCSETSF                  0x5404
#define TARGET_TCGETA                   0x5405
#define TARGET_TCSETA                   0x5406
#define TARGET_TCSETAW                  0x5407
#define TARGET_TCSETAF                  0x5408
#define TARGET_TCSBRK                   0x5409
#define TARGET_TCXONC                   0x540A
#define TARGET_TCFLSH                   0x540B
#define TARGET_TIOCEXCL                 0x540C
#define TARGET_TIOCNXCL                 0x540D
#define TARGET_TIOCSCTTY                0x540E
#define TARGET_TIOCGPGRP                0x540F
#define TARGET_TIOCSPGRP                0x5410
#define TARGET_TIOCOUTQ                 0x5411
#define TARGET_TIOCSTI                  0x5412
#define TARGET_TIOCGWINSZ               0x5413
#define TARGET_TIOCSWINSZ               0x5414
#define TARGET_TIOCMGET                 0x5415
#define TARGET_TIOCMBIS                 0x5416
#define TARGET_TIOCMBIC                 0x5417
#define TARGET_TIOCMSET                 0x5418
#define TARGET_TIOCGSOFTCAR             0x5419
#define TARGET_TIOCSSOFTCAR             0x541A
#define TARGET_FIONREAD                 0x541B
#define TARGET_TIOCINQ                  TARGET_FIONREAD
#define TARGET_TIOCLINUX                0x541C
#define TARGET_TIOCCONS                 0x541D
#define TARGET_TIOCGSERIAL              0x541E
#define TARGET_TIOCSSERIAL              0x541F
#define TARGET_TIOCPKT                  0x5420
#define TARGET_FIONBIO                  0x5421
#define TARGET_TIOCNOTTY                0x5422
#define TARGET_TIOCSETD                 0x5423
#define TARGET_TIOCGETD                 0x5424
#define TARGET_TCSBRKP                  0x5425
#define TARGET_TIOCSBRK                 0x5427
#define TARGET_TIOCCBRK                 0x5428
#define TARGET_TIOCGSID                 0x5429
#define TARGET_TCGETS2                  TARGET_IOR('T', 0x2A, struct termios2)
#define TARGET_TCSETS2                  TARGET_IOW('T', 0x2B, struct termios2)
#define TARGET_TCSETSW2                 TARGET_IOW('T', 0x2C, struct termios2)
#define TARGET_TCSETSF2                 TARGET_IOW('T', 0x2D, struct termios2)
#define TARGET_TIOCGRS485               0x542E
#define TARGET_TIOCSRS485               0x542F
#define TARGET_TIOCGPTN                 TARGET_IOR('T', 0x30, unsigned int)
#define TARGET_TIOCSPTLCK               TARGET_IOW('T', 0x31, int)
#define TARGET_TIOCGDEV                 TARGET_IOR('T', 0x32, unsigned int)
#define TARGET_TCGETX                   0x5432
#define TARGET_TCSETX                   0x5433
#define TARGET_TCSETXF                  0x5434
#define TARGET_TCSETXW                  0x5435
#define TARGET_TIOCSIG                  TARGET_IOW('T', 0x36, int)
#define TARGET_TIOCVHANGUP              0x5437
#define TARGET_TIOCGPKT                 TARGET_IOR('T', 0x38, int)
#define TARGET_TIOCGPTLCK               TARGET_IOR('T', 0x39, int)
#define TARGET_TIOCGEXCL                TARGET_IOR('T', 0x40, int)

#define TARGET_FIONCLEX                 0x5450
#define TARGET_FIOCLEX                  0x5451
#define TARGET_FIOASYNC                 0x5452
#define TARGET_TIOCSERCONFIG            0x5453
#define TARGET_TIOCSERGWILD             0x5454
#define TARGET_TIOCSERSWILD             0x5455
#define TARGET_TIOCGLCKTRMIOS           0x5456
#define TARGET_TIOCSLCKTRMIOS           0x5457
#define TARGET_TIOCSERGSTRUCT           0x5458
#define TARGET_TIOCSERGETLSR            0x5459
#define TARGET_TIOCSERGETMULTI          0x545A
#define TARGET_TIOCSERSETMULTI          0x545B

#define TARGET_TIOCMIWAIT               0x545C
#define TARGET_TIOCGICOUNT              0x545D
#define TARGET_FIOQSIZE                 0x5460

#define TARGET_TIOCPKT_DATA             0
#define TARGET_TIOCPKT_FLUSHREAD        1
#define TARGET_TIOCPKT_FLUSHWRITE       2
#define TARGET_TIOCPKT_STOP             4
#define TARGET_TIOCPKT_START            8
#define TARGET_TIOCPKT_NOSTOP           16
#define TARGET_TIOCPKT_DOSTOP           32
#define TARGET_TIOCPKT_IOCTL            64

#define TARGET_TIOCSER_TEMT             0x01

#endif
