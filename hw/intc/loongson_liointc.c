/*
 * QEMU Loongson Local I/O interrupt controller.
 *
 * Copyright (c) 2020 Huacai Chen <chenhc@lemote.com>
 * Copyright (c) 2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/intc/loongson_liointc.h"

#define NUM_IRQS                32

#define NUM_CORES               4
#define NUM_IPS                 4
#define NUM_PARENTS             (NUM_CORES * NUM_IPS)
#define PARENT_COREx_IPy(x, y)  (NUM_IPS * x + y)

#define R_MAPPER_START          0x0
#define R_MAPPER_END            0x20
#define R_ISR                   R_MAPPER_END
#define R_IEN                   0x24
#define R_IEN_SET               0x28
#define R_IEN_CLR               0x2c
#define R_ISR_SIZE              0x8
#define R_START                 0x40
#define R_END                   (R_START + R_ISR_SIZE * NUM_CORES)

struct loongson_liointc {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq parent_irq[NUM_PARENTS];

    uint8_t mapper[NUM_IRQS]; /* 0:3 for core, 4:7 for IP */
    uint32_t isr;
    uint32_t ien;
    uint32_t per_core_isr[NUM_CORES];

    /* state of the interrupt input pins */
    uint32_t pin_state;
    bool parent_state[NUM_PARENTS];
};

static void update_irq(struct loongson_liointc *p)
{
    uint32_t irq, core, ip;
    uint32_t per_ip_isr[NUM_IPS] = {0};

    /* level triggered interrupt */
    p->isr = p->pin_state;

    /* Clear disabled IRQs */
    p->isr &= p->ien;

    /* Clear per_core_isr */
    for (core = 0; core < NUM_CORES; core++) {
        p->per_core_isr[core] = 0;
    }

    /* Update per_core_isr and per_ip_isr */
    for (irq = 0; irq < NUM_IRQS; irq++) {
        if (!(p->isr & (1 << irq))) {
            continue;
        }

        for (core = 0; core < NUM_CORES; core++) {
            if ((p->mapper[irq] & (1 << core))) {
                p->per_core_isr[core] |= (1 << irq);
            }
        }

        for (ip = 0; ip < NUM_IPS; ip++) {
            if ((p->mapper[irq] & (1 << (ip + 4)))) {
                per_ip_isr[ip] |= (1 << irq);
            }
        }
    }

    /* Emit IRQ to parent! */
    for (core = 0; core < NUM_CORES; core++) {
        for (ip = 0; ip < NUM_IPS; ip++) {
            int parent = PARENT_COREx_IPy(core, ip);
            if (p->parent_state[parent] !=
                (!!p->per_core_isr[core] && !!per_ip_isr[ip])) {
                p->parent_state[parent] = !p->parent_state[parent];
                qemu_set_irq(p->parent_irq[parent], p->parent_state[parent]);
            }
        }
    }
}

static uint64_t
liointc_read(void *opaque, hwaddr addr, unsigned int size)
{
    struct loongson_liointc *p = opaque;
    uint32_t r = 0;

    /* Mapper is 1 byte */
    if (size == 1 && addr < R_MAPPER_END) {
        r = p->mapper[addr];
        goto out;
    }

    /* Rest are 4 bytes */
    if (size != 4 || (addr % 4)) {
        goto out;
    }

    if (addr >= R_START && addr < R_END) {
        hwaddr offset = addr - R_START;
        int core = offset / R_ISR_SIZE;

        if (offset % R_ISR_SIZE) {
            goto out;
        }
        r = p->per_core_isr[core];
        goto out;
    }

    switch (addr) {
    case R_ISR:
        r = p->isr;
        break;
    case R_IEN:
        r = p->ien;
        break;
    default:
        break;
    }

out:
    qemu_log_mask(CPU_LOG_INT, "%s: size=%d, addr=%"HWADDR_PRIx", val=%x\n",
                  __func__, size, addr, r);
    return r;
}

static void
liointc_write(void *opaque, hwaddr addr,
          uint64_t val64, unsigned int size)
{
    struct loongson_liointc *p = opaque;
    uint32_t value = val64;

    qemu_log_mask(CPU_LOG_INT, "%s: size=%d, addr=%"HWADDR_PRIx", val=%x\n",
                  __func__, size, addr, value);

    /* Mapper is 1 byte */
    if (size == 1 && addr < R_MAPPER_END) {
        p->mapper[addr] = value;
        goto out;
    }

    /* Rest are 4 bytes */
    if (size != 4 || (addr % 4)) {
        goto out;
    }

    if (addr >= R_START && addr < R_END) {
        hwaddr offset = addr - R_START;
        int core = offset / R_ISR_SIZE;

        if (offset % R_ISR_SIZE) {
            goto out;
        }
        p->per_core_isr[core] = value;
        goto out;
    }

    switch (addr) {
    case R_IEN_SET:
        p->ien |= value;
        break;
    case R_IEN_CLR:
        p->ien &= ~value;
        break;
    default:
        break;
    }

out:
    update_irq(p);
}

static const MemoryRegionOps pic_ops = {
    .read = liointc_read,
    .write = liointc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void irq_handler(void *opaque, int irq, int level)
{
    struct loongson_liointc *p = opaque;

    p->pin_state &= ~(1 << irq);
    p->pin_state |= level << irq;
    update_irq(p);
}

static void loongson_liointc_init(Object *obj)
{
    struct loongson_liointc *p = LOONGSON_LIOINTC(obj);
    int i;

    qdev_init_gpio_in(DEVICE(obj), irq_handler, 32);

    for (i = 0; i < NUM_PARENTS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &p->parent_irq[i]);
    }

    memory_region_init_io(&p->mmio, obj, &pic_ops, p,
                         TYPE_LOONGSON_LIOINTC, R_END);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &p->mmio);
}

static const TypeInfo loongson_liointc_info = {
    .name          = TYPE_LOONGSON_LIOINTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct loongson_liointc),
    .instance_init = loongson_liointc_init,
};

static void loongson_liointc_register_types(void)
{
    type_register_static(&loongson_liointc_info);
}

type_init(loongson_liointc_register_types)
