/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/tb-lookup.h"
#include "disas/disas.h"
#include "exec/log.h"
#include "tcg/tcg.h"

#include "qemuafl/common.h"

uint32_t afl_hash_ip(uint64_t);

void HELPER(afl_entry_routine)(CPUArchState *env) {

  afl_forkserver(env_cpu(env));

}

void HELPER(afl_persistent_routine)(CPUArchState *env) {

  afl_persistent_loop(env);

}

void HELPER(afl_compcov_16)(target_ulong cur_loc, target_ulong arg1,
                            target_ulong arg2) {

  register uintptr_t idx = cur_loc;

  if ((arg1 & 0xff00) == (arg2 & 0xff00)) { INC_AFL_AREA(idx); }

}

void HELPER(afl_compcov_32)(target_ulong cur_loc, target_ulong arg1,
                            target_ulong arg2) {

  register uintptr_t idx = cur_loc;

  if ((arg1 & 0xff000000) == (arg2 & 0xff000000)) {

    INC_AFL_AREA(idx + 2);
    if ((arg1 & 0xff0000) == (arg2 & 0xff0000)) {

      INC_AFL_AREA(idx + 1);
      if ((arg1 & 0xff00) == (arg2 & 0xff00)) { INC_AFL_AREA(idx); }

    }

  }

}

void HELPER(afl_compcov_64)(target_ulong cur_loc, target_ulong arg1,
                            target_ulong arg2) {

  register uintptr_t idx = cur_loc;

  if ((arg1 & 0xff00000000000000) == (arg2 & 0xff00000000000000)) {

    INC_AFL_AREA(idx + 6);
    if ((arg1 & 0xff000000000000) == (arg2 & 0xff000000000000)) {

      INC_AFL_AREA(idx + 5);
      if ((arg1 & 0xff0000000000) == (arg2 & 0xff0000000000)) {

        INC_AFL_AREA(idx + 4);
        if ((arg1 & 0xff00000000) == (arg2 & 0xff00000000)) {

          INC_AFL_AREA(idx + 3);
          if ((arg1 & 0xff000000) == (arg2 & 0xff000000)) {

            INC_AFL_AREA(idx + 2);
            if ((arg1 & 0xff0000) == (arg2 & 0xff0000)) {

              INC_AFL_AREA(idx + 1);
              if ((arg1 & 0xff00) == (arg2 & 0xff00)) { INC_AFL_AREA(idx); }

            }

          }

        }

      }

    }

  }

}

void HELPER(afl_cmplog_8)(target_ulong cur_loc, target_ulong arg1,
                          target_ulong arg2) {

  register uintptr_t k = (uintptr_t)cur_loc;
  u32 hits = 0;

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_INS)
    __afl_cmp_map->headers[k].hits = 0;

  if (__afl_cmp_map->headers[k].hits == 0) {

    __afl_cmp_map->headers[k].type = CMP_TYPE_INS;
    __afl_cmp_map->headers[k].shape = 0;

  } else {

    hits = __afl_cmp_map->headers[k].hits;

  }

  __afl_cmp_map->headers[k].hits = hits + 1;

  hits &= CMP_MAP_H - 1;
  __afl_cmp_map->log[k][hits].v0 = arg1;
  __afl_cmp_map->log[k][hits].v1 = arg2;

}

void HELPER(afl_cmplog_16)(target_ulong cur_loc, target_ulong arg1,
                           target_ulong arg2) {

  register uintptr_t k = (uintptr_t)cur_loc;
  u32 hits = 0;

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_INS)
    __afl_cmp_map->headers[k].hits = 0;

  if (__afl_cmp_map->headers[k].hits == 0) {

    __afl_cmp_map->headers[k].type = CMP_TYPE_INS;
    __afl_cmp_map->headers[k].shape = 1;

  } else {

    hits = __afl_cmp_map->headers[k].hits;

  }

  __afl_cmp_map->headers[k].hits = hits + 1;

  hits &= CMP_MAP_H - 1;
  __afl_cmp_map->log[k][hits].v0 = arg1;
  __afl_cmp_map->log[k][hits].v1 = arg2;

}

void HELPER(afl_cmplog_32)(target_ulong cur_loc, target_ulong arg1,
                           target_ulong arg2) {

  register uintptr_t k = (uintptr_t)cur_loc;
  u32 hits = 0;

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_INS)
    __afl_cmp_map->headers[k].hits = 0;

  if (__afl_cmp_map->headers[k].hits == 0) {

    __afl_cmp_map->headers[k].type = CMP_TYPE_INS;
    __afl_cmp_map->headers[k].shape = 3;

  } else {

    hits = __afl_cmp_map->headers[k].hits;

  }

  __afl_cmp_map->headers[k].hits = hits + 1;

  hits &= CMP_MAP_H - 1;
  __afl_cmp_map->log[k][hits].v0 = arg1;
  __afl_cmp_map->log[k][hits].v1 = arg2;

}

void HELPER(afl_cmplog_64)(target_ulong cur_loc, target_ulong arg1,
                           target_ulong arg2) {

  register uintptr_t k = (uintptr_t)cur_loc;
  u32 hits = 0;

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_INS)
    __afl_cmp_map->headers[k].hits = 0;

  if (__afl_cmp_map->headers[k].hits == 0) {

    __afl_cmp_map->headers[k].type = CMP_TYPE_INS;
    __afl_cmp_map->headers[k].shape = 7;

  } else {

    hits = __afl_cmp_map->headers[k].hits;

  }

  __afl_cmp_map->headers[k].hits = hits + 1;

  hits &= CMP_MAP_H - 1;
  __afl_cmp_map->log[k][hits].v0 = arg1;
  __afl_cmp_map->log[k][hits].v1 = arg2;

}

#include <sys/mman.h>
#include "linux-user/qemu.h" /* access_ok decls. */

/*
static int area_is_mapped(void *ptr, size_t len) {

  char *p = ptr;
  char *page = (char *)((uintptr_t)p & ~(sysconf(_SC_PAGE_SIZE) - 1));

  int r = msync(page, (p - page) + len, MS_ASYNC);
  if (r < 0) return errno != ENOMEM;
  return 1;

}
*/

void HELPER(afl_cmplog_rtn)(CPUArchState *env) {

#if defined(TARGET_X86_64)

  target_ulong arg1 = env->regs[R_EDI];
  target_ulong arg2 = env->regs[R_ESI];

#elif defined(TARGET_I386)

  target_ulong *stack = AFL_G2H(env->regs[R_ESP]);
  
  if (!access_ok(env_cpu(env), VERIFY_READ, env->regs[R_ESP],
                 sizeof(target_ulong) * 2))
    return;

  // when this hook is executed, the retaddr is not on stack yet
  target_ulong arg1 = stack[0];
  target_ulong arg2 = stack[1];

#else

  // stupid code to make it compile
  target_ulong arg1 = 0;
  target_ulong arg2 = 0;
  return;

#endif

  if (!access_ok(env_cpu(env), VERIFY_READ, arg1, 0x20) ||
      !access_ok(env_cpu(env), VERIFY_READ, arg2, 0x20))
    return;

  void *ptr1 = AFL_G2H(arg1);
  void *ptr2 = AFL_G2H(arg2);

#if defined(TARGET_X86_64) || defined(TARGET_I386)
  uintptr_t k = (uintptr_t)env->eip;
#else
  uintptr_t k = 0;
#endif

  k = (uintptr_t)(afl_hash_ip((uint64_t)k));
  k &= (CMP_MAP_W - 1);

  u32 hits = 0;

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_RTN) {
    __afl_cmp_map->headers[k].type = CMP_TYPE_RTN;
    __afl_cmp_map->headers[k].hits = 0;
    __afl_cmp_map->headers[k].shape = 30;
  } else {
    hits = __afl_cmp_map->headers[k].hits;
  }

  __afl_cmp_map->headers[k].hits += 1;

  hits &= CMP_MAP_RTN_H - 1;
  ((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v0_len = 31;
  ((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v1_len = 31;
  __builtin_memcpy(((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v0,
                   ptr1, 31);
  __builtin_memcpy(((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v1,
                   ptr2, 31);

}

/* 32-bit helpers */

int32_t HELPER(div_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 / arg2;
}

int32_t HELPER(rem_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 % arg2;
}

uint32_t HELPER(divu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 / arg2;
}

uint32_t HELPER(remu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 % arg2;
}

/* 64-bit helpers */

uint64_t HELPER(shl_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 << arg2;
}

uint64_t HELPER(shr_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(sar_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(div_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 / arg2;
}

int64_t HELPER(rem_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(divu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 / arg2;
}

uint64_t HELPER(remu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(muluh_i64)(uint64_t arg1, uint64_t arg2)
{
    uint64_t l, h;
    mulu64(&l, &h, arg1, arg2);
    return h;
}

int64_t HELPER(mulsh_i64)(int64_t arg1, int64_t arg2)
{
    uint64_t l, h;
    muls64(&l, &h, arg1, arg2);
    return h;
}

uint32_t HELPER(clz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? clz32(arg) : zero_val;
}

uint32_t HELPER(ctz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? ctz32(arg) : zero_val;
}

uint64_t HELPER(clz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? clz64(arg) : zero_val;
}

uint64_t HELPER(ctz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? ctz64(arg) : zero_val;
}

uint32_t HELPER(clrsb_i32)(uint32_t arg)
{
    return clrsb32(arg);
}

uint64_t HELPER(clrsb_i64)(uint64_t arg)
{
    return clrsb64(arg);
}

uint32_t HELPER(ctpop_i32)(uint32_t arg)
{
    return ctpop32(arg);
}

uint64_t HELPER(ctpop_i64)(uint64_t arg)
{
    return ctpop64(arg);
}

const void *HELPER(lookup_tb_ptr)(CPUArchState *env)
{
    CPUState *cpu = env_cpu(env);
    TranslationBlock *tb;
    target_ulong cs_base, pc;
    uint32_t flags;

    tb = tb_lookup__cpu_state(cpu, &pc, &cs_base, &flags, curr_cflags());
    if (tb == NULL) {
        return tcg_code_gen_epilogue;
    }
    qemu_log_mask_and_addr(CPU_LOG_EXEC, pc,
                           "Chain %d: %p ["
                           TARGET_FMT_lx "/" TARGET_FMT_lx "/%#x] %s\n",
                           cpu->cpu_index, tb->tc.ptr, cs_base, pc, flags,
                           lookup_symbol(pc));
    return tb->tc.ptr;
}

void HELPER(exit_atomic)(CPUArchState *env)
{
    cpu_loop_exit_atomic(env_cpu(env), GETPC());
}

/////////////////////////////////////////////////
//                   QASAN
/////////////////////////////////////////////////

#include "qemuafl/qasan-qemu.h"

// options
int qasan_max_call_stack = 16; // QASAN_MAX_CALL_STACK
int qasan_symbolize = 1; // QASAN_SYMBOLIZE
int use_qasan = 0;

__thread int qasan_disabled;

__thread struct shadow_stack qasan_shadow_stack;

#ifdef ASAN_GIOVESE

#ifndef DO_NOT_USE_QASAN

#include "qemuafl/asan-giovese-inl.h"

#include <sys/types.h>
#include <sys/syscall.h>

void asan_giovese_populate_context(struct call_context* ctx, target_ulong pc) {

  ctx->size = MIN(qasan_shadow_stack.size, qasan_max_call_stack -1) +1;
  ctx->addresses = calloc(sizeof(void*), ctx->size);
  
#ifdef __NR_gettid
  ctx->tid = (uint32_t)syscall(__NR_gettid);
#else
  pthread_id_np_t tid;
  pthread_t self = pthread_self();
  pthread_getunique_np(&self, &tid);
  ctx->tid = (uint32_t)tid;
#endif

  ctx->addresses[0] = pc;
  
  if (qasan_shadow_stack.size <= 0) return; //can be negative when pop does not find nothing
  
  int i, j = 1;
  for (i = qasan_shadow_stack.first->index -1; i >= 0 && j < qasan_max_call_stack; --i)
    ctx->addresses[j++] = qasan_shadow_stack.first->buf[i];

  struct shadow_stack_block* b = qasan_shadow_stack.first->next;
  while (b && j < qasan_max_call_stack) {
  
    for (i = SHADOW_BK_SIZE-1; i >= 0; --i)
      ctx->addresses[j++] = b->buf[i];
  
  }

}

static void addr2line_cmd(char* lib, uintptr_t off, char** function, char** line) {
  
  if (!qasan_symbolize) goto addr2line_cmd_skip;
  
  FILE *fp;

  size_t cmd_siz = 128 + strlen(lib);
  char* cmd = malloc(cmd_siz);
  snprintf(cmd, cmd_siz, "addr2line -f -e '%s' 0x%lx", lib, off);

  fp = popen(cmd, "r");
  free(cmd);
  
  if (fp == NULL) goto addr2line_cmd_skip;

  *function = malloc(PATH_MAX + 32);
  
  if (!fgets(*function, PATH_MAX + 32, fp) || !strncmp(*function, "??", 2)) {

    free(*function);
    *function = NULL;

  } else {

    size_t l = strlen(*function);
    if (l && (*function)[l-1] == '\n')
      (*function)[l-1] = 0;
      
  }
  
  *line = malloc(PATH_MAX + 32);
  
  if (!fgets(*line, PATH_MAX + 32, fp) || !strncmp(*line, "??:", 3) ||
      !strncmp(*line, ":?", 2)) {

    free(*line);
    *line = NULL;

  } else {

    size_t l = strlen(*line);
    if (l && (*line)[l-1] == '\n')
      (*line)[l-1] = 0;
      
  }
  
  pclose(fp);
  
  return;

addr2line_cmd_skip:
  *line = NULL;
  *function = NULL;
  
}

char* asan_giovese_printaddr(target_ulong guest_addr) {

  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  fp = fopen("/proc/self/maps", "r");
  if (fp == NULL)
      return NULL;
  
  uint64_t img_min = 0; //, img_max = 0;
  char img_path[512] = {0};

  while ((read = getline(&line, &len, fp)) != -1) {
  
    int fields, dev_maj, dev_min, inode;
    uint64_t min, max, offset;
    char flag_r, flag_w, flag_x, flag_p;
    char path[512] = "";
    fields = sscanf(line, "%"PRIx64"-%"PRIx64" %c%c%c%c %"PRIx64" %x:%x %d"
                    " %512s", &min, &max, &flag_r, &flag_w, &flag_x,
                    &flag_p, &offset, &dev_maj, &dev_min, &inode, path);

    if ((fields < 10) || (fields > 11))
        continue;

    if (h2g_valid(min)) {

      int flags = page_get_flags(h2g(min));
      max = h2g_valid(max - 1) ? max : (uintptr_t)AFL_G2H(GUEST_ADDR_MAX) + 1;
      if (page_check_range(h2g(min), max - min, flags) == -1)
          continue;
      
      if (img_min && !strcmp(img_path, path)) {
        //img_max = max;
      } else {
        img_min = min;
        //img_max = max;
        strncpy(img_path, path, 512);
      }

      if (guest_addr >= h2g(min) && guest_addr < h2g(max - 1) + 1) {
      
        uintptr_t off = guest_addr - h2g(img_min);
      
        char* s;
        char * function = NULL;
        char * codeline = NULL;
        if (strlen(path)) {
          addr2line_cmd(path, off, &function, &codeline);
          if (!function)
            addr2line_cmd(path, guest_addr, &function, &codeline);
        }

        if (function) {
        
          if (codeline) {
          
            size_t l = strlen(function) + strlen(codeline) + 32;
            s = malloc(l);
            snprintf(s, l, " in %s %s", function, codeline);
            free(codeline);
            
          } else {

            size_t l = strlen(function) + strlen(path) + 32;
            s = malloc(l);
            snprintf(s, l, " in %s (%s+0x%lx)", function, path,
                     off);

          }
          
          free(function);
        
        } else {

          size_t l = strlen(path) + 32;
          s = malloc(l);
          snprintf(s, l, " (%s+0x%lx)", path, off);

        }

        free(line);
        fclose(fp);
        return s;
        
      }

    }

  }

  free(line);
  fclose(fp);

  return NULL;

}

#endif

void HELPER(qasan_shadow_stack_push)(target_ulong ptr) {

#ifndef DO_NOT_USE_QASAN
#if defined(TARGET_ARM)
  ptr &= ~1;
#endif

  if (unlikely(!qasan_shadow_stack.first)) {
    
    qasan_shadow_stack.first = malloc(sizeof(struct shadow_stack_block));
    qasan_shadow_stack.first->index = 0;
    qasan_shadow_stack.size = 0; // may be negative due to last pop
    qasan_shadow_stack.first->next = NULL;

  }
    
  qasan_shadow_stack.first->buf[qasan_shadow_stack.first->index++] = ptr;
  qasan_shadow_stack.size++;

  if (qasan_shadow_stack.first->index >= SHADOW_BK_SIZE) {

      struct shadow_stack_block* ns = malloc(sizeof(struct shadow_stack_block));
      ns->next = qasan_shadow_stack.first;
      ns->index = 0;
      qasan_shadow_stack.first = ns;
  }
#endif

}

void HELPER(qasan_shadow_stack_pop)(target_ulong ptr) {

#ifndef DO_NOT_USE_QASAN
#if defined(TARGET_ARM)
  ptr &= ~1;
#endif

  struct shadow_stack_block* cur_bk = qasan_shadow_stack.first;
  if (unlikely(cur_bk == NULL)) return;

  do {
      
      cur_bk->index--;
      qasan_shadow_stack.size--;
      
      if (cur_bk->index < 0) {
          
          struct shadow_stack_block* ns = cur_bk->next;
          free(cur_bk);
          cur_bk = ns;
          if (!cur_bk) break;
          cur_bk->index--;
      }
      
  } while(cur_bk->buf[cur_bk->index] != ptr);
  
  qasan_shadow_stack.first = cur_bk;
#endif

}

#endif

target_long qasan_actions_dispatcher(void *cpu_env,
                                     target_long action, target_long arg1,
                                     target_long arg2, target_long arg3) {

#ifndef DO_NOT_USE_QASAN
    CPUArchState *env = cpu_env;

    switch(action) {
#ifdef ASAN_GIOVESE
        case QASAN_ACTION_CHECK_LOAD:
        if (asan_giovese_guest_loadN(arg1, arg2)) {
          asan_giovese_report_and_crash(ACCESS_TYPE_LOAD, arg1, arg2, PC_GET(env), BP_GET(env), SP_GET(env));
        }
        break;
        
        case QASAN_ACTION_CHECK_STORE:
        if (asan_giovese_guest_storeN(arg1, arg2)) {
          asan_giovese_report_and_crash(ACCESS_TYPE_STORE, arg1, arg2, PC_GET(env), BP_GET(env), SP_GET(env));
        }
        break;
        
        case QASAN_ACTION_POISON:
        asan_giovese_poison_guest_region(arg1, arg2, arg3);
        break;
        
        case QASAN_ACTION_USER_POISON:
        asan_giovese_user_poison_guest_region(arg1, arg2);
        break;
        
        case QASAN_ACTION_UNPOISON:
        asan_giovese_unpoison_guest_region(arg1, arg2);
        break;
        
        case QASAN_ACTION_IS_POISON:
        return asan_giovese_guest_loadN(arg1, arg2);
        
        case QASAN_ACTION_ALLOC: {
          struct call_context* ctx = calloc(sizeof(struct call_context), 1);
          asan_giovese_populate_context(ctx, PC_GET(env));
          asan_giovese_alloc_insert(arg1, arg2, ctx);
          break;
        }
        
        case QASAN_ACTION_DEALLOC: {
          struct chunk_info* ckinfo = asan_giovese_alloc_search(arg1);
          if (ckinfo) {
            if (ckinfo->start != arg1)
              asan_giovese_badfree(arg1, PC_GET(env));
            ckinfo->free_ctx = calloc(sizeof(struct call_context), 1);
            asan_giovese_populate_context(ckinfo->free_ctx, PC_GET(env));
          } else {
            asan_giovese_badfree(arg1, PC_GET(env));
          }
          break;
        }
#else
        case QASAN_ACTION_CHECK_LOAD:
        __asan_loadN(AFL_G2H(arg1), arg2);
        break;
        
        case QASAN_ACTION_CHECK_STORE:
        __asan_storeN(AFL_G2H(arg1), arg2);
        break;
        
        case QASAN_ACTION_POISON:
        __asan_poison_memory_region(AFL_G2H(arg1), arg2);
        break;
        
        case QASAN_ACTION_USER_POISON:
        __asan_poison_memory_region(AFL_G2H(arg1), arg2);
        break;
        
        case QASAN_ACTION_UNPOISON:
        __asan_unpoison_memory_region(AFL_G2H(arg1), arg2);
        break;
        
        case QASAN_ACTION_IS_POISON:
        return __asan_region_is_poisoned(AFL_G2H(arg1), arg2) != NULL;
        
        case QASAN_ACTION_ALLOC:
          break;
        
        case QASAN_ACTION_DEALLOC:
          break;
#endif

        case QASAN_ACTION_ENABLE:
        qasan_disabled = 0;
        break;
        
        case QASAN_ACTION_DISABLE:
        qasan_disabled = 1;
        break;

        case QASAN_ACTION_SWAP_STATE: {
          int r = qasan_disabled;
          qasan_disabled = arg1;
          return r;
        }

        default:
        fprintf(stderr, "Invalid QASAN action " TARGET_FMT_ld "\n", action);
        abort();
    }
#endif

    return 0;
}

dh_ctype(tl) HELPER(qasan_fake_instr)(CPUArchState *env, dh_ctype(tl) action,
                                      dh_ctype(tl) arg1, dh_ctype(tl) arg2,
                                      dh_ctype(tl) arg3) {

  return qasan_actions_dispatcher(env, action, arg1, arg2, arg3);

}

void HELPER(qasan_load1)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;
  
  void* ptr = (void*)AFL_G2H(addr);

#ifdef ASAN_GIOVESE
  if (asan_giovese_load1(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_LOAD, addr, 1, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_load1(ptr);
#endif
#endif

}

void HELPER(qasan_load2)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;

  void* ptr = (void*)AFL_G2H(addr);

#ifdef ASAN_GIOVESE
  if (asan_giovese_load2(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_LOAD, addr, 2, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_load2(ptr);
#endif
#endif

}

void HELPER(qasan_load4)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;
  
  void* ptr = (void*)AFL_G2H(addr);

#ifdef ASAN_GIOVESE
  if (asan_giovese_load4(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_LOAD, addr, 4, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_load4(ptr);
#endif
#endif

}

void HELPER(qasan_load8)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;
  
  void* ptr = (void*)AFL_G2H(addr);

#ifdef ASAN_GIOVESE
  if (asan_giovese_load8(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_LOAD, addr, 8, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_load8(ptr);
#endif
#endif

}

void HELPER(qasan_store1)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;
  
  void* ptr = (void*)AFL_G2H(addr);

#ifdef ASAN_GIOVESE
  if (asan_giovese_store1(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_STORE, addr, 1, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_store1(ptr);
#endif
#endif

}

void HELPER(qasan_store2)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;
  
  void* ptr = (void*)AFL_G2H(addr);
  
#ifdef ASAN_GIOVESE
  if (asan_giovese_store2(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_STORE, addr, 2, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_store2(ptr);
#endif
#endif

}

void HELPER(qasan_store4)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;
  
  void* ptr = (void*)AFL_G2H(addr);

#ifdef ASAN_GIOVESE
  if (asan_giovese_store4(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_STORE, addr, 4, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_store4(ptr);
#endif
#endif

}

void HELPER(qasan_store8)(CPUArchState *env, target_ulong addr) {

#ifndef DO_NOT_USE_QASAN
  if (qasan_disabled) return;

  void* ptr = (void*)AFL_G2H(addr);

#ifdef ASAN_GIOVESE
  if (asan_giovese_store8(ptr)) {
    asan_giovese_report_and_crash(ACCESS_TYPE_STORE, addr, 8, PC_GET(env), BP_GET(env), SP_GET(env));
  }
#else
  __asan_store8(ptr);
#endif
#endif

}
