/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef ISA_CONSTANTS_H
#define ISA_CONSTANTS_H

/********************************************************************/
/*                These are ISA defined constants                   */
/********************************************************************/

/* Event types */
#define EXCEPT_TYPE_RESET		0x00
#define EXCEPT_TYPE_IMPRECISE	0x01
#define EXCEPT_TYPE_PRECISE		0x02
#define EXCEPT_TYPE_TLB_MISS_X	0x04
#define EXCEPT_TYPE_TLB_MISS_RW	0x06
#define EXCEPT_TYPE_TRAP0		0x08
#define EXCEPT_TYPE_TRAP1		0x09
#define EXCEPT_TYPE_FPTRAP		0x0b
#define EXCEPT_TYPE_DEBUG		0x0c

/* Precise exception cause codes */
#define PRECISE_CAUSE_BIU_PRECISE        0x01
#define PRECISE_CAUSE_DOUBLE_EXCEPT      0x03
#define PRECISE_CAUSE_NATIVE_PACKET      0x04
#define PRECISE_CAUSE_FETCH_NO_XPAGE     0x11
#define PRECISE_CAUSE_FETCH_NO_UPAGE     0x12
#define PRECISE_CAUSE_INVALID_PACKET     0x15
#define PRECISE_CAUSE_INVALID_OPCODE     0x15
#define PRECISE_CAUSE_NO_COPROC_ENABLE   0x16
#define PRECISE_CAUSE_HW_BADFETCH        0x17
#define PRECISE_CAUSE_NO_COPROC2_ENABLE  0x18
#define PRECISE_CAUSE_PRIV_USER_NO_GINSN 0x1A
#define PRECISE_CAUSE_PRIV_USER_NO_SINSN 0x1B
#define PRECISE_CAUSE_REG_WRITE_CONFLICT 0x1D
#define PRECISE_CAUSE_PC_NOT_ALIGNED     0x1E
#define PRECISE_CAUSE_MISALIGNED_LOAD    0x20
#define PRECISE_CAUSE_MISALIGNED_STORE   0x21
#define PRECISE_CAUSE_PRIV_NO_READ       0x22
#define PRECISE_CAUSE_PRIV_NO_WRITE      0x23
#define PRECISE_CAUSE_PRIV_NO_UREAD      0x24
#define PRECISE_CAUSE_PRIV_NO_UWRITE     0x25
#define PRECISE_CAUSE_COPROC_LDST        0x26
#define PRECISE_CAUSE_STACK_LIMIT        0x27
#define PRECISE_CAUSE_NO_ACCESS          0x28

#define IMPRECISE_CAUSE_NMI              0x43
#define IMPRECISE_CAUSE_DATA_ABORT       0x42
#define IMPRECISE_CAUSE_MULTI_TLB_MATCH  0x44
#define IMPRECISE_CAUSE_LIVELOCK         0x45

#define TLBMISSX_CAUSE_NORMAL            0x60
#define TLBMISSX_CAUSE_NEXTPAGE          0x61
#define TLBMISSX_CAUSE_ICINVA            0x62

#define TLBMISSRW_CAUSE_READ             0x70
#define TLBMISSRW_CAUSE_WRITE            0x71

#define DEBUG_CAUSE_SINGLE_STEP          0x80

#define FPTRAP_CAUSE_BADFLOAT            0xBF

#define INTERRUPT_CAUSE_INTERRUPT0       0xC0


#ifdef FIXME
#define ICLASS_EXTENDER   "0000"
#define ICLASS_CJ         "0001"
#define ICLASS_NCJ        "0010"
#define ICLASS_V4LDST     "0011"
#define ICLASS_V2LDST     "0100"
#define ICLASS_J          "0101"
#define ICLASS_CR         "0110"
#define ICLASS_ALU2op     "0111"
#define ICLASS_S2op       "1000"
#define ICLASS_LD         "1001"
#define ICLASS_ST         "1010"
#define ICLASS_ADDI       "1011"
#define ICLASS_S3op       "1100"
#define ICLASS_ALU64      "1101"
#define ICLASS_M          "1110"
#define ICLASS_ALU3op     "1111"
#else
#define ICLASS_EXTENDER   0
#define ICLASS_CJ         1
#define ICLASS_NCJ        2
#define ICLASS_V4LDST     3
#define ICLASS_V2LDST     4
#define ICLASS_J          5
#define ICLASS_CR         6
#define ICLASS_ALU2op     7
#define ICLASS_S2op       8
#define ICLASS_LD         9
#define ICLASS_ST         10
#define ICLASS_ADDI       11
#define ICLASS_S3op       12
#define ICLASS_ALU64      13
#define ICLASS_M          14
#define ICLASS_ALU3op     15
#endif

#endif
