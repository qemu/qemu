/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef __EXT_MMVEC_DECODE__
#define __EXT_MMVEC_DECODE__

#ifdef FIXME
int mmvec_ext_decode_checks(thread_t * thread, packet_t *pkt, exception_info *einfo);
#else
int mmvec_ext_decode_checks(packet_t *pkt);
#endif
const char * mmvec_ext_decode_find_iclass_slots(int opcode);


#endif
