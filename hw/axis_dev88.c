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
#include "sysemu.h"
#include "etraxfs.h"

#define D(x)
#define DNAND(x)

struct nand_state_t
{
    NANDFlashState *nand;
    unsigned int rdy:1;
    unsigned int ale:1;
    unsigned int cle:1;
    unsigned int ce:1;
};

static struct nand_state_t nand_state;
static uint32_t nand_readl (void *opaque, target_phys_addr_t addr)
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
nand_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct nand_state_t *s = opaque;
    int rdy;

    DNAND(printf("%s addr=%x v=%x\n", __func__, addr, value));
    nand_setpins(s->nand, s->cle, s->ale, s->ce, 1, 0);
    nand_setio(s->nand, value);
    nand_getpins(s->nand, &rdy);
    s->rdy = rdy;
}

static CPUReadMemoryFunc *nand_read[] = {
    &nand_readl,
    &nand_readl,
    &nand_readl,
};

static CPUWriteMemoryFunc *nand_write[] = {
    &nand_writel,
    &nand_writel,
    &nand_writel,
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
    struct nand_state_t *nand;
    struct tempsensor_t tempsensor;
    uint32_t regs[0x5c / 4];
} gpio_state;

static uint32_t gpio_readl (void *opaque, target_phys_addr_t addr)
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

static void gpio_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    struct gpio_state_t *s = opaque;
    D(printf("%s %x=%x\n", __func__, addr, value));

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

static CPUReadMemoryFunc *gpio_read[] = {
    NULL, NULL,
    &gpio_readl,
};

static CPUWriteMemoryFunc *gpio_write[] = {
    NULL, NULL,
    &gpio_writel,
};

#define INTMEM_SIZE (128 * 1024)

static uint32_t bootstrap_pc;
static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);

    env->pc = bootstrap_pc;
}

static
void axisdev88_init (ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    DeviceState *dev;
    SysBusDevice *s;
    qemu_irq irq[30], nmi[2], *cpu_irq;
    void *etraxfs_dmac;
    struct etraxfs_dma_client *eth[2] = {NULL, NULL};
    int kernel_size;
    int i;
    int nand_regs;
    int gpio_regs;
    ram_addr_t phys_ram;
    ram_addr_t phys_intmem;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "crisv32";
    }
    env = cpu_init(cpu_model);
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    phys_ram = qemu_ram_alloc(ram_size);
    cpu_register_physical_memory(0x40000000, ram_size, phys_ram | IO_MEM_RAM);

    /* The ETRAX-FS has 128Kb on chip ram, the docs refer to it as the 
       internal memory.  */
    phys_intmem = qemu_ram_alloc(INTMEM_SIZE);
    cpu_register_physical_memory(0x38000000, INTMEM_SIZE,
                                 phys_intmem | IO_MEM_RAM);


      /* Attach a NAND flash to CS1.  */
    nand_state.nand = nand_init(NAND_MFR_STMICRO, 0x39);
    nand_regs = cpu_register_io_memory(0, nand_read, nand_write, &nand_state);
    cpu_register_physical_memory(0x10000000, 0x05000000, nand_regs);

    gpio_state.nand = &nand_state;
    gpio_regs = cpu_register_io_memory(0, gpio_read, gpio_write, &gpio_state);
    cpu_register_physical_memory(0x3001a000, 0x5c, gpio_regs);


    cpu_irq = cris_pic_init_cpu(env);
    dev = qdev_create(NULL, "etraxfs,pic");
    /* FIXME: Is there a proper way to signal vectors to the CPU core?  */
    qdev_set_prop_ptr(dev, "interrupt_vector", &env->interrupt_vector);
    qdev_init(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, 0x3001c000);
    sysbus_connect_irq(s, 0, cpu_irq[0]);
    sysbus_connect_irq(s, 1, cpu_irq[1]);
    for (i = 0; i < 30; i++) {
        irq[i] = qdev_get_irq_sink(dev, i);
    }
    nmi[0] = qdev_get_irq_sink(dev, 30);
    nmi[1] = qdev_get_irq_sink(dev, 31);

    etraxfs_dmac = etraxfs_dmac_init(env, 0x30000000, 10);
    for (i = 0; i < 10; i++) {
        /* On ETRAX, odd numbered channels are inputs.  */
        etraxfs_dmac_connect(etraxfs_dmac, i, irq + 7 + i, i & 1);
    }

    /* Add the two ethernet blocks.  */
    eth[0] = etraxfs_eth_init(&nd_table[0], env, 0x30034000, 1);
    if (nb_nics > 1)
        eth[1] = etraxfs_eth_init(&nd_table[1], env, 0x30036000, 2);

    /* The DMA Connector block is missing, hardwire things for now.  */
    etraxfs_dmac_connect_client(etraxfs_dmac, 0, eth[0]);
    etraxfs_dmac_connect_client(etraxfs_dmac, 1, eth[0] + 1);
    if (eth[1]) {
        etraxfs_dmac_connect_client(etraxfs_dmac, 6, eth[1]);
        etraxfs_dmac_connect_client(etraxfs_dmac, 7, eth[1] + 1);
    }

    /* 2 timers.  */
    sysbus_create_varargs("etraxfs,timer", 0x3001e000, irq[0x1b], nmi[1], NULL);
    sysbus_create_varargs("etraxfs,timer", 0x3005e000, irq[0x1b], nmi[1], NULL);

    for (i = 0; i < 4; i++) {
        sysbus_create_simple("etraxfs,serial", 0x30026000 + i * 0x2000,
                             irq[0x14 + i]);
    }

    if (kernel_filename) {
        uint64_t entry, high;
        int kcmdline_len;

        /* Boots a kernel elf binary, os/linux-2.6/vmlinux from the axis 
           devboard SDK.  */
        kernel_size = load_elf(kernel_filename, -0x80000000LL,
                               &entry, NULL, &high);
        bootstrap_pc = entry;
        if (kernel_size < 0) {
            /* Takes a kimage from the axis devboard SDK.  */
            kernel_size = load_image_targphys(kernel_filename, 0x40004000,
                                              ram_size);
            bootstrap_pc = 0x40004000;
            env->regs[9] = 0x40004000 + kernel_size;
        }
        env->regs[8] = 0x56902387; /* RAM init magic.  */

        if (kernel_cmdline && (kcmdline_len = strlen(kernel_cmdline))) {
            if (kcmdline_len > 256) {
                fprintf(stderr, "Too long CRIS kernel cmdline (max 256)\n");
                exit(1);
            }
            /* Let the kernel know we are modifying the cmdline.  */
            env->regs[10] = 0x87109563;
            env->regs[11] = 0x40000000;
            pstrcpy_targphys(env->regs[11], 256, kernel_cmdline);
        }
    }
    env->pc = bootstrap_pc;

    printf ("pc =%x\n", env->pc);
    printf ("ram size =%ld\n", ram_size);
}

static QEMUMachine axisdev88_machine = {
    .name = "axis-dev88",
    .desc = "AXIS devboard 88",
    .init = axisdev88_init,
};

static void axisdev88_machine_init(void)
{
    qemu_register_machine(&axisdev88_machine);
}

machine_init(axisdev88_machine_init);
