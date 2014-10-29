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

#ifndef VIXL_A64_ASSEMBLER_A64_H_
#define VIXL_A64_ASSEMBLER_A64_H_

#include <list>
#include <stack>

#include "globals.h"
#include "utils.h"
#include "code-buffer.h"
#include "a64/instructions-a64.h"

namespace vixl {

typedef uint64_t RegList;
static const int kRegListSizeInBits = sizeof(RegList) * 8;


// Registers.

// Some CPURegister methods can return Register and FPRegister types, so we
// need to declare them in advance.
class Register;
class FPRegister;


class CPURegister {
 public:
  enum RegisterType {
    // The kInvalid value is used to detect uninitialized static instances,
    // which are always zero-initialized before any constructors are called.
    kInvalid = 0,
    kRegister,
    kFPRegister,
    kNoRegister
  };

  CPURegister() : code_(0), size_(0), type_(kNoRegister) {
    VIXL_ASSERT(!IsValid());
    VIXL_ASSERT(IsNone());
  }

  CPURegister(unsigned code, unsigned size, RegisterType type)
      : code_(code), size_(size), type_(type) {
    VIXL_ASSERT(IsValidOrNone());
  }

  unsigned code() const {
    VIXL_ASSERT(IsValid());
    return code_;
  }

  RegisterType type() const {
    VIXL_ASSERT(IsValidOrNone());
    return type_;
  }

  RegList Bit() const {
    VIXL_ASSERT(code_ < (sizeof(RegList) * 8));
    return IsValid() ? (static_cast<RegList>(1) << code_) : 0;
  }

  unsigned size() const {
    VIXL_ASSERT(IsValid());
    return size_;
  }

  int SizeInBytes() const {
    VIXL_ASSERT(IsValid());
    VIXL_ASSERT(size() % 8 == 0);
    return size_ / 8;
  }

  int SizeInBits() const {
    VIXL_ASSERT(IsValid());
    return size_;
  }

  bool Is32Bits() const {
    VIXL_ASSERT(IsValid());
    return size_ == 32;
  }

  bool Is64Bits() const {
    VIXL_ASSERT(IsValid());
    return size_ == 64;
  }

  bool IsValid() const {
    if (IsValidRegister() || IsValidFPRegister()) {
      VIXL_ASSERT(!IsNone());
      return true;
    } else {
      VIXL_ASSERT(IsNone());
      return false;
    }
  }

  bool IsValidRegister() const {
    return IsRegister() &&
           ((size_ == kWRegSize) || (size_ == kXRegSize)) &&
           ((code_ < kNumberOfRegisters) || (code_ == kSPRegInternalCode));
  }

  bool IsValidFPRegister() const {
    return IsFPRegister() &&
           ((size_ == kSRegSize) || (size_ == kDRegSize)) &&
           (code_ < kNumberOfFPRegisters);
  }

  bool IsNone() const {
    // kNoRegister types should always have size 0 and code 0.
    VIXL_ASSERT((type_ != kNoRegister) || (code_ == 0));
    VIXL_ASSERT((type_ != kNoRegister) || (size_ == 0));

    return type_ == kNoRegister;
  }

  bool Aliases(const CPURegister& other) const {
    VIXL_ASSERT(IsValidOrNone() && other.IsValidOrNone());
    return (code_ == other.code_) && (type_ == other.type_);
  }

  bool Is(const CPURegister& other) const {
    VIXL_ASSERT(IsValidOrNone() && other.IsValidOrNone());
    return Aliases(other) && (size_ == other.size_);
  }

  inline bool IsZero() const {
    VIXL_ASSERT(IsValid());
    return IsRegister() && (code_ == kZeroRegCode);
  }

  inline bool IsSP() const {
    VIXL_ASSERT(IsValid());
    return IsRegister() && (code_ == kSPRegInternalCode);
  }

  inline bool IsRegister() const {
    return type_ == kRegister;
  }

  inline bool IsFPRegister() const {
    return type_ == kFPRegister;
  }

  bool IsW() const { return IsValidRegister() && Is32Bits(); }
  bool IsX() const { return IsValidRegister() && Is64Bits(); }
  bool IsS() const { return IsValidFPRegister() && Is32Bits(); }
  bool IsD() const { return IsValidFPRegister() && Is64Bits(); }

  const Register& W() const;
  const Register& X() const;
  const FPRegister& S() const;
  const FPRegister& D() const;

  inline bool IsSameSizeAndType(const CPURegister& other) const {
    return (size_ == other.size_) && (type_ == other.type_);
  }

 protected:
  unsigned code_;
  unsigned size_;
  RegisterType type_;

 private:
  bool IsValidOrNone() const {
    return IsValid() || IsNone();
  }
};


class Register : public CPURegister {
 public:
  Register() : CPURegister() {}
  inline explicit Register(const CPURegister& other)
      : CPURegister(other.code(), other.size(), other.type()) {
    VIXL_ASSERT(IsValidRegister());
  }
  Register(unsigned code, unsigned size)
      : CPURegister(code, size, kRegister) {}

  bool IsValid() const {
    VIXL_ASSERT(IsRegister() || IsNone());
    return IsValidRegister();
  }

  static const Register& WRegFromCode(unsigned code);
  static const Register& XRegFromCode(unsigned code);

  // V8 compatibility.
  static const int kNumRegisters = kNumberOfRegisters;
  static const int kNumAllocatableRegisters = kNumberOfRegisters - 1;

 private:
  static const Register wregisters[];
  static const Register xregisters[];
};


class FPRegister : public CPURegister {
 public:
  inline FPRegister() : CPURegister() {}
  inline explicit FPRegister(const CPURegister& other)
      : CPURegister(other.code(), other.size(), other.type()) {
    VIXL_ASSERT(IsValidFPRegister());
  }
  inline FPRegister(unsigned code, unsigned size)
      : CPURegister(code, size, kFPRegister) {}

  bool IsValid() const {
    VIXL_ASSERT(IsFPRegister() || IsNone());
    return IsValidFPRegister();
  }

  static const FPRegister& SRegFromCode(unsigned code);
  static const FPRegister& DRegFromCode(unsigned code);

  // V8 compatibility.
  static const int kNumRegisters = kNumberOfFPRegisters;
  static const int kNumAllocatableRegisters = kNumberOfFPRegisters - 1;

 private:
  static const FPRegister sregisters[];
  static const FPRegister dregisters[];
};


// No*Reg is used to indicate an unused argument, or an error case. Note that
// these all compare equal (using the Is() method). The Register and FPRegister
// variants are provided for convenience.
const Register NoReg;
const FPRegister NoFPReg;
const CPURegister NoCPUReg;


#define DEFINE_REGISTERS(N)  \
const Register w##N(N, kWRegSize);  \
const Register x##N(N, kXRegSize);
REGISTER_CODE_LIST(DEFINE_REGISTERS)
#undef DEFINE_REGISTERS
const Register wsp(kSPRegInternalCode, kWRegSize);
const Register sp(kSPRegInternalCode, kXRegSize);


#define DEFINE_FPREGISTERS(N)  \
const FPRegister s##N(N, kSRegSize);  \
const FPRegister d##N(N, kDRegSize);
REGISTER_CODE_LIST(DEFINE_FPREGISTERS)
#undef DEFINE_FPREGISTERS


// Registers aliases.
const Register ip0 = x16;
const Register ip1 = x17;
const Register lr = x30;
const Register xzr = x31;
const Register wzr = w31;


// AreAliased returns true if any of the named registers overlap. Arguments
// set to NoReg are ignored. The system stack pointer may be specified.
bool AreAliased(const CPURegister& reg1,
                const CPURegister& reg2,
                const CPURegister& reg3 = NoReg,
                const CPURegister& reg4 = NoReg,
                const CPURegister& reg5 = NoReg,
                const CPURegister& reg6 = NoReg,
                const CPURegister& reg7 = NoReg,
                const CPURegister& reg8 = NoReg);


// AreSameSizeAndType returns true if all of the specified registers have the
// same size, and are of the same type. The system stack pointer may be
// specified. Arguments set to NoReg are ignored, as are any subsequent
// arguments. At least one argument (reg1) must be valid (not NoCPUReg).
bool AreSameSizeAndType(const CPURegister& reg1,
                        const CPURegister& reg2,
                        const CPURegister& reg3 = NoCPUReg,
                        const CPURegister& reg4 = NoCPUReg,
                        const CPURegister& reg5 = NoCPUReg,
                        const CPURegister& reg6 = NoCPUReg,
                        const CPURegister& reg7 = NoCPUReg,
                        const CPURegister& reg8 = NoCPUReg);


// Lists of registers.
class CPURegList {
 public:
  inline explicit CPURegList(CPURegister reg1,
                             CPURegister reg2 = NoCPUReg,
                             CPURegister reg3 = NoCPUReg,
                             CPURegister reg4 = NoCPUReg)
      : list_(reg1.Bit() | reg2.Bit() | reg3.Bit() | reg4.Bit()),
        size_(reg1.size()), type_(reg1.type()) {
    VIXL_ASSERT(AreSameSizeAndType(reg1, reg2, reg3, reg4));
    VIXL_ASSERT(IsValid());
  }

  inline CPURegList(CPURegister::RegisterType type, unsigned size, RegList list)
      : list_(list), size_(size), type_(type) {
    VIXL_ASSERT(IsValid());
  }

  inline CPURegList(CPURegister::RegisterType type, unsigned size,
                    unsigned first_reg, unsigned last_reg)
      : size_(size), type_(type) {
    VIXL_ASSERT(((type == CPURegister::kRegister) &&
                 (last_reg < kNumberOfRegisters)) ||
                ((type == CPURegister::kFPRegister) &&
                 (last_reg < kNumberOfFPRegisters)));
    VIXL_ASSERT(last_reg >= first_reg);
    list_ = (UINT64_C(1) << (last_reg + 1)) - 1;
    list_ &= ~((UINT64_C(1) << first_reg) - 1);
    VIXL_ASSERT(IsValid());
  }

  inline CPURegister::RegisterType type() const {
    VIXL_ASSERT(IsValid());
    return type_;
  }

  // Combine another CPURegList into this one. Registers that already exist in
  // this list are left unchanged. The type and size of the registers in the
  // 'other' list must match those in this list.
  void Combine(const CPURegList& other) {
    VIXL_ASSERT(IsValid());
    VIXL_ASSERT(other.type() == type_);
    VIXL_ASSERT(other.RegisterSizeInBits() == size_);
    list_ |= other.list();
  }

  // Remove every register in the other CPURegList from this one. Registers that
  // do not exist in this list are ignored. The type and size of the registers
  // in the 'other' list must match those in this list.
  void Remove(const CPURegList& other) {
    VIXL_ASSERT(IsValid());
    VIXL_ASSERT(other.type() == type_);
    VIXL_ASSERT(other.RegisterSizeInBits() == size_);
    list_ &= ~other.list();
  }

  // Variants of Combine and Remove which take a single register.
  inline void Combine(const CPURegister& other) {
    VIXL_ASSERT(other.type() == type_);
    VIXL_ASSERT(other.size() == size_);
    Combine(other.code());
  }

  inline void Remove(const CPURegister& other) {
    VIXL_ASSERT(other.type() == type_);
    VIXL_ASSERT(other.size() == size_);
    Remove(other.code());
  }

  // Variants of Combine and Remove which take a single register by its code;
  // the type and size of the register is inferred from this list.
  inline void Combine(int code) {
    VIXL_ASSERT(IsValid());
    VIXL_ASSERT(CPURegister(code, size_, type_).IsValid());
    list_ |= (UINT64_C(1) << code);
  }

  inline void Remove(int code) {
    VIXL_ASSERT(IsValid());
    VIXL_ASSERT(CPURegister(code, size_, type_).IsValid());
    list_ &= ~(UINT64_C(1) << code);
  }

  inline RegList list() const {
    VIXL_ASSERT(IsValid());
    return list_;
  }

  inline void set_list(RegList new_list) {
    VIXL_ASSERT(IsValid());
    list_ = new_list;
  }

  // Remove all callee-saved registers from the list. This can be useful when
  // preparing registers for an AAPCS64 function call, for example.
  void RemoveCalleeSaved();

  CPURegister PopLowestIndex();
  CPURegister PopHighestIndex();

  // AAPCS64 callee-saved registers.
  static CPURegList GetCalleeSaved(unsigned size = kXRegSize);
  static CPURegList GetCalleeSavedFP(unsigned size = kDRegSize);

  // AAPCS64 caller-saved registers. Note that this includes lr.
  static CPURegList GetCallerSaved(unsigned size = kXRegSize);
  static CPURegList GetCallerSavedFP(unsigned size = kDRegSize);

  inline bool IsEmpty() const {
    VIXL_ASSERT(IsValid());
    return list_ == 0;
  }

  inline bool IncludesAliasOf(const CPURegister& other) const {
    VIXL_ASSERT(IsValid());
    return (type_ == other.type()) && ((other.Bit() & list_) != 0);
  }

  inline bool IncludesAliasOf(int code) const {
    VIXL_ASSERT(IsValid());
    return ((code & list_) != 0);
  }

  inline int Count() const {
    VIXL_ASSERT(IsValid());
    return CountSetBits(list_, kRegListSizeInBits);
  }

  inline unsigned RegisterSizeInBits() const {
    VIXL_ASSERT(IsValid());
    return size_;
  }

  inline unsigned RegisterSizeInBytes() const {
    int size_in_bits = RegisterSizeInBits();
    VIXL_ASSERT((size_in_bits % 8) == 0);
    return size_in_bits / 8;
  }

  inline unsigned TotalSizeInBytes() const {
    VIXL_ASSERT(IsValid());
    return RegisterSizeInBytes() * Count();
  }

 private:
  RegList list_;
  unsigned size_;
  CPURegister::RegisterType type_;

  bool IsValid() const;
};


// AAPCS64 callee-saved registers.
extern const CPURegList kCalleeSaved;
extern const CPURegList kCalleeSavedFP;


// AAPCS64 caller-saved registers. Note that this includes lr.
extern const CPURegList kCallerSaved;
extern const CPURegList kCallerSavedFP;


// Operand.
class Operand {
 public:
  // #<immediate>
  // where <immediate> is int64_t.
  // This is allowed to be an implicit constructor because Operand is
  // a wrapper class that doesn't normally perform any type conversion.
  Operand(int64_t immediate);           // NOLINT(runtime/explicit)

  // rm, {<shift> #<shift_amount>}
  // where <shift> is one of {LSL, LSR, ASR, ROR}.
  //       <shift_amount> is uint6_t.
  // This is allowed to be an implicit constructor because Operand is
  // a wrapper class that doesn't normally perform any type conversion.
  Operand(Register reg,
          Shift shift = LSL,
          unsigned shift_amount = 0);   // NOLINT(runtime/explicit)

  // rm, {<extend> {#<shift_amount>}}
  // where <extend> is one of {UXTB, UXTH, UXTW, UXTX, SXTB, SXTH, SXTW, SXTX}.
  //       <shift_amount> is uint2_t.
  explicit Operand(Register reg, Extend extend, unsigned shift_amount = 0);

  bool IsImmediate() const;
  bool IsShiftedRegister() const;
  bool IsExtendedRegister() const;
  bool IsZero() const;

  // This returns an LSL shift (<= 4) operand as an equivalent extend operand,
  // which helps in the encoding of instructions that use the stack pointer.
  Operand ToExtendedRegister() const;

  int64_t immediate() const {
    VIXL_ASSERT(IsImmediate());
    return immediate_;
  }

  Register reg() const {
    VIXL_ASSERT(IsShiftedRegister() || IsExtendedRegister());
    return reg_;
  }

  Shift shift() const {
    VIXL_ASSERT(IsShiftedRegister());
    return shift_;
  }

  Extend extend() const {
    VIXL_ASSERT(IsExtendedRegister());
    return extend_;
  }

  unsigned shift_amount() const {
    VIXL_ASSERT(IsShiftedRegister() || IsExtendedRegister());
    return shift_amount_;
  }

 private:
  int64_t immediate_;
  Register reg_;
  Shift shift_;
  Extend extend_;
  unsigned shift_amount_;
};


// MemOperand represents the addressing mode of a load or store instruction.
class MemOperand {
 public:
  explicit MemOperand(Register base,
                      int64_t offset = 0,
                      AddrMode addrmode = Offset);
  explicit MemOperand(Register base,
                      Register regoffset,
                      Shift shift = LSL,
                      unsigned shift_amount = 0);
  explicit MemOperand(Register base,
                      Register regoffset,
                      Extend extend,
                      unsigned shift_amount = 0);
  explicit MemOperand(Register base,
                      const Operand& offset,
                      AddrMode addrmode = Offset);

  const Register& base() const { return base_; }
  const Register& regoffset() const { return regoffset_; }
  int64_t offset() const { return offset_; }
  AddrMode addrmode() const { return addrmode_; }
  Shift shift() const { return shift_; }
  Extend extend() const { return extend_; }
  unsigned shift_amount() const { return shift_amount_; }
  bool IsImmediateOffset() const;
  bool IsRegisterOffset() const;
  bool IsPreIndex() const;
  bool IsPostIndex() const;

 private:
  Register base_;
  Register regoffset_;
  int64_t offset_;
  AddrMode addrmode_;
  Shift shift_;
  Extend extend_;
  unsigned shift_amount_;
};


class Label {
 public:
  Label() : location_(kLocationUnbound) {}
  ~Label() {
    // If the label has been linked to, it needs to be bound to a target.
    VIXL_ASSERT(!IsLinked() || IsBound());
  }

  inline bool IsBound() const { return location_ >= 0; }
  inline bool IsLinked() const { return !links_.empty(); }

 private:
  // The list of linked instructions is stored in a stack-like structure. We
  // don't use std::stack directly because it's slow for the common case where
  // only one or two instructions refer to a label, and labels themselves are
  // short-lived. This class behaves like std::stack, but the first few links
  // are preallocated (configured by kPreallocatedLinks).
  //
  // If more than N links are required, this falls back to std::stack.
  class LinksStack {
   public:
    LinksStack() : size_(0), links_extended_(NULL) {}
    ~LinksStack() {
      delete links_extended_;
    }

    size_t size() const {
      return size_;
    }

    bool empty() const {
      return size_ == 0;
    }

    void push(ptrdiff_t value) {
      if (size_ < kPreallocatedLinks) {
        links_[size_] = value;
      } else {
        if (links_extended_ == NULL) {
          links_extended_ = new std::stack<ptrdiff_t>();
        }
        VIXL_ASSERT(size_ == (links_extended_->size() + kPreallocatedLinks));
        links_extended_->push(value);
      }
      size_++;
    }

    ptrdiff_t top() const {
      return (size_ <= kPreallocatedLinks) ? links_[size_ - 1]
                                           : links_extended_->top();
    }

    void pop() {
      size_--;
      if (size_ >= kPreallocatedLinks) {
        links_extended_->pop();
        VIXL_ASSERT(size_ == (links_extended_->size() + kPreallocatedLinks));
      }
    }

   private:
    static const size_t kPreallocatedLinks = 4;

    size_t size_;
    ptrdiff_t links_[kPreallocatedLinks];
    std::stack<ptrdiff_t> * links_extended_;
  };

  inline ptrdiff_t location() const { return location_; }

  inline void Bind(ptrdiff_t location) {
    // Labels can only be bound once.
    VIXL_ASSERT(!IsBound());
    location_ = location;
  }

  inline void AddLink(ptrdiff_t instruction) {
    // If a label is bound, the assembler already has the information it needs
    // to write the instruction, so there is no need to add it to links_.
    VIXL_ASSERT(!IsBound());
    links_.push(instruction);
  }

  inline ptrdiff_t GetAndRemoveNextLink() {
    VIXL_ASSERT(IsLinked());
    ptrdiff_t link = links_.top();
    links_.pop();
    return link;
  }

  // The offsets of the instructions that have linked to this label.
  LinksStack links_;
  // The label location.
  ptrdiff_t location_;

  static const ptrdiff_t kLocationUnbound = -1;

  // It is not safe to copy labels, so disable the copy constructor by declaring
  // it private (without an implementation).
  Label(const Label&);

  // The Assembler class is responsible for binding and linking labels, since
  // the stored offsets need to be consistent with the Assembler's buffer.
  friend class Assembler;
};


// A literal is a 32-bit or 64-bit piece of data stored in the instruction
// stream and loaded through a pc relative load. The same literal can be
// referred to by multiple instructions but a literal can only reside at one
// place in memory. A literal can be used by a load before or after being
// placed in memory.
//
// Internally an offset of 0 is associated with a literal which has been
// neither used nor placed. Then two possibilities arise:
//  1) the label is placed, the offset (stored as offset + 1) is used to
//     resolve any subsequent load using the label.
//  2) the label is not placed and offset is the offset of the last load using
//     the literal (stored as -offset -1). If multiple loads refer to this
//     literal then the last load holds the offset of the preceding load and
//     all loads form a chain. Once the offset is placed all the loads in the
//     chain are resolved and future loads fall back to possibility 1.
class RawLiteral {
 public:
  RawLiteral() : size_(0), offset_(0), raw_value_(0) {}

  size_t size() {
    VIXL_STATIC_ASSERT(kDRegSizeInBytes == kXRegSizeInBytes);
    VIXL_STATIC_ASSERT(kSRegSizeInBytes == kWRegSizeInBytes);
    VIXL_ASSERT((size_ == kXRegSizeInBytes) || (size_ == kWRegSizeInBytes));
    return size_;
  }
  uint64_t raw_value64() {
    VIXL_ASSERT(size_ == kXRegSizeInBytes);
    return raw_value_;
  }
  uint32_t raw_value32() {
    VIXL_ASSERT(size_ == kWRegSizeInBytes);
    VIXL_ASSERT(is_uint32(raw_value_) || is_int32(raw_value_));
    return static_cast<uint32_t>(raw_value_);
  }
  bool IsUsed() { return offset_ < 0; }
  bool IsPlaced() { return offset_ > 0; }

 protected:
  ptrdiff_t offset() {
    VIXL_ASSERT(IsPlaced());
    return offset_ - 1;
  }
  void set_offset(ptrdiff_t offset) {
    VIXL_ASSERT(offset >= 0);
    VIXL_ASSERT(IsWordAligned(offset));
    VIXL_ASSERT(!IsPlaced());
    offset_ = offset + 1;
  }
  ptrdiff_t last_use() {
    VIXL_ASSERT(IsUsed());
    return -offset_ - 1;
  }
  void set_last_use(ptrdiff_t offset) {
    VIXL_ASSERT(offset >= 0);
    VIXL_ASSERT(IsWordAligned(offset));
    VIXL_ASSERT(!IsPlaced());
    offset_ = -offset - 1;
  }

  size_t size_;
  ptrdiff_t offset_;
  uint64_t raw_value_;

  friend class Assembler;
};


template <typename T>
class Literal : public RawLiteral {
 public:
  explicit Literal(T value) {
    size_ = sizeof(value);
    memcpy(&raw_value_, &value, sizeof(value));
  }
};


// Control whether or not position-independent code should be emitted.
enum PositionIndependentCodeOption {
  // All code generated will be position-independent; all branches and
  // references to labels generated with the Label class will use PC-relative
  // addressing.
  PositionIndependentCode,

  // Allow VIXL to generate code that refers to absolute addresses. With this
  // option, it will not be possible to copy the code buffer and run it from a
  // different address; code must be generated in its final location.
  PositionDependentCode,

  // Allow VIXL to assume that the bottom 12 bits of the address will be
  // constant, but that the top 48 bits may change. This allows `adrp` to
  // function in systems which copy code between pages, but otherwise maintain
  // 4KB page alignment.
  PageOffsetDependentCode
};


// Control how scaled- and unscaled-offset loads and stores are generated.
enum LoadStoreScalingOption {
  // Prefer scaled-immediate-offset instructions, but emit unscaled-offset,
  // register-offset, pre-index or post-index instructions if necessary.
  PreferScaledOffset,

  // Prefer unscaled-immediate-offset instructions, but emit scaled-offset,
  // register-offset, pre-index or post-index instructions if necessary.
  PreferUnscaledOffset,

  // Require scaled-immediate-offset instructions.
  RequireScaledOffset,

  // Require unscaled-immediate-offset instructions.
  RequireUnscaledOffset
};


// Assembler.
class Assembler {
 public:
  Assembler(size_t capacity,
            PositionIndependentCodeOption pic = PositionIndependentCode);
  Assembler(byte* buffer, size_t capacity,
            PositionIndependentCodeOption pic = PositionIndependentCode);

  // The destructor asserts that one of the following is true:
  //  * The Assembler object has not been used.
  //  * Nothing has been emitted since the last Reset() call.
  //  * Nothing has been emitted since the last FinalizeCode() call.
  ~Assembler();

  // System functions.

  // Start generating code from the beginning of the buffer, discarding any code
  // and data that has already been emitted into the buffer.
  void Reset();

  // Finalize a code buffer of generated instructions. This function must be
  // called before executing or copying code from the buffer.
  void FinalizeCode();

  // Label.
  // Bind a label to the current PC.
  void bind(Label* label);

  // Bind a label to a specified offset from the start of the buffer.
  void BindToOffset(Label* label, ptrdiff_t offset);

  // Place a literal at the current PC.
  void place(RawLiteral* literal);

  ptrdiff_t CursorOffset() const {
    return buffer_->CursorOffset();
  }

  ptrdiff_t BufferEndOffset() const {
    return static_cast<ptrdiff_t>(buffer_->capacity());
  }

  // Return the address of an offset in the buffer.
  template <typename T>
  inline T GetOffsetAddress(ptrdiff_t offset) {
    VIXL_STATIC_ASSERT(sizeof(T) >= sizeof(uintptr_t));
    return buffer_->GetOffsetAddress<T>(offset);
  }

  // Return the address of a bound label.
  template <typename T>
  inline T GetLabelAddress(const Label * label) {
    VIXL_ASSERT(label->IsBound());
    VIXL_STATIC_ASSERT(sizeof(T) >= sizeof(uintptr_t));
    return GetOffsetAddress<T>(label->location());
  }

  // Return the address of the cursor.
  template <typename T>
  inline T GetCursorAddress() {
    VIXL_STATIC_ASSERT(sizeof(T) >= sizeof(uintptr_t));
    return GetOffsetAddress<T>(CursorOffset());
  }

  // Return the address of the start of the buffer.
  template <typename T>
  inline T GetStartAddress() {
    VIXL_STATIC_ASSERT(sizeof(T) >= sizeof(uintptr_t));
    return GetOffsetAddress<T>(0);
  }

  // Instruction set functions.

  // Branch / Jump instructions.
  // Branch to register.
  void br(const Register& xn);

  // Branch with link to register.
  void blr(const Register& xn);

  // Branch to register with return hint.
  void ret(const Register& xn = lr);

  // Unconditional branch to label.
  void b(Label* label);

  // Conditional branch to label.
  void b(Label* label, Condition cond);

  // Unconditional branch to PC offset.
  void b(int imm26);

  // Conditional branch to PC offset.
  void b(int imm19, Condition cond);

  // Branch with link to label.
  void bl(Label* label);

  // Branch with link to PC offset.
  void bl(int imm26);

  // Compare and branch to label if zero.
  void cbz(const Register& rt, Label* label);

  // Compare and branch to PC offset if zero.
  void cbz(const Register& rt, int imm19);

  // Compare and branch to label if not zero.
  void cbnz(const Register& rt, Label* label);

  // Compare and branch to PC offset if not zero.
  void cbnz(const Register& rt, int imm19);

  // Test bit and branch to label if zero.
  void tbz(const Register& rt, unsigned bit_pos, Label* label);

  // Test bit and branch to PC offset if zero.
  void tbz(const Register& rt, unsigned bit_pos, int imm14);

  // Test bit and branch to label if not zero.
  void tbnz(const Register& rt, unsigned bit_pos, Label* label);

  // Test bit and branch to PC offset if not zero.
  void tbnz(const Register& rt, unsigned bit_pos, int imm14);

  // Address calculation instructions.
  // Calculate a PC-relative address. Unlike for branches the offset in adr is
  // unscaled (i.e. the result can be unaligned).

  // Calculate the address of a label.
  void adr(const Register& rd, Label* label);

  // Calculate the address of a PC offset.
  void adr(const Register& rd, int imm21);

  // Calculate the page address of a label.
  void adrp(const Register& rd, Label* label);

  // Calculate the page address of a PC offset.
  void adrp(const Register& rd, int imm21);

  // Data Processing instructions.
  // Add.
  void add(const Register& rd,
           const Register& rn,
           const Operand& operand);

  // Add and update status flags.
  void adds(const Register& rd,
            const Register& rn,
            const Operand& operand);

  // Compare negative.
  void cmn(const Register& rn, const Operand& operand);

  // Subtract.
  void sub(const Register& rd,
           const Register& rn,
           const Operand& operand);

  // Subtract and update status flags.
  void subs(const Register& rd,
            const Register& rn,
            const Operand& operand);

  // Compare.
  void cmp(const Register& rn, const Operand& operand);

  // Negate.
  void neg(const Register& rd,
           const Operand& operand);

  // Negate and update status flags.
  void negs(const Register& rd,
            const Operand& operand);

  // Add with carry bit.
  void adc(const Register& rd,
           const Register& rn,
           const Operand& operand);

  // Add with carry bit and update status flags.
  void adcs(const Register& rd,
            const Register& rn,
            const Operand& operand);

  // Subtract with carry bit.
  void sbc(const Register& rd,
           const Register& rn,
           const Operand& operand);

  // Subtract with carry bit and update status flags.
  void sbcs(const Register& rd,
            const Register& rn,
            const Operand& operand);

  // Negate with carry bit.
  void ngc(const Register& rd,
           const Operand& operand);

  // Negate with carry bit and update status flags.
  void ngcs(const Register& rd,
            const Operand& operand);

  // Logical instructions.
  // Bitwise and (A & B).
  void and_(const Register& rd,
            const Register& rn,
            const Operand& operand);

  // Bitwise and (A & B) and update status flags.
  void ands(const Register& rd,
            const Register& rn,
            const Operand& operand);

  // Bit test and set flags.
  void tst(const Register& rn, const Operand& operand);

  // Bit clear (A & ~B).
  void bic(const Register& rd,
           const Register& rn,
           const Operand& operand);

  // Bit clear (A & ~B) and update status flags.
  void bics(const Register& rd,
            const Register& rn,
            const Operand& operand);

  // Bitwise or (A | B).
  void orr(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise nor (A | ~B).
  void orn(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise eor/xor (A ^ B).
  void eor(const Register& rd, const Register& rn, const Operand& operand);

  // Bitwise enor/xnor (A ^ ~B).
  void eon(const Register& rd, const Register& rn, const Operand& operand);

  // Logical shift left by variable.
  void lslv(const Register& rd, const Register& rn, const Register& rm);

  // Logical shift right by variable.
  void lsrv(const Register& rd, const Register& rn, const Register& rm);

  // Arithmetic shift right by variable.
  void asrv(const Register& rd, const Register& rn, const Register& rm);

  // Rotate right by variable.
  void rorv(const Register& rd, const Register& rn, const Register& rm);

  // Bitfield instructions.
  // Bitfield move.
  void bfm(const Register& rd,
           const Register& rn,
           unsigned immr,
           unsigned imms);

  // Signed bitfield move.
  void sbfm(const Register& rd,
            const Register& rn,
            unsigned immr,
            unsigned imms);

  // Unsigned bitfield move.
  void ubfm(const Register& rd,
            const Register& rn,
            unsigned immr,
            unsigned imms);

  // Bfm aliases.
  // Bitfield insert.
  inline void bfi(const Register& rd,
                  const Register& rn,
                  unsigned lsb,
                  unsigned width) {
    VIXL_ASSERT(width >= 1);
    VIXL_ASSERT(lsb + width <= rn.size());
    bfm(rd, rn, (rd.size() - lsb) & (rd.size() - 1), width - 1);
  }

  // Bitfield extract and insert low.
  inline void bfxil(const Register& rd,
                    const Register& rn,
                    unsigned lsb,
                    unsigned width) {
    VIXL_ASSERT(width >= 1);
    VIXL_ASSERT(lsb + width <= rn.size());
    bfm(rd, rn, lsb, lsb + width - 1);
  }

  // Sbfm aliases.
  // Arithmetic shift right.
  inline void asr(const Register& rd, const Register& rn, unsigned shift) {
    VIXL_ASSERT(shift < rd.size());
    sbfm(rd, rn, shift, rd.size() - 1);
  }

  // Signed bitfield insert with zero at right.
  inline void sbfiz(const Register& rd,
                    const Register& rn,
                    unsigned lsb,
                    unsigned width) {
    VIXL_ASSERT(width >= 1);
    VIXL_ASSERT(lsb + width <= rn.size());
    sbfm(rd, rn, (rd.size() - lsb) & (rd.size() - 1), width - 1);
  }

  // Signed bitfield extract.
  inline void sbfx(const Register& rd,
                   const Register& rn,
                   unsigned lsb,
                   unsigned width) {
    VIXL_ASSERT(width >= 1);
    VIXL_ASSERT(lsb + width <= rn.size());
    sbfm(rd, rn, lsb, lsb + width - 1);
  }

  // Signed extend byte.
  inline void sxtb(const Register& rd, const Register& rn) {
    sbfm(rd, rn, 0, 7);
  }

  // Signed extend halfword.
  inline void sxth(const Register& rd, const Register& rn) {
    sbfm(rd, rn, 0, 15);
  }

  // Signed extend word.
  inline void sxtw(const Register& rd, const Register& rn) {
    sbfm(rd, rn, 0, 31);
  }

  // Ubfm aliases.
  // Logical shift left.
  inline void lsl(const Register& rd, const Register& rn, unsigned shift) {
    unsigned reg_size = rd.size();
    VIXL_ASSERT(shift < reg_size);
    ubfm(rd, rn, (reg_size - shift) % reg_size, reg_size - shift - 1);
  }

  // Logical shift right.
  inline void lsr(const Register& rd, const Register& rn, unsigned shift) {
    VIXL_ASSERT(shift < rd.size());
    ubfm(rd, rn, shift, rd.size() - 1);
  }

  // Unsigned bitfield insert with zero at right.
  inline void ubfiz(const Register& rd,
                    const Register& rn,
                    unsigned lsb,
                    unsigned width) {
    VIXL_ASSERT(width >= 1);
    VIXL_ASSERT(lsb + width <= rn.size());
    ubfm(rd, rn, (rd.size() - lsb) & (rd.size() - 1), width - 1);
  }

  // Unsigned bitfield extract.
  inline void ubfx(const Register& rd,
                   const Register& rn,
                   unsigned lsb,
                   unsigned width) {
    VIXL_ASSERT(width >= 1);
    VIXL_ASSERT(lsb + width <= rn.size());
    ubfm(rd, rn, lsb, lsb + width - 1);
  }

  // Unsigned extend byte.
  inline void uxtb(const Register& rd, const Register& rn) {
    ubfm(rd, rn, 0, 7);
  }

  // Unsigned extend halfword.
  inline void uxth(const Register& rd, const Register& rn) {
    ubfm(rd, rn, 0, 15);
  }

  // Unsigned extend word.
  inline void uxtw(const Register& rd, const Register& rn) {
    ubfm(rd, rn, 0, 31);
  }

  // Extract.
  void extr(const Register& rd,
            const Register& rn,
            const Register& rm,
            unsigned lsb);

  // Conditional select: rd = cond ? rn : rm.
  void csel(const Register& rd,
            const Register& rn,
            const Register& rm,
            Condition cond);

  // Conditional select increment: rd = cond ? rn : rm + 1.
  void csinc(const Register& rd,
             const Register& rn,
             const Register& rm,
             Condition cond);

  // Conditional select inversion: rd = cond ? rn : ~rm.
  void csinv(const Register& rd,
             const Register& rn,
             const Register& rm,
             Condition cond);

  // Conditional select negation: rd = cond ? rn : -rm.
  void csneg(const Register& rd,
             const Register& rn,
             const Register& rm,
             Condition cond);

  // Conditional set: rd = cond ? 1 : 0.
  void cset(const Register& rd, Condition cond);

  // Conditional set mask: rd = cond ? -1 : 0.
  void csetm(const Register& rd, Condition cond);

  // Conditional increment: rd = cond ? rn + 1 : rn.
  void cinc(const Register& rd, const Register& rn, Condition cond);

  // Conditional invert: rd = cond ? ~rn : rn.
  void cinv(const Register& rd, const Register& rn, Condition cond);

  // Conditional negate: rd = cond ? -rn : rn.
  void cneg(const Register& rd, const Register& rn, Condition cond);

  // Rotate right.
  inline void ror(const Register& rd, const Register& rs, unsigned shift) {
    extr(rd, rs, rs, shift);
  }

  // Conditional comparison.
  // Conditional compare negative.
  void ccmn(const Register& rn,
            const Operand& operand,
            StatusFlags nzcv,
            Condition cond);

  // Conditional compare.
  void ccmp(const Register& rn,
            const Operand& operand,
            StatusFlags nzcv,
            Condition cond);

  // Multiply.
  void mul(const Register& rd, const Register& rn, const Register& rm);

  // Negated multiply.
  void mneg(const Register& rd, const Register& rn, const Register& rm);

  // Signed long multiply: 32 x 32 -> 64-bit.
  void smull(const Register& rd, const Register& rn, const Register& rm);

  // Signed multiply high: 64 x 64 -> 64-bit <127:64>.
  void smulh(const Register& xd, const Register& xn, const Register& xm);

  // Multiply and accumulate.
  void madd(const Register& rd,
            const Register& rn,
            const Register& rm,
            const Register& ra);

  // Multiply and subtract.
  void msub(const Register& rd,
            const Register& rn,
            const Register& rm,
            const Register& ra);

  // Signed long multiply and accumulate: 32 x 32 + 64 -> 64-bit.
  void smaddl(const Register& rd,
              const Register& rn,
              const Register& rm,
              const Register& ra);

  // Unsigned long multiply and accumulate: 32 x 32 + 64 -> 64-bit.
  void umaddl(const Register& rd,
              const Register& rn,
              const Register& rm,
              const Register& ra);

  // Signed long multiply and subtract: 64 - (32 x 32) -> 64-bit.
  void smsubl(const Register& rd,
              const Register& rn,
              const Register& rm,
              const Register& ra);

  // Unsigned long multiply and subtract: 64 - (32 x 32) -> 64-bit.
  void umsubl(const Register& rd,
              const Register& rn,
              const Register& rm,
              const Register& ra);

  // Signed integer divide.
  void sdiv(const Register& rd, const Register& rn, const Register& rm);

  // Unsigned integer divide.
  void udiv(const Register& rd, const Register& rn, const Register& rm);

  // Bit reverse.
  void rbit(const Register& rd, const Register& rn);

  // Reverse bytes in 16-bit half words.
  void rev16(const Register& rd, const Register& rn);

  // Reverse bytes in 32-bit words.
  void rev32(const Register& rd, const Register& rn);

  // Reverse bytes.
  void rev(const Register& rd, const Register& rn);

  // Count leading zeroes.
  void clz(const Register& rd, const Register& rn);

  // Count leading sign bits.
  void cls(const Register& rd, const Register& rn);

  // Memory instructions.
  // Load integer or FP register.
  void ldr(const CPURegister& rt, const MemOperand& src,
           LoadStoreScalingOption option = PreferScaledOffset);

  // Store integer or FP register.
  void str(const CPURegister& rt, const MemOperand& dst,
           LoadStoreScalingOption option = PreferScaledOffset);

  // Load word with sign extension.
  void ldrsw(const Register& rt, const MemOperand& src,
             LoadStoreScalingOption option = PreferScaledOffset);

  // Load byte.
  void ldrb(const Register& rt, const MemOperand& src,
            LoadStoreScalingOption option = PreferScaledOffset);

  // Store byte.
  void strb(const Register& rt, const MemOperand& dst,
            LoadStoreScalingOption option = PreferScaledOffset);

  // Load byte with sign extension.
  void ldrsb(const Register& rt, const MemOperand& src,
             LoadStoreScalingOption option = PreferScaledOffset);

  // Load half-word.
  void ldrh(const Register& rt, const MemOperand& src,
            LoadStoreScalingOption option = PreferScaledOffset);

  // Store half-word.
  void strh(const Register& rt, const MemOperand& dst,
            LoadStoreScalingOption option = PreferScaledOffset);

  // Load half-word with sign extension.
  void ldrsh(const Register& rt, const MemOperand& src,
             LoadStoreScalingOption option = PreferScaledOffset);

  // Load integer or FP register (with unscaled offset).
  void ldur(const CPURegister& rt, const MemOperand& src,
            LoadStoreScalingOption option = PreferUnscaledOffset);

  // Store integer or FP register (with unscaled offset).
  void stur(const CPURegister& rt, const MemOperand& src,
            LoadStoreScalingOption option = PreferUnscaledOffset);

  // Load word with sign extension.
  void ldursw(const Register& rt, const MemOperand& src,
              LoadStoreScalingOption option = PreferUnscaledOffset);

  // Load byte (with unscaled offset).
  void ldurb(const Register& rt, const MemOperand& src,
             LoadStoreScalingOption option = PreferUnscaledOffset);

  // Store byte (with unscaled offset).
  void sturb(const Register& rt, const MemOperand& dst,
             LoadStoreScalingOption option = PreferUnscaledOffset);

  // Load byte with sign extension (and unscaled offset).
  void ldursb(const Register& rt, const MemOperand& src,
              LoadStoreScalingOption option = PreferUnscaledOffset);

  // Load half-word (with unscaled offset).
  void ldurh(const Register& rt, const MemOperand& src,
             LoadStoreScalingOption option = PreferUnscaledOffset);

  // Store half-word (with unscaled offset).
  void sturh(const Register& rt, const MemOperand& dst,
             LoadStoreScalingOption option = PreferUnscaledOffset);

  // Load half-word with sign extension (and unscaled offset).
  void ldursh(const Register& rt, const MemOperand& src,
              LoadStoreScalingOption option = PreferUnscaledOffset);

  // Load integer or FP register pair.
  void ldp(const CPURegister& rt, const CPURegister& rt2,
           const MemOperand& src);

  // Store integer or FP register pair.
  void stp(const CPURegister& rt, const CPURegister& rt2,
           const MemOperand& dst);

  // Load word pair with sign extension.
  void ldpsw(const Register& rt, const Register& rt2, const MemOperand& src);

  // Load integer or FP register pair, non-temporal.
  void ldnp(const CPURegister& rt, const CPURegister& rt2,
            const MemOperand& src);

  // Store integer or FP register pair, non-temporal.
  void stnp(const CPURegister& rt, const CPURegister& rt2,
            const MemOperand& dst);

  // Load integer or FP register from literal pool.
  void ldr(const CPURegister& rt, RawLiteral* literal);

  // Load word with sign extension from literal pool.
  void ldrsw(const Register& rt, RawLiteral* literal);

  // Load integer or FP register from pc + imm19 << 2.
  void ldr(const CPURegister& rt, int imm19);

  // Load word with sign extension from pc + imm19 << 2.
  void ldrsw(const Register& rt, int imm19);

  // Store exclusive byte.
  void stxrb(const Register& rs, const Register& rt, const MemOperand& dst);

  // Store exclusive half-word.
  void stxrh(const Register& rs, const Register& rt, const MemOperand& dst);

  // Store exclusive register.
  void stxr(const Register& rs, const Register& rt, const MemOperand& dst);

  // Load exclusive byte.
  void ldxrb(const Register& rt, const MemOperand& src);

  // Load exclusive half-word.
  void ldxrh(const Register& rt, const MemOperand& src);

  // Load exclusive register.
  void ldxr(const Register& rt, const MemOperand& src);

  // Store exclusive register pair.
  void stxp(const Register& rs,
            const Register& rt,
            const Register& rt2,
            const MemOperand& dst);

  // Load exclusive register pair.
  void ldxp(const Register& rt, const Register& rt2, const MemOperand& src);

  // Store-release exclusive byte.
  void stlxrb(const Register& rs, const Register& rt, const MemOperand& dst);

  // Store-release exclusive half-word.
  void stlxrh(const Register& rs, const Register& rt, const MemOperand& dst);

  // Store-release exclusive register.
  void stlxr(const Register& rs, const Register& rt, const MemOperand& dst);

  // Load-acquire exclusive byte.
  void ldaxrb(const Register& rt, const MemOperand& src);

  // Load-acquire exclusive half-word.
  void ldaxrh(const Register& rt, const MemOperand& src);

  // Load-acquire exclusive register.
  void ldaxr(const Register& rt, const MemOperand& src);

  // Store-release exclusive register pair.
  void stlxp(const Register& rs,
             const Register& rt,
             const Register& rt2,
             const MemOperand& dst);

  // Load-acquire exclusive register pair.
  void ldaxp(const Register& rt, const Register& rt2, const MemOperand& src);

  // Store-release byte.
  void stlrb(const Register& rt, const MemOperand& dst);

  // Store-release half-word.
  void stlrh(const Register& rt, const MemOperand& dst);

  // Store-release register.
  void stlr(const Register& rt, const MemOperand& dst);

  // Load-acquire byte.
  void ldarb(const Register& rt, const MemOperand& src);

  // Load-acquire half-word.
  void ldarh(const Register& rt, const MemOperand& src);

  // Load-acquire register.
  void ldar(const Register& rt, const MemOperand& src);


  // Move instructions. The default shift of -1 indicates that the move
  // instruction will calculate an appropriate 16-bit immediate and left shift
  // that is equal to the 64-bit immediate argument. If an explicit left shift
  // is specified (0, 16, 32 or 48), the immediate must be a 16-bit value.
  //
  // For movk, an explicit shift can be used to indicate which half word should
  // be overwritten, eg. movk(x0, 0, 0) will overwrite the least-significant
  // half word with zero, whereas movk(x0, 0, 48) will overwrite the
  // most-significant.

  // Move immediate and keep.
  void movk(const Register& rd, uint64_t imm, int shift = -1) {
    MoveWide(rd, imm, shift, MOVK);
  }

  // Move inverted immediate.
  void movn(const Register& rd, uint64_t imm, int shift = -1) {
    MoveWide(rd, imm, shift, MOVN);
  }

  // Move immediate.
  void movz(const Register& rd, uint64_t imm, int shift = -1) {
    MoveWide(rd, imm, shift, MOVZ);
  }

  // Misc instructions.
  // Monitor debug-mode breakpoint.
  void brk(int code);

  // Halting debug-mode breakpoint.
  void hlt(int code);

  // Move register to register.
  void mov(const Register& rd, const Register& rn);

  // Move inverted operand to register.
  void mvn(const Register& rd, const Operand& operand);

  // System instructions.
  // Move to register from system register.
  void mrs(const Register& rt, SystemRegister sysreg);

  // Move from register to system register.
  void msr(SystemRegister sysreg, const Register& rt);

  // System hint.
  void hint(SystemHint code);

  // Clear exclusive monitor.
  void clrex(int imm4 = 0xf);

  // Data memory barrier.
  void dmb(BarrierDomain domain, BarrierType type);

  // Data synchronization barrier.
  void dsb(BarrierDomain domain, BarrierType type);

  // Instruction synchronization barrier.
  void isb();

  // Alias for system instructions.
  // No-op.
  void nop() {
    hint(NOP);
  }

  // FP instructions.
  // Move double precision immediate to FP register.
  void fmov(const FPRegister& fd, double imm);

  // Move single precision immediate to FP register.
  void fmov(const FPRegister& fd, float imm);

  // Move FP register to register.
  void fmov(const Register& rd, const FPRegister& fn);

  // Move register to FP register.
  void fmov(const FPRegister& fd, const Register& rn);

  // Move FP register to FP register.
  void fmov(const FPRegister& fd, const FPRegister& fn);

  // FP add.
  void fadd(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP subtract.
  void fsub(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP multiply.
  void fmul(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP fused multiply and add.
  void fmadd(const FPRegister& fd,
             const FPRegister& fn,
             const FPRegister& fm,
             const FPRegister& fa);

  // FP fused multiply and subtract.
  void fmsub(const FPRegister& fd,
             const FPRegister& fn,
             const FPRegister& fm,
             const FPRegister& fa);

  // FP fused multiply, add and negate.
  void fnmadd(const FPRegister& fd,
              const FPRegister& fn,
              const FPRegister& fm,
              const FPRegister& fa);

  // FP fused multiply, subtract and negate.
  void fnmsub(const FPRegister& fd,
              const FPRegister& fn,
              const FPRegister& fm,
              const FPRegister& fa);

  // FP divide.
  void fdiv(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP maximum.
  void fmax(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP minimum.
  void fmin(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP maximum number.
  void fmaxnm(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP minimum number.
  void fminnm(const FPRegister& fd, const FPRegister& fn, const FPRegister& fm);

  // FP absolute.
  void fabs(const FPRegister& fd, const FPRegister& fn);

  // FP negate.
  void fneg(const FPRegister& fd, const FPRegister& fn);

  // FP square root.
  void fsqrt(const FPRegister& fd, const FPRegister& fn);

  // FP round to integer (nearest with ties to away).
  void frinta(const FPRegister& fd, const FPRegister& fn);

  // FP round to integer (toward minus infinity).
  void frintm(const FPRegister& fd, const FPRegister& fn);

  // FP round to integer (nearest with ties to even).
  void frintn(const FPRegister& fd, const FPRegister& fn);

  // FP round to integer (towards zero).
  void frintz(const FPRegister& fd, const FPRegister& fn);

  // FP compare registers.
  void fcmp(const FPRegister& fn, const FPRegister& fm);

  // FP compare immediate.
  void fcmp(const FPRegister& fn, double value);

  // FP conditional compare.
  void fccmp(const FPRegister& fn,
             const FPRegister& fm,
             StatusFlags nzcv,
             Condition cond);

  // FP conditional select.
  void fcsel(const FPRegister& fd,
             const FPRegister& fn,
             const FPRegister& fm,
             Condition cond);

  // Common FP Convert function.
  void FPConvertToInt(const Register& rd,
                      const FPRegister& fn,
                      FPIntegerConvertOp op);

  // FP convert between single and double precision.
  void fcvt(const FPRegister& fd, const FPRegister& fn);

  // Convert FP to signed integer (nearest with ties to away).
  void fcvtas(const Register& rd, const FPRegister& fn);

  // Convert FP to unsigned integer (nearest with ties to away).
  void fcvtau(const Register& rd, const FPRegister& fn);

  // Convert FP to signed integer (round towards -infinity).
  void fcvtms(const Register& rd, const FPRegister& fn);

  // Convert FP to unsigned integer (round towards -infinity).
  void fcvtmu(const Register& rd, const FPRegister& fn);

  // Convert FP to signed integer (nearest with ties to even).
  void fcvtns(const Register& rd, const FPRegister& fn);

  // Convert FP to unsigned integer (nearest with ties to even).
  void fcvtnu(const Register& rd, const FPRegister& fn);

  // Convert FP to signed integer (round towards zero).
  void fcvtzs(const Register& rd, const FPRegister& fn);

  // Convert FP to unsigned integer (round towards zero).
  void fcvtzu(const Register& rd, const FPRegister& fn);

  // Convert signed integer or fixed point to FP.
  void scvtf(const FPRegister& fd, const Register& rn, unsigned fbits = 0);

  // Convert unsigned integer or fixed point to FP.
  void ucvtf(const FPRegister& fd, const Register& rn, unsigned fbits = 0);

  // Emit generic instructions.
  // Emit raw instructions into the instruction stream.
  inline void dci(Instr raw_inst) { Emit(raw_inst); }

  // Emit 32 bits of data into the instruction stream.
  inline void dc32(uint32_t data) {
    VIXL_ASSERT(buffer_monitor_ > 0);
    buffer_->Emit32(data);
  }

  // Emit 64 bits of data into the instruction stream.
  inline void dc64(uint64_t data) {
    VIXL_ASSERT(buffer_monitor_ > 0);
    buffer_->Emit64(data);
  }

  // Copy a string into the instruction stream, including the terminating NULL
  // character. The instruction pointer is then aligned correctly for
  // subsequent instructions.
  void EmitString(const char * string) {
    VIXL_ASSERT(string != NULL);
    VIXL_ASSERT(buffer_monitor_ > 0);

    buffer_->EmitString(string);
    buffer_->Align();
  }

  // Code generation helpers.

  // Register encoding.
  static Instr Rd(CPURegister rd) {
    VIXL_ASSERT(rd.code() != kSPRegInternalCode);
    return rd.code() << Rd_offset;
  }

  static Instr Rn(CPURegister rn) {
    VIXL_ASSERT(rn.code() != kSPRegInternalCode);
    return rn.code() << Rn_offset;
  }

  static Instr Rm(CPURegister rm) {
    VIXL_ASSERT(rm.code() != kSPRegInternalCode);
    return rm.code() << Rm_offset;
  }

  static Instr Ra(CPURegister ra) {
    VIXL_ASSERT(ra.code() != kSPRegInternalCode);
    return ra.code() << Ra_offset;
  }

  static Instr Rt(CPURegister rt) {
    VIXL_ASSERT(rt.code() != kSPRegInternalCode);
    return rt.code() << Rt_offset;
  }

  static Instr Rt2(CPURegister rt2) {
    VIXL_ASSERT(rt2.code() != kSPRegInternalCode);
    return rt2.code() << Rt2_offset;
  }

  static Instr Rs(CPURegister rs) {
    VIXL_ASSERT(rs.code() != kSPRegInternalCode);
    return rs.code() << Rs_offset;
  }

  // These encoding functions allow the stack pointer to be encoded, and
  // disallow the zero register.
  static Instr RdSP(Register rd) {
    VIXL_ASSERT(!rd.IsZero());
    return (rd.code() & kRegCodeMask) << Rd_offset;
  }

  static Instr RnSP(Register rn) {
    VIXL_ASSERT(!rn.IsZero());
    return (rn.code() & kRegCodeMask) << Rn_offset;
  }

  // Flags encoding.
  static Instr Flags(FlagsUpdate S) {
    if (S == SetFlags) {
      return 1 << FlagsUpdate_offset;
    } else if (S == LeaveFlags) {
      return 0 << FlagsUpdate_offset;
    }
    VIXL_UNREACHABLE();
    return 0;
  }

  static Instr Cond(Condition cond) {
    return cond << Condition_offset;
  }

  // PC-relative address encoding.
  static Instr ImmPCRelAddress(int imm21) {
    VIXL_ASSERT(is_int21(imm21));
    Instr imm = static_cast<Instr>(truncate_to_int21(imm21));
    Instr immhi = (imm >> ImmPCRelLo_width) << ImmPCRelHi_offset;
    Instr immlo = imm << ImmPCRelLo_offset;
    return (immhi & ImmPCRelHi_mask) | (immlo & ImmPCRelLo_mask);
  }

  // Branch encoding.
  static Instr ImmUncondBranch(int imm26) {
    VIXL_ASSERT(is_int26(imm26));
    return truncate_to_int26(imm26) << ImmUncondBranch_offset;
  }

  static Instr ImmCondBranch(int imm19) {
    VIXL_ASSERT(is_int19(imm19));
    return truncate_to_int19(imm19) << ImmCondBranch_offset;
  }

  static Instr ImmCmpBranch(int imm19) {
    VIXL_ASSERT(is_int19(imm19));
    return truncate_to_int19(imm19) << ImmCmpBranch_offset;
  }

  static Instr ImmTestBranch(int imm14) {
    VIXL_ASSERT(is_int14(imm14));
    return truncate_to_int14(imm14) << ImmTestBranch_offset;
  }

  static Instr ImmTestBranchBit(unsigned bit_pos) {
    VIXL_ASSERT(is_uint6(bit_pos));
    // Subtract five from the shift offset, as we need bit 5 from bit_pos.
    unsigned b5 = bit_pos << (ImmTestBranchBit5_offset - 5);
    unsigned b40 = bit_pos << ImmTestBranchBit40_offset;
    b5 &= ImmTestBranchBit5_mask;
    b40 &= ImmTestBranchBit40_mask;
    return b5 | b40;
  }

  // Data Processing encoding.
  static Instr SF(Register rd) {
      return rd.Is64Bits() ? SixtyFourBits : ThirtyTwoBits;
  }

  static Instr ImmAddSub(int64_t imm) {
    VIXL_ASSERT(IsImmAddSub(imm));
    if (is_uint12(imm)) {  // No shift required.
      return imm << ImmAddSub_offset;
    } else {
      return ((imm >> 12) << ImmAddSub_offset) | (1 << ShiftAddSub_offset);
    }
  }

  static inline Instr ImmS(unsigned imms, unsigned reg_size) {
    VIXL_ASSERT(((reg_size == kXRegSize) && is_uint6(imms)) ||
           ((reg_size == kWRegSize) && is_uint5(imms)));
    USE(reg_size);
    return imms << ImmS_offset;
  }

  static inline Instr ImmR(unsigned immr, unsigned reg_size) {
    VIXL_ASSERT(((reg_size == kXRegSize) && is_uint6(immr)) ||
           ((reg_size == kWRegSize) && is_uint5(immr)));
    USE(reg_size);
    VIXL_ASSERT(is_uint6(immr));
    return immr << ImmR_offset;
  }

  static inline Instr ImmSetBits(unsigned imms, unsigned reg_size) {
    VIXL_ASSERT((reg_size == kWRegSize) || (reg_size == kXRegSize));
    VIXL_ASSERT(is_uint6(imms));
    VIXL_ASSERT((reg_size == kXRegSize) || is_uint6(imms + 3));
    USE(reg_size);
    return imms << ImmSetBits_offset;
  }

  static inline Instr ImmRotate(unsigned immr, unsigned reg_size) {
    VIXL_ASSERT((reg_size == kWRegSize) || (reg_size == kXRegSize));
    VIXL_ASSERT(((reg_size == kXRegSize) && is_uint6(immr)) ||
           ((reg_size == kWRegSize) && is_uint5(immr)));
    USE(reg_size);
    return immr << ImmRotate_offset;
  }

  static inline Instr ImmLLiteral(int imm19) {
    VIXL_ASSERT(is_int19(imm19));
    return truncate_to_int19(imm19) << ImmLLiteral_offset;
  }

  static inline Instr BitN(unsigned bitn, unsigned reg_size) {
    VIXL_ASSERT((reg_size == kWRegSize) || (reg_size == kXRegSize));
    VIXL_ASSERT((reg_size == kXRegSize) || (bitn == 0));
    USE(reg_size);
    return bitn << BitN_offset;
  }

  static Instr ShiftDP(Shift shift) {
    VIXL_ASSERT(shift == LSL || shift == LSR || shift == ASR || shift == ROR);
    return shift << ShiftDP_offset;
  }

  static Instr ImmDPShift(unsigned amount) {
    VIXL_ASSERT(is_uint6(amount));
    return amount << ImmDPShift_offset;
  }

  static Instr ExtendMode(Extend extend) {
    return extend << ExtendMode_offset;
  }

  static Instr ImmExtendShift(unsigned left_shift) {
    VIXL_ASSERT(left_shift <= 4);
    return left_shift << ImmExtendShift_offset;
  }

  static Instr ImmCondCmp(unsigned imm) {
    VIXL_ASSERT(is_uint5(imm));
    return imm << ImmCondCmp_offset;
  }

  static Instr Nzcv(StatusFlags nzcv) {
    return ((nzcv >> Flags_offset) & 0xf) << Nzcv_offset;
  }

  // MemOperand offset encoding.
  static Instr ImmLSUnsigned(int imm12) {
    VIXL_ASSERT(is_uint12(imm12));
    return imm12 << ImmLSUnsigned_offset;
  }

  static Instr ImmLS(int imm9) {
    VIXL_ASSERT(is_int9(imm9));
    return truncate_to_int9(imm9) << ImmLS_offset;
  }

  static Instr ImmLSPair(int imm7, LSDataSize size) {
    VIXL_ASSERT(((imm7 >> size) << size) == imm7);
    int scaled_imm7 = imm7 >> size;
    VIXL_ASSERT(is_int7(scaled_imm7));
    return truncate_to_int7(scaled_imm7) << ImmLSPair_offset;
  }

  static Instr ImmShiftLS(unsigned shift_amount) {
    VIXL_ASSERT(is_uint1(shift_amount));
    return shift_amount << ImmShiftLS_offset;
  }

  static Instr ImmException(int imm16) {
    VIXL_ASSERT(is_uint16(imm16));
    return imm16 << ImmException_offset;
  }

  static Instr ImmSystemRegister(int imm15) {
    VIXL_ASSERT(is_uint15(imm15));
    return imm15 << ImmSystemRegister_offset;
  }

  static Instr ImmHint(int imm7) {
    VIXL_ASSERT(is_uint7(imm7));
    return imm7 << ImmHint_offset;
  }

  static Instr CRm(int imm4) {
    VIXL_ASSERT(is_uint4(imm4));
    return imm4 << CRm_offset;
  }

  static Instr ImmBarrierDomain(int imm2) {
    VIXL_ASSERT(is_uint2(imm2));
    return imm2 << ImmBarrierDomain_offset;
  }

  static Instr ImmBarrierType(int imm2) {
    VIXL_ASSERT(is_uint2(imm2));
    return imm2 << ImmBarrierType_offset;
  }

  static LSDataSize CalcLSDataSize(LoadStoreOp op) {
    VIXL_ASSERT((SizeLS_offset + SizeLS_width) == (kInstructionSize * 8));
    return static_cast<LSDataSize>(op >> SizeLS_offset);
  }

  // Move immediates encoding.
  static Instr ImmMoveWide(uint64_t imm) {
    VIXL_ASSERT(is_uint16(imm));
    return imm << ImmMoveWide_offset;
  }

  static Instr ShiftMoveWide(int64_t shift) {
    VIXL_ASSERT(is_uint2(shift));
    return shift << ShiftMoveWide_offset;
  }

  // FP Immediates.
  static Instr ImmFP32(float imm);
  static Instr ImmFP64(double imm);

  // FP register type.
  static Instr FPType(FPRegister fd) {
    return fd.Is64Bits() ? FP64 : FP32;
  }

  static Instr FPScale(unsigned scale) {
    VIXL_ASSERT(is_uint6(scale));
    return scale << FPScale_offset;
  }

  // Size of the code generated since label to the current position.
  size_t SizeOfCodeGeneratedSince(Label* label) const {
    VIXL_ASSERT(label->IsBound());
    return buffer_->OffsetFrom(label->location());
  }

  size_t BufferCapacity() const { return buffer_->capacity(); }

  size_t RemainingBufferSpace() const { return buffer_->RemainingBytes(); }

  void EnsureSpaceFor(size_t amount) {
    if (buffer_->RemainingBytes() < amount) {
      size_t capacity = buffer_->capacity();
      size_t size = buffer_->CursorOffset();
      do {
        // TODO(all): refine.
        capacity *= 2;
      } while ((capacity - size) <  amount);
      buffer_->Grow(capacity);
    }
  }

#ifdef DEBUG
  void AcquireBuffer() {
    VIXL_ASSERT(buffer_monitor_ >= 0);
    buffer_monitor_++;
  }

  void ReleaseBuffer() {
    buffer_monitor_--;
    VIXL_ASSERT(buffer_monitor_ >= 0);
  }
#endif

  inline PositionIndependentCodeOption pic() {
    return pic_;
  }

  inline bool AllowPageOffsetDependentCode() {
    return (pic() == PageOffsetDependentCode) ||
           (pic() == PositionDependentCode);
  }

  static inline const Register& AppropriateZeroRegFor(const CPURegister& reg) {
    return reg.Is64Bits() ? xzr : wzr;
  }


 protected:
  void LoadStore(const CPURegister& rt,
                 const MemOperand& addr,
                 LoadStoreOp op,
                 LoadStoreScalingOption option = PreferScaledOffset);
  static bool IsImmLSUnscaled(int64_t offset);
  static bool IsImmLSScaled(int64_t offset, LSDataSize size);

  void LoadStorePair(const CPURegister& rt,
                     const CPURegister& rt2,
                     const MemOperand& addr,
                     LoadStorePairOp op);
  static bool IsImmLSPair(int64_t offset, LSDataSize size);

  // TODO(all): The third parameter should be passed by reference but gcc 4.8.2
  // reports a bogus uninitialised warning then.
  void Logical(const Register& rd,
               const Register& rn,
               const Operand operand,
               LogicalOp op);
  void LogicalImmediate(const Register& rd,
                        const Register& rn,
                        unsigned n,
                        unsigned imm_s,
                        unsigned imm_r,
                        LogicalOp op);
  static bool IsImmLogical(uint64_t value,
                           unsigned width,
                           unsigned* n = NULL,
                           unsigned* imm_s = NULL,
                           unsigned* imm_r = NULL);

  void ConditionalCompare(const Register& rn,
                          const Operand& operand,
                          StatusFlags nzcv,
                          Condition cond,
                          ConditionalCompareOp op);
  static bool IsImmConditionalCompare(int64_t immediate);

  void AddSubWithCarry(const Register& rd,
                       const Register& rn,
                       const Operand& operand,
                       FlagsUpdate S,
                       AddSubWithCarryOp op);

  static bool IsImmFP32(float imm);
  static bool IsImmFP64(double imm);

  // Functions for emulating operands not directly supported by the instruction
  // set.
  void EmitShift(const Register& rd,
                 const Register& rn,
                 Shift shift,
                 unsigned amount);
  void EmitExtendShift(const Register& rd,
                       const Register& rn,
                       Extend extend,
                       unsigned left_shift);

  void AddSub(const Register& rd,
              const Register& rn,
              const Operand& operand,
              FlagsUpdate S,
              AddSubOp op);
  static bool IsImmAddSub(int64_t immediate);

  // Find an appropriate LoadStoreOp or LoadStorePairOp for the specified
  // registers. Only simple loads are supported; sign- and zero-extension (such
  // as in LDPSW_x or LDRB_w) are not supported.
  static LoadStoreOp LoadOpFor(const CPURegister& rt);
  static LoadStorePairOp LoadPairOpFor(const CPURegister& rt,
                                       const CPURegister& rt2);
  static LoadStoreOp StoreOpFor(const CPURegister& rt);
  static LoadStorePairOp StorePairOpFor(const CPURegister& rt,
                                        const CPURegister& rt2);
  static LoadStorePairNonTemporalOp LoadPairNonTemporalOpFor(
    const CPURegister& rt, const CPURegister& rt2);
  static LoadStorePairNonTemporalOp StorePairNonTemporalOpFor(
    const CPURegister& rt, const CPURegister& rt2);
  static LoadLiteralOp LoadLiteralOpFor(const CPURegister& rt);


 private:
  // Instruction helpers.
  void MoveWide(const Register& rd,
                uint64_t imm,
                int shift,
                MoveWideImmediateOp mov_op);
  void DataProcShiftedRegister(const Register& rd,
                               const Register& rn,
                               const Operand& operand,
                               FlagsUpdate S,
                               Instr op);
  void DataProcExtendedRegister(const Register& rd,
                                const Register& rn,
                                const Operand& operand,
                                FlagsUpdate S,
                                Instr op);
  void LoadStorePairNonTemporal(const CPURegister& rt,
                                const CPURegister& rt2,
                                const MemOperand& addr,
                                LoadStorePairNonTemporalOp op);
  void LoadLiteral(const CPURegister& rt, uint64_t imm, LoadLiteralOp op);
  void ConditionalSelect(const Register& rd,
                         const Register& rn,
                         const Register& rm,
                         Condition cond,
                         ConditionalSelectOp op);
  void DataProcessing1Source(const Register& rd,
                             const Register& rn,
                             DataProcessing1SourceOp op);
  void DataProcessing3Source(const Register& rd,
                             const Register& rn,
                             const Register& rm,
                             const Register& ra,
                             DataProcessing3SourceOp op);
  void FPDataProcessing1Source(const FPRegister& fd,
                               const FPRegister& fn,
                               FPDataProcessing1SourceOp op);
  void FPDataProcessing2Source(const FPRegister& fd,
                               const FPRegister& fn,
                               const FPRegister& fm,
                               FPDataProcessing2SourceOp op);
  void FPDataProcessing3Source(const FPRegister& fd,
                               const FPRegister& fn,
                               const FPRegister& fm,
                               const FPRegister& fa,
                               FPDataProcessing3SourceOp op);

  // Link the current (not-yet-emitted) instruction to the specified label, then
  // return an offset to be encoded in the instruction. If the label is not yet
  // bound, an offset of 0 is returned.
  ptrdiff_t LinkAndGetByteOffsetTo(Label * label);
  ptrdiff_t LinkAndGetInstructionOffsetTo(Label * label);
  ptrdiff_t LinkAndGetPageOffsetTo(Label * label);

  // A common implementation for the LinkAndGet<Type>OffsetTo helpers.
  template <int element_shift>
  ptrdiff_t LinkAndGetOffsetTo(Label* label);

  // Literal load offset are in words (32-bit).
  ptrdiff_t LinkAndGetWordOffsetTo(RawLiteral* literal);

  // Emit the instruction in buffer_.
  void Emit(Instr instruction) {
    VIXL_STATIC_ASSERT(sizeof(instruction) == kInstructionSize);
    VIXL_ASSERT(buffer_monitor_ > 0);
    buffer_->Emit32(instruction);
  }

  // Buffer where the code is emitted.
  CodeBuffer* buffer_;
  PositionIndependentCodeOption pic_;

#ifdef DEBUG
  int64_t buffer_monitor_;
#endif
};


// All Assembler emits MUST acquire/release the underlying code buffer. The
// helper scope below will do so and optionally ensure the buffer is big enough
// to receive the emit. It is possible to request the scope not to perform any
// checks (kNoCheck) if for example it is known in advance the buffer size is
// adequate or there is some other size checking mechanism in place.
class CodeBufferCheckScope {
 public:
  // Tell whether or not the scope needs to ensure the associated CodeBuffer
  // has enough space for the requested size.
  enum CheckPolicy {
    kNoCheck,
    kCheck
  };

  // Tell whether or not the scope should assert the amount of code emitted
  // within the scope is consistent with the requested amount.
  enum AssertPolicy {
    kNoAssert,    // No assert required.
    kExactSize,   // The code emitted must be exactly size bytes.
    kMaximumSize  // The code emitted must be at most size bytes.
  };

  CodeBufferCheckScope(Assembler* assm,
                       size_t size,
                       CheckPolicy check_policy = kCheck,
                       AssertPolicy assert_policy = kMaximumSize)
      : assm_(assm) {
    if (check_policy == kCheck) assm->EnsureSpaceFor(size);
#ifdef DEBUG
    assm->bind(&start_);
    size_ = size;
    assert_policy_ = assert_policy;
    assm->AcquireBuffer();
#else
    USE(assert_policy);
#endif
  }

  // This is a shortcut for CodeBufferCheckScope(assm, 0, kNoCheck, kNoAssert).
  explicit CodeBufferCheckScope(Assembler* assm) : assm_(assm) {
#ifdef DEBUG
    size_ = 0;
    assert_policy_ = kNoAssert;
    assm->AcquireBuffer();
#endif
  }

  ~CodeBufferCheckScope() {
#ifdef DEBUG
    assm_->ReleaseBuffer();
    switch (assert_policy_) {
      case kNoAssert: break;
      case kExactSize:
        VIXL_ASSERT(assm_->SizeOfCodeGeneratedSince(&start_) == size_);
        break;
      case kMaximumSize:
        VIXL_ASSERT(assm_->SizeOfCodeGeneratedSince(&start_) <= size_);
        break;
      default:
        VIXL_UNREACHABLE();
    }
#endif
  }

 protected:
  Assembler* assm_;
#ifdef DEBUG
  Label start_;
  size_t size_;
  AssertPolicy assert_policy_;
#endif
};

}  // namespace vixl

#endif  // VIXL_A64_ASSEMBLER_A64_H_
