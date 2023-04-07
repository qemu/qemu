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

/*
 * Generate TB finalization at the end of block
 */

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l);
static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l);

static int tcg_out_ldst_finalize(TCGContext *s)
{
    TCGLabelQemuLdst *lb;

    /* qemu_ld/st slow paths */
    QSIMPLEQ_FOREACH(lb, &s->ldst_labels, next) {
        if (lb->is_ld
            ? !tcg_out_qemu_ld_slow_path(s, lb)
            : !tcg_out_qemu_st_slow_path(s, lb)) {
            return -2;
        }

        /* Test for (pending) buffer overflow.  The assumption is that any
           one operation beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           generating code without having to check during generation.  */
        if (unlikely((void *)s->code_ptr > s->code_gen_highwater)) {
            return -1;
        }
    }
    return 0;
}

/*
 * Allocate a new TCGLabelQemuLdst entry.
 */

static inline TCGLabelQemuLdst *new_ldst_label(TCGContext *s)
{
    TCGLabelQemuLdst *l = tcg_malloc(sizeof(*l));

    memset(l, 0, sizeof(*l));
    QSIMPLEQ_INSERT_TAIL(&s->ldst_labels, l, next);

    return l;
}
