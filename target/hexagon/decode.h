/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef DECODE_H
#define DECODE_H

#include "cpu.h"
#include "opcodes.h"
#include "global_types.h"

extern packet_t *decode_this(size4u_t *words, packet_t *decode_pkt);
extern void decode_send_insn_to(packet_t * packet, int start, int newloc);


#endif
