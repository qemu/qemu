// Copyright 2013, ARM Limited
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of ARM Limited nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef VIXL_A64_DISASM_A64_H
#define VIXL_A64_DISASM_A64_H

#include "globals.h"
#include "utils.h"
#include "instructions-a64.h"
#include "decoder-a64.h"
#include "assembler-a64.h"

namespace vixl {

class Disassembler: public DecoderVisitor {
 public:
  Disassembler();
  Disassembler(char* text_buffer, int buffer_size);
  virtual ~Disassembler();
  char* GetOutput();

  // Declare all Visitor functions.
  #define DECLARE(A)  void Visit##A(const Instruction* instr);
  VISITOR_LIST(DECLARE)
  #undef DECLARE

 protected:
  virtual void ProcessOutput(const Instruction* instr);

  // Default output functions.  The functions below implement a default way of
  // printing elements in the disassembly. A sub-class can override these to
  // customize the disassembly output.

  // Prints the name of a register.
  virtual void AppendRegisterNameToOutput(const Instruction* instr,
                                          const CPURegister& reg);

  // Prints a PC-relative offset. This is used for example when disassembling
  // branches to immediate offsets.
  virtual void AppendPCRelativeOffsetToOutput(const Instruction* instr,
                                              int64_t offset);

  // Prints an address, in the general case. It can be code or data. This is
  // used for example to print the target address of an ADR instruction.
  virtual void AppendAddressToOutput(const Instruction* instr,
                                     const void* addr);

  // Prints the address of some code.
  // This is used for example to print the target address of a branch to an
  // immediate offset.
  // A sub-class can for example override this method to lookup the address and
  // print an appropriate name.
  virtual void AppendCodeAddressToOutput(const Instruction* instr,
                                         const void* addr);

  // Prints the address of some data.
  // This is used for example to print the source address of a load literal
  // instruction.
  virtual void AppendDataAddressToOutput(const Instruction* instr,
                                         const void* addr);

 private:
  void Format(
      const Instruction* instr, const char* mnemonic, const char* format);
  void Substitute(const Instruction* instr, const char* string);
  int SubstituteField(const Instruction* instr, const char* format);
  int SubstituteRegisterField(const Instruction* instr, const char* format);
  int SubstituteImmediateField(const Instruction* instr, const char* format);
  int SubstituteLiteralField(const Instruction* instr, const char* format);
  int SubstituteBitfieldImmediateField(
      const Instruction* instr, const char* format);
  int SubstituteShiftField(const Instruction* instr, const char* format);
  int SubstituteExtendField(const Instruction* instr, const char* format);
  int SubstituteConditionField(const Instruction* instr, const char* format);
  int SubstitutePCRelAddressField(const Instruction* instr, const char* format);
  int SubstituteBranchTargetField(const Instruction* instr, const char* format);
  int SubstituteLSRegOffsetField(const Instruction* instr, const char* format);
  int SubstitutePrefetchField(const Instruction* instr, const char* format);
  int SubstituteBarrierField(const Instruction* instr, const char* format);

  inline bool RdIsZROrSP(const Instruction* instr) const {
    return (instr->Rd() == kZeroRegCode);
  }

  inline bool RnIsZROrSP(const Instruction* instr) const {
    return (instr->Rn() == kZeroRegCode);
  }

  inline bool RmIsZROrSP(const Instruction* instr) const {
    return (instr->Rm() == kZeroRegCode);
  }

  inline bool RaIsZROrSP(const Instruction* instr) const {
    return (instr->Ra() == kZeroRegCode);
  }

  bool IsMovzMovnImm(unsigned reg_size, uint64_t value);

 protected:
  void ResetOutput();
  void AppendToOutput(const char* string, ...) PRINTF_CHECK(2, 3);

  char* buffer_;
  uint32_t buffer_pos_;
  uint32_t buffer_size_;
  bool own_buffer_;
};


class PrintDisassembler: public Disassembler {
 public:
  explicit PrintDisassembler(FILE* stream) : stream_(stream) { }
  virtual ~PrintDisassembler() { }

 protected:
  virtual void ProcessOutput(const Instruction* instr);

 private:
  FILE *stream_;
};
}  // namespace vixl

#endif  // VIXL_A64_DISASM_A64_H
