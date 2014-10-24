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

#include "a64/disasm-a64.h"

extern "C" {
#include "disas/bfd.h"
}

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
    explicit QEMUDisassembler(FILE *stream) : stream_(stream) { }
    ~QEMUDisassembler() { }

protected:
    virtual void ProcessOutput(const Instruction *instr) {
        fprintf(stream_, "%08" PRIx32 "      %s",
                instr->InstructionBits(), GetOutput());
    }

private:
    FILE *stream_;
};

static int vixl_is_initialized(void)
{
    return vixl_decoder != NULL;
}

static void vixl_init(FILE *f) {
    vixl_decoder = new Decoder();
    vixl_disasm = new QEMUDisassembler(f);
    vixl_decoder->AppendVisitor(vixl_disasm);
}

#define INSN_SIZE 4

/* Disassemble ARM A64 instruction. This is our only entry
 * point from QEMU's C code.
 */
int print_insn_arm_a64(uint64_t addr, disassemble_info *info)
{
    uint8_t bytes[INSN_SIZE];
    uint32_t instr;
    int status;

    status = info->read_memory_func(addr, bytes, INSN_SIZE, info);
    if (status != 0) {
        info->memory_error_func(status, addr, info);
        return -1;
    }

    if (!vixl_is_initialized()) {
        vixl_init(info->stream);
    }

    instr = bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24;
    vixl_decoder->Decode(reinterpret_cast<Instruction*>(&instr));

    return INSN_SIZE;
}
