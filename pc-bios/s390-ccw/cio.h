/*
 * Channel IO definitions
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * Inspired by various s390 headers in Linux 3.9.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef CIO_H
#define CIO_H

/*
 * path management control word
 */
struct pmcw {
    __u32 intparm;        /* interruption parameter */
    __u32 qf      : 1;    /* qdio facility */
    __u32 w       : 1;
    __u32 isc     : 3;    /* interruption sublass */
    __u32 res5    : 3;    /* reserved zeros */
    __u32 ena     : 1;    /* enabled */
    __u32 lm      : 2;    /* limit mode */
    __u32 mme     : 2;    /* measurement-mode enable */
    __u32 mp      : 1;    /* multipath mode */
    __u32 tf      : 1;    /* timing facility */
    __u32 dnv     : 1;    /* device number valid */
    __u32 dev     : 16;   /* device number */
    __u8  lpm;            /* logical path mask */
    __u8  pnom;           /* path not operational mask */
    __u8  lpum;           /* last path used mask */
    __u8  pim;            /* path installed mask */
    __u16 mbi;            /* measurement-block index */
    __u8  pom;            /* path operational mask */
    __u8  pam;            /* path available mask */
    __u8  chpid[8];       /* CHPID 0-7 (if available) */
    __u32 unused1 : 8;    /* reserved zeros */
    __u32 st      : 3;    /* subchannel type */
    __u32 unused2 : 18;   /* reserved zeros */
    __u32 mbfc    : 1;    /* measurement block format control */
    __u32 xmwme   : 1;    /* extended measurement word mode enable */
    __u32 csense  : 1;    /* concurrent sense; can be enabled ...*/
                /*  ... per MSCH, however, if facility */
                /*  ... is not installed, this results */
                /*  ... in an operand exception.       */
} __attribute__ ((packed));

/* Target SCHIB configuration. */
struct schib_config {
    __u64 mba;
    __u32 intparm;
    __u16 mbi;
    __u32 isc:3;
    __u32 ena:1;
    __u32 mme:2;
    __u32 mp:1;
    __u32 csense:1;
    __u32 mbfc:1;
} __attribute__ ((packed));

struct scsw {
    __u16 flags;
    __u16 ctrl;
    __u32 cpa;
    __u8 dstat;
    __u8 cstat;
    __u16 count;
} __attribute__ ((packed));

#define SCSW_FCTL_CLEAR_FUNC 0x1000
#define SCSW_FCTL_HALT_FUNC 0x2000
#define SCSW_FCTL_START_FUNC 0x4000

/*
 * subchannel information block
 */
struct schib {
    struct pmcw pmcw;     /* path management control word */
    struct scsw scsw;     /* subchannel status word */
    __u64 mba;            /* measurement block address */
    __u8 mda[4];          /* model dependent area */
} __attribute__ ((packed,aligned(4)));

struct subchannel_id {
        __u32 cssid  : 8;
        __u32        : 4;
        __u32 m      : 1;
        __u32 ssid   : 2;
        __u32 one    : 1;
        __u32 sch_no : 16;
} __attribute__ ((packed, aligned(4)));

struct chsc_header {
    __u16 length;
    __u16 code;
} __attribute__((packed));

struct chsc_area_sda {
    struct chsc_header request;
    __u8 reserved1:4;
    __u8 format:4;
    __u8 reserved2;
    __u16 operation_code;
    __u32 reserved3;
    __u32 reserved4;
    __u32 operation_data_area[252];
    struct chsc_header response;
    __u32 reserved5:4;
    __u32 format2:4;
    __u32 reserved6:24;
} __attribute__((packed));

/*
 * TPI info structure
 */
struct tpi_info {
    struct subchannel_id schid;
    __u32 intparm;         /* interruption parameter */
    __u32 adapter_IO : 1;
    __u32 reserved2  : 1;
    __u32 isc        : 3;
    __u32 reserved3  : 12;
    __u32 int_type   : 3;
    __u32 reserved4  : 12;
} __attribute__ ((packed));

/* channel command word (type 1) */
struct ccw1 {
    __u8 cmd_code;
    __u8 flags;
    __u16 count;
    __u32 cda;
} __attribute__ ((packed, aligned(8)));

#define CCW_FLAG_DC              0x80
#define CCW_FLAG_CC              0x40
#define CCW_FLAG_SLI             0x20
#define CCW_FLAG_SKIP            0x10
#define CCW_FLAG_PCI             0x08
#define CCW_FLAG_IDA             0x04
#define CCW_FLAG_SUSPEND         0x02

#define CCW_CMD_NOOP             0x03
#define CCW_CMD_BASIC_SENSE      0x04
#define CCW_CMD_TIC              0x08
#define CCW_CMD_SENSE_ID         0xe4

#define CCW_CMD_SET_VQ           0x13
#define CCW_CMD_VDEV_RESET       0x33
#define CCW_CMD_READ_FEAT        0x12
#define CCW_CMD_WRITE_FEAT       0x11
#define CCW_CMD_READ_CONF        0x22
#define CCW_CMD_WRITE_CONF       0x21
#define CCW_CMD_WRITE_STATUS     0x31
#define CCW_CMD_SET_IND          0x43
#define CCW_CMD_SET_CONF_IND     0x53
#define CCW_CMD_READ_VQ_CONF     0x32

/*
 * Command-mode operation request block
 */
struct cmd_orb {
    __u32 intparm;    /* interruption parameter */
    __u32 key:4;      /* flags, like key, suspend control, etc. */
    __u32 spnd:1;     /* suspend control */
    __u32 res1:1;     /* reserved */
    __u32 mod:1;      /* modification control */
    __u32 sync:1;     /* synchronize control */
    __u32 fmt:1;      /* format control */
    __u32 pfch:1;     /* prefetch control */
    __u32 isic:1;     /* initial-status-interruption control */
    __u32 alcc:1;     /* address-limit-checking control */
    __u32 ssic:1;     /* suppress-suspended-interr. control */
    __u32 res2:1;     /* reserved */
    __u32 c64:1;      /* IDAW/QDIO 64 bit control  */
    __u32 i2k:1;      /* IDAW 2/4kB block size control */
    __u32 lpm:8;      /* logical path mask */
    __u32 ils:1;      /* incorrect length */
    __u32 zero:6;     /* reserved zeros */
    __u32 orbx:1;     /* ORB extension control */
    __u32 cpa;    /* channel program address */
}  __attribute__ ((packed, aligned(4)));

struct ciw {
    __u8 type;
    __u8 command;
    __u16 count;
};

/*
 * sense-id response buffer layout
 */
struct senseid {
    /* common part */
    __u8  reserved;   /* always 0x'FF' */
    __u16 cu_type;    /* control unit type */
    __u8  cu_model;   /* control unit model */
    __u16 dev_type;   /* device type */
    __u8  dev_model;  /* device model */
    __u8  unused;     /* padding byte */
    /* extended part */
    struct ciw ciw[62];
}  __attribute__ ((packed, aligned(4)));

/* interruption response block */
struct irb {
    struct scsw scsw;
    __u32 esw[5];
    __u32 ecw[8];
    __u32 emw[8];
}  __attribute__ ((packed, aligned(4)));

/*
 * Some S390 specific IO instructions as inline
 */

static inline int stsch_err(struct subchannel_id schid, struct schib *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode = -EIO;

    asm volatile(
        "    stsch    0(%3)\n"
        "0:  ipm    %0\n"
        "    srl    %0,28\n"
        "1:\n"
        : "+d" (ccode), "=m" (*addr)
        : "d" (reg1), "a" (addr)
        : "cc");
    return ccode;
}

static inline int msch(struct subchannel_id schid, struct schib *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode;

    asm volatile(
        "    msch    0(%2)\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode)
        : "d" (reg1), "a" (addr), "m" (*addr)
        : "cc");
    return ccode;
}

static inline int msch_err(struct subchannel_id schid, struct schib *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode = -EIO;

    asm volatile(
        "    msch    0(%2)\n"
        "0:  ipm    %0\n"
        "    srl    %0,28\n"
        "1:\n"
        : "+d" (ccode)
        : "d" (reg1), "a" (addr), "m" (*addr)
        : "cc");
    return ccode;
}

static inline int tsch(struct subchannel_id schid, struct irb *addr)
{
    register struct subchannel_id reg1 asm ("1") = schid;
    int ccode;

    asm volatile(
        "    tsch    0(%3)\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode), "=m" (*addr)
        : "d" (reg1), "a" (addr)
        : "cc");
    return ccode;
}

static inline int ssch(struct subchannel_id schid, struct cmd_orb *addr)
{
    register struct subchannel_id reg1 asm("1") = schid;
    int ccode = -EIO;

    asm volatile(
        "    ssch    0(%2)\n"
        "0:  ipm    %0\n"
        "    srl    %0,28\n"
        "1:\n"
        : "+d" (ccode)
        : "d" (reg1), "a" (addr), "m" (*addr)
        : "cc", "memory");
    return ccode;
}

static inline int csch(struct subchannel_id schid)
{
    register struct subchannel_id reg1 asm("1") = schid;
    int ccode;

    asm volatile(
        "    csch\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode)
        : "d" (reg1)
        : "cc");
    return ccode;
}

static inline int tpi(struct tpi_info *addr)
{
    int ccode;

    asm volatile(
        "    tpi    0(%2)\n"
        "    ipm    %0\n"
        "    srl    %0,28"
        : "=d" (ccode), "=m" (*addr)
        : "a" (addr)
        : "cc");
    return ccode;
}

static inline int chsc(void *chsc_area)
{
    typedef struct { char _[4096]; } addr_type;
    int cc;

    asm volatile(
        "    .insn    rre,0xb25f0000,%2,0\n"
        "    ipm    %0\n"
        "    srl    %0,28\n"
        : "=d" (cc), "=m" (*(addr_type *) chsc_area)
        : "d" (chsc_area), "m" (*(addr_type *) chsc_area)
        : "cc");
    return cc;
}

#endif /* CIO_H */
