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
    tcg_insn_unit *label;
    intptr_t addend;
    int rtype;
    unsigned nlong;
    tcg_target_ulong data[];
} TCGLabelPoolData;


static TCGLabelPoolData *new_pool_alloc(TCGContext *s, int nlong, int rtype,
                                        tcg_insn_unit *label, intptr_t addend)
{
    TCGLabelPoolData *n = tcg_malloc(sizeof(TCGLabelPoolData)
                                     + sizeof(tcg_target_ulong) * nlong);

    n->label = label;
    n->addend = addend;
    n->rtype = rtype;
    n->nlong = nlong;
    return n;
}

static void new_pool_insert(TCGContext *s, TCGLabelPoolData *n)
{
    TCGLabelPoolData *i, **pp;
    int nlong = n->nlong;

    /* Insertion sort on the pool.  */
    for (pp = &s->pool_labels; (i = *pp) != NULL; pp = &i->next) {
        if (nlong > i->nlong) {
            break;
        }
        if (nlong < i->nlong) {
            continue;
        }
        if (memcmp(n->data, i->data, sizeof(tcg_target_ulong) * nlong) >= 0) {
            break;
        }
    }
    n->next = *pp;
    *pp = n;
}

/* The "usual" for generic integer code.  */
static inline void new_pool_label(TCGContext *s, tcg_target_ulong d, int rtype,
                                  tcg_insn_unit *label, intptr_t addend)
{
    TCGLabelPoolData *n = new_pool_alloc(s, 1, rtype, label, addend);
    n->data[0] = d;
    new_pool_insert(s, n);
}

/* For v64 or v128, depending on the host.  */
static inline void new_pool_l2(TCGContext *s, int rtype, tcg_insn_unit *label,
                               intptr_t addend, tcg_target_ulong d0,
                               tcg_target_ulong d1)
{
    TCGLabelPoolData *n = new_pool_alloc(s, 2, rtype, label, addend);
    n->data[0] = d0;
    n->data[1] = d1;
    new_pool_insert(s, n);
}

/* For v128 or v256, depending on the host.  */
static inline void new_pool_l4(TCGContext *s, int rtype, tcg_insn_unit *label,
                               intptr_t addend, tcg_target_ulong d0,
                               tcg_target_ulong d1, tcg_target_ulong d2,
                               tcg_target_ulong d3)
{
    TCGLabelPoolData *n = new_pool_alloc(s, 4, rtype, label, addend);
    n->data[0] = d0;
    n->data[1] = d1;
    n->data[2] = d2;
    n->data[3] = d3;
    new_pool_insert(s, n);
}

/* For v256, for 32-bit host.  */
static inline void new_pool_l8(TCGContext *s, int rtype, tcg_insn_unit *label,
                               intptr_t addend, tcg_target_ulong d0,
                               tcg_target_ulong d1, tcg_target_ulong d2,
                               tcg_target_ulong d3, tcg_target_ulong d4,
                               tcg_target_ulong d5, tcg_target_ulong d6,
                               tcg_target_ulong d7)
{
    TCGLabelPoolData *n = new_pool_alloc(s, 8, rtype, label, addend);
    n->data[0] = d0;
    n->data[1] = d1;
    n->data[2] = d2;
    n->data[3] = d3;
    n->data[4] = d4;
    n->data[5] = d5;
    n->data[6] = d6;
    n->data[7] = d7;
    new_pool_insert(s, n);
}

/* To be provided by cpu/tcg-target.inc.c.  */
static void tcg_out_nop_fill(tcg_insn_unit *p, int count);

static int tcg_out_pool_finalize(TCGContext *s)
{
    TCGLabelPoolData *p = s->pool_labels;
    TCGLabelPoolData *l = NULL;
    void *a;

    if (p == NULL) {
        return 0;
    }

    /* ??? Round up to qemu_icache_linesize, but then do not round
       again when allocating the next TranslationBlock structure.  */
    a = (void *)ROUND_UP((uintptr_t)s->code_ptr,
                         sizeof(tcg_target_ulong) * p->nlong);
    tcg_out_nop_fill(s->code_ptr, (tcg_insn_unit *)a - s->code_ptr);
    s->data_gen_ptr = a;

    for (; p != NULL; p = p->next) {
        size_t size = sizeof(tcg_target_ulong) * p->nlong;
        if (!l || l->nlong != p->nlong || memcmp(l->data, p->data, size)) {
            if (unlikely(a > s->code_gen_highwater)) {
                return -1;
            }
            memcpy(a, p->data, size);
            a += size;
            l = p;
        }
        if (!patch_reloc(p->label, p->rtype, (intptr_t)a - size, p->addend)) {
            return -2;
        }
    }

    s->code_ptr = a;
    return 0;
}
