/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC__TRANSLATOR_H
#define EXEC__TRANSLATOR_H

/*
 * Include this header from a target-specific file, and add a
 *
 *     DisasContextBase base;
 *
 * member in your target-specific DisasContext.
 */

#include "exec/memop.h"
#include "exec/vaddr.h"

/**
 * DisasJumpType:
 * @DISAS_NEXT: Next instruction in program order.
 * @DISAS_TOO_MANY: Too many instructions translated.
 * @DISAS_NORETURN: Following code is dead.
 * @DISAS_TARGET_*: Start of target-specific conditions.
 *
 * What instruction to disassemble next.
 */
typedef enum DisasJumpType {
    DISAS_NEXT,
    DISAS_TOO_MANY,
    DISAS_NORETURN,
    DISAS_TARGET_0,
    DISAS_TARGET_1,
    DISAS_TARGET_2,
    DISAS_TARGET_3,
    DISAS_TARGET_4,
    DISAS_TARGET_5,
    DISAS_TARGET_6,
    DISAS_TARGET_7,
    DISAS_TARGET_8,
    DISAS_TARGET_9,
    DISAS_TARGET_10,
    DISAS_TARGET_11,
} DisasJumpType;

/**
 * DisasContextBase:
 * @tb: Translation block for this disassembly.
 * @pc_first: Address of first guest instruction in this TB.
 * @pc_next: Address of next guest instruction in this TB (current during
 *           disassembly).
 * @is_jmp: What instruction to disassemble next.
 * @num_insns: Number of translated instructions (including current).
 * @max_insns: Maximum number of instructions to be translated in this TB.
 * @plugin_enabled: TCG plugin enabled in this TB.
 * @fake_insn: True if translator_fake_ldb used.
 * @insn_start: The last op emitted by the insn_start hook,
 *              which is expected to be INDEX_op_insn_start.
 *
 * Architecture-agnostic disassembly context.
 */
struct DisasContextBase {
    TranslationBlock *tb;
    vaddr pc_first;
    vaddr pc_next;
    DisasJumpType is_jmp;
    int num_insns;
    int max_insns;
    bool plugin_enabled;
    bool fake_insn;
    uint8_t code_mmuidx;
    struct TCGOp *insn_start;
    void *host_addr[2];

    /*
     * Record insn data that we cannot read directly from host memory.
     * There are only two reasons we cannot use host memory:
     * (1) We are executing from I/O,
     * (2) We are executing a synthetic instruction (s390x EX).
     * In both cases we need record exactly one instruction,
     * and thus the maximum amount of data we record is limited.
     */
    int record_start;
    int record_len;
    uint8_t record[32];
};

/**
 * TranslatorOps:
 * @init_disas_context:
 *      Initialize the target-specific portions of DisasContext struct.
 *      The generic DisasContextBase has already been initialized.
 *
 * @tb_start:
 *      Emit any code required before the start of the main loop,
 *      after the generic gen_tb_start().
 *
 * @insn_start:
 *      Emit the tcg_gen_insn_start opcode.
 *
 * @translate_insn:
 *      Disassemble one instruction and set db->pc_next for the start
 *      of the following instruction.  Set db->is_jmp as necessary to
 *      terminate the main loop.
 *
 * @tb_stop:
 *      Emit any opcodes required to exit the TB, based on db->is_jmp.
 *
 * @disas_log:
 *      Print instruction disassembly to log.
 */
typedef struct TranslatorOps {
    void (*init_disas_context)(DisasContextBase *db, CPUState *cpu);
    void (*tb_start)(DisasContextBase *db, CPUState *cpu);
    void (*insn_start)(DisasContextBase *db, CPUState *cpu);
    void (*translate_insn)(DisasContextBase *db, CPUState *cpu);
    void (*tb_stop)(DisasContextBase *db, CPUState *cpu);
    bool (*disas_log)(const DisasContextBase *db, CPUState *cpu, FILE *f);
} TranslatorOps;

/**
 * translator_loop:
 * @cpu: Target vCPU.
 * @tb: Translation block.
 * @max_insns: Maximum number of insns to translate.
 * @pc: guest virtual program counter address
 * @host_pc: host physical program counter address
 * @ops: Target-specific operations.
 * @db: Disassembly context.
 *
 * Generic translator loop.
 *
 * Translation will stop in the following cases (in order):
 * - When is_jmp set by #TranslatorOps::breakpoint_check.
 *   - set to DISAS_TOO_MANY exits after translating one more insn
 *   - set to any other value than DISAS_NEXT exits immediately.
 * - When is_jmp set by #TranslatorOps::translate_insn.
 *   - set to any value other than DISAS_NEXT exits immediately.
 * - When the TCG operation buffer is full.
 * - When single-stepping is enabled (system-wide or on the current vCPU).
 * - When too many instructions have been translated.
 */
void translator_loop(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                     vaddr pc, void *host_pc, const TranslatorOps *ops,
                     DisasContextBase *db);

/**
 * translator_use_goto_tb
 * @db: Disassembly context
 * @dest: target pc of the goto
 *
 * Return true if goto_tb is allowed between the current TB
 * and the destination PC.
 */
bool translator_use_goto_tb(DisasContextBase *db, vaddr dest);

/**
 * translator_io_start
 * @db: Disassembly context
 *
 * If icount is enabled, set cpu->can_do_io, adjust db->is_jmp to
 * DISAS_TOO_MANY if it is still DISAS_NEXT, and return true.
 * Otherwise return false.
 */
bool translator_io_start(DisasContextBase *db);

/*
 * Translator Load Functions
 *
 * These are intended to replace the direct usage of the cpu_ld*_code
 * functions and are mandatory for front-ends that have been migrated
 * to the common translator_loop. These functions are only intended
 * to be called from the translation stage and should not be called
 * from helper functions. Those functions should be converted to encode
 * the relevant information at translation time.
 */

uint8_t translator_ldub(CPUArchState *env, DisasContextBase *db, vaddr pc);
uint16_t translator_lduw_end(CPUArchState *env, DisasContextBase *db,
                             vaddr pc, MemOp endian);
uint32_t translator_ldl_end(CPUArchState *env, DisasContextBase *db,
                            vaddr pc, MemOp endian);
uint64_t translator_ldq_end(CPUArchState *env, DisasContextBase *db,
                            vaddr pc, MemOp endian);

#ifdef COMPILING_PER_TARGET
static inline uint16_t
translator_lduw(CPUArchState *env, DisasContextBase *db, vaddr pc)
{
    return translator_lduw_end(env, db, pc, MO_TE);
}

static inline uint32_t
translator_ldl(CPUArchState *env, DisasContextBase *db, vaddr pc)
{
    return translator_ldl_end(env, db, pc, MO_TE);
}

static inline uint64_t
translator_ldq(CPUArchState *env, DisasContextBase *db, vaddr pc)
{
    return translator_ldq_end(env, db, pc, MO_TE);
}

static inline uint16_t
translator_lduw_swap(CPUArchState *env, DisasContextBase *db,
                     vaddr pc, bool do_swap)
{
    return translator_lduw_end(env, db, pc, MO_TE ^ (do_swap * MO_BSWAP));
}

static inline uint32_t
translator_ldl_swap(CPUArchState *env, DisasContextBase *db,
                    vaddr pc, bool do_swap)
{
    return translator_ldl_end(env, db, pc, MO_TE ^ (do_swap * MO_BSWAP));
}

static inline uint64_t
translator_ldq_swap(CPUArchState *env, DisasContextBase *db,
                    vaddr pc, bool do_swap)
{
    return translator_ldq_end(env, db, pc, MO_TE ^ (do_swap * MO_BSWAP));
}
#endif /* COMPILING_PER_TARGET */

/**
 * translator_fake_ld - fake instruction load
 * @db: Disassembly context
 * @data: bytes of instruction
 * @len: number of bytes
 *
 * This is a special case helper used where the instruction we are
 * about to translate comes from somewhere else (e.g. being
 * re-synthesised for s390x "ex"). It ensures we update other areas of
 * the translator with details of the executed instruction.
 */
void translator_fake_ld(DisasContextBase *db, const void *data, size_t len);

/**
 * translator_st
 * @db: disassembly context
 * @dest: address to copy into
 * @addr: virtual address within TB
 * @len: length
 *
 * Copy @len bytes from @addr into @dest.
 * All bytes must have been read during translation.
 * Return true on success or false on failure.
 */
bool translator_st(const DisasContextBase *db, void *dest,
                   vaddr addr, size_t len);

/**
 * translator_st_len
 * @db: disassembly context
 *
 * Return the number of bytes available to copy from the
 * current translation block with translator_st.
 */
size_t translator_st_len(const DisasContextBase *db);

/**
 * translator_is_same_page
 * @db: disassembly context
 * @addr: virtual address within TB
 *
 * Return whether @addr is on the same page as where disassembly started.
 * Translators can use this to enforce the rule that only single-insn
 * translation blocks are allowed to cross page boundaries.
 */
bool translator_is_same_page(const DisasContextBase *db, vaddr addr);

#endif /* EXEC__TRANSLATOR_H */
