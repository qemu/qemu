
/* common syscall defines for all architectures */

#define SOCKOP_socket           1
#define SOCKOP_bind             2
#define SOCKOP_connect          3
#define SOCKOP_listen           4
#define SOCKOP_accept           5
#define SOCKOP_getsockname      6
#define SOCKOP_getpeername      7
#define SOCKOP_socketpair       8
#define SOCKOP_send             9
#define SOCKOP_recv             10
#define SOCKOP_sendto           11
#define SOCKOP_recvfrom         12
#define SOCKOP_shutdown         13
#define SOCKOP_setsockopt       14
#define SOCKOP_getsockopt       15
#define SOCKOP_sendmsg          16
#define SOCKOP_recvmsg          17

struct target_timeval {
    target_long tv_sec;
    target_long tv_usec;
};

struct target_timespec {
    target_long tv_sec;
    target_long tv_nsec;
};

struct target_iovec {
    target_long iov_base;   /* Starting address */
    target_long iov_len;   /* Number of bytes */
};

struct target_msghdr {
    target_long	 msg_name;	/* Socket name			*/
    int		 msg_namelen;	/* Length of name		*/
    target_long	 msg_iov;	/* Data blocks			*/
    target_long	 msg_iovlen;	/* Number of blocks		*/
    target_long  msg_control;	/* Per protocol magic (eg BSD file descriptor passing) */
    target_long	 msg_controllen;	/* Length of cmsg list */
    unsigned int msg_flags;
};

struct  target_rusage {
        struct target_timeval ru_utime;        /* user time used */
        struct target_timeval ru_stime;        /* system time used */
        target_long    ru_maxrss;              /* maximum resident set size */
        target_long    ru_ixrss;               /* integral shared memory size */
        target_long    ru_idrss;               /* integral unshared data size */
        target_long    ru_isrss;               /* integral unshared stack size */
        target_long    ru_minflt;              /* page reclaims */
        target_long    ru_majflt;              /* page faults */
        target_long    ru_nswap;               /* swaps */
        target_long    ru_inblock;             /* block input operations */
        target_long    ru_oublock;             /* block output operations */
        target_long    ru_msgsnd;              /* messages sent */
        target_long    ru_msgrcv;              /* messages received */
        target_long    ru_nsignals;            /* signals received */
        target_long    ru_nvcsw;               /* voluntary context switches */
        target_long    ru_nivcsw;              /* involuntary " */
};

typedef struct {
        int     val[2];
} kernel_fsid_t;

struct kernel_statfs {
	int f_type;
	int f_bsize;
	int f_blocks;
	int f_bfree;
	int f_bavail;
	int f_files;
	int f_ffree;
        kernel_fsid_t f_fsid;
	int f_namelen;
	int f_spare[6];
};

struct target_dirent {
	target_long	d_ino;
	target_long	d_off;
	unsigned short	d_reclen;
	char		d_name[256]; /* We must not include limits.h! */
};

struct target_dirent64 {
	uint64_t	d_ino;
	int64_t		d_off;
	unsigned short	d_reclen;
	unsigned char	d_type;
	char		d_name[256];
};


/* mostly generic signal stuff */
#define TARGET_SIG_DFL	((target_long)0)	/* default signal handling */
#define TARGET_SIG_IGN	((target_long)1)	/* ignore signal */
#define TARGET_SIG_ERR	((target_long)-1)	/* error return from signal */

#ifdef TARGET_MIPS
#define TARGET_NSIG	   128
#else
#define TARGET_NSIG	   64
#endif
#define TARGET_NSIG_BPW	   TARGET_LONG_BITS
#define TARGET_NSIG_WORDS  (TARGET_NSIG / TARGET_NSIG_BPW)

typedef struct {
    target_ulong sig[TARGET_NSIG_WORDS];
} target_sigset_t;

/* Networking ioctls */
#define TARGET_SIOCADDRT       0x890B          /* add routing table entry */
#define TARGET_SIOCDELRT       0x890C          /* delete routing table entry */
#define TARGET_SIOCGIFNAME     0x8910          /* get iface name               */
#define TARGET_SIOCSIFLINK     0x8911          /* set iface channel            */
#define TARGET_SIOCGIFCONF     0x8912          /* get iface list               */
#define TARGET_SIOCGIFFLAGS    0x8913          /* get flags                    */
#define TARGET_SIOCSIFFLAGS    0x8914          /* set flags                    */
#define TARGET_SIOCGIFADDR     0x8915          /* get PA address               */
#define TARGET_SIOCSIFADDR     0x8916          /* set PA address               */
#define TARGET_SIOCGIFDSTADDR  0x8917          /* get remote PA address        */
#define TARGET_SIOCSIFDSTADDR  0x8918          /* set remote PA address        */
#define TARGET_SIOCGIFBRDADDR  0x8919          /* get broadcast PA address     */
#define TARGET_SIOCSIFBRDADDR  0x891a          /* set broadcast PA address     */
#define TARGET_SIOCGIFNETMASK  0x891b          /* get network PA mask          */
#define TARGET_SIOCSIFNETMASK  0x891c          /* set network PA mask          */
#define TARGET_SIOCGIFMETRIC   0x891d          /* get metric                   */
#define TARGET_SIOCSIFMETRIC   0x891e          /* set metric                   */
#define TARGET_SIOCGIFMEM      0x891f          /* get memory address (BSD)     */
#define TARGET_SIOCSIFMEM      0x8920          /* set memory address (BSD)     */
#define TARGET_SIOCGIFMTU      0x8921          /* get MTU size                 */
#define TARGET_SIOCSIFMTU      0x8922          /* set MTU size                 */
#define TARGET_SIOCSIFHWADDR   0x8924          /* set hardware address (NI)    */
#define TARGET_SIOCGIFENCAP    0x8925          /* get/set slip encapsulation   */
#define TARGET_SIOCSIFENCAP    0x8926
#define TARGET_SIOCGIFHWADDR   0x8927          /* Get hardware address         */
#define TARGET_SIOCGIFSLAVE    0x8929          /* Driver slaving support       */
#define TARGET_SIOCSIFSLAVE    0x8930
#define TARGET_SIOCADDMULTI    0x8931          /* Multicast address lists      */
#define TARGET_SIOCDELMULTI    0x8932

/* Bridging control calls */
#define TARGET_SIOCGIFBR       0x8940          /* Bridging support             */
#define TARGET_SIOCSIFBR       0x8941          /* Set bridging options         */

#define TARGET_SIOCGIFTXQLEN   0x8942          /* Get the tx queue length      */
#define TARGET_SIOCSIFTXQLEN   0x8943          /* Set the tx queue length      */

/* ARP cache control calls. */
#define TARGET_OLD_SIOCDARP    0x8950          /* old delete ARP table entry   */
#define TARGET_OLD_SIOCGARP    0x8951          /* old get ARP table entry      */
#define TARGET_OLD_SIOCSARP    0x8952          /* old set ARP table entry      */
#define TARGET_SIOCDARP        0x8953          /* delete ARP table entry       */
#define TARGET_SIOCGARP        0x8954          /* get ARP table entry          */
#define TARGET_SIOCSARP        0x8955          /* set ARP table entry          */

/* RARP cache control calls. */
#define TARGET_SIOCDRARP       0x8960          /* delete RARP table entry      */
#define TARGET_SIOCGRARP       0x8961          /* get RARP table entry         */
#define TARGET_SIOCSRARP       0x8962          /* set RARP table entry         */

/* Driver configuration calls */
#define TARGET_SIOCGIFMAP      0x8970          /* Get device parameters        */
#define TARGET_SIOCSIFMAP      0x8971          /* Set device parameters        */

/* DLCI configuration calls */
#define TARGET_SIOCADDDLCI     0x8980          /* Create new DLCI device       */
#define TARGET_SIOCDELDLCI     0x8981          /* Delete DLCI device           */


/* From <linux/fs.h> */

#define TARGET_BLKROSET   TARGET_IO(0x12,93) /* set device read-only (0 = read-write) */
#define TARGET_BLKROGET   TARGET_IO(0x12,94) /* get read-only status (0 = read_write) */
#define TARGET_BLKRRPART  TARGET_IO(0x12,95) /* re-read partition table */
#define TARGET_BLKGETSIZE TARGET_IO(0x12,96) /* return device size /512 (long *arg) */
#define TARGET_BLKFLSBUF  TARGET_IO(0x12,97) /* flush buffer cache */
#define TARGET_BLKRASET   TARGET_IO(0x12,98) /* Set read ahead for block device */
#define TARGET_BLKRAGET   TARGET_IO(0x12,99) /* get current read ahead setting */
#define TARGET_BLKFRASET  TARGET_IO(0x12,100)/* set filesystem (mm/filemap.c) read-ahead */
#define TARGET_BLKFRAGET  TARGET_IO(0x12,101)/* get filesystem (mm/filemap.c) read-ahead */
#define TARGET_BLKSECTSET TARGET_IO(0x12,102)/* set max sectors per request (ll_rw_blk.c) */
#define TARGET_BLKSECTGET TARGET_IO(0x12,103)/* get max sectors per request (ll_rw_blk.c) */
#define TARGET_BLKSSZGET  TARGET_IO(0x12,104)/* get block device sector size */
/* A jump here: 108-111 have been used for various private purposes. */
#define TARGET_BLKBSZGET  TARGET_IOR(0x12,112,sizeof(int))
#define TARGET_BLKBSZSET  TARGET_IOW(0x12,113,sizeof(int))
#define TARGET_BLKGETSIZE64 TARGET_IOR(0x12,114,sizeof(uint64_t)) /* return device size in bytes (u64 *arg) */
#define TARGET_FIBMAP     TARGET_IO(0x00,1)  /* bmap access */
#define TARGET_FIGETBSZ   TARGET_IO(0x00,2)  /* get the block size used for bmap */

/* cdrom commands */
#define TARGET_CDROMPAUSE		0x5301 /* Pause Audio Operation */ 
#define TARGET_CDROMRESUME		0x5302 /* Resume paused Audio Operation */
#define TARGET_CDROMPLAYMSF		0x5303 /* Play Audio MSF (struct cdrom_msf) */
#define TARGET_CDROMPLAYTRKIND		0x5304 /* Play Audio Track/index 
                                           (struct cdrom_ti) */
#define TARGET_CDROMREADTOCHDR		0x5305 /* Read TOC header 
                                           (struct cdrom_tochdr) */
#define TARGET_CDROMREADTOCENTRY	0x5306 /* Read TOC entry 
                                           (struct cdrom_tocentry) */
#define TARGET_CDROMSTOP		0x5307 /* Stop the cdrom drive */
#define TARGET_CDROMSTART		0x5308 /* Start the cdrom drive */
#define TARGET_CDROMEJECT		0x5309 /* Ejects the cdrom media */
#define TARGET_CDROMVOLCTRL		0x530a /* Control output volume 
                                           (struct cdrom_volctrl) */
#define TARGET_CDROMSUBCHNL		0x530b /* Read subchannel data 
                                           (struct cdrom_subchnl) */
#define TARGET_CDROMREADMODE2		0x530c /* Read TARGET_CDROM mode 2 data (2336 Bytes) 
                                           (struct cdrom_read) */
#define TARGET_CDROMREADMODE1		0x530d /* Read TARGET_CDROM mode 1 data (2048 Bytes)
                                           (struct cdrom_read) */
#define TARGET_CDROMREADAUDIO		0x530e /* (struct cdrom_read_audio) */
#define TARGET_CDROMEJECT_SW		0x530f /* enable(1)/disable(0) auto-ejecting */
#define TARGET_CDROMMULTISESSION	0x5310 /* Obtain the start-of-last-session 
                                           address of multi session disks 
                                           (struct cdrom_multisession) */
#define TARGET_CDROM_GET_MCN		0x5311 /* Obtain the "Universal Product Code" 
                                           if available (struct cdrom_mcn) */
#define TARGET_CDROM_GET_UPC		TARGET_CDROM_GET_MCN  /* This one is depricated, 
                                          but here anyway for compatability */
#define TARGET_CDROMRESET		0x5312 /* hard-reset the drive */
#define TARGET_CDROMVOLREAD		0x5313 /* Get the drive's volume setting 
                                          (struct cdrom_volctrl) */
#define TARGET_CDROMREADRAW		0x5314	/* read data in raw mode (2352 Bytes)
                                           (struct cdrom_read) */
/* 
 * These ioctls are used only used in aztcd.c and optcd.c
 */
#define TARGET_CDROMREADCOOKED		0x5315	/* read data in cooked mode */
#define TARGET_CDROMSEEK		0x5316  /* seek msf address */
  
/*
 * This ioctl is only used by the scsi-cd driver.  
   It is for playing audio in logical block addressing mode.
 */
#define TARGET_CDROMPLAYBLK		0x5317	/* (struct cdrom_blk) */

/* 
 * These ioctls are only used in optcd.c
 */
#define TARGET_CDROMREADALL		0x5318	/* read all 2646 bytes */

/* 
 * These ioctls are (now) only in ide-cd.c for controlling 
 * drive spindown time.  They should be implemented in the
 * Uniform driver, via generic packet commands, GPCMD_MODE_SELECT_10,
 * GPCMD_MODE_SENSE_10 and the GPMODE_POWER_PAGE...
 *  -Erik
 */
#define TARGET_CDROMGETSPINDOWN        0x531d
#define TARGET_CDROMSETSPINDOWN        0x531e

/* 
 * These ioctls are implemented through the uniform CD-ROM driver
 * They _will_ be adopted by all CD-ROM drivers, when all the CD-ROM
 * drivers are eventually ported to the uniform CD-ROM driver interface.
 */
#define TARGET_CDROMCLOSETRAY		0x5319	/* pendant of CDROMEJECT */
#define TARGET_CDROM_SET_OPTIONS	0x5320  /* Set behavior options */
#define TARGET_CDROM_CLEAR_OPTIONS	0x5321  /* Clear behavior options */
#define TARGET_CDROM_SELECT_SPEED	0x5322  /* Set the CD-ROM speed */
#define TARGET_CDROM_SELECT_DISC	0x5323  /* Select disc (for juke-boxes) */
#define TARGET_CDROM_MEDIA_CHANGED	0x5325  /* Check is media changed  */
#define TARGET_CDROM_DRIVE_STATUS	0x5326  /* Get tray position, etc. */
#define TARGET_CDROM_DISC_STATUS	0x5327  /* Get disc type, etc. */
#define TARGET_CDROM_CHANGER_NSLOTS    0x5328  /* Get number of slots */
#define TARGET_CDROM_LOCKDOOR		0x5329  /* lock or unlock door */
#define TARGET_CDROM_DEBUG		0x5330	/* Turn debug messages on/off */
#define TARGET_CDROM_GET_CAPABILITY	0x5331	/* get capabilities */

/* Note that scsi/scsi_ioctl.h also uses 0x5382 - 0x5386.
 * Future CDROM ioctls should be kept below 0x537F
 */

/* This ioctl is only used by sbpcd at the moment */
#define TARGET_CDROMAUDIOBUFSIZ        0x5382	/* set the audio buffer size */
					/* conflict with SCSI_IOCTL_GET_IDLUN */

/* DVD-ROM Specific ioctls */
#define TARGET_DVD_READ_STRUCT		0x5390  /* Read structure */
#define TARGET_DVD_WRITE_STRUCT	0x5391  /* Write structure */
#define TARGET_DVD_AUTH		0x5392  /* Authentication */

#define TARGET_CDROM_SEND_PACKET	0x5393	/* send a packet to the drive */
#define TARGET_CDROM_NEXT_WRITABLE	0x5394	/* get next writable block */
#define TARGET_CDROM_LAST_WRITTEN	0x5395	/* get last block written on disc */

/* HD commands */

/* hd/ide ctl's that pass (arg) ptrs to user space are numbered 0x030n/0x031n */
#define TARGET_HDIO_GETGEO            0x0301  /* get device geometry */
#define TARGET_HDIO_GET_UNMASKINTR    0x0302  /* get current unmask setting */
#define TARGET_HDIO_GET_MULTCOUNT     0x0304  /* get current IDE blockmode setting */
#define TARGET_HDIO_GET_IDENTITY      0x0307  /* get IDE identification info */
#define TARGET_HDIO_GET_KEEPSETTINGS  0x0308  /* get keep-settings-on-reset flag */
#define TARGET_HDIO_GET_32BIT         0x0309  /* get current io_32bit setting */
#define TARGET_HDIO_GET_NOWERR        0x030a  /* get ignore-write-error flag */
#define TARGET_HDIO_GET_DMA           0x030b  /* get use-dma flag */
#define TARGET_HDIO_DRIVE_CMD         0x031f  /* execute a special drive command */

/* hd/ide ctl's that pass (arg) non-ptr values are numbered 0x032n/0x033n */
#define TARGET_HDIO_SET_MULTCOUNT     0x0321  /* change IDE blockmode */
#define TARGET_HDIO_SET_UNMASKINTR    0x0322  /* permit other irqs during I/O */
#define TARGET_HDIO_SET_KEEPSETTINGS  0x0323  /* keep ioctl settings on reset */
#define TARGET_HDIO_SET_32BIT         0x0324  /* change io_32bit flags */
#define TARGET_HDIO_SET_NOWERR        0x0325  /* change ignore-write-error flag */
#define TARGET_HDIO_SET_DMA           0x0326  /* change use-dma flag */
#define TARGET_HDIO_SET_PIO_MODE      0x0327  /* reconfig interface to new speed */
