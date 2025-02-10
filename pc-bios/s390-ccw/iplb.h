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
#include <string.h>

extern QemuIplParameters qipl;
extern IplParameterBlock iplb __attribute__((__aligned__(PAGE_SIZE)));
extern bool have_iplb;

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

/*
 * The IPL started on the device, but failed in some way.  If the IPLB chain
 * still has more devices left to try, use the next device in order.
 */
static inline bool load_next_iplb(void)
{
    IplParameterBlock *next_iplb;

    if (qipl.chain_len < 1) {
        return false;
    }

    qipl.index++;
    next_iplb = (IplParameterBlock *) qipl.next_iplb;
    memcpy(&iplb, next_iplb, sizeof(IplParameterBlock));

    qipl.chain_len--;
    qipl.next_iplb = qipl.next_iplb + sizeof(IplParameterBlock);

    return true;
}

#endif /* IPLB_H */
