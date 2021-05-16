// Copyright 2014, ARM Limited
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

#ifndef VIXL_CODE_BUFFER_H
#define VIXL_CODE_BUFFER_H

#include <cstring>
#include "vixl/globals.h"

namespace vixl {

class CodeBuffer {
 public:
  explicit CodeBuffer(size_t capacity = 4 * KBytes);
  CodeBuffer(void* buffer, size_t capacity);
  ~CodeBuffer();

  void Reset();

  ptrdiff_t OffsetFrom(ptrdiff_t offset) const {
    ptrdiff_t cursor_offset = cursor_ - buffer_;
    VIXL_ASSERT((offset >= 0) && (offset <= cursor_offset));
    return cursor_offset - offset;
  }

  ptrdiff_t CursorOffset() const {
    return OffsetFrom(0);
  }

  template <typename T>
  T GetOffsetAddress(ptrdiff_t offset) const {
    VIXL_ASSERT((offset >= 0) && (offset <= (cursor_ - buffer_)));
    return reinterpret_cast<T>(buffer_ + offset);
  }

  size_t RemainingBytes() const {
    VIXL_ASSERT((cursor_ >= buffer_) && (cursor_ <= (buffer_ + capacity_)));
    return (buffer_ + capacity_) - cursor_;
  }

  // A code buffer can emit:
  //  * 32-bit data: instruction and constant.
  //  * 64-bit data: constant.
  //  * string: debug info.
  void Emit32(uint32_t data) { Emit(data); }

  void Emit64(uint64_t data) { Emit(data); }

  void EmitString(const char* string);

  // Align to kInstructionSize.
  void Align();

  size_t capacity() const { return capacity_; }

  bool IsManaged() const { return managed_; }

  void Grow(size_t new_capacity);

  bool IsDirty() const { return dirty_; }

  void SetClean() { dirty_ = false; }

 private:
  template <typename T>
  void Emit(T value) {
    VIXL_ASSERT(RemainingBytes() >= sizeof(value));
    dirty_ = true;
    memcpy(cursor_, &value, sizeof(value));
    cursor_ += sizeof(value);
  }

  // Backing store of the buffer.
  byte* buffer_;
  // If true the backing store is allocated and deallocated by the buffer. The
  // backing store can then grow on demand. If false the backing store is
  // provided by the user and cannot be resized internally.
  bool managed_;
  // Pointer to the next location to be written.
  byte* cursor_;
  // True if there has been any write since the buffer was created or cleaned.
  bool dirty_;
  // Capacity in bytes of the backing store.
  size_t capacity_;
};

}  // namespace vixl

#endif  // VIXL_CODE_BUFFER_H

