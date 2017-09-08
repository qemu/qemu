/*
 * TCG Backend Data: constant pool.
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

typedef struct TCGLabelPoolData {
    struct TCGLabelPoolData *next;
    tcg_target_ulong data;
    tcg_insn_unit *label;
    intptr_t addend;
    int type;
} TCGLabelPoolData;


static void new_pool_label(TCGContext *s, tcg_target_ulong data, int type,
                           tcg_insn_unit *label, intptr_t addend)
{
    TCGLabelPoolData *n = tcg_malloc(sizeof(*n));
    TCGLabelPoolData *i, **pp;

    n->data = data;
    n->label = label;
    n->type = type;
    n->addend = addend;

    /* Insertion sort on the pool.  */
    for (pp = &s->pool_labels; (i = *pp) && i->data < data; pp = &i->next) {
        continue;
    }
    n->next = *pp;
    *pp = n;
}

/* To be provided by cpu/tcg-target.inc.c.  */
static void tcg_out_nop_fill(tcg_insn_unit *p, int count);

static bool tcg_out_pool_finalize(TCGContext *s)
{
    TCGLabelPoolData *p = s->pool_labels;
    tcg_target_ulong d, *a;

    if (p == NULL) {
        return true;
    }

    /* ??? Round up to qemu_icache_linesize, but then do not round
       again when allocating the next TranslationBlock structure.  */
    a = (void *)ROUND_UP((uintptr_t)s->code_ptr, sizeof(tcg_target_ulong));
    tcg_out_nop_fill(s->code_ptr, (tcg_insn_unit *)a - s->code_ptr);
    s->data_gen_ptr = a;

    /* Ensure the first comparison fails.  */
    d = p->data + 1;

    for (; p != NULL; p = p->next) {
        if (p->data != d) {
            d = p->data;
            if (unlikely((void *)a > s->code_gen_highwater)) {
                return false;
            }
            *a++ = d;
        }
        patch_reloc(p->label, p->type, (intptr_t)(a - 1), p->addend);
    }

    s->code_ptr = (void *)a;
    return true;
}
