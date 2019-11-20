/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * QEMU Hexagon Disassembler
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"

extern int disassemble_hexagon(uint32_t *words, int nwords,
                               char *buf, int bufsize);

int print_insn_hexagon(bfd_vma memaddr, struct disassemble_info *info)
{
    uint32_t words[4];
    int len, slen;
    char buf[1028];
    int status;
    int i;

    for (i = 0; i < 4; i++) {
        status = (*info->read_memory_func)(memaddr + i*sizeof(uint32_t),
                                           (bfd_byte *)&words[i],
                                           sizeof(uint32_t), info);
        if (status) {
           if (i > 0) {
               break;
           }
           (*info->memory_error_func)(status, memaddr, info);
           return status;
        }
    }

    len = disassemble_hexagon(words, i, buf, 1028);
    slen = strlen(buf);
    if (buf[slen - 1] == '\n') {
        buf[slen - 1] = '\0';
    }
    (*info->fprintf_func)(info->stream, "%s", buf);

    return len;
}

