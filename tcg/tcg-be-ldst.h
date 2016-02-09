/*
 * TCG Backend Data: load-store optimization only.
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

#ifdef CONFIG_SOFTMMU

typedef struct TCGLabelQemuLdst {
    bool is_ld;             /* qemu_ld: true, qemu_st: false */
    TCGMemOpIdx oi;
    TCGType type;           /* result type of a load */
    TCGReg addrlo_reg;      /* reg index for low word of guest virtual addr */
    TCGReg addrhi_reg;      /* reg index for high word of guest virtual addr */
    TCGReg datalo_reg;      /* reg index for low word to be loaded or stored */
    TCGReg datahi_reg;      /* reg index for high word to be loaded or stored */
    tcg_insn_unit *raddr;   /* gen code addr of the next IR of qemu_ld/st IR */
    tcg_insn_unit *label_ptr[2]; /* label pointers to be updated */
    struct TCGLabelQemuLdst *next;
} TCGLabelQemuLdst;

typedef struct TCGBackendData {
    TCGLabelQemuLdst *labels;
} TCGBackendData;


/*
 * Initialize TB backend data at the beginning of the TB.
 */

static inline void tcg_out_tb_init(TCGContext *s)
{
    s->be->labels = NULL;
}

/*
 * Generate TB finalization at the end of block
 */

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l);
static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l);

static bool tcg_out_tb_finalize(TCGContext *s)
{
    TCGLabelQemuLdst *lb;

    /* qemu_ld/st slow paths */
    for (lb = s->be->labels; lb != NULL; lb = lb->next) {
        if (lb->is_ld) {
            tcg_out_qemu_ld_slow_path(s, lb);
        } else {
            tcg_out_qemu_st_slow_path(s, lb);
        }

        /* Test for (pending) buffer overflow.  The assumption is that any
           one operation beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           generating code without having to check during generation.  */
        if (unlikely((void *)s->code_ptr > s->code_gen_highwater)) {
            return false;
        }
    }
    return true;
}

/*
 * Allocate a new TCGLabelQemuLdst entry.
 */

static inline TCGLabelQemuLdst *new_ldst_label(TCGContext *s)
{
    TCGBackendData *be = s->be;
    TCGLabelQemuLdst *l = tcg_malloc(sizeof(*l));

    l->next = be->labels;
    be->labels = l;
    return l;
}
#else
#include "tcg-be-null.h"
#endif /* CONFIG_SOFTMMU */
