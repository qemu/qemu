#ifndef HW_PC_H
#define HW_PC_H

#include "qemu-common.h"
#include "ioport.h"
#include "isa.h"
#include "fdc.h"
#include "net.h"

/* PC-style peripherals (also used by other machines).  */

/* serial.c */

SerialState *serial_init(int base, qemu_irq irq, int baudbase,
                         CharDriverState *chr);
SerialState *serial_mm_init (target_phys_addr_t base, int it_shift,
                             qemu_irq irq, int baudbase,
                             CharDriverState *chr, int ioregister,
                             int be);
static inline bool serial_isa_init(int index, CharDriverState *chr)
{
    ISADevice *dev;

    dev = isa_try_create("isa-serial");
    if (!dev) {
        return false;
    }
    qdev_prop_set_uint32(&dev->qdev, "index", index);
    qdev_prop_set_chr(&dev->qdev, "chardev", chr);
    if (qdev_init(&dev->qdev) < 0) {
        return false;
    }
    return true;
}

void serial_set_frequency(SerialState *s, uint32_t frequency);

/* parallel.c */
static inline bool parallel_init(int index, CharDriverState *chr)
{
    ISADevice *dev;

    dev = isa_try_create("isa-parallel");
    if (!dev) {
        return false;
    }
    qdev_prop_set_uint32(&dev->qdev, "index", index);
    qdev_prop_set_chr(&dev->qdev, "chardev", chr);
    if (qdev_init(&dev->qdev) < 0) {
        return false;
    }
    return true;
}

bool parallel_mm_init(target_phys_addr_t base, int it_shift, qemu_irq irq,
                      CharDriverState *chr);

/* i8259.c */

typedef struct PicState2 PicState2;
extern PicState2 *isa_pic;
void pic_set_irq(int irq, int level);
void pic_set_irq_new(void *opaque, int irq, int level);
qemu_irq *i8259_init(qemu_irq parent_irq);
int pic_read_irq(PicState2 *s);
void pic_update_irq(PicState2 *s);
uint32_t pic_intack_read(PicState2 *s);
void pic_info(Monitor *mon);
void irq_info(Monitor *mon);

/* ISA */
#define IOAPIC_NUM_PINS 0x18

typedef struct isa_irq_state {
    qemu_irq *i8259;
    qemu_irq ioapic[IOAPIC_NUM_PINS];
} IsaIrqState;

void isa_irq_handler(void *opaque, int n, int level);

/* i8254.c */

#define PIT_FREQ 1193182

static inline ISADevice *pit_init(int base, int irq)
{
    ISADevice *dev;

    dev = isa_create("isa-pit");
    qdev_prop_set_uint32(&dev->qdev, "iobase", base);
    qdev_prop_set_uint32(&dev->qdev, "irq", irq);
    qdev_init_nofail(&dev->qdev);

    return dev;
}

void pit_set_gate(ISADevice *dev, int channel, int val);
int pit_get_gate(ISADevice *dev, int channel);
int pit_get_initial_count(ISADevice *dev, int channel);
int pit_get_mode(ISADevice *dev, int channel);
int pit_get_out(ISADevice *dev, int channel, int64_t current_time);

void hpet_pit_disable(void);
void hpet_pit_enable(void);

/* vmport.c */
static inline void vmport_init(void)
{
    isa_create_simple("vmport");
}
void vmport_register(unsigned char command, IOPortReadFunc *func, void *opaque);
void vmmouse_get_data(uint32_t *data);
void vmmouse_set_data(const uint32_t *data);

/* pckbd.c */

void i8042_init(qemu_irq kbd_irq, qemu_irq mouse_irq, uint32_t io_base);
void i8042_mm_init(qemu_irq kbd_irq, qemu_irq mouse_irq,
                   target_phys_addr_t base, ram_addr_t size,
                   target_phys_addr_t mask);
void i8042_isa_mouse_fake_event(void *opaque);
void i8042_setup_a20_line(ISADevice *dev, qemu_irq *a20_out);

/* pc.c */
extern int fd_bootchk;

void pc_register_ferr_irq(qemu_irq irq);
void pc_cmos_set_s3_resume(void *opaque, int irq, int level);
void pc_acpi_smi_interrupt(void *opaque, int irq, int level);

void pc_cpus_init(const char *cpu_model);
void pc_memory_init(const char *kernel_filename,
                    const char *kernel_cmdline,
                    const char *initrd_filename,
                    ram_addr_t below_4g_mem_size,
                    ram_addr_t above_4g_mem_size);
qemu_irq *pc_allocate_cpu_irq(void);
void pc_vga_init(PCIBus *pci_bus);
void pc_basic_device_init(qemu_irq *isa_irq,
                          ISADevice **rtc_state,
                          bool no_vmport);
void pc_init_ne2k_isa(NICInfo *nd);
void pc_cmos_init(ram_addr_t ram_size, ram_addr_t above_4g_mem_size,
                  const char *boot_device,
                  BusState *ide0, BusState *ide1,
                  ISADevice *s);
void pc_pci_device_init(PCIBus *pci_bus);

typedef void (*cpu_set_smm_t)(int smm, void *arg);
void cpu_smm_register(cpu_set_smm_t callback, void *arg);

/* acpi.c */
extern int acpi_enabled;
extern char *acpi_tables;
extern size_t acpi_tables_len;

void acpi_bios_init(void);
int acpi_table_add(const char *table_desc);

/* acpi_piix.c */

i2c_bus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                       qemu_irq sci_irq, qemu_irq cmos_s3, qemu_irq smi_irq,
                       int kvm_enabled);
void piix4_smbus_register_device(SMBusDevice *dev, uint8_t addr);

/* hpet.c */
extern int no_hpet;

/* pcspk.c */
void pcspk_init(ISADevice *pit);
int pcspk_audio_init(qemu_irq *pic);

/* piix_pci.c */
struct PCII440FXState;
typedef struct PCII440FXState PCII440FXState;

PCIBus *i440fx_init(PCII440FXState **pi440fx_state, int *piix_devfn, qemu_irq *pic, ram_addr_t ram_size);
PCIBus *i440fx_xen_init(PCII440FXState **pi440fx_state, int *piix3_devfn, qemu_irq *pic, ram_addr_t ram_size);
void i440fx_init_memory_mappings(PCII440FXState *d);

/* piix4.c */
extern PCIDevice *piix4_dev;
int piix4_init(PCIBus *bus, int devfn);

/* vga.c */
enum vga_retrace_method {
    VGA_RETRACE_DUMB,
    VGA_RETRACE_PRECISE
};

extern enum vga_retrace_method vga_retrace_method;

static inline int isa_vga_init(void)
{
    ISADevice *dev;

    dev = isa_try_create("isa-vga");
    if (!dev) {
        fprintf(stderr, "Warning: isa-vga not available\n");
        return 0;
    }
    qdev_init_nofail(&dev->qdev);
    return 1;
}

int pci_vga_init(PCIBus *bus);
int isa_vga_mm_init(target_phys_addr_t vram_base,
                    target_phys_addr_t ctrl_base, int it_shift);

/* cirrus_vga.c */
void pci_cirrus_vga_init(PCIBus *bus);
void isa_cirrus_vga_init(void);

/* ne2000.c */
static inline bool isa_ne2000_init(int base, int irq, NICInfo *nd)
{
    ISADevice *dev;

    qemu_check_nic_model(nd, "ne2k_isa");

    dev = isa_try_create("ne2k_isa");
    if (!dev) {
        return false;
    }
    qdev_prop_set_uint32(&dev->qdev, "iobase", base);
    qdev_prop_set_uint32(&dev->qdev, "irq",    irq);
    qdev_set_nic_properties(&dev->qdev, nd);
    qdev_init_nofail(&dev->qdev);
    return true;
}

/* e820 types */
#define E820_RAM        1
#define E820_RESERVED   2
#define E820_ACPI       3
#define E820_NVS        4
#define E820_UNUSABLE   5

int e820_add_entry(uint64_t, uint64_t, uint32_t);

#endif
