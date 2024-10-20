/*
 * QEMU S390 IPL Block
 *
 * Copyright 2015 IBM Corp.
 * Author(s): Alexander Yarygin <yarygin@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef IPLB_H
#define IPLB_H

#ifndef QEMU_PACKED
#define QEMU_PACKED __attribute__((packed))
#endif

#include <qipl.h>

extern QemuIplParameters qipl;
extern IplParameterBlock iplb __attribute__((__aligned__(PAGE_SIZE)));

#define S390_IPL_TYPE_FCP 0x00
#define S390_IPL_TYPE_CCW 0x02
#define S390_IPL_TYPE_QEMU_SCSI 0xff

static inline bool manage_iplb(IplParameterBlock *iplb, bool store)
{
    register unsigned long addr asm("0") = (unsigned long) iplb;
    register unsigned long rc asm("1") = 0;
    unsigned long subcode = store ? 6 : 5;

    asm volatile ("diag %0,%2,0x308\n"
                  : "+d" (addr), "+d" (rc)
                  : "d" (subcode)
                  : "memory", "cc");
    return rc == 0x01;
}


static inline bool store_iplb(IplParameterBlock *iplb)
{
    return manage_iplb(iplb, true);
}

static inline bool set_iplb(IplParameterBlock *iplb)
{
    return manage_iplb(iplb, false);
}

#endif /* IPLB_H */
