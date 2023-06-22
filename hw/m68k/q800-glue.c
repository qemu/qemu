/*
 * QEMU q800 logic GLUE (General Logic Unit)
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
#include "cpu.h"
#include "hw/m68k/q800-glue.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/nmi.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

/*
 * The GLUE (General Logic Unit) is an Apple custom integrated circuit chip
 * that performs a variety of functions (RAM management, clock generation, ...).
 * The GLUE chip receives interrupt requests from various devices,
 * assign priority to each, and asserts one or more interrupt line to the
 * CPU.
 */

/*
 * The GLUE logic on the Quadra 800 supports 2 different IRQ routing modes
 * controlled from the VIA1 auxmode GPIO (port B bit 6) which are documented
 * in NetBSD as follows:
 *
 * A/UX mode (Linux, NetBSD, auxmode GPIO low)
 *
 *   Level 0:        Spurious: ignored
 *   Level 1:        Software
 *   Level 2:        VIA2 (except ethernet, sound)
 *   Level 3:        Ethernet
 *   Level 4:        Serial (SCC)
 *   Level 5:        Sound
 *   Level 6:        VIA1
 *   Level 7:        NMIs: parity errors, RESET button, YANCC error
 *
 * Classic mode (default: used by MacOS, A/UX 3.0.1, auxmode GPIO high)
 *
 *   Level 0:        Spurious: ignored
 *   Level 1:        VIA1 (clock, ADB)
 *   Level 2:        VIA2 (NuBus, SCSI)
 *   Level 3:
 *   Level 4:        Serial (SCC)
 *   Level 5:
 *   Level 6:
 *   Level 7:        Non-maskable: parity errors, RESET button
 *
 * Note that despite references to A/UX mode in Linux and NetBSD, at least
 * A/UX 3.0.1 still uses Classic mode.
 */

static void GLUE_set_irq(void *opaque, int irq, int level)
{
    GLUEState *s = opaque;
    int i;

    if (s->auxmode) {
        /* Classic mode */
        switch (irq) {
        case GLUE_IRQ_IN_VIA1:
            irq = 0;
            break;

        case GLUE_IRQ_IN_VIA2:
            irq = 1;
            break;

        case GLUE_IRQ_IN_SONIC:
            /* Route to VIA2 instead */
            qemu_set_irq(s->irqs[GLUE_IRQ_NUBUS_9], level);
            return;

        case GLUE_IRQ_IN_ESCC:
            irq = 3;
            break;

        case GLUE_IRQ_IN_NMI:
            irq = 6;
            break;

        default:
            g_assert_not_reached();
        }
    } else {
        /* A/UX mode */
        switch (irq) {
        case GLUE_IRQ_IN_VIA1:
            irq = 5;
            break;

        case GLUE_IRQ_IN_VIA2:
            irq = 1;
            break;

        case GLUE_IRQ_IN_SONIC:
            irq = 2;
            break;

        case GLUE_IRQ_IN_ESCC:
            irq = 3;
            break;

        case GLUE_IRQ_IN_NMI:
            irq = 6;
            break;

        default:
            g_assert_not_reached();
        }
    }

    if (level) {
        s->ipr |= 1 << irq;
    } else {
        s->ipr &= ~(1 << irq);
    }

    for (i = 7; i >= 0; i--) {
        if ((s->ipr >> i) & 1) {
            m68k_set_irq_level(s->cpu, i + 1, i + 25);
            return;
        }
    }
    m68k_set_irq_level(s->cpu, 0, 0);
}

static void glue_auxmode_set_irq(void *opaque, int irq, int level)
{
    GLUEState *s = GLUE(opaque);

    s->auxmode = level;
}

static void glue_nmi(NMIState *n, int cpu_index, Error **errp)
{
    GLUEState *s = GLUE(n);

    /* Hold NMI active for 100ms */
    GLUE_set_irq(s, GLUE_IRQ_IN_NMI, 1);
    timer_mod(s->nmi_release, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
}

static void glue_nmi_release(void *opaque)
{
    GLUEState *s = GLUE(opaque);

    GLUE_set_irq(s, GLUE_IRQ_IN_NMI, 0);
}

static void glue_reset(DeviceState *dev)
{
    GLUEState *s = GLUE(dev);

    s->ipr = 0;
    s->auxmode = 0;

    timer_del(s->nmi_release);
}

static const VMStateDescription vmstate_glue = {
    .name = "q800-glue",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(ipr, GLUEState),
        VMSTATE_UINT8(auxmode, GLUEState),
        VMSTATE_TIMER_PTR(nmi_release, GLUEState),
        VMSTATE_END_OF_LIST(),
    },
};

/*
 * If the m68k CPU implemented its inbound irq lines as GPIO lines
 * rather than via the m68k_set_irq_level() function we would not need
 * this cpu link property and could instead provide outbound IRQ lines
 * that the board could wire up to the CPU.
 */
static Property glue_properties[] = {
    DEFINE_PROP_LINK("cpu", GLUEState, cpu, TYPE_M68K_CPU, M68kCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static void glue_finalize(Object *obj)
{
    GLUEState *s = GLUE(obj);

    timer_free(s->nmi_release);
}

static void glue_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    GLUEState *s = GLUE(dev);

    qdev_init_gpio_in(dev, GLUE_set_irq, 8);
    qdev_init_gpio_in_named(dev, glue_auxmode_set_irq, "auxmode", 1);

    qdev_init_gpio_out(dev, s->irqs, 1);

    /* NMI release timer */
    s->nmi_release = timer_new_ms(QEMU_CLOCK_VIRTUAL, glue_nmi_release, s);
}

static void glue_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    NMIClass *nc = NMI_CLASS(klass);

    dc->vmsd = &vmstate_glue;
    dc->reset = glue_reset;
    device_class_set_props(dc, glue_properties);
    nc->nmi_monitor_handler = glue_nmi;
}

static const TypeInfo glue_info_types[] = {
    {
        .name = TYPE_GLUE,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(GLUEState),
        .instance_init = glue_init,
        .instance_finalize = glue_finalize,
        .class_init = glue_class_init,
        .interfaces = (InterfaceInfo[]) {
             { TYPE_NMI },
             { }
        },
    },
};

DEFINE_TYPES(glue_info_types)
