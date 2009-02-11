/*
 * QEMU IRQ/GPIO common code.
 *
 * Copyright (c) 2007 CodeSourcery.
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
#include "qemu-common.h"
#include "irq.h"

struct IRQState {
    qemu_irq_handler handler;
    void *opaque;
    int n;
};

void qemu_set_irq(qemu_irq irq, int level)
{
    if (!irq)
        return;

    irq->handler(irq->opaque, irq->n, level);
}

qemu_irq *qemu_allocate_irqs(qemu_irq_handler handler, void *opaque, int n)
{
    qemu_irq *s;
    struct IRQState *p;
    int i;

    s = (qemu_irq *)qemu_mallocz(sizeof(qemu_irq) * n);
    p = (struct IRQState *)qemu_mallocz(sizeof(struct IRQState) * n);
    for (i = 0; i < n; i++) {
        p->handler = handler;
        p->opaque = opaque;
        p->n = i;
        s[i] = p;
        p++;
    }
    return s;
}

void qemu_free_irqs(qemu_irq *s)
{
    qemu_free(s[0]);
    qemu_free(s);
}

static void qemu_notirq(void *opaque, int line, int level)
{
    struct IRQState *irq = opaque;

    irq->handler(irq->opaque, irq->n, !level);
}

qemu_irq qemu_irq_invert(qemu_irq irq)
{
    /* The default state for IRQs is low, so raise the output now.  */
    qemu_irq_raise(irq);
    return qemu_allocate_irqs(qemu_notirq, irq, 1)[0];
}
