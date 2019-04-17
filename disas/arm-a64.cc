/*
 * ARM A64 disassembly output wrapper to libvixl
 * Copyright (c) 2013 Linaro Limited
 * Written by Claudio Fontana
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

extern "C" {
#include "qemu/osdep.h"
#include "disas/dis-asm.h"
}

#include "vixl/a64/disasm-a64.h"

using namespace vixl;

static Decoder *vixl_decoder = NULL;
static Disassembler *vixl_disasm = NULL;

/* We don't use libvixl's PrintDisassembler because its output
 * is a little unhelpful (trailing newlines, for example).
 * Instead we use our own very similar variant so we have
 * control over the format.
 */
class QEMUDisassembler : public Disassembler {
public:
    QEMUDisassembler() : printf_(NULL), stream_(NULL) { }
    ~QEMUDisassembler() { }

    void SetStream(FILE *stream) {
        stream_ = stream;
    }

    void SetPrintf(fprintf_function printf_fn) {
        printf_ = printf_fn;
    }

protected:
    virtual void ProcessOutput(const Instruction *instr) {
        printf_(stream_, "%08" PRIx32 "      %s",
                instr->InstructionBits(), GetOutput());
    }

private:
    fprintf_function printf_;
    FILE *stream_;
};

static int vixl_is_initialized(void)
{
    return vixl_decoder != NULL;
}

static void vixl_init() {
    vixl_decoder = new Decoder();
    vixl_disasm = new QEMUDisassembler();
    vixl_decoder->AppendVisitor(vixl_disasm);
}

#define INSN_SIZE 4

/* Disassemble ARM A64 instruction. This is our only entry
 * point from QEMU's C code.
 */
int print_insn_arm_a64(uint64_t addr, disassemble_info *info)
{
    uint8_t bytes[INSN_SIZE];
    uint32_t instrval;
    const Instruction *instr;
    int status;

    status = info->read_memory_func(addr, bytes, INSN_SIZE, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }

    if (!vixl_is_initialized()) {
        vixl_init();
    }

    ((QEMUDisassembler *)vixl_disasm)->SetPrintf(info->fprintf_func);
    ((QEMUDisassembler *)vixl_disasm)->SetStream(info->stream);

    instrval = bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24;
    instr = reinterpret_cast<const Instruction *>(&instrval);
    vixl_disasm->MapCodeAddress(addr, instr);
    vixl_decoder->Decode(instr);

    return INSN_SIZE;
}
