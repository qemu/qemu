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
#define TCG_MAX_QEMU_LDST       640

typedef struct TCGLabelQemuLdst {
    bool is_ld:1;           /* qemu_ld: true, qemu_st: false */
    TCGMemOp opc:4;
    TCGReg addrlo_reg;      /* reg index for low word of guest virtual addr */
    TCGReg addrhi_reg;      /* reg index for high word of guest virtual addr */
    TCGReg datalo_reg;      /* reg index for low word to be loaded or stored */
    TCGReg datahi_reg;      /* reg index for high word to be loaded or stored */
    int mem_index;          /* soft MMU memory index */
    tcg_insn_unit *raddr;   /* gen code addr of the next IR of qemu_ld/st IR */
    tcg_insn_unit *label_ptr[2]; /* label pointers to be updated */
} TCGLabelQemuLdst;

typedef struct TCGBackendData {
    int nb_ldst_labels;
    TCGLabelQemuLdst ldst_labels[TCG_MAX_QEMU_LDST];
} TCGBackendData;


/*
 * Initialize TB backend data at the beginning of the TB.
 */

static inline void tcg_out_tb_init(TCGContext *s)
{
    s->be->nb_ldst_labels = 0;
}

/*
 * Generate TB finalization at the end of block
 */

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l);
static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l);

static void tcg_out_tb_finalize(TCGContext *s)
{
    TCGLabelQemuLdst *lb = s->be->ldst_labels;
    int i, n = s->be->nb_ldst_labels;

    /* qemu_ld/st slow paths */
    for (i = 0; i < n; i++) {
        if (lb[i].is_ld) {
            tcg_out_qemu_ld_slow_path(s, lb + i);
        } else {
            tcg_out_qemu_st_slow_path(s, lb + i);
        }
    }
}

/*
 * Allocate a new TCGLabelQemuLdst entry.
 */

static inline TCGLabelQemuLdst *new_ldst_label(TCGContext *s)
{
    TCGBackendData *be = s->be;
    int n = be->nb_ldst_labels;

    assert(n < TCG_MAX_QEMU_LDST);
    be->nb_ldst_labels = n + 1;
    return &be->ldst_labels[n];
}
#else
#include "tcg-be-null.h"
#endif /* CONFIG_SOFTMMU */
