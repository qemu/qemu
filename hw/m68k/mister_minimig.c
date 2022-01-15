/*
 * MiSTer Minimig system emulation.
 *
 * Copyright (c) 2021 Mark Watson
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/m68k/mcf.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "sysemu/reset.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

#include <stdio.h>

void tb_invalidate_phys_range(unsigned long start, unsigned long end);

unsigned char volatile * chip_addr;
void * z3fastram_addr_shared;

      //      qemu_mutex_lock_iothread();
      //      mpc8xxx_gpio_set_irq(arg,mg->pin,mg->state);
      //      qemu_mutex_unlock_iothread();
      //      see:
      //      ../svn/Linux-Kernel_MiSTer/drivers/video/fbdev/MiSTer_fb.c
      //      also:
      //../svn/Minimig-AGA_MiSTer/sys/sys_top.v
      // wire [63:0] f2h_irq = {video_sync,HDMI_TX_VS};
      // cyclonev_hps_interface_interrupts interrupts
      // (
      //         .irq(f2h_irq)
      // );
      //  if (ioctl(fb, FBIO_WAITFORVSYNC, &zero) == -1)


static void mister_irq_func(void *opaque, int n, int level)
{
    M68kCPU *cpu = opaque;

 //   fprintf(stderr,"IRQ:%d\n",level);
    if (n==0)
    {
    	int irqlevel = 7&(level);
    	static int last = -1;
    	if (last!=irqlevel)
    	{
    	    last = irqlevel;
    	    m68k_set_irq_level(cpu, irqlevel, 24+irqlevel);
    	}
    }
}

static void mister_irq_init(M68kCPU *cpu)
{
    qemu_irq *mister_irq = qemu_allocate_irqs(mister_irq_func, cpu, 1);
    SysBusDevice *s;

    DeviceState *dev;
    dev = qdev_new("mister.interruptpoll");

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, mister_irq[0]);
    sysbus_mmio_map(s, 0, 0xdff01c);
    sysbus_mmio_map(s, 1, 0xdff09a);
    sysbus_mmio_map(s, 2, 0xbfd000);

    // 1c, 1e, 9a, 9c
}

extern int cia_written;

static void main_cpu_reset(void *opaque)
{
    fprintf(stderr,"CPU reset!: %d\n",gettid());
    M68kCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    unsigned int hpsbridgeaddr = 0xc0000000;
    tb_invalidate_phys_range(hpsbridgeaddr, (hpsbridgeaddr+0x200000));
    cia_written = 0;

    cpu_reset(cs);
    cpu->env.aregs[7] = ldl_phys(cs->as, 0);
    cpu->env.pc = ldl_phys(cs->as, 4);
}

static void mister_minimig_init(MachineState *machine)
{
    M68kCPU *cpu;
 //   CPUM68KState *env;

    ram_addr_t ram_size = machine->ram_size;
    fprintf(stderr,"qemu ram size:%d KB\n",ram_size);

    //hwaddr entry;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *chipram = g_new(MemoryRegion, 1);
 //   MemoryRegion *z2fastram = g_new(MemoryRegion, 1);
    MemoryRegion *z3fastram = g_new(MemoryRegion, 1);
    MemoryRegion *rtgcard = g_new(MemoryRegion, 1);
    MemoryRegion *hardware = g_new(MemoryRegion, 1);
    MemoryRegion *rom = g_new(MemoryRegion, 1);

    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    qemu_register_reset(main_cpu_reset, cpu);
 //   env = &cpu->env;

    unsigned int hpsbridgeaddr = 0xc0000000;
    int fduncached = open("/dev/mem",(O_RDWR|O_SYNC));
    int fdcached = open("/sys/kernel/debug/minimig_irq/mmap_cached",(O_RDWR));

    int chipram_bytes = 2*1024*1024;
    void * chipram_addr = mmap(NULL,chipram_bytes,(PROT_READ|PROT_WRITE),MAP_SHARED,fduncached,hpsbridgeaddr+0); //cached?
    chip_addr = chipram_addr;

  //  int z2fastram_bytes = 8*1024*1024;
//    unsigned int z2fast_physical = 0x38000000; // physical ddr address, shared with f2h bridge
 //   void * z2fastram_addr = mmap(NULL,z2fastram_bytes,(PROT_READ|PROT_WRITE),MAP_SHARED,fdcached,z2fast_physical);

    int z3fastram_bytes = 384*1024*1024;
    //int z3fastram_bytes = 8*1024*1024;
    unsigned int z3fast_physical = 0x28000000; // physical ddr address, shared with f2h bridge
    void * z3fastram_addr = mmap(NULL,z3fastram_bytes,(PROT_READ|PROT_WRITE),MAP_SHARED,fdcached,z3fast_physical);
//    void * z3fastram_addr = malloc(z3fastram_bytes);
//    int locked = mlock(z3fastram_addr,z3fastram_bytes);
//    fprintf(stderr,"Locked:%d\n",locked);
//    z3fastram_addr_shared = z3fastram_addr;


    int rtgcard_bytes = 8*1024*1024;
    //int z3fastram_bytes = 8*1024*1024;
    unsigned int rtg_physical = 0x27000000; // physical ddr address, shared with f2h bridge
    void * rtgcard_addr = mmap(NULL,rtgcard_bytes,(PROT_READ|PROT_WRITE),MAP_SHARED,fduncached,rtg_physical);

    int hardware_bytes =  13*1024*1024;
    void * hardware_addr = mmap(NULL,hardware_bytes,(PROT_READ|PROT_WRITE),MAP_SHARED,fduncached,hpsbridgeaddr + 0x200000);

    int rom_bytes = 1*1024*1024;
    void * rom_addr_orig = mmap(NULL,rom_bytes,(PROT_READ|PROT_WRITE),MAP_SHARED,fdcached,hpsbridgeaddr+0xf00000);
    void * rom_addr_fast = malloc(rom_bytes);
    for (int i=0;i!=rom_bytes;i+=4)
    {
	    *((unsigned int *)(rom_addr_fast+i)) = *((unsigned int *)(rom_addr_orig+i));
    }

    memory_region_init_ram_ptr(chipram, NULL, "mister_minimig.chipram", chipram_bytes, chipram_addr);
 //   memory_region_init_ram_ptr(z2fastram, NULL, "mister_minimig.z2fastram", z2fastram_bytes, z2fastram_addr );
    memory_region_init_ram_ptr(z3fastram, NULL, "mister_minimig.z3fastram", z3fastram_bytes, z3fastram_addr );
    memory_region_init_ram_ptr(rtgcard, NULL, "mister_minimig.rtg", rtgcard_bytes, rtgcard_addr );

    memory_region_init_ram_ptr(hardware, NULL, "mister_minimig.hardware", hardware_bytes, hardware_addr);
    memory_region_init_ram_ptr(rom, NULL, "mister_minimig.rom", rom_bytes, rom_addr_fast);
    rom->readonly = true;

    memory_region_add_subregion(address_space_mem, 0, chipram);
 //   memory_region_add_subregion(address_space_mem, 0x200000, z2fastram);
    memory_region_add_subregion(address_space_mem,  0x2000000, rtgcard);
    memory_region_add_subregion(address_space_mem, 0x40000000, z3fastram);
    //memory_region_add_subregion(address_space_mem, 0x40000000, machine->ram);
    memory_region_add_subregion(address_space_mem, 0x200000, hardware);
    memory_region_add_subregion(address_space_mem, 0xf00000, rom);

    mister_irq_init(cpu);
}

static void mister_minimig_machine_init(MachineClass *mc)
{
    mc->desc = "MiSTer minimig";
    mc->init = mister_minimig_init;
  //  mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68040");
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68020");
    mc->default_ram_id = "mister_minimig.ram";
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("mister_minimig", mister_minimig_machine_init)

