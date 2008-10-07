/* vim:set shiftwidth=4 ts=4 et: */
/*
 * PXA255 Sharp Zaurus SL-6000 PDA platform
 *
 * Copyright (c) 2008 Dmitry Baryshkov
 *
 * Code based on spitz platform by Andrzej Zaborowski <balrog@zabor.org>
 * This code is licensed under the GNU GPL v2.
 */

#include "hw.h"
#include "pxa.h"
#include "arm-misc.h"
#include "sysemu.h"
#include "devices.h"
#include "sharpsl.h"
#include "pcmcia.h"
#include "block.h"
#include "boards.h"

#define TOSA_RAM    0x04000000
#define TOSA_ROM	0x00800000

#define TOSA_GPIO_nSD_DETECT	(9)
#define TOSA_GPIO_ON_RESET		(19)
#define TOSA_GPIO_CF_IRQ		(21)	/* CF slot0 Ready */
#define TOSA_GPIO_CF_CD			(13)
#define TOSA_GPIO_JC_CF_IRQ		(36)	/* CF slot1 Ready */

#define TOSA_SCOOP_GPIO_BASE	0
#define TOSA_GPIO_IR_POWERDWN	(TOSA_SCOOP_GPIO_BASE + 2)
#define TOSA_GPIO_SD_WP			(TOSA_SCOOP_GPIO_BASE + 3)
#define TOSA_GPIO_PWR_ON		(TOSA_SCOOP_GPIO_BASE + 4)

static void tosa_microdrive_attach(struct pxa2xx_state_s *cpu)
{
    struct pcmcia_card_s *md;
    int index;
    BlockDriverState *bs;

    index = drive_get_index(IF_IDE, 0, 0);
    if (index == -1)
        return;
    bs = drives_table[index].bdrv;
    if (bdrv_is_inserted(bs) && !bdrv_is_removable(bs)) {
        md = dscm1xxxx_init(bs);
        pxa2xx_pcmcia_attach(cpu->pcmcia[0], md);
    }
}

static void tosa_gpio_setup(struct pxa2xx_state_s *cpu,
                struct scoop_info_s *scp0,
                struct scoop_info_s *scp1)
{
    /* MMC/SD host */
    pxa2xx_mmci_handlers(cpu->mmc,
                    scoop_gpio_in_get(scp0)[TOSA_GPIO_SD_WP],
                    qemu_irq_invert(pxa2xx_gpio_in_get(cpu->gpio)[TOSA_GPIO_nSD_DETECT]));

    /* Handle reset */
    pxa2xx_gpio_out_set(cpu->gpio, TOSA_GPIO_ON_RESET, cpu->reset);

    /* PCMCIA signals: card's IRQ and Card-Detect */
    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[0],
                        pxa2xx_gpio_in_get(cpu->gpio)[TOSA_GPIO_CF_IRQ],
                        pxa2xx_gpio_in_get(cpu->gpio)[TOSA_GPIO_CF_CD]);

    pxa2xx_pcmcia_set_irq_cb(cpu->pcmcia[1],
                        pxa2xx_gpio_in_get(cpu->gpio)[TOSA_GPIO_JC_CF_IRQ],
                        NULL);

}

static struct arm_boot_info tosa_binfo = {
    .loader_start = PXA2XX_SDRAM_BASE,
    .ram_size = 0x04000000,
};

static void tosa_init(ram_addr_t ram_size, int vga_ram_size,
                const char *boot_device, DisplayState *ds,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    struct pxa2xx_state_s *cpu;
    struct scoop_info_s *scp0, *scp1;

    if (ram_size < (TOSA_RAM + TOSA_ROM + PXA2XX_INTERNAL_SIZE)) {
        fprintf(stderr, "This platform requires %i bytes of memory\n",
                TOSA_RAM + TOSA_ROM + PXA2XX_INTERNAL_SIZE);
        exit(1);
    }

    if (!cpu_model)
        cpu_model = "pxa255";

    cpu = pxa255_init(tosa_binfo.ram_size, ds);

    cpu_register_physical_memory(0, TOSA_ROM,
                    qemu_ram_alloc(TOSA_ROM) | IO_MEM_ROM);

    tc6393xb_init(0x10000000, NULL);

    scp0 = scoop_init(cpu, 0, 0x08800000);
    scp1 = scoop_init(cpu, 1, 0x14800040);

    tosa_gpio_setup(cpu, scp0, scp1);

    tosa_microdrive_attach(cpu);

    /* Setup initial (reset) machine state */
    cpu->env->regs[15] = tosa_binfo.loader_start;

    tosa_binfo.kernel_filename = kernel_filename;
    tosa_binfo.kernel_cmdline = kernel_cmdline;
    tosa_binfo.initrd_filename = initrd_filename;
    tosa_binfo.board_id = 0x208;
    arm_load_kernel(cpu->env, &tosa_binfo);
    sl_bootparam_write(SL_PXA_PARAM_BASE - PXA2XX_SDRAM_BASE);
}

QEMUMachine tosapda_machine = {
    .name = "tosa",
    .desc = "Tosa PDA (PXA255)",
    .init = tosa_init,
    .ram_require = TOSA_RAM + TOSA_ROM + PXA2XX_INTERNAL_SIZE + RAMSIZE_FIXED,
};
