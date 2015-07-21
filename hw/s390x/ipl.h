/*
 * s390 IPL device
 *
 * Copyright 2015 IBM Corp.
 * Author(s): Zhang Fan <bjfanzh@cn.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_IPL_H
#define HW_S390_IPL_H

#include "cpu.h"

typedef struct IplParameterBlock {
      uint8_t  reserved1[110];
      uint16_t devno;
      uint8_t  reserved2[88];
} IplParameterBlock;

void s390_ipl_update_diag308(IplParameterBlock *iplb);
void s390_ipl_prepare_cpu(S390CPU *cpu);
IplParameterBlock *s390_ipl_get_iplb(void);
void s390_reipl_request(void);

#endif
