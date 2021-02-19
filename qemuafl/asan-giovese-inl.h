/*******************************************************************************
BSD 2-Clause License

Copyright (c) 2020-2021, Andrea Fioraldi
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include "asan-giovese.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <assert.h>

#define DEFAULT_REDZONE_SIZE 128

// ------------------------------------------------------------------------- //
// Alloc
// ------------------------------------------------------------------------- //

#include "interval-tree/rbtree.h"
#include "interval-tree/interval_tree_generic.h"

// TODO use a mutex for locking insert/delete

struct alloc_tree_node {

  struct rb_node    rb;
  struct chunk_info ckinfo;
  target_ulong      __subtree_last;

};

#define START(node) ((node)->ckinfo.start)
#define LAST(node) ((node)->ckinfo.end)

INTERVAL_TREE_DEFINE(struct alloc_tree_node, rb, target_ulong, __subtree_last,
                     START, LAST, static, alloc_tree)

static struct rb_root root = RB_ROOT;

struct chunk_info* asan_giovese_alloc_search(target_ulong query) {

  struct alloc_tree_node* node = alloc_tree_iter_first(&root, query, query);
  if (node) return &node->ckinfo;
  return NULL;

}

void asan_giovese_alloc_insert(target_ulong start, target_ulong end,
                               struct call_context* alloc_ctx) {

  struct alloc_tree_node* prev_node = alloc_tree_iter_first(&root, start, end);
  while (prev_node) {

    struct alloc_tree_node* n = alloc_tree_iter_next(prev_node, start, end);
    free(prev_node->ckinfo.alloc_ctx);
    free(prev_node->ckinfo.free_ctx);
    alloc_tree_remove(prev_node, &root);
    prev_node = n;

  }

  struct alloc_tree_node* node = calloc(sizeof(struct alloc_tree_node), 1);
  node->ckinfo.start = start;
  node->ckinfo.end = end;
  node->ckinfo.alloc_ctx = alloc_ctx;
  alloc_tree_insert(node, &root);

}

// ------------------------------------------------------------------------- //
// Init
// ------------------------------------------------------------------------- //

void* __ag_high_shadow = HIGH_SHADOW_ADDR;
void* __ag_low_shadow = LOW_SHADOW_ADDR;

void asan_giovese_init(void) {

#if UINTPTR_MAX == 0xffffffff
  fprintf(stderr, "ERROR: Cannot allocate sanitizer shadow memory on 32 bit "
                  "platforms.");
  exit(1);
#else
  assert(mmap(__ag_high_shadow, HIGH_SHADOW_SIZE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE | MAP_ANON, -1,
              0) != MAP_FAILED);

  assert(mmap(__ag_low_shadow, LOW_SHADOW_SIZE, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE | MAP_ANON, -1,
              0) != MAP_FAILED);

  assert(mmap(GAP_SHADOW_ADDR, GAP_SHADOW_SIZE, PROT_NONE,
              MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE | MAP_ANON, -1,
              0) != MAP_FAILED);
#endif

}

// ------------------------------------------------------------------------- //
// Checks
// ------------------------------------------------------------------------- //

int asan_giovese_load1(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  int8_t    k = *shadow_addr;
  return k != 0 && (intptr_t)((h & 7) + 1) > k;

}

int asan_giovese_load2(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  int8_t    k = *shadow_addr;
  return k != 0 && (intptr_t)((h & 7) + 2) > k;

}

int asan_giovese_load4(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  int8_t    k = *shadow_addr;
  return k != 0 && (intptr_t)((h & 7) + 4) > k;

}

int asan_giovese_load8(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  return (*shadow_addr);

}

int asan_giovese_store1(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  int8_t    k = *shadow_addr;
  return k != 0 && (intptr_t)((h & 7) + 1) > k;

}

int asan_giovese_store2(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  int8_t    k = *shadow_addr;
  return k != 0 && (intptr_t)((h & 7) + 2) > k;

}

int asan_giovese_store4(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  int8_t    k = *shadow_addr;
  return k != 0 && (intptr_t)((h & 7) + 4) > k;

}

int asan_giovese_store8(void* ptr) {

  uintptr_t h = (uintptr_t)ptr;
  int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
  return (*shadow_addr);

}

int asan_giovese_loadN(void* ptr, size_t n) {

  if (!n) return 0;

  uintptr_t start = (uintptr_t)ptr;
  uintptr_t end = start + n;
  uintptr_t last_8 = end & ~7;

  if (start & 0x7) {

    uintptr_t next_8 = (start & ~7) + 8;
    size_t       first_size = next_8 - start;

    if (n <= first_size) {

      uintptr_t h = start;
      int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
      int8_t    k = *shadow_addr;
      return k != 0 && ((intptr_t)((h & 7) + n) > k);

    }

    uintptr_t h = start;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    if (k != 0 && ((intptr_t)((h & 7) + first_size) > k)) return 1;

    start = next_8;

  }

  while (start < last_8) {

    uintptr_t h = start;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    if (*shadow_addr) return 1;
    start += 8;

  }

  if (last_8 != end) {

    uintptr_t h = start;
    size_t    last_size = end - last_8;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    return k != 0 && ((intptr_t)((h & 7) + last_size) > k);

  }

  return 0;

}

int asan_giovese_storeN(void* ptr, size_t n) {

  if (!n) return 0;

  uintptr_t start = (uintptr_t)ptr;
  uintptr_t end = start + n;
  uintptr_t last_8 = end & ~7;

  if (start & 0x7) {

    uintptr_t next_8 = (start & ~7) + 8;
    size_t       first_size = next_8 - start;

    if (n <= first_size) {

      uintptr_t h = start;
      int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
      int8_t    k = *shadow_addr;
      return k != 0 && ((intptr_t)((h & 7) + n) > k);

    }

    uintptr_t h = start;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    if (k != 0 && ((intptr_t)((h & 7) + first_size) > k)) return 1;

    start = next_8;

  }

  while (start < last_8) {

    uintptr_t h = start;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    if (*shadow_addr) return 1;
    start += 8;

  }

  if (last_8 != end) {

    uintptr_t h = start;
    size_t    last_size = end - last_8;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    return k != 0 && ((intptr_t)((h & 7) + last_size) > k);

  }

  return 0;

}

int asan_giovese_guest_loadN(target_ulong addr, size_t n) {

  if (!n) return 0;

  target_ulong start = addr;
  target_ulong end = start + n;
  target_ulong last_8 = end & ~7;

  if (start & 0x7) {

    target_ulong next_8 = (start & ~7) + 8;
    size_t       first_size = next_8 - start;

    if (n <= first_size) {

      uintptr_t h = (uintptr_t)AFL_G2H(start);
      int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
      int8_t    k = *shadow_addr;
      return k != 0 && ((intptr_t)((h & 7) + n) > k);

    }

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    if (k != 0 && ((intptr_t)((h & 7) + first_size) > k)) return 1;

    start = next_8;

  }

  while (start < last_8) {

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    if (*shadow_addr) return 1;
    start += 8;

  }

  if (last_8 != end) {

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    size_t    last_size = end - last_8;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    return k != 0 && ((intptr_t)((h & 7) + last_size) > k);

  }

  return 0;

}

int asan_giovese_guest_storeN(target_ulong addr, size_t n) {

  if (!n) return 0;

  target_ulong start = addr;
  target_ulong end = start + n;
  target_ulong last_8 = end & ~7;

  if (start & 0x7) {

    target_ulong next_8 = (start & ~7) + 8;
    size_t       first_size = next_8 - start;

    if (n <= first_size) {

      uintptr_t h = (uintptr_t)AFL_G2H(start);
      int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
      int8_t    k = *shadow_addr;
      return k != 0 && ((intptr_t)((h & 7) + n) > k);

    }

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    if (k != 0 && ((intptr_t)((h & 7) + first_size) > k)) return 1;

    start = next_8;

  }

  while (start < last_8) {

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    if (*shadow_addr) return 1;
    start += 8;

  }

  if (last_8 != end) {

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    size_t    last_size = end - last_8;
    int8_t*   shadow_addr = (int8_t*)(h >> 3) + SHADOW_OFFSET;
    int8_t    k = *shadow_addr;
    return k != 0 && ((intptr_t)((h & 7) + last_size) > k);

  }

  return 0;

}

// ------------------------------------------------------------------------- //
// Poison
// ------------------------------------------------------------------------- //

int asan_giovese_poison_region(void* ptr, size_t n,
                               uint8_t poison_byte) {

  if (!n) return 0;

  uintptr_t start = (uintptr_t)ptr;
  uintptr_t end = start + n;
  uintptr_t last_8 = end & ~7;

  if (start & 0x7) {

    target_ulong next_8 = (start & ~7) + 8;
    size_t       first_size = next_8 - start;

    if (n < first_size) return 0;

    uintptr_t h = start;
    uint8_t*  shadow_addr = (uint8_t*)(h >> 3) + SHADOW_OFFSET;
    *shadow_addr = 8 - first_size;

    start = next_8;

  }

  while (start < last_8) {

    uintptr_t h = start;
    uint8_t*  shadow_addr = (uint8_t*)(h >> 3) + SHADOW_OFFSET;
    *shadow_addr = poison_byte;
    start += 8;

  }

  return 1;

}

int asan_giovese_user_poison_region(void* ptr, size_t n) {

  return asan_giovese_poison_region(ptr, n, ASAN_USER);

}

int asan_giovese_unpoison_region(void* ptr, size_t n) {

  target_ulong start = (uintptr_t)ptr;
  target_ulong end = start + n;

  while (start < end) {

    uintptr_t h = start;
    uint8_t*  shadow_addr = (uint8_t*)(h >> 3) + SHADOW_OFFSET;
    *shadow_addr = 0;
    start += 8;

  }

  return 1;

}

int asan_giovese_poison_guest_region(target_ulong addr, size_t n,
                                     uint8_t poison_byte) {

  if (!n) return 0;
  
  target_ulong start = addr;
  target_ulong end = start + n;
  target_ulong last_8 = end & ~7;
  
  if (start & 0x7) {

    target_ulong next_8 = (start & ~7) + 8;
    size_t       first_size = next_8 - start;

    if (n < first_size) return 0;

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    uint8_t*  shadow_addr = (uint8_t*)(h >> 3) + SHADOW_OFFSET;
    *shadow_addr = 8 - first_size;

    start = next_8;

  }

  while (start < last_8) {

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    uint8_t*  shadow_addr = (uint8_t*)(h >> 3) + SHADOW_OFFSET;
    *shadow_addr = poison_byte;
    start += 8;

  }

  return 1;

}

int asan_giovese_user_poison_guest_region(target_ulong addr, size_t n) {

  return asan_giovese_poison_guest_region(addr, n, ASAN_USER);

}

int asan_giovese_unpoison_guest_region(target_ulong addr, size_t n) {

  target_ulong start = addr;
  target_ulong end = start + n;

  while (start < end) {

    uintptr_t h = (uintptr_t)AFL_G2H(start);
    uint8_t*  shadow_addr = (uint8_t*)(h >> 3) + SHADOW_OFFSET;
    *shadow_addr = 0;
    start += 8;

  }

  return 1;

}


// ------------------------------------------------------------------------- //
// Report
// ------------------------------------------------------------------------- //

// from https://gist.github.com/RabaDabaDoba/145049536f815903c79944599c6f952a

// Regular text
#define ANSI_COLOR_BLK "\e[0;30m"
#define ANSI_COLOR_RED "\e[0;31m"
#define ANSI_COLOR_GRN "\e[0;32m"
#define ANSI_COLOR_YEL "\e[0;33m"
#define ANSI_COLOR_BLU "\e[0;34m"
#define ANSI_COLOR_MAG "\e[0;35m"
#define ANSI_COLOR_CYN "\e[0;36m"
#define ANSI_COLOR_WHT "\e[0;37m"

// High intensty text
#define ANSI_COLOR_HBLK "\e[0;90m"
#define ANSI_COLOR_HRED "\e[0;91m"
#define ANSI_COLOR_HGRN "\e[0;92m"
#define ANSI_COLOR_HYEL "\e[0;93m"
#define ANSI_COLOR_HBLU "\e[0;94m"
#define ANSI_COLOR_HMAG "\e[0;95m"
#define ANSI_COLOR_HCYN "\e[0;96m"
#define ANSI_COLOR_HWHT "\e[0;97m"

// Reset
#define ANSI_COLOR_RESET "\e[0m"

static const char* shadow_color_map[] = {

    "" /* 0x0 */,
    "" /* 0x1 */,
    "" /* 0x2 */,
    "" /* 0x3 */,
    "" /* 0x4 */,
    "" /* 0x5 */,
    "" /* 0x6 */,
    "" /* 0x7 */,
    "" /* 0x8 */,
    "" /* 0x9 */,
    "" /* 0xa */,
    "" /* 0xb */,
    "" /* 0xc */,
    "" /* 0xd */,
    "" /* 0xe */,
    "" /* 0xf */,
    "" /* 0x10 */,
    "" /* 0x11 */,
    "" /* 0x12 */,
    "" /* 0x13 */,
    "" /* 0x14 */,
    "" /* 0x15 */,
    "" /* 0x16 */,
    "" /* 0x17 */,
    "" /* 0x18 */,
    "" /* 0x19 */,
    "" /* 0x1a */,
    "" /* 0x1b */,
    "" /* 0x1c */,
    "" /* 0x1d */,
    "" /* 0x1e */,
    "" /* 0x1f */,
    "" /* 0x20 */,
    "" /* 0x21 */,
    "" /* 0x22 */,
    "" /* 0x23 */,
    "" /* 0x24 */,
    "" /* 0x25 */,
    "" /* 0x26 */,
    "" /* 0x27 */,
    "" /* 0x28 */,
    "" /* 0x29 */,
    "" /* 0x2a */,
    "" /* 0x2b */,
    "" /* 0x2c */,
    "" /* 0x2d */,
    "" /* 0x2e */,
    "" /* 0x2f */,
    "" /* 0x30 */,
    "" /* 0x31 */,
    "" /* 0x32 */,
    "" /* 0x33 */,
    "" /* 0x34 */,
    "" /* 0x35 */,
    "" /* 0x36 */,
    "" /* 0x37 */,
    "" /* 0x38 */,
    "" /* 0x39 */,
    "" /* 0x3a */,
    "" /* 0x3b */,
    "" /* 0x3c */,
    "" /* 0x3d */,
    "" /* 0x3e */,
    "" /* 0x3f */,
    "" /* 0x40 */,
    "" /* 0x41 */,
    "" /* 0x42 */,
    "" /* 0x43 */,
    "" /* 0x44 */,
    "" /* 0x45 */,
    "" /* 0x46 */,
    "" /* 0x47 */,
    "" /* 0x48 */,
    "" /* 0x49 */,
    "" /* 0x4a */,
    "" /* 0x4b */,
    "" /* 0x4c */,
    "" /* 0x4d */,
    "" /* 0x4e */,
    "" /* 0x4f */,
    "" /* 0x50 */,
    "" /* 0x51 */,
    "" /* 0x52 */,
    "" /* 0x53 */,
    "" /* 0x54 */,
    "" /* 0x55 */,
    "" /* 0x56 */,
    "" /* 0x57 */,
    "" /* 0x58 */,
    "" /* 0x59 */,
    "" /* 0x5a */,
    "" /* 0x5b */,
    "" /* 0x5c */,
    "" /* 0x5d */,
    "" /* 0x5e */,
    "" /* 0x5f */,
    "" /* 0x60 */,
    "" /* 0x61 */,
    "" /* 0x62 */,
    "" /* 0x63 */,
    "" /* 0x64 */,
    "" /* 0x65 */,
    "" /* 0x66 */,
    "" /* 0x67 */,
    "" /* 0x68 */,
    "" /* 0x69 */,
    "" /* 0x6a */,
    "" /* 0x6b */,
    "" /* 0x6c */,
    "" /* 0x6d */,
    "" /* 0x6e */,
    "" /* 0x6f */,
    "" /* 0x70 */,
    "" /* 0x71 */,
    "" /* 0x72 */,
    "" /* 0x73 */,
    "" /* 0x74 */,
    "" /* 0x75 */,
    "" /* 0x76 */,
    "" /* 0x77 */,
    "" /* 0x78 */,
    "" /* 0x79 */,
    "" /* 0x7a */,
    "" /* 0x7b */,
    "" /* 0x7c */,
    "" /* 0x7d */,
    "" /* 0x7e */,
    "" /* 0x7f */,
    "" /* 0x80 */,
    "" /* 0x81 */,
    "" /* 0x82 */,
    "" /* 0x83 */,
    "" /* 0x84 */,
    "" /* 0x85 */,
    "" /* 0x86 */,
    "" /* 0x87 */,
    "" /* 0x88 */,
    "" /* 0x89 */,
    "" /* 0x8a */,
    "" /* 0x8b */,
    "" /* 0x8c */,
    "" /* 0x8d */,
    "" /* 0x8e */,
    "" /* 0x8f */,
    "" /* 0x90 */,
    "" /* 0x91 */,
    "" /* 0x92 */,
    "" /* 0x93 */,
    "" /* 0x94 */,
    "" /* 0x95 */,
    "" /* 0x96 */,
    "" /* 0x97 */,
    "" /* 0x98 */,
    "" /* 0x99 */,
    "" /* 0x9a */,
    "" /* 0x9b */,
    "" /* 0x9c */,
    "" /* 0x9d */,
    "" /* 0x9e */,
    "" /* 0x9f */,
    "" /* 0xa0 */,
    "" /* 0xa1 */,
    "" /* 0xa2 */,
    "" /* 0xa3 */,
    "" /* 0xa4 */,
    "" /* 0xa5 */,
    "" /* 0xa6 */,
    "" /* 0xa7 */,
    "" /* 0xa8 */,
    "" /* 0xa9 */,
    "" /* 0xaa */,
    "" /* 0xab */,
    ANSI_COLOR_HRED /* 0xac */,
    "" /* 0xad */,
    "" /* 0xae */,
    "" /* 0xaf */,
    "" /* 0xb0 */,
    "" /* 0xb1 */,
    "" /* 0xb2 */,
    "" /* 0xb3 */,
    "" /* 0xb4 */,
    "" /* 0xb5 */,
    "" /* 0xb6 */,
    "" /* 0xb7 */,
    "" /* 0xb8 */,
    "" /* 0xb9 */,
    "" /* 0xba */,
    ANSI_COLOR_HYEL /* 0xbb */,
    "" /* 0xbc */,
    "" /* 0xbd */,
    "" /* 0xbe */,
    "" /* 0xbf */,
    "" /* 0xc0 */,
    "" /* 0xc1 */,
    "" /* 0xc2 */,
    "" /* 0xc3 */,
    "" /* 0xc4 */,
    "" /* 0xc5 */,
    "" /* 0xc6 */,
    "" /* 0xc7 */,
    "" /* 0xc8 */,
    "" /* 0xc9 */,
    ANSI_COLOR_HBLU /* 0xca */,
    ANSI_COLOR_HBLU /* 0xcb */,
    "" /* 0xcc */,
    "" /* 0xcd */,
    "" /* 0xce */,
    "" /* 0xcf */,
    "" /* 0xd0 */,
    "" /* 0xd1 */,
    "" /* 0xd2 */,
    "" /* 0xd3 */,
    "" /* 0xd4 */,
    "" /* 0xd5 */,
    "" /* 0xd6 */,
    "" /* 0xd7 */,
    "" /* 0xd8 */,
    "" /* 0xd9 */,
    "" /* 0xda */,
    "" /* 0xdb */,
    "" /* 0xdc */,
    "" /* 0xdd */,
    "" /* 0xde */,
    "" /* 0xdf */,
    "" /* 0xe0 */,
    "" /* 0xe1 */,
    "" /* 0xe2 */,
    "" /* 0xe3 */,
    "" /* 0xe4 */,
    "" /* 0xe5 */,
    "" /* 0xe6 */,
    "" /* 0xe7 */,
    "" /* 0xe8 */,
    "" /* 0xe9 */,
    "" /* 0xea */,
    "" /* 0xeb */,
    "" /* 0xec */,
    "" /* 0xed */,
    "" /* 0xee */,
    "" /* 0xef */,
    "" /* 0xf0 */,
    ANSI_COLOR_HRED /* 0xf1 */,
    ANSI_COLOR_HRED /* 0xf2 */,
    ANSI_COLOR_HRED /* 0xf3 */,
    "" /* 0xf4 */,
    ANSI_COLOR_HMAG /* 0xf5 */,
    ANSI_COLOR_HCYN /* 0xf6 */,
    ANSI_COLOR_HBLU /* 0xf7 */,
    ANSI_COLOR_HMAG /* 0xf8 */,
    ANSI_COLOR_HRED /* 0xf9 */,
    ANSI_COLOR_HRED /* 0xfa */,
    ANSI_COLOR_HRED /* 0xfb */,
    ANSI_COLOR_HBLU /* 0xfc */,
    ANSI_COLOR_HMAG /* 0xfd */,
    ANSI_COLOR_HYEL /* 0xfe */,
    ""                                                              /* 0xff */

};

static const char* access_type_str[] = {"READ", "WRITE"};

static const char* poisoned_strerror(uint8_t poison_byte) {

  switch (poison_byte) {

    case ASAN_HEAP_RZ:
    case ASAN_HEAP_LEFT_RZ:
    case ASAN_HEAP_RIGHT_RZ: return "heap-buffer-overflow";
    case ASAN_HEAP_FREED: return "heap-use-after-free";

  }

  return "use-after-poison";

}

static int poisoned_find_error(target_ulong addr, size_t n,
                               target_ulong* fault_addr,
                               const char**  err_string) {

  target_ulong start = addr;
  target_ulong end = start + n;
  int          have_partials = 0;

  while (start < end) {

    uintptr_t rs = (uintptr_t)AFL_G2H(start);
    int8_t*   shadow_addr = (int8_t*)(rs >> 3) + SHADOW_OFFSET;
    switch (*shadow_addr) {

      case ASAN_VALID: have_partials = 0; break;
      case ASAN_PARTIAL1:
      case ASAN_PARTIAL2:
      case ASAN_PARTIAL3:
      case ASAN_PARTIAL4:
      case ASAN_PARTIAL5:
      case ASAN_PARTIAL6:
      case ASAN_PARTIAL7: {

        have_partials = 1;
        target_ulong a = (start & ~7) + *shadow_addr;
        if (*fault_addr == 0 && a >= start && a < end) *fault_addr = a;
        break;

      }

      default: {

        if (*fault_addr == 0) *fault_addr = start;
        *err_string = poisoned_strerror(*shadow_addr);
        return 1;

      }

    }

    start += 8;

  }

  if (have_partials) {

    uintptr_t rs = (uintptr_t)AFL_G2H((end & ~7) + 8);
    uint8_t*  last_shadow_addr = (uint8_t*)(rs >> 3) + SHADOW_OFFSET;
    *err_string = poisoned_strerror(*last_shadow_addr);
    return 1;

  }

  if (*fault_addr == 0) *fault_addr = addr;
  *err_string = "use-after-poison";
  return 1;

}

#define _MEM2SHADOW(x) ((uint8_t*)((uintptr_t)AFL_G2H(x) >> 3) + SHADOW_OFFSET)

#define _MEM2SHADOWPRINT(x) shadow_color_map[*_MEM2SHADOW(x)], *_MEM2SHADOW(x)

static int print_shadow_line(target_ulong addr) {

  fprintf(stderr,
          "  0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
          " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
          " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
          " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
          " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
          " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
          " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
          " "
          "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
          " %s%02x" ANSI_COLOR_RESET "\n",
          (uintptr_t)_MEM2SHADOW(addr), _MEM2SHADOWPRINT(addr),
          _MEM2SHADOWPRINT(addr + 8), _MEM2SHADOWPRINT(addr + 16),
          _MEM2SHADOWPRINT(addr + 24), _MEM2SHADOWPRINT(addr + 32),
          _MEM2SHADOWPRINT(addr + 40), _MEM2SHADOWPRINT(addr + 48),
          _MEM2SHADOWPRINT(addr + 56), _MEM2SHADOWPRINT(addr + 64),
          _MEM2SHADOWPRINT(addr + 72), _MEM2SHADOWPRINT(addr + 80),
          _MEM2SHADOWPRINT(addr + 88), _MEM2SHADOWPRINT(addr + 96),
          _MEM2SHADOWPRINT(addr + 104), _MEM2SHADOWPRINT(addr + 112),
          _MEM2SHADOWPRINT(addr + 120));

  return 1;

}

static int print_shadow_line_fault(target_ulong addr, target_ulong fault_addr) {

  int         i = (fault_addr - addr) / 8;
  const char* format =
      "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
      " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
      " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
      " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
      " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
      " "
      "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
      " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
      " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
  switch (i) {

    case 0:
      format = "=>0x%012" PRIxPTR ":[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 1:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               "[%s%02x" ANSI_COLOR_RESET "]%s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 2:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 3:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               "[%s%02x" ANSI_COLOR_RESET "]%s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 4:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 5:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               "[%s%02x" ANSI_COLOR_RESET "]%s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 6:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 7:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               "[%s%02x" ANSI_COLOR_RESET "]%s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 8:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 9:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               "[%s%02x" ANSI_COLOR_RESET "]%s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 10:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 11:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 12:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 13:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET
               "]%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 14:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               "[%s%02x" ANSI_COLOR_RESET "]%s%02x" ANSI_COLOR_RESET "\n";
      break;
    case 15:
      format = "=>0x%012" PRIxPTR ": %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET
               " "
               "%s%02x" ANSI_COLOR_RESET " %s%02x" ANSI_COLOR_RESET
               " %s%02x" ANSI_COLOR_RESET "[%s%02x" ANSI_COLOR_RESET "]\n";
      break;

  }

  fprintf(stderr, format, (uintptr_t)_MEM2SHADOW(addr), _MEM2SHADOWPRINT(addr),
          _MEM2SHADOWPRINT(addr + 8), _MEM2SHADOWPRINT(addr + 16),
          _MEM2SHADOWPRINT(addr + 24), _MEM2SHADOWPRINT(addr + 32),
          _MEM2SHADOWPRINT(addr + 40), _MEM2SHADOWPRINT(addr + 48),
          _MEM2SHADOWPRINT(addr + 56), _MEM2SHADOWPRINT(addr + 64),
          _MEM2SHADOWPRINT(addr + 72), _MEM2SHADOWPRINT(addr + 80),
          _MEM2SHADOWPRINT(addr + 88), _MEM2SHADOWPRINT(addr + 96),
          _MEM2SHADOWPRINT(addr + 104), _MEM2SHADOWPRINT(addr + 112),
          _MEM2SHADOWPRINT(addr + 120));

  return 1;

}

#undef _MEM2SHADOW
#undef _MEM2SHADOWPRINT

static void print_shadow(target_ulong addr) {

  target_ulong center = addr & ~127;
  print_shadow_line(center - 16 * 8 * 5);
  print_shadow_line(center - 16 * 8 * 4);
  print_shadow_line(center - 16 * 8 * 3);
  print_shadow_line(center - 16 * 8 * 2);
  print_shadow_line(center - 16 * 8);
  print_shadow_line_fault(center, addr);
  print_shadow_line(center + 16 * 8);
  print_shadow_line(center + 16 * 8 * 2);
  print_shadow_line(center + 16 * 8 * 3);
  print_shadow_line(center + 16 * 8 * 4);
  print_shadow_line(center + 16 * 8 * 5);

}

static void print_alloc_location_chunk(struct chunk_info* ckinfo,
                                       target_ulong       fault_addr) {

  if (fault_addr >= ckinfo->start && fault_addr < ckinfo->end)
    fprintf(stderr,
            ANSI_COLOR_HGRN
            "0x" TARGET_FMT_lx " is located " TARGET_FMT_ld
            " bytes inside of " TARGET_FMT_ld "-byte region [0x"
            TARGET_FMT_lx ",0x" TARGET_FMT_lx ")" ANSI_COLOR_RESET "\n",
            fault_addr, fault_addr - ckinfo->start, ckinfo->end - ckinfo->start,
            ckinfo->start, ckinfo->end);
  else if (ckinfo->start >= fault_addr)
    fprintf(stderr,
            ANSI_COLOR_HGRN
            "0x" TARGET_FMT_lx " is located " TARGET_FMT_ld
            " bytes to the left of " TARGET_FMT_ld "-byte region [0x"
            TARGET_FMT_lx ",0x" TARGET_FMT_lx ")" ANSI_COLOR_RESET "\n",
            fault_addr, ckinfo->start - fault_addr, ckinfo->end - ckinfo->start,
            ckinfo->start, ckinfo->end);
  else
    fprintf(stderr,
            ANSI_COLOR_HGRN
            "0x" TARGET_FMT_lx " is located " TARGET_FMT_ld
            " bytes to the right of " TARGET_FMT_ld "-byte region [0x"
            TARGET_FMT_lx ",0x" TARGET_FMT_lx ")" ANSI_COLOR_RESET "\n",
            fault_addr, fault_addr - ckinfo->end, ckinfo->end - ckinfo->start,
            ckinfo->start, ckinfo->end);

  if (ckinfo->free_ctx) {

    fprintf(stderr,
            ANSI_COLOR_HMAG "freed by thread T%d here:" ANSI_COLOR_RESET "\n",
            ckinfo->free_ctx->tid);
    size_t i;
    for (i = 0; i < ckinfo->free_ctx->size; ++i) {

      char* printable = asan_giovese_printaddr(ckinfo->free_ctx->addresses[i]);
      if (printable)
        fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "%s\n", i,
                ckinfo->free_ctx->addresses[i], printable);
      else
        fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "\n", i,
                ckinfo->free_ctx->addresses[i]);
    }

    fputc('\n', stderr);

    fprintf(stderr,
            ANSI_COLOR_HMAG
            "previously allocated by thread T%d here:" ANSI_COLOR_RESET "\n",
            ckinfo->free_ctx->tid);

  } else

    fprintf(stderr,
            ANSI_COLOR_HMAG "allocated by thread T%d here:" ANSI_COLOR_RESET
                            "\n",
            ckinfo->alloc_ctx->tid);

  size_t i;
  for (i = 0; i < ckinfo->alloc_ctx->size; ++i) {

    char* printable = asan_giovese_printaddr(ckinfo->alloc_ctx->addresses[i]);
    if (printable)
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "%s\n", i,
              ckinfo->alloc_ctx->addresses[i], printable);
    else
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "\n", i,
              ckinfo->alloc_ctx->addresses[i]);

  }

  fputc('\n', stderr);

}

static void print_alloc_location(target_ulong addr, target_ulong fault_addr) {

  struct chunk_info* ckinfo = asan_giovese_alloc_search(fault_addr);
  if (!ckinfo && addr != fault_addr) ckinfo = asan_giovese_alloc_search(addr);

  if (ckinfo) {

    print_alloc_location_chunk(ckinfo, fault_addr);
    return;

  }

  int i = 0;
  while (!ckinfo && i < DEFAULT_REDZONE_SIZE)
    ckinfo = asan_giovese_alloc_search(fault_addr - (i++));
  if (ckinfo) {

    print_alloc_location_chunk(ckinfo, fault_addr);
    return;

  }

  i = 0;
  while (!ckinfo && i < DEFAULT_REDZONE_SIZE)
    ckinfo = asan_giovese_alloc_search(fault_addr + (i++));
  if (ckinfo) {

    print_alloc_location_chunk(ckinfo, fault_addr);
    return;

  }

  fprintf(stderr, "Address 0x" TARGET_FMT_lx " is a wild pointer.\n",
          fault_addr);

}

int asan_giovese_report_and_crash(int access_type, target_ulong addr, size_t n,
                                  target_ulong pc, target_ulong bp,
                                  target_ulong sp) {

  struct call_context ctx;
  asan_giovese_populate_context(&ctx, pc);
  target_ulong fault_addr = 0;
  const char*  error_type;

  if (!poisoned_find_error(addr, n, &fault_addr, &error_type)) return 0;
  
  fprintf(stderr,
          "=================================================================\n"
          ANSI_COLOR_HRED "==%d==ERROR: " ASAN_NAME_STR ": %s on address 0x"
          TARGET_FMT_lx " at pc 0x" TARGET_FMT_lx " bp 0x" TARGET_FMT_lx
          " sp 0x" TARGET_FMT_lx ANSI_COLOR_RESET "\n",
          getpid(), error_type, addr, pc, bp, sp);

  fprintf(stderr,
          ANSI_COLOR_HBLU "%s of size %zu at 0x" TARGET_FMT_lx " thread T%d"
          ANSI_COLOR_RESET "\n",
          access_type_str[access_type], n, addr, ctx.tid);
  size_t i;
  for (i = 0; i < ctx.size; ++i) {

    char* printable = asan_giovese_printaddr(ctx.addresses[i]);
    if (printable)
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "%s\n", i, ctx.addresses[i],
              printable);
    else
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "\n", i, ctx.addresses[i]);

  }

  fputc('\n', stderr);

  print_alloc_location(addr, fault_addr);

  const char* printable_pc = asan_giovese_printaddr(pc);
  if (!printable_pc) printable_pc = "";
  fprintf(stderr,
          "SUMMARY: " ASAN_NAME_STR
          ": %s%s\n"
          "Shadow bytes around the buggy address:\n",
          error_type, printable_pc);

  print_shadow(fault_addr);

  fprintf(
      stderr,
      "Shadow byte legend (one shadow byte represents 8 application bytes):\n"
      "  Addressable:           00\n"
      "  Partially addressable: 01 02 03 04 05 06 07\n"
      "  Heap left redzone:       " ANSI_COLOR_HRED "fa" ANSI_COLOR_RESET "\n"
      "  Heap right redzone:      " ANSI_COLOR_HRED "fb" ANSI_COLOR_RESET "\n"
      "  Freed heap region:       " ANSI_COLOR_HMAG "fd" ANSI_COLOR_RESET "\n"
      //"  Stack left redzone:      " ANSI_COLOR_HRED "f1" ANSI_COLOR_RESET "\n"
      //"  Stack mid redzone:       " ANSI_COLOR_HRED "f2" ANSI_COLOR_RESET "\n"
      //"  Stack right redzone:     " ANSI_COLOR_HRED "f3" ANSI_COLOR_RESET "\n"
      //"  Stack after return:      " ANSI_COLOR_HMAG "f5" ANSI_COLOR_RESET "\n"
      //"  Stack use after scope:   " ANSI_COLOR_HMAG "f8" ANSI_COLOR_RESET "\n"
      //"  Global redzone:          " ANSI_COLOR_HRED "f9" ANSI_COLOR_RESET "\n"
      //"  Global init order:       " ANSI_COLOR_HCYN "f6" ANSI_COLOR_RESET "\n"
      "  Poisoned by user:        " ANSI_COLOR_HBLU "f7" ANSI_COLOR_RESET "\n"
      //"  Container overflow:      " ANSI_COLOR_HBLU "fc" ANSI_COLOR_RESET "\n"
      //"  Array cookie:            " ANSI_COLOR_HRED "ac" ANSI_COLOR_RESET "\n"
      //"  Intra object redzone:    " ANSI_COLOR_HYEL "bb" ANSI_COLOR_RESET "\n"
      "  ASan internal:           " ANSI_COLOR_HYEL "fe" ANSI_COLOR_RESET "\n"
      //"  Left alloca redzone:     " ANSI_COLOR_HBLU "ca" ANSI_COLOR_RESET "\n"
      //"  Right alloca redzone:    " ANSI_COLOR_HBLU "cb" ANSI_COLOR_RESET "\n"
      "  Shadow gap:              cc\n"
      "==%d==ABORTING\n",
      getpid());

  signal(SIGABRT, SIG_DFL);
  abort();

}

static const char* singal_to_string[] = {
    [SIGHUP] = "HUP",
    [SIGINT] = "INT",
    [SIGQUIT] = "QUIT",
    [SIGILL] = "ILL",
    [SIGTRAP] = "TRAP",
    [SIGABRT] = "ABRT",
    [SIGBUS] = "BUS",
    [SIGFPE] = "FPE",
    [SIGKILL] = "KILL",
    [SIGUSR1] = "USR1",
    [SIGSEGV] = "SEGV",
    [SIGUSR2] = "USR2",
    [SIGPIPE] = "PIPE",
    [SIGALRM] = "ALRM",
    [SIGTERM] = "TERM",
#ifdef SIGSTKFLT
    [SIGSTKFLT] = "STKFLT",
#endif
    [SIGCHLD] = "CHLD",
    [SIGCONT] = "CONT",
    [SIGSTOP] = "STOP",
    [SIGTSTP] = "TSTP",
    [SIGTTIN] = "TTIN",
    [SIGTTOU] = "TTOU",
    [SIGURG] = "URG",
    [SIGXCPU] = "XCPU",
    [SIGXFSZ] = "XFSZ",
    [SIGVTALRM] = "VTALRM",
    [SIGPROF] = "PROF",
    [SIGWINCH] = "WINCH",
    [SIGIO] = "IO",
    [SIGPWR] = "PWR",
    [SIGSYS] = "SYS",
};

int asan_giovese_deadly_signal(int signum, target_ulong addr, target_ulong pc, target_ulong bp, target_ulong sp) {

  struct call_context ctx;
  asan_giovese_populate_context(&ctx, pc);
  const char* error_type = singal_to_string[signum];

  fprintf(stderr,
          ASAN_NAME_STR ":DEADLYSIGNAL\n"
          "=================================================================\n"
          ANSI_COLOR_HRED "==%d==ERROR: " ASAN_NAME_STR
          ": %s on unknown address 0x" TARGET_FMT_lx " (pc 0x" TARGET_FMT_lx
          " bp 0x" TARGET_FMT_lx " sp 0x" TARGET_FMT_lx " T%d)" ANSI_COLOR_RESET
          "\n",
          getpid(), error_type, addr, pc, bp, sp, ctx.tid);

  size_t i;
  for (i = 0; i < ctx.size; ++i) {

    char* printable = asan_giovese_printaddr(ctx.addresses[i]);
    if (printable)
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "%s\n", i, ctx.addresses[i],
              printable);
    else
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "\n", i, ctx.addresses[i]);

  }
  
  fputc('\n', stderr);
  fprintf(stderr, ASAN_NAME_STR " can not provide additional info.\n");
  
  const char* printable_pc = asan_giovese_printaddr(pc);
  if (!printable_pc) printable_pc = "";
  fprintf(stderr,
          "SUMMARY: " ASAN_NAME_STR
          ": %s\n", printable_pc);

  fprintf(stderr, "==%d==ABORTING\n", getpid());
  return signum;

}

int asan_giovese_badfree(target_ulong addr, target_ulong pc) {

  struct call_context ctx;
  asan_giovese_populate_context(&ctx, pc);

  fprintf(stderr,
          "================================================================="
          "\n" ANSI_COLOR_HRED "==%d==ERROR: " ASAN_NAME_STR
          ": attempting free on address which was not malloc()-ed: 0x"
          TARGET_FMT_lx " in thread T%d" ANSI_COLOR_RESET "\n", getpid(), addr,
          ctx.tid);

  size_t i;
  for (i = 0; i < ctx.size; ++i) {

    char* printable = asan_giovese_printaddr(ctx.addresses[i]);
    if (printable)
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "%s\n", i, ctx.addresses[i],
              printable);
    else
      fprintf(stderr, "    #%zu 0x" TARGET_FMT_lx "\n", i, ctx.addresses[i]);

  }
  
  fputc('\n', stderr);
  print_alloc_location(addr, addr);
  
  const char* printable_pc = asan_giovese_printaddr(pc);
  if (!printable_pc) printable_pc = "";
  fprintf(stderr,
          "SUMMARY: " ASAN_NAME_STR
          ": bad-free %s\n", printable_pc);

  fprintf(stderr, "==%d==ABORTING\n", getpid());
  signal(SIGABRT, SIG_DFL);
  abort();

}

