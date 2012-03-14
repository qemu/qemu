/*
 * QEMU model for the AXIS devboard 88.
 *
 * Copyright (c) 2009 Edgar E. Iglesias, Axis Communications AB.
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

#include "sysbus.h"
#include "net.h"
#include "flash.h"
#include "boards.h"
#include "etraxfs.h"
#include "loader.h"
#include "elf.h"
#include "cris-boot.h"
#include "blockdev.h"
#include "exec-memory.h"

#define D(x)
#define DNAND(x)

struct nand_state_t
{
    DeviceState *nand;
    MemoryRegion iomem;
    unsigned int rdy:1;
    unsigned int ale:1;
    unsigned int cle:1;
    unsigned int ce:1;
};

static struct nand_state_t nand_state;
static uint64_t nand_read(void *opaque, target_phys_addr_t addr, unsigned size)
{
    struct nand_state_t *s = opaque;
    uint32_t r;
    int rdy;

    r = nand_getio(s->nand);
    nand_getpins(s->nand, &rdy);
    s->rdy = rdy;

    DNAND(printf("%s addr=%x r=%x\n", __func__, addr, r));
    return r;
}

static void
nand_write(void *opaque, target_phys_addr_t addr, uint64_t value,
           unsigned size)
{
    struct nand_state_t *s = opaque;
    int rdy;

    DNAND(printf("%s addr=%x v=%x\n", __func__, addr, (unsigned)value));
    nand_setpins(s->nand, s->cle, s->ale, s->ce, 1, 0);
    nand_setio(s->nand, value);
    nand_getpins(s->nand, &rdy);
    s->rdy = rdy;
}

static const MemoryRegionOps nand_ops = {
    .read = nand_read,
    .write = nand_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

struct tempsensor_t
{
    unsigned int shiftreg;
    unsigned int count;
    enum {
        ST_OUT, ST_IN, ST_Z
    } state;

    uint16_t regs[3];
};

static void tempsensor_clkedge(struct tempsensor_t *s,
                               unsigned int clk, unsigned int data_in)
{
    D(printf("%s clk=%d state=%d sr=%x\n", __func__,
             clk, s->state, s->shiftreg));
    if (s->count == 0) {
        s->count = 16;
        s->state = ST_OUT;
    }
    switch (s->state) {
        case ST_OUT:
            /* Output reg is clocked at negedge.  */
            if (!clk) {
                s->count--;
                s->shiftreg <<= 1;
                if (s->count == 0) {
                    s->shiftreg = 0;
                    s->state = ST_IN;
                    s->count = 16;
                }
            }
            break;
        case ST_Z:
            if (clk) {
                s->count--;
                if (s->count == 0) {
                    s->shiftreg = 0;
                    s->state = ST_OUT;
                    s->count = 16;
                }
            }
            break;
        case ST_IN:
            /* Indata is sampled at posedge.  */
            if (clk) {
                s->count--;
                s->shiftreg <<= 1;
                s->shiftreg |= data_in & 1;
                if (s->count == 0) {
                    D(printf("%s cfgreg=%x\n", __func__, s->shiftreg));
                    s->regs[0] = s->shiftreg;
                    s->state = ST_OUT;
                    s->count = 16;

                    if ((s->regs[0] & 0xff) == 0) {
                        /* 25 degrees celcius.  */
                        s->shiftreg = 0x0b9f;
                    } else if ((s->regs[0] & 0xff) == 0xff) {
                        /* Sensor ID, 0x8100 LM70.  */
                        s->shiftreg = 0x8100;
                    } else
                        printf("Invalid tempsens state %x\n", s->regs[0]);
                }
            }
            break;
    }
}


#define RW_PA_DOUT    0x00
#define R_PA_DIN      0x01
#define RW_PA_OE      0x02
#define RW_PD_DOUT    0x10
#define R_PD_DIN      0x11
#define RW_PD_OE      0x12

static struct gpio_state_t
{
    MemoryRegion iomem;
    struct nand_state_t *nand;
    struct tempsensor_t tempsensor;
    uint32_t regs[0x5c / 4];
} gpio_state;

static uint64_t gpio_read(void *opaque, target_phys_addr_t addr, unsigned size)
{
    struct gpio_state_t *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr)
    {
        case R_PA_DIN:
            r = s->regs[RW_PA_DOUT] & s->regs[RW_PA_OE];

            /* Encode pins from the nand.  */
            r |= s->nand->rdy << 7;
            break;
        case R_PD_DIN:
            r = s->regs[RW_PD_DOUT] & s->regs[RW_PD_OE];

            /* Encode temp sensor pins.  */
            r |= (!!(s->tempsensor.shiftreg & 0x10000)) << 4;
            break;

        default:
            r = s->regs[addr];
            break;
    }
    return r;
    D(printf("%s %x=%x\n", __func__, addr, r));
}

static void gpio_write(void *opaque, target_phys_addr_t addr, uint64_t value,
                       unsigned size)
{
    struct gpio_state_t *s = opaque;
    D(printf("%s %x=%x\n", __func__, addr, (unsigned)value));

    addr >>= 2;
    switch (addr)
    {
        case RW_PA_DOUT:
            /* Decode nand pins.  */
            s->nand->ale = !!(value & (1 << 6));
            s->nand->cle = !!(value & (1 << 5));
            s->nand->ce  = !!(value & (1 << 4));

            s->regs[addr] = value;
            break;

        case RW_PD_DOUT:
            /* Temp sensor clk.  */
            if ((s->regs[addr] ^ value) & 2)
                tempsensor_clkedge(&s->tempsensor, !!(value & 2),
                                   !!(value & 16));
            s->regs[addr] = value;
            break;

        default:
            s->regs[addr] = value;
            break;
    }
}

static const MemoryRegionOps gpio_ops = {
    .read = gpio_read,
    .write = gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

#define INTMEM_SIZE (128 * 1024)

static struct cris_load_info li;

static
void axisdev88_init (ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUCRISState *env;
    DeviceState *dev;
    SysBusDevice *s;
    DriveInfo *nand;
    qemu_irq irq[30], nmi[2], *cpu_irq;
    void *etraxfs_dmac;
    struct etraxfs_dma_client *dma_eth;
    int i;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *phys_ram = g_new(MemoryRegion, 1);
    MemoryRegion *phys_intmem = g_new(MemoryRegion, 1);

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "crisv32";
    }
    env = cpu_init(cpu_model);

    /* allocate RAM */
    memory_region_init_ram(phys_ram, "axisdev88.ram", ram_size);
    vmstate_register_ram_global(phys_ram);
    memory_region_add_subregion(address_space_mem, 0x40000000, phys_ram);

    /* The ETRAX-FS has 128Kb on chip ram, the docs refer to it as the 
       internal memory.  */
    memory_region_init_ram(phys_intmem, "axisdev88.chipram", INTMEM_SIZE);
    vmstate_register_ram_global(phys_intmem);
    memory_region_add_subregion(address_space_mem, 0x38000000, phys_intmem);

      /* Attach a NAND flash to CS1.  */
    nand = drive_get(IF_MTD, 0, 0);
    nand_state.nand = nand_init(nand ? nand->bdrv : NULL,
                                NAND_MFR_STMICRO, 0x39);
    memory_region_init_io(&nand_state.iomem, &nand_ops, &nand_state,
                          "nand", 0x05000000);
    memory_region_add_subregion(address_space_mem, 0x10000000,
                                &nand_state.iomem);

    gpio_state.nand = &nand_state;
    memory_region_init_io(&gpio_state.iomem, &gpio_ops, &gpio_state,
                          "gpio", 0x5c);
    memory_region_add_subregion(address_space_mem, 0x3001a000,
                                &gpio_state.iomem);


    cpu_irq = cris_pic_init_cpu(env);
    dev = qdev_create(NULL, "etraxfs,pic");
    /* FIXME: Is there a proper way to signal vectors to the CPU core?  */
    qdev_prop_set_ptr(dev, "interrupt_vector", &env->interrupt_vector);
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, 0x3001c000);
    sysbus_connect_irq(s, 0, cpu_irq[0]);
    sysbus_connect_irq(s, 1, cpu_irq[1]);
    for (i = 0; i < 30; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }
    nmi[0] = qdev_get_gpio_in(dev, 30);
    nmi[1] = qdev_get_gpio_in(dev, 31);

    etraxfs_dmac = etraxfs_dmac_init(0x30000000, 10);
    for (i = 0; i < 10; i++) {
        /* On ETRAX, odd numbered channels are inputs.  */
        etraxfs_dmac_connect(etraxfs_dmac, i, irq + 7 + i, i & 1);
    }

    /* Add the two ethernet blocks.  */
    dma_eth = g_malloc0(sizeof dma_eth[0] * 4); /* Allocate 4 channels.  */
    etraxfs_eth_init(&nd_table[0], 0x30034000, 1, &dma_eth[0], &dma_eth[1]);
    if (nb_nics > 1) {
        etraxfs_eth_init(&nd_table[1], 0x30036000, 2, &dma_eth[2], &dma_eth[3]);
    }

    /* The DMA Connector block is missing, hardwire things for now.  */
    etraxfs_dmac_connect_client(etraxfs_dmac, 0, &dma_eth[0]);
    etraxfs_dmac_connect_client(etraxfs_dmac, 1, &dma_eth[1]);
    if (nb_nics > 1) {
        etraxfs_dmac_connect_client(etraxfs_dmac, 6, &dma_eth[2]);
        etraxfs_dmac_connect_client(etraxfs_dmac, 7, &dma_eth[3]);
    }

    /* 2 timers.  */
    sysbus_create_varargs("etraxfs,timer", 0x3001e000, irq[0x1b], nmi[1], NULL);
    sysbus_create_varargs("etraxfs,timer", 0x3005e000, irq[0x1b], nmi[1], NULL);

    for (i = 0; i < 4; i++) {
        sysbus_create_simple("etraxfs,serial", 0x30026000 + i * 0x2000,
                             irq[0x14 + i]);
    }

    if (!kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    li.image_filename = kernel_filename;
    li.cmdline = kernel_cmdline;
    cris_load_image(env, &li);
}

static QEMUMachine axisdev88_machine = {
    .name = "axis-dev88",
    .desc = "AXIS devboard 88",
    .init = axisdev88_init,
    .is_default = 1,
};

static void axisdev88_machine_init(void)
{
    qemu_register_machine(&axisdev88_machine);
}

machine_init(axisdev88_machine_init);
