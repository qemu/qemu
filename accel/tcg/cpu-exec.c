/*
 *  emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "hw/core/tcg-cpu-ops.h"
#include "trace.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "qemu/atomic.h"
#include "qemu/compiler.h"
#include "sysemu/qtest.h"
#include "qemu/timer.h"
#include "qemu/rcu.h"
#include "exec/tb-hash.h"
#include "exec/tb-lookup.h"
#include "exec/log.h"
#include "qemu/main-loop.h"
#include "qemu/selfmap.h"
#if defined(TARGET_I386) && !defined(CONFIG_USER_ONLY)
#include "hw/i386/apic.h"
#endif
#include "sysemu/cpus.h"
#include "exec/cpu-all.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/replay.h"
#include "internal.h"

#include "qemuafl/common.h"
#include "qemuafl/imported/snapshot-inl.h"

#include <string.h>
#include <sys/shm.h>
#ifndef AFL_QEMU_STATIC_BUILD
  #include <dlfcn.h>
#endif

/***************************
 * VARIOUS AUXILIARY STUFF *
 ***************************/

/* This is equivalent to afl-as.h: */

static unsigned char
               dummy[MAP_SIZE]; /* costs MAP_SIZE but saves a few instructions */
unsigned char *afl_area_ptr = dummy;          /* Exported for afl_gen_trace */

/* Exported variables populated by the code patched into elfload.c: */

abi_ulong afl_entry_point,                      /* ELF entry point (_start) */
    afl_start_code,                             /* .text start pointer      */
    afl_end_code;                               /* .text end pointer        */

struct vmrange* afl_instr_code;

abi_ulong    afl_persistent_addr, afl_persistent_ret_addr;
unsigned int afl_persistent_cnt;

u8 afl_compcov_level;

__thread abi_ulong afl_prev_loc;

struct cmp_map *__afl_cmp_map;

/* Set in the child process in forkserver mode: */

static int forkserver_installed = 0;
static int disable_caching = 0;

unsigned char afl_fork_child;
unsigned int  afl_forksrv_pid;
unsigned char is_persistent;
target_long   persistent_stack_offset;
unsigned char persistent_first_pass = 1;
unsigned char persistent_exits;
unsigned char persistent_save_gpr;
unsigned char persistent_memory;
int           persisent_retaddr_offset;

struct api_regs saved_regs;

u8 * shared_buf;
u32 *shared_buf_len;
u8   sharedmem_fuzzing;

afl_persistent_hook_fn afl_persistent_hook_ptr;

/* Instrumentation ratio: */

unsigned int afl_inst_rms = MAP_SIZE;         /* Exported for afl_gen_trace */

/* Function declarations. */

static void afl_wait_tsl(CPUState *, int);
static void afl_request_tsl(target_ulong, target_ulong, uint32_t, uint32_t,
                            TranslationBlock *, int);

/* Data structures passed around by the translate handlers: */

struct afl_tb {

  target_ulong pc;
  target_ulong cs_base;
  uint32_t     flags;
  uint32_t     cf_mask;

};

struct afl_chain {

  struct afl_tb last_tb;
  uint32_t      cf_mask;
  int           tb_exit;

};

struct afl_tsl {

  struct afl_tb tb;
  struct afl_chain chain;
  char is_chain;

};

/* Some forward decls: */

static inline TranslationBlock *tb_find(CPUState *, TranslationBlock *, int,
                                        uint32_t);
static inline void              tb_add_jump(TranslationBlock *tb, int n,
                                            TranslationBlock *tb_next);
static void                     afl_map_shm_fuzz(void);

/*************************
 * ACTUAL IMPLEMENTATION *
 *************************/

/* Snapshot memory */

struct saved_region {

  void* addr;
  size_t size;
  void* saved;

};

abi_ulong saved_brk;
int lkm_snapshot;
struct saved_region* memory_snapshot;
size_t memory_snapshot_len;

static void collect_memory_snapshot(void) {

  saved_brk = afl_get_brk();

  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  fp = fopen("/proc/self/maps", "r");
  if (fp == NULL) {
    fprintf(stderr, "[AFL] ERROR: cannot open /proc/self/maps\n");
    exit(1);
  }
  
  size_t memory_snapshot_allocd = 32;
  if (!lkm_snapshot)
    memory_snapshot = malloc(memory_snapshot_allocd *
                             sizeof(struct saved_region));

  while ((read = getline(&line, &len, fp)) != -1) {
  
    int fields, dev_maj, dev_min, inode;
    uint64_t min, max, offset;
    char flag_r, flag_w, flag_x, flag_p;
    char path[512] = "";

    fields = sscanf(line, "%"PRIx64"-%"PRIx64" %c%c%c%c %"PRIx64" %x:%x %d"
                    " %512s", &min, &max, &flag_r, &flag_w, &flag_x,
                    &flag_p, &offset, &dev_maj, &dev_min, &inode, path);

    if ((fields < 10) || (fields > 11) || !h2g_valid(min))
        continue;
    
    int flags = page_get_flags(h2g(min));
    
    max = h2g_valid(max - 1) ? max : (uintptr_t)AFL_G2H(GUEST_ADDR_MAX) + 1;
    if (page_check_range(h2g(min), max - min, flags) == -1)
        continue;

    if (lkm_snapshot) {
    
      afl_snapshot_include_vmrange((void*)min, (void*)max);
    
    } else {

      if (!(flags & PROT_WRITE)) continue;

      if (memory_snapshot_allocd == memory_snapshot_len) {
        memory_snapshot_allocd *= 2;
        memory_snapshot = realloc(memory_snapshot, memory_snapshot_allocd *
                                  sizeof(struct saved_region));
      }
      
      void* saved = malloc(max - min);
      memcpy(saved, (void*)min, max - min);
      
      size_t i = memory_snapshot_len++;
      memory_snapshot[i].addr = (void*)min;
      memory_snapshot[i].size = max - min;
      memory_snapshot[i].saved = saved;
    
    }
    
  }
  
  if (lkm_snapshot)
    afl_snapshot_take(AFL_SNAPSHOT_BLOCK | AFL_SNAPSHOT_FDS);
    
  fclose(fp);

}

static void restore_memory_snapshot(void) {

  afl_set_brk(saved_brk);
  
  if (lkm_snapshot) {
  
    afl_snapshot_restore();
  
  } else {
  
    size_t i;
    for (i = 0; i < memory_snapshot_len; ++i) {
    
      // TODO avoid munmap of snapshot pages
      
      memcpy(memory_snapshot[i].addr, memory_snapshot[i].saved,
             memory_snapshot[i].size);
    
    }
  
  }
  
  afl_target_unmap_trackeds();

}

/* Set up SHM region and initialize other stuff. */

static void afl_map_shm_fuzz(void) {

  char *id_str = getenv(SHM_FUZZ_ENV_VAR);

  if (id_str) {

    u32 shm_id = atoi(id_str);
    u8 *map = (u8 *)shmat(shm_id, NULL, 0);
    /* Whooooops. */

    if (!map || map == (void *)-1) {

      perror("[AFL] ERROR: could not access fuzzing shared memory");
      exit(1);

    }

    shared_buf_len = (u32 *)map;
    shared_buf = map + sizeof(u32);

    if (getenv("AFL_DEBUG")) {

      fprintf(stderr, "[AFL] DEBUG: successfully got fuzzing shared memory\n");

    }

  } else {

    fprintf(stderr,
            "[AFL] ERROR:  variable for fuzzing shared memory is not set\n");
    exit(1);

  }

}

void afl_setup(void) {

  char *id_str = getenv(SHM_ENV_VAR), *inst_r = getenv("AFL_INST_RATIO");

  int shm_id;

  if (inst_r) {

    unsigned int r;

    r = atoi(inst_r);

    if (r > 100) r = 100;
    if (!r) r = 1;

    afl_inst_rms = MAP_SIZE * r / 100;

  }

  if (id_str) {

    shm_id = atoi(id_str);
    afl_area_ptr = shmat(shm_id, NULL, 0);

    if (afl_area_ptr == (void *)-1) exit(1);

    /* With AFL_INST_RATIO set to a low value, we want to touch the bitmap
       so that the parent doesn't give up on us. */

    if (inst_r) afl_area_ptr[0] = 1;

  }
  
  disable_caching = getenv("AFL_QEMU_DISABLE_CACHE") != NULL;

  if (getenv("___AFL_EINS_ZWEI_POLIZEI___")) {  // CmpLog forkserver

    id_str = getenv(CMPLOG_SHM_ENV_VAR);

    if (id_str) {

      u32 shm_id = atoi(id_str);

      __afl_cmp_map = shmat(shm_id, NULL, 0);

      if (__afl_cmp_map == (void *)-1) exit(1);

    }

  }

  if (getenv("AFL_INST_LIBS")) {

    afl_start_code = 0;
    afl_end_code = (abi_ulong)-1;

  }
  
  if (getenv("AFL_CODE_START"))
    afl_start_code = strtoll(getenv("AFL_CODE_START"), NULL, 16);
  if (getenv("AFL_CODE_END"))
    afl_end_code = strtoll(getenv("AFL_CODE_END"), NULL, 16);

  int have_names = 0;
  if (getenv("AFL_QEMU_INST_RANGES")) {
    char *str = getenv("AFL_QEMU_INST_RANGES");
    char *saveptr1, *saveptr2 = NULL, *save_pt1 = NULL;
    char *pt1, *pt2, *pt3 = NULL;
    
    while (1) {

      pt1 = strtok_r(str, ",", &saveptr1);
      if (pt1 == NULL) break;
      str = NULL;
      save_pt1 = strdup(pt1);
      
      pt2 = strtok_r(pt1, "-", &saveptr2);
      pt3 = strtok_r(NULL, "-", &saveptr2);
      
      struct vmrange* n = calloc(1, sizeof(struct vmrange));
      n->next = afl_instr_code;

      if (pt3 == NULL) { // filename
        have_names = 1;
        n->start = (target_ulong)-1;
        n->end = 0;
        n->name = save_pt1;
      } else {
        n->start = strtoull(pt2, NULL, 16);
        n->end = strtoull(pt3, NULL, 16);
        if (n->start && n->end) {
          n->name = NULL;
          free(save_pt1);
        } else {
          have_names = 1;
          n->start = (target_ulong)-1;
          n->end = 0;
          n->name = save_pt1;
        }
      }
      
      afl_instr_code = n;

    }
  }

  if (getenv("AFL_QEMU_EXCLUDE_RANGES")) {
    char *str = getenv("AFL_QEMU_EXCLUDE_RANGES");
    char *saveptr1, *saveptr2 = NULL, *save_pt1;
    char *pt1, *pt2, *pt3 = NULL;

    while (1) {

      pt1 = strtok_r(str, ",", &saveptr1);
      if (pt1 == NULL) break;
      str = NULL;
      save_pt1 = strdup(pt1);

      pt2 = strtok_r(pt1, "-", &saveptr2);
      pt3 = strtok_r(NULL, "-", &saveptr2);

      struct vmrange* n = calloc(1, sizeof(struct vmrange));
      n->exclude = true; // These are "exclusion" regions.
      n->next = afl_instr_code;

      if (pt3 == NULL) { // filename
        have_names = 1;
        n->start = (target_ulong)-1;
        n->end = 0;
        n->name = save_pt1;
      } else {
        n->start = strtoull(pt2, NULL, 16);
        n->end = strtoull(pt3, NULL, 16);
        if (n->start && n->end) {
          n->name = NULL;
          free(save_pt1);
        } else {
          have_names = 1;
          n->start = (target_ulong)-1;
          n->end = 0;
          n->name = save_pt1;
        }
      }

      afl_instr_code = n;

    }
  }

  if (have_names) {
    GSList *map_info = read_self_maps();
    for (GSList *s = map_info; s; s = g_slist_next(s)) {
      MapInfo *e = (MapInfo *) s->data;

      if (h2g_valid(e->start)) {
        unsigned long min = e->start;
        unsigned long max = e->end;
        int flags = page_get_flags(h2g(min));

        max = h2g_valid(max - 1) ? max : (uintptr_t) AFL_G2H(GUEST_ADDR_MAX) + 1;

        if (page_check_range(h2g(min), max - min, flags) == -1) {
          continue;
        }

        // Now that we have a valid guest address region, compare its
        // name against the names we care about:
        target_ulong gmin = h2g(min);
        target_ulong gmax = h2g(max);

        struct vmrange* n = afl_instr_code;
        while (n) {
          if (n->name && strstr(e->path, n->name)) {
            if (gmin < n->start) n->start = gmin;
            if (gmax > n->end) n->end = gmax;
            break;
          }
          n = n->next;
        }
      }
    }
    free_self_maps(map_info);
  }

  if (getenv("AFL_DEBUG") && afl_instr_code) {
    struct vmrange* n = afl_instr_code;
    while (n) {
      if (n->exclude) {
        fprintf(stderr, "Exclude range: 0x%lx-0x%lx (%s)\n",
                (unsigned long)n->start, (unsigned long)n->end,
                n->name ? n->name : "<noname>");
      } else {
        fprintf(stderr, "Instrument range: 0x%lx-0x%lx (%s)\n",
                (unsigned long)n->start, (unsigned long)n->end,
                n->name ? n->name : "<noname>");
      }
      n = n->next;
    }
  }

  /* Maintain for compatibility */
  if (getenv("AFL_QEMU_COMPCOV")) { afl_compcov_level = 1; }
  if (getenv("AFL_COMPCOV_LEVEL")) {

    afl_compcov_level = atoi(getenv("AFL_COMPCOV_LEVEL"));

  }

  /* pthread_atfork() seems somewhat broken in util/rcu.c, and I'm
     not entirely sure what is the cause. This disables that
     behaviour, and seems to work alright? */

  rcu_disable_atfork();
  
  if (getenv("AFL_QEMU_PERSISTENT_HOOK")) {

#ifdef AFL_QEMU_STATIC_BUILD

    fprintf(stderr,
            "[AFL] ERROR: you cannot use AFL_QEMU_PERSISTENT_HOOK when "
            "afl-qemu-trace is static\n");
    exit(1);

#else

    persistent_save_gpr = 1;

    void *plib = dlopen(getenv("AFL_QEMU_PERSISTENT_HOOK"), RTLD_NOW);
    if (!plib) {

      fprintf(stderr, "[AFL] ERROR: invalid AFL_QEMU_PERSISTENT_HOOK=%s - %s\n",
              getenv("AFL_QEMU_PERSISTENT_HOOK"),
              dlerror());
      exit(1);

    }

    int (*afl_persistent_hook_init_ptr)(void) =
        dlsym(plib, "afl_persistent_hook_init");
    if (afl_persistent_hook_init_ptr)
      sharedmem_fuzzing = afl_persistent_hook_init_ptr();

    afl_persistent_hook_ptr = dlsym(plib, "afl_persistent_hook");
    if (!afl_persistent_hook_ptr) {

      fprintf(stderr,
              "[AFL] ERROR: failed to find the function "
              "\"afl_persistent_hook\" in %s\n",
              getenv("AFL_QEMU_PERSISTENT_HOOK"));
      exit(1);

    }

#endif

  }
  
  if (__afl_cmp_map) return; // no persistent for cmplog
  
  is_persistent = getenv("AFL_QEMU_PERSISTENT_ADDR") != NULL;

  if (is_persistent)
    afl_persistent_addr = strtoll(getenv("AFL_QEMU_PERSISTENT_ADDR"), NULL, 0);

  if (getenv("AFL_QEMU_PERSISTENT_RET"))
    afl_persistent_ret_addr =
        strtoll(getenv("AFL_QEMU_PERSISTENT_RET"), NULL, 0);
  /* If AFL_QEMU_PERSISTENT_RET is not specified patch the return addr */

  if (getenv("AFL_QEMU_PERSISTENT_GPR")) persistent_save_gpr = 1;
  if (getenv("AFL_QEMU_PERSISTENT_MEM"))
    persistent_memory = 1;

  if (getenv("AFL_QEMU_PERSISTENT_RETADDR_OFFSET"))
    persisent_retaddr_offset =
        strtoll(getenv("AFL_QEMU_PERSISTENT_RETADDR_OFFSET"), NULL, 0);

  if (getenv("AFL_QEMU_PERSISTENT_CNT"))
    afl_persistent_cnt = strtoll(getenv("AFL_QEMU_PERSISTENT_CNT"), NULL, 0);
  else
    afl_persistent_cnt = 0;
    
  if (getenv("AFL_QEMU_PERSISTENT_EXITS")) persistent_exits = 1;

  // TODO persistent exits for other archs not x86
  // TODO persistent mode for other archs not x86
  // TODO cmplog rtn for arm
  
  if (getenv("AFL_QEMU_SNAPSHOT")) {
  
    is_persistent = 1;
    persistent_save_gpr = 1;
    persistent_memory = 1;
    persistent_exits = 1;
    
    if (afl_persistent_addr == 0)
      afl_persistent_addr = strtoll(getenv("AFL_QEMU_SNAPSHOT"), NULL, 0);
  
  }
  
  if (persistent_memory && afl_snapshot_init() >= 0)
    lkm_snapshot = 1;
  
  if (getenv("AFL_DEBUG")) {
    if (is_persistent)
      fprintf(stderr, "Persistent: 0x%lx [0x%lx] %s%s%s\n",
              (unsigned long)afl_persistent_addr,
              (unsigned long)afl_persistent_ret_addr,
              (persistent_save_gpr ? "gpr ": ""),
              (persistent_memory ? "mem ": ""),
              (persistent_exits ? "exits ": ""));
  }

}

/* Fork server logic, invoked once we hit _start. */

void afl_forkserver(CPUState *cpu) {

  // u32           map_size = 0;
  unsigned char tmp[4] = {0};

  if (forkserver_installed == 1) return;
  forkserver_installed = 1;

  if (getenv("AFL_QEMU_DEBUG_MAPS")) open_self_maps(cpu->env_ptr, 1);

  pid_t child_pid;
  int   t_fd[2];
  u8    child_stopped = 0;
  u32   was_killed;
  int   status = 0;

  // with the max ID value
  if (MAP_SIZE <= FS_OPT_MAX_MAPSIZE)
    status |= (FS_OPT_SET_MAPSIZE(MAP_SIZE) | FS_OPT_MAPSIZE);
  if (lkm_snapshot) status |= FS_OPT_SNAPSHOT;
  if (sharedmem_fuzzing != 0) status |= FS_OPT_SHDMEM_FUZZ;
  if (status) status |= (FS_OPT_ENABLED | FS_OPT_NEWCMPLOG);
  if (getenv("AFL_DEBUG"))
    fprintf(stderr, "Debug: Sending status %08x\n", status);
  memcpy(tmp, &status, 4);

  /* Tell the parent that we're alive. If the parent doesn't want
     to talk, assume that we're not running in forkserver mode. */

  if (write(FORKSRV_FD + 1, tmp, 4) != 4) return;

  afl_forksrv_pid = getpid();

  int first_run = 1;

  if (sharedmem_fuzzing) {

    if (read(FORKSRV_FD, &was_killed, 4) != 4) exit(2);

    if ((was_killed & (0xffffffff & (FS_OPT_ENABLED | FS_OPT_SHDMEM_FUZZ))) ==
        (FS_OPT_ENABLED | FS_OPT_SHDMEM_FUZZ))
      afl_map_shm_fuzz();
    else {

      fprintf(stderr,
              "[AFL] ERROR: afl-fuzz is old and does not support"
              " shmem input");
      exit(1);

    }

  }

  /* All right, let's await orders... */

  while (1) {

    /* Whoops, parent dead? */

    if (read(FORKSRV_FD, &was_killed, 4) != 4) exit(2);

    /* If we stopped the child in persistent mode, but there was a race
       condition and afl-fuzz already issued SIGKILL, write off the old
       process. */

    if (child_stopped && was_killed) {

      child_stopped = 0;
      if (waitpid(child_pid, &status, 0) < 0) exit(8);

    }

    if (!child_stopped) {

      /* Establish a channel with child to grab translation commands. We'll
       read from t_fd[0], child will write to TSL_FD. */

      if (pipe(t_fd) || dup2(t_fd[1], TSL_FD) < 0) exit(3);
      close(t_fd[1]);

      child_pid = fork();
      if (child_pid < 0) exit(4);

      if (!child_pid) {

        /* Child process. Close descriptors and run free. */

        afl_fork_child = 1;
        close(FORKSRV_FD);
        close(FORKSRV_FD + 1);
        close(t_fd[0]);
        return;

      }

      /* Parent. */

      close(TSL_FD);

    } else {

      /* Special handling for persistent mode: if the child is alive but
         currently stopped, simply restart it with SIGCONT. */

      kill(child_pid, SIGCONT);
      child_stopped = 0;

    }

    /* Parent. */

    if (write(FORKSRV_FD + 1, &child_pid, 4) != 4) exit(5);

    /* Collect translation requests until child dies and closes the pipe. */

    afl_wait_tsl(cpu, t_fd[0]);

    /* Get and relay exit status to parent. */

    if (waitpid(child_pid, &status, is_persistent ? WUNTRACED : 0) < 0) exit(6);

    /* In persistent mode, the child stops itself with SIGSTOP to indicate
       a successful run. In this case, we want to wake it up without forking
       again. */

    if (WIFSTOPPED(status))
      child_stopped = 1;
    else if (unlikely(first_run && is_persistent)) {

      fprintf(stderr, "[AFL] ERROR: no persistent iteration executed\n");
      exit(12);  // Persistent is wrong

    }

    first_run = 0;

    if (write(FORKSRV_FD + 1, &status, 4) != 4) exit(7);

  }

}

/* A simplified persistent mode handler, used as explained in
 * llvm_mode/README.md. */

static u32 cycle_cnt;

void afl_persistent_iter(CPUArchState *env) {

  static struct afl_tsl exit_cmd_tsl;

  if (!afl_persistent_cnt || --cycle_cnt) {
  
    if (persistent_memory) restore_memory_snapshot();
  
    if (persistent_save_gpr && !afl_persistent_hook_ptr) {
      afl_restore_regs(&saved_regs, env);
    }

    if (!disable_caching) {
  
      memset(&exit_cmd_tsl, 0, sizeof(struct afl_tsl));
      exit_cmd_tsl.tb.pc = (target_ulong)(-1);

      if (write(TSL_FD, &exit_cmd_tsl, sizeof(struct afl_tsl)) !=
          sizeof(struct afl_tsl)) {

        /* Exit the persistent loop on pipe error */
        afl_area_ptr = dummy;
        exit(0);

      }
    
    }

    // TODO use only pipe
    raise(SIGSTOP);

    
    // now we have shared_buf updated and ready to use
    if (persistent_save_gpr && afl_persistent_hook_ptr) {
    
      struct api_regs hook_regs = saved_regs;
      afl_persistent_hook_ptr(&hook_regs, guest_base, shared_buf,
                              *shared_buf_len);
      afl_restore_regs(&hook_regs, env);

    }

    afl_area_ptr[0] = 1;
    afl_prev_loc = 0;

  } else {

    afl_area_ptr = dummy;
    exit(0);

  }

}

void afl_persistent_loop(CPUArchState *env) {

  if (!afl_fork_child) return;

  if (persistent_first_pass) {

    /* Make sure that every iteration of __AFL_LOOP() starts with a clean slate.
       On subsequent calls, the parent will take care of that, but on the first
       iteration, it's our job to erase any trace of whatever happened
       before the loop. */

    if (is_persistent) {

      memset(afl_area_ptr, 0, MAP_SIZE);
      afl_area_ptr[0] = 1;
      afl_prev_loc = 0;

    }
    
    if (persistent_memory) collect_memory_snapshot();
    
    if (persistent_save_gpr) {
    
      afl_save_regs(&saved_regs, env);
      
      if (afl_persistent_hook_ptr) {
      
        struct api_regs hook_regs = saved_regs;
        afl_persistent_hook_ptr(&hook_regs, guest_base, shared_buf,
                                *shared_buf_len);
        afl_restore_regs(&hook_regs, env);

      }

    }
    
    cycle_cnt = afl_persistent_cnt;
    persistent_first_pass = 0;
    persistent_stack_offset = TARGET_LONG_BITS / 8;

    return;

  }

  if (is_persistent) {

    afl_persistent_iter(env);

  }

}

/* This code is invoked whenever QEMU decides that it doesn't have a
   translation of a particular block and needs to compute it, or when it
   decides to chain two TBs together. When this happens, we tell the parent to
   mirror the operation, so that the next fork() has a cached copy. */

static void afl_request_tsl(target_ulong pc, target_ulong cb, uint32_t flags,
                            uint32_t cf_mask, TranslationBlock *last_tb,
                            int tb_exit) {

  if (disable_caching) return;

  struct afl_tsl t;

  if (!afl_fork_child) return;

  t.tb.pc = pc;
  t.tb.cs_base = cb;
  t.tb.flags = flags;
  t.tb.cf_mask = cf_mask;
  t.is_chain = (last_tb != NULL);

  if (t.is_chain) {

    t.chain.last_tb.pc = last_tb->pc;
    t.chain.last_tb.cs_base = last_tb->cs_base;
    t.chain.last_tb.flags = last_tb->flags;
    t.chain.cf_mask = cf_mask;
    t.chain.tb_exit = tb_exit;

  }
  
  if (write(TSL_FD, &t, sizeof(struct afl_tsl)) != sizeof(struct afl_tsl))
    return;

}

static inline TranslationBlock *
afl_tb_lookup(CPUState *cpu, target_ulong pc, target_ulong cs_base,
                     uint32_t flags, uint32_t cf_mask)
{
    TranslationBlock *tb;
    uint32_t hash;

    hash = tb_jmp_cache_hash_func(pc);
    tb = qatomic_rcu_read(&cpu->tb_jmp_cache[hash]);

    cf_mask &= ~CF_CLUSTER_MASK;
    cf_mask |= cpu->cluster_index << CF_CLUSTER_SHIFT;

    if (likely(tb &&
               tb->pc == pc &&
               tb->cs_base == cs_base &&
               tb->flags == flags &&
               tb->trace_vcpu_dstate == *cpu->trace_dstate &&
               (tb_cflags(tb) & (CF_HASH_MASK | CF_INVALID)) == cf_mask)) {
        return tb;
    }
    tb = tb_htable_lookup(cpu, pc, cs_base, flags, cf_mask);
    if (tb == NULL) {
        return NULL;
    }
    qatomic_set(&cpu->tb_jmp_cache[hash], tb);
    return tb;
}

/* This is the other side of the same channel. Since timeouts are handled by
   afl-fuzz simply killing the child, we can just wait until the pipe breaks. */

static void afl_wait_tsl(CPUState *cpu, int fd) {

  struct afl_tsl t;
  TranslationBlock *tb, *last_tb;

  if (disable_caching) return;

  while (1) {

    u8 invalid_pc = 0;

    /* Broken pipe means it's time to return to the fork server routine. */

    if (read(fd, &t, sizeof(struct afl_tsl)) != sizeof(struct afl_tsl)) break;

    /* Exit command for persistent */

    if (t.tb.pc == (target_ulong)(-1)) return;

    tb = afl_tb_lookup(cpu, t.tb.pc, t.tb.cs_base, t.tb.flags, t.tb.cf_mask);

    if (!tb) {

      /* The child may request to transate a block of memory that is not
         mapped in the parent (e.g. jitted code or dlopened code).
         This causes a SIGSEV in gen_intermediate_code() and associated
         subroutines. We simply avoid caching of such blocks. */

      if (is_valid_addr(t.tb.pc)) {

        mmap_lock();
        tb = tb_gen_code(cpu, t.tb.pc, t.tb.cs_base, t.tb.flags, t.tb.cf_mask);
        mmap_unlock();

      } else {

        invalid_pc = 1;

      }

    }

    if (t.is_chain && !invalid_pc) {

      last_tb = afl_tb_lookup(cpu, t.chain.last_tb.pc,
                                 t.chain.last_tb.cs_base,
                                 t.chain.last_tb.flags,
                                 t.chain.cf_mask);
#define TB_JMP_RESET_OFFSET_INVALID 0xffff
        if (last_tb && (last_tb->jmp_reset_offset[t.chain.tb_exit] !=
                        TB_JMP_RESET_OFFSET_INVALID)) {

          tb_add_jump(last_tb, t.chain.tb_exit, tb);

        }

    }

  }

  close(fd);

}

/* -icount align implementation. */

typedef struct SyncClocks {
    int64_t diff_clk;
    int64_t last_cpu_icount;
    int64_t realtime_clock;
} SyncClocks;

#if !defined(CONFIG_USER_ONLY)
/* Allow the guest to have a max 3ms advance.
 * The difference between the 2 clocks could therefore
 * oscillate around 0.
 */
#define VM_CLOCK_ADVANCE 3000000
#define THRESHOLD_REDUCE 1.5
#define MAX_DELAY_PRINT_RATE 2000000000LL
#define MAX_NB_PRINTS 100

static int64_t max_delay;
static int64_t max_advance;

static void align_clocks(SyncClocks *sc, CPUState *cpu)
{
    int64_t cpu_icount;

    if (!icount_align_option) {
        return;
    }

    cpu_icount = cpu->icount_extra + cpu_neg(cpu)->icount_decr.u16.low;
    sc->diff_clk += icount_to_ns(sc->last_cpu_icount - cpu_icount);
    sc->last_cpu_icount = cpu_icount;

    if (sc->diff_clk > VM_CLOCK_ADVANCE) {
#ifndef _WIN32
        struct timespec sleep_delay, rem_delay;
        sleep_delay.tv_sec = sc->diff_clk / 1000000000LL;
        sleep_delay.tv_nsec = sc->diff_clk % 1000000000LL;
        if (nanosleep(&sleep_delay, &rem_delay) < 0) {
            sc->diff_clk = rem_delay.tv_sec * 1000000000LL + rem_delay.tv_nsec;
        } else {
            sc->diff_clk = 0;
        }
#else
        Sleep(sc->diff_clk / SCALE_MS);
        sc->diff_clk = 0;
#endif
    }
}

static void print_delay(const SyncClocks *sc)
{
    static float threshold_delay;
    static int64_t last_realtime_clock;
    static int nb_prints;

    if (icount_align_option &&
        sc->realtime_clock - last_realtime_clock >= MAX_DELAY_PRINT_RATE &&
        nb_prints < MAX_NB_PRINTS) {
        if ((-sc->diff_clk / (float)1000000000LL > threshold_delay) ||
            (-sc->diff_clk / (float)1000000000LL <
             (threshold_delay - THRESHOLD_REDUCE))) {
            threshold_delay = (-sc->diff_clk / 1000000000LL) + 1;
            qemu_printf("Warning: The guest is now late by %.1f to %.1f seconds\n",
                        threshold_delay - 1,
                        threshold_delay);
            nb_prints++;
            last_realtime_clock = sc->realtime_clock;
        }
    }
}

static void init_delay_params(SyncClocks *sc, CPUState *cpu)
{
    if (!icount_align_option) {
        return;
    }
    sc->realtime_clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT);
    sc->diff_clk = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - sc->realtime_clock;
    sc->last_cpu_icount
        = cpu->icount_extra + cpu_neg(cpu)->icount_decr.u16.low;
    if (sc->diff_clk < max_delay) {
        max_delay = sc->diff_clk;
    }
    if (sc->diff_clk > max_advance) {
        max_advance = sc->diff_clk;
    }

    /* Print every 2s max if the guest is late. We limit the number
       of printed messages to NB_PRINT_MAX(currently 100) */
    print_delay(sc);
}
#else
static void align_clocks(SyncClocks *sc, const CPUState *cpu)
{
}

static void init_delay_params(SyncClocks *sc, const CPUState *cpu)
{
}
#endif /* CONFIG USER ONLY */

/* Execute a TB, and fix up the CPU state afterwards if necessary */
/*
 * Disable CFI checks.
 * TCG creates binary blobs at runtime, with the transformed code.
 * A TB is a blob of binary code, created at runtime and called with an
 * indirect function call. Since such function did not exist at compile time,
 * the CFI runtime has no way to verify its signature and would fail.
 * TCG is not considered a security-sensitive part of QEMU so this does not
 * affect the impact of CFI in environment with high security requirements
 */
static inline TranslationBlock * QEMU_DISABLE_CFI
cpu_tb_exec(CPUState *cpu, TranslationBlock *itb, int *tb_exit)
{
    CPUArchState *env = cpu->env_ptr;
    uintptr_t ret;
    TranslationBlock *last_tb;
    const void *tb_ptr = itb->tc.ptr;

    qemu_log_mask_and_addr(CPU_LOG_EXEC, itb->pc,
                           "Trace %d: %p ["
                           TARGET_FMT_lx "/" TARGET_FMT_lx "/%#x] %s\n",
                           cpu->cpu_index, itb->tc.ptr,
                           itb->cs_base, itb->pc, itb->flags,
                           lookup_symbol(itb->pc));

#if defined(DEBUG_DISAS)
    if (qemu_loglevel_mask(CPU_LOG_TB_CPU)
        && qemu_log_in_addr_range(itb->pc)) {
        FILE *logfile = qemu_log_lock();
        int flags = 0;
        if (qemu_loglevel_mask(CPU_LOG_TB_FPU)) {
            flags |= CPU_DUMP_FPU;
        }
#if defined(TARGET_I386)
        flags |= CPU_DUMP_CCOP;
#endif
        log_cpu_state(cpu, flags);
        qemu_log_unlock(logfile);
    }
#endif /* DEBUG_DISAS */

    qemu_thread_jit_execute();
    ret = tcg_qemu_tb_exec(env, tb_ptr);
    cpu->can_do_io = 1;
    /*
     * TODO: Delay swapping back to the read-write region of the TB
     * until we actually need to modify the TB.  The read-only copy,
     * coming from the rx region, shares the same host TLB entry as
     * the code that executed the exit_tb opcode that arrived here.
     * If we insist on touching both the RX and the RW pages, we
     * double the host TLB pressure.
     */
    last_tb = tcg_splitwx_to_rw((void *)(ret & ~TB_EXIT_MASK));
    *tb_exit = ret & TB_EXIT_MASK;

    trace_exec_tb_exit(last_tb, *tb_exit);

    if (*tb_exit > TB_EXIT_IDX1) {
        /* We didn't start executing this TB (eg because the instruction
         * counter hit zero); we must restore the guest PC to the address
         * of the start of the TB.
         */
        CPUClass *cc = CPU_GET_CLASS(cpu);
        qemu_log_mask_and_addr(CPU_LOG_EXEC, last_tb->pc,
                               "Stopped execution of TB chain before %p ["
                               TARGET_FMT_lx "] %s\n",
                               last_tb->tc.ptr, last_tb->pc,
                               lookup_symbol(last_tb->pc));
        if (cc->tcg_ops->synchronize_from_tb) {
            cc->tcg_ops->synchronize_from_tb(cpu, last_tb);
        } else {
            assert(cc->set_pc);
            cc->set_pc(cpu, last_tb->pc);
        }
    }
    return last_tb;
}


static void cpu_exec_enter(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->tcg_ops->cpu_exec_enter) {
        cc->tcg_ops->cpu_exec_enter(cpu);
    }
}

static void cpu_exec_exit(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->tcg_ops->cpu_exec_exit) {
        cc->tcg_ops->cpu_exec_exit(cpu);
    }
}

void cpu_exec_step_atomic(CPUState *cpu)
{
    TranslationBlock *tb;
    target_ulong cs_base, pc;
    uint32_t flags;
    uint32_t cflags = 1;
    uint32_t cf_mask = cflags & CF_HASH_MASK;
    int tb_exit;

    if (sigsetjmp(cpu->jmp_env, 0) == 0) {
        start_exclusive();
        g_assert(cpu == current_cpu);
        g_assert(!cpu->running);
        cpu->running = true;

        tb = tb_lookup__cpu_state(cpu, &pc, &cs_base, &flags, cf_mask);
        if (tb == NULL) {
            mmap_lock();
            tb = tb_gen_code(cpu, pc, cs_base, flags, cflags);
            mmap_unlock();
        }

        /* Since we got here, we know that parallel_cpus must be true.  */
        parallel_cpus = false;
        cpu_exec_enter(cpu);
        /* execute the generated code */
        trace_exec_tb(tb, pc);
        cpu_tb_exec(cpu, tb, &tb_exit);
        cpu_exec_exit(cpu);
    } else {
        /*
         * The mmap_lock is dropped by tb_gen_code if it runs out of
         * memory.
         */
#ifndef CONFIG_SOFTMMU
        tcg_debug_assert(!have_mmap_lock());
#endif
        if (qemu_mutex_iothread_locked()) {
            qemu_mutex_unlock_iothread();
        }
        assert_no_pages_locked();
        qemu_plugin_disable_mem_helpers(cpu);
    }


    /*
     * As we start the exclusive region before codegen we must still
     * be in the region if we longjump out of either the codegen or
     * the execution.
     */
    g_assert(cpu_in_exclusive_context(cpu));
    parallel_cpus = true;
    cpu->running = false;
    end_exclusive();
}

struct tb_desc {
    target_ulong pc;
    target_ulong cs_base;
    CPUArchState *env;
    tb_page_addr_t phys_page1;
    uint32_t flags;
    uint32_t cf_mask;
    uint32_t trace_vcpu_dstate;
};

static bool tb_lookup_cmp(const void *p, const void *d)
{
    const TranslationBlock *tb = p;
    const struct tb_desc *desc = d;

    if (tb->pc == desc->pc &&
        tb->page_addr[0] == desc->phys_page1 &&
        tb->cs_base == desc->cs_base &&
        tb->flags == desc->flags &&
        tb->trace_vcpu_dstate == desc->trace_vcpu_dstate &&
        (tb_cflags(tb) & (CF_HASH_MASK | CF_INVALID)) == desc->cf_mask) {
        /* check next page if needed */
        if (tb->page_addr[1] == -1) {
            return true;
        } else {
            tb_page_addr_t phys_page2;
            target_ulong virt_page2;

            virt_page2 = (desc->pc & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
            phys_page2 = get_page_addr_code(desc->env, virt_page2);
            if (tb->page_addr[1] == phys_page2) {
                return true;
            }
        }
    }
    return false;
}

TranslationBlock *tb_htable_lookup(CPUState *cpu, target_ulong pc,
                                   target_ulong cs_base, uint32_t flags,
                                   uint32_t cf_mask)
{
    tb_page_addr_t phys_pc;
    struct tb_desc desc;
    uint32_t h;

    desc.env = (CPUArchState *)cpu->env_ptr;
    desc.cs_base = cs_base;
    desc.flags = flags;
    desc.cf_mask = cf_mask;
    desc.trace_vcpu_dstate = *cpu->trace_dstate;
    desc.pc = pc;
    phys_pc = get_page_addr_code(desc.env, pc);
    if (phys_pc == -1) {
        return NULL;
    }
    desc.phys_page1 = phys_pc & TARGET_PAGE_MASK;
    h = tb_hash_func(phys_pc, pc, flags, cf_mask, *cpu->trace_dstate);
    return qht_lookup_custom(&tb_ctx.htable, &desc, h, tb_lookup_cmp);
}

void tb_set_jmp_target(TranslationBlock *tb, int n, uintptr_t addr)
{
    if (TCG_TARGET_HAS_direct_jump) {
        uintptr_t offset = tb->jmp_target_arg[n];
        uintptr_t tc_ptr = (uintptr_t)tb->tc.ptr;
        uintptr_t jmp_rx = tc_ptr + offset;
        uintptr_t jmp_rw = jmp_rx - tcg_splitwx_diff;
        tb_target_set_jmp_target(tc_ptr, jmp_rx, jmp_rw, addr);
    } else {
        tb->jmp_target_arg[n] = addr;
    }
}

static inline void tb_add_jump(TranslationBlock *tb, int n,
                               TranslationBlock *tb_next)
{
    uintptr_t old;

    qemu_thread_jit_write();
    assert(n < ARRAY_SIZE(tb->jmp_list_next));
    qemu_spin_lock(&tb_next->jmp_lock);

    /* make sure the destination TB is valid */
    if (tb_next->cflags & CF_INVALID) {
        goto out_unlock_next;
    }
    /* Atomically claim the jump destination slot only if it was NULL */
    old = qatomic_cmpxchg(&tb->jmp_dest[n], (uintptr_t)NULL,
                          (uintptr_t)tb_next);
    if (old) {
        goto out_unlock_next;
    }

    /* patch the native jump address */
    tb_set_jmp_target(tb, n, (uintptr_t)tb_next->tc.ptr);

    /* add in TB jmp list */
    tb->jmp_list_next[n] = tb_next->jmp_list_head;
    tb_next->jmp_list_head = (uintptr_t)tb | n;

    qemu_spin_unlock(&tb_next->jmp_lock);

    qemu_log_mask_and_addr(CPU_LOG_EXEC, tb->pc,
                           "Linking TBs %p [" TARGET_FMT_lx
                           "] index %d -> %p [" TARGET_FMT_lx "]\n",
                           tb->tc.ptr, tb->pc, n,
                           tb_next->tc.ptr, tb_next->pc);
    return;

 out_unlock_next:
    qemu_spin_unlock(&tb_next->jmp_lock);
    return;
}

static inline TranslationBlock *tb_find(CPUState *cpu,
                                        TranslationBlock *last_tb,
                                        int tb_exit, uint32_t cf_mask)
{
    TranslationBlock *tb;
    target_ulong cs_base, pc;
    uint32_t flags;
    bool was_translated = false, was_chained = false;

    tb = tb_lookup__cpu_state(cpu, &pc, &cs_base, &flags, cf_mask);
    if (tb == NULL) {
        mmap_lock();
        tb = tb_gen_code(cpu, pc, cs_base, flags, cf_mask);
        was_translated = true;
        mmap_unlock();
        /* We add the TB in the virtual pc hash table for the fast lookup */
        qatomic_set(&cpu->tb_jmp_cache[tb_jmp_cache_hash_func(pc)], tb);
    }
#ifndef CONFIG_USER_ONLY
    /* We don't take care of direct jumps when address mapping changes in
     * system emulation. So it's not safe to make a direct jump to a TB
     * spanning two pages because the mapping for the second page can change.
     */
    if (tb->page_addr[1] != -1) {
        last_tb = NULL;
    }
#endif
    /* See if we can patch the calling TB. */
    if (last_tb) {
        tb_add_jump(last_tb, tb_exit, tb);
        was_chained = true;
    }
    if (was_translated || was_chained) {
        afl_request_tsl(pc, cs_base, flags, cf_mask,
                        was_chained ? last_tb : NULL, tb_exit);
    }
    return tb;
}

static inline bool cpu_handle_halt(CPUState *cpu)
{
    if (cpu->halted) {
#if defined(TARGET_I386) && !defined(CONFIG_USER_ONLY)
        if (cpu->interrupt_request & CPU_INTERRUPT_POLL) {
            X86CPU *x86_cpu = X86_CPU(cpu);
            qemu_mutex_lock_iothread();
            apic_poll_irq(x86_cpu->apic_state);
            cpu_reset_interrupt(cpu, CPU_INTERRUPT_POLL);
            qemu_mutex_unlock_iothread();
        }
#endif
        if (!cpu_has_work(cpu)) {
            return true;
        }

        cpu->halted = 0;
    }

    return false;
}

static inline void cpu_handle_debug_exception(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    CPUWatchpoint *wp;

    if (!cpu->watchpoint_hit) {
        QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
            wp->flags &= ~BP_WATCHPOINT_HIT;
        }
    }

    if (cc->tcg_ops->debug_excp_handler) {
        cc->tcg_ops->debug_excp_handler(cpu);
    }
}

static inline bool cpu_handle_exception(CPUState *cpu, int *ret)
{
    if (cpu->exception_index < 0) {
#ifndef CONFIG_USER_ONLY
        if (replay_has_exception()
            && cpu_neg(cpu)->icount_decr.u16.low + cpu->icount_extra == 0) {
            /* Execute just one insn to trigger exception pending in the log */
            cpu->cflags_next_tb = (curr_cflags() & ~CF_USE_ICOUNT) | 1;
        }
#endif
        return false;
    }
    if (cpu->exception_index >= EXCP_INTERRUPT) {
        /* exit request from the cpu execution loop */
        *ret = cpu->exception_index;
        if (*ret == EXCP_DEBUG) {
            cpu_handle_debug_exception(cpu);
        }
        cpu->exception_index = -1;
        return true;
    } else {
#if defined(CONFIG_USER_ONLY)
        /* if user mode only, we simulate a fake exception
           which will be handled outside the cpu execution
           loop */
#if defined(TARGET_I386)
        CPUClass *cc = CPU_GET_CLASS(cpu);
        cc->tcg_ops->do_interrupt(cpu);
#endif
        *ret = cpu->exception_index;
        cpu->exception_index = -1;
        return true;
#else
        if (replay_exception()) {
            CPUClass *cc = CPU_GET_CLASS(cpu);
            qemu_mutex_lock_iothread();
            cc->tcg_ops->do_interrupt(cpu);
            qemu_mutex_unlock_iothread();
            cpu->exception_index = -1;

            if (unlikely(cpu->singlestep_enabled)) {
                /*
                 * After processing the exception, ensure an EXCP_DEBUG is
                 * raised when single-stepping so that GDB doesn't miss the
                 * next instruction.
                 */
                *ret = EXCP_DEBUG;
                cpu_handle_debug_exception(cpu);
                return true;
            }
        } else if (!replay_has_interrupt()) {
            /* give a chance to iothread in replay mode */
            *ret = EXCP_INTERRUPT;
            return true;
        }
#endif
    }

    return false;
}

/*
 * CPU_INTERRUPT_POLL is a virtual event which gets converted into a
 * "real" interrupt event later. It does not need to be recorded for
 * replay purposes.
 */
static inline bool need_replay_interrupt(int interrupt_request)
{
#if defined(TARGET_I386)
    return !(interrupt_request & CPU_INTERRUPT_POLL);
#else
    return true;
#endif
}

static inline bool cpu_handle_interrupt(CPUState *cpu,
                                        TranslationBlock **last_tb)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    /* Clear the interrupt flag now since we're processing
     * cpu->interrupt_request and cpu->exit_request.
     * Ensure zeroing happens before reading cpu->exit_request or
     * cpu->interrupt_request (see also smp_wmb in cpu_exit())
     */
    qatomic_mb_set(&cpu_neg(cpu)->icount_decr.u16.high, 0);

    if (unlikely(qatomic_read(&cpu->interrupt_request))) {
        int interrupt_request;
        qemu_mutex_lock_iothread();
        interrupt_request = cpu->interrupt_request;
        if (unlikely(cpu->singlestep_enabled & SSTEP_NOIRQ)) {
            /* Mask out external interrupts for this step. */
            interrupt_request &= ~CPU_INTERRUPT_SSTEP_MASK;
        }
        if (interrupt_request & CPU_INTERRUPT_DEBUG) {
            cpu->interrupt_request &= ~CPU_INTERRUPT_DEBUG;
            cpu->exception_index = EXCP_DEBUG;
            qemu_mutex_unlock_iothread();
            return true;
        }
        if (replay_mode == REPLAY_MODE_PLAY && !replay_has_interrupt()) {
            /* Do nothing */
        } else if (interrupt_request & CPU_INTERRUPT_HALT) {
            replay_interrupt();
            cpu->interrupt_request &= ~CPU_INTERRUPT_HALT;
            cpu->halted = 1;
            cpu->exception_index = EXCP_HLT;
            qemu_mutex_unlock_iothread();
            return true;
        }
#if defined(TARGET_I386)
        else if (interrupt_request & CPU_INTERRUPT_INIT) {
            X86CPU *x86_cpu = X86_CPU(cpu);
            CPUArchState *env = &x86_cpu->env;
            replay_interrupt();
            cpu_svm_check_intercept_param(env, SVM_EXIT_INIT, 0, 0);
            do_cpu_init(x86_cpu);
            cpu->exception_index = EXCP_HALTED;
            qemu_mutex_unlock_iothread();
            return true;
        }
#else
        else if (interrupt_request & CPU_INTERRUPT_RESET) {
            replay_interrupt();
            cpu_reset(cpu);
            qemu_mutex_unlock_iothread();
            return true;
        }
#endif
        /* The target hook has 3 exit conditions:
           False when the interrupt isn't processed,
           True when it is, and we should restart on a new TB,
           and via longjmp via cpu_loop_exit.  */
        else {
            if (cc->tcg_ops->cpu_exec_interrupt &&
                cc->tcg_ops->cpu_exec_interrupt(cpu, interrupt_request)) {
                if (need_replay_interrupt(interrupt_request)) {
                    replay_interrupt();
                }
                /*
                 * After processing the interrupt, ensure an EXCP_DEBUG is
                 * raised when single-stepping so that GDB doesn't miss the
                 * next instruction.
                 */
                cpu->exception_index =
                    (cpu->singlestep_enabled ? EXCP_DEBUG : -1);
                *last_tb = NULL;
            }
            /* The target hook may have updated the 'cpu->interrupt_request';
             * reload the 'interrupt_request' value */
            interrupt_request = cpu->interrupt_request;
        }
        if (interrupt_request & CPU_INTERRUPT_EXITTB) {
            cpu->interrupt_request &= ~CPU_INTERRUPT_EXITTB;
            /* ensure that no TB jump will be modified as
               the program flow was changed */
            *last_tb = NULL;
        }

        /* If we exit via cpu_loop_exit/longjmp it is reset in cpu_exec */
        qemu_mutex_unlock_iothread();
    }

    /* Finally, check if we need to exit to the main loop.  */
    if (unlikely(qatomic_read(&cpu->exit_request))
        || (icount_enabled()
            && (cpu->cflags_next_tb == -1 || cpu->cflags_next_tb & CF_USE_ICOUNT)
            && cpu_neg(cpu)->icount_decr.u16.low + cpu->icount_extra == 0)) {
        qatomic_set(&cpu->exit_request, 0);
        if (cpu->exception_index == -1) {
            cpu->exception_index = EXCP_INTERRUPT;
        }
        return true;
    }

    return false;
}

static inline void cpu_loop_exec_tb(CPUState *cpu, TranslationBlock *tb,
                                    TranslationBlock **last_tb, int *tb_exit)
{
    int32_t insns_left;

    trace_exec_tb(tb, tb->pc);
    tb = cpu_tb_exec(cpu, tb, tb_exit);
    if (*tb_exit != TB_EXIT_REQUESTED) {
        *last_tb = tb;
        return;
    }

    *last_tb = NULL;
    insns_left = qatomic_read(&cpu_neg(cpu)->icount_decr.u32);
    if (insns_left < 0) {
        /* Something asked us to stop executing chained TBs; just
         * continue round the main loop. Whatever requested the exit
         * will also have set something else (eg exit_request or
         * interrupt_request) which will be handled by
         * cpu_handle_interrupt.  cpu_handle_interrupt will also
         * clear cpu->icount_decr.u16.high.
         */
        return;
    }

    /* Instruction counter expired.  */
    assert(icount_enabled());
#ifndef CONFIG_USER_ONLY
    /* Ensure global icount has gone forward */
    icount_update(cpu);
    /* Refill decrementer and continue execution.  */
    insns_left = MIN(CF_COUNT_MASK, cpu->icount_budget);
    cpu_neg(cpu)->icount_decr.u16.low = insns_left;
    cpu->icount_extra = cpu->icount_budget - insns_left;

    /*
     * If the next tb has more instructions than we have left to
     * execute we need to ensure we find/generate a TB with exactly
     * insns_left instructions in it.
     */
    if (!cpu->icount_extra && insns_left > 0 && insns_left < tb->icount)  {
        cpu->cflags_next_tb = (tb->cflags & ~CF_COUNT_MASK) | insns_left;
    }
#endif
}

/* main execution loop */

int cpu_exec(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    int ret;
    SyncClocks sc = { 0 };

    /* replay_interrupt may need current_cpu */
    current_cpu = cpu;

    if (cpu_handle_halt(cpu)) {
        return EXCP_HALTED;
    }

    rcu_read_lock();

    cpu_exec_enter(cpu);

    /* Calculate difference between guest clock and host clock.
     * This delay includes the delay of the last cycle, so
     * what we have to do is sleep until it is 0. As for the
     * advance/delay we gain here, we try to fix it next time.
     */
    init_delay_params(&sc, cpu);

    /* prepare setjmp context for exception handling */
    if (sigsetjmp(cpu->jmp_env, 0) != 0) {
#if defined(__clang__)
        /*
         * Some compilers wrongly smash all local variables after
         * siglongjmp (the spec requires that only non-volatile locals
         * which are changed between the sigsetjmp and siglongjmp are
         * permitted to be trashed). There were bug reports for gcc
         * 4.5.0 and clang.  The bug is fixed in all versions of gcc
         * that we support, but is still unfixed in clang:
         *   https://bugs.llvm.org/show_bug.cgi?id=21183
         *
         * Reload essential local variables here for those compilers.
         * Newer versions of gcc would complain about this code (-Wclobbered),
         * so we only perform the workaround for clang.
         */
        cpu = current_cpu;
        cc = CPU_GET_CLASS(cpu);
#else
        /*
         * Non-buggy compilers preserve these locals; assert that
         * they have the correct value.
         */
        g_assert(cpu == current_cpu);
        g_assert(cc == CPU_GET_CLASS(cpu));
#endif

#ifndef CONFIG_SOFTMMU
        tcg_debug_assert(!have_mmap_lock());
#endif
        if (qemu_mutex_iothread_locked()) {
            qemu_mutex_unlock_iothread();
        }
        qemu_plugin_disable_mem_helpers(cpu);

        assert_no_pages_locked();
    }

    /* if an exception is pending, we execute it here */
    while (!cpu_handle_exception(cpu, &ret)) {
        TranslationBlock *last_tb = NULL;
        int tb_exit = 0;

        while (!cpu_handle_interrupt(cpu, &last_tb)) {
            uint32_t cflags = cpu->cflags_next_tb;
            TranslationBlock *tb;

            /* When requested, use an exact setting for cflags for the next
               execution.  This is used for icount, precise smc, and stop-
               after-access watchpoints.  Since this request should never
               have CF_INVALID set, -1 is a convenient invalid value that
               does not require tcg headers for cpu_common_reset.  */
            if (cflags == -1) {
                cflags = curr_cflags();
            } else {
                cpu->cflags_next_tb = -1;
            }

            tb = tb_find(cpu, last_tb, tb_exit, cflags);
            cpu_loop_exec_tb(cpu, tb, &last_tb, &tb_exit);
            /* Try to align the host and virtual clocks
               if the guest is in advance */
            align_clocks(&sc, cpu);
        }
    }

    cpu_exec_exit(cpu);
    rcu_read_unlock();

    return ret;
}

void tcg_exec_realizefn(CPUState *cpu, Error **errp)
{
    static bool tcg_target_initialized;
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (!tcg_target_initialized) {
        cc->tcg_ops->initialize();
        tcg_target_initialized = true;
    }
    tlb_init(cpu);
    qemu_plugin_vcpu_init_hook(cpu);

#ifndef CONFIG_USER_ONLY
    tcg_iommu_init_notifier_list(cpu);
#endif /* !CONFIG_USER_ONLY */
}

/* undo the initializations in reverse order */
void tcg_exec_unrealizefn(CPUState *cpu)
{
#ifndef CONFIG_USER_ONLY
    tcg_iommu_free_notifier_list(cpu);
#endif /* !CONFIG_USER_ONLY */

    qemu_plugin_vcpu_exit_hook(cpu);
    tlb_destroy(cpu);
}

#ifndef CONFIG_USER_ONLY

void dump_drift_info(void)
{
    if (!icount_enabled()) {
        return;
    }

    qemu_printf("Host - Guest clock  %"PRIi64" ms\n",
                (cpu_get_clock() - icount_get()) / SCALE_MS);
    if (icount_align_option) {
        qemu_printf("Max guest delay     %"PRIi64" ms\n",
                    -max_delay / SCALE_MS);
        qemu_printf("Max guest advance   %"PRIi64" ms\n",
                    max_advance / SCALE_MS);
    } else {
        qemu_printf("Max guest delay     NA\n");
        qemu_printf("Max guest advance   NA\n");
    }
}

#endif /* !CONFIG_USER_ONLY */
