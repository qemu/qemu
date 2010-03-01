#ifndef HW_PC_H
#define HW_PC_H

#include "qemu-common.h"
#include "ioport.h"

/* PC-style peripherals (also used by other machines).  */

/* serial.c */

SerialState *serial_init(int base, qemu_irq irq, int baudbase,
                         CharDriverState *chr);
SerialState *serial_mm_init (target_phys_addr_t base, int it_shift,
                             qemu_irq irq, int baudbase,
                             CharDriverState *chr, int ioregister);
SerialState *serial_isa_init(int index, CharDriverState *chr);
void serial_set_frequency(SerialState *s, uint32_t frequency);

/* parallel.c */

typedef struct ParallelState ParallelState;
ParallelState *parallel_init(int index, CharDriverState *chr);
ParallelState *parallel_mm_init(target_phys_addr_t base, int it_shift, qemu_irq irq, CharDriverState *chr);

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

/* APIC */
typedef struct IOAPICState IOAPICState;
void apic_deliver_irq(uint8_t dest, uint8_t dest_mode,
                             uint8_t delivery_mode,
                             uint8_t vector_num, uint8_t polarity,
                             uint8_t trigger_mode);
int apic_init(CPUState *env);
int apic_accept_pic_intr(CPUState *env);
void apic_deliver_pic_intr(CPUState *env, int level);
int apic_get_interrupt(CPUState *env);
qemu_irq *ioapic_init(void);
void ioapic_set_irq(void *opaque, int vector, int level);
void apic_reset_irq_delivered(void);
int apic_get_irq_delivered(void);

/* i8254.c */

#define PIT_FREQ 1193182

typedef struct PITState PITState;

PITState *pit_init(int base, qemu_irq irq);
void pit_set_gate(PITState *pit, int channel, int val);
int pit_get_gate(PITState *pit, int channel);
int pit_get_initial_count(PITState *pit, int channel);
int pit_get_mode(PITState *pit, int channel);
int pit_get_out(PITState *pit, int channel, int64_t current_time);

void hpet_pit_disable(void);
void hpet_pit_enable(void);

/* vmport.c */
void vmport_init(void);
void vmport_register(unsigned char command, IOPortReadFunc *func, void *opaque);

/* vmmouse.c */
void *vmmouse_init(void *m);

/* pckbd.c */

void i8042_init(qemu_irq kbd_irq, qemu_irq mouse_irq, uint32_t io_base);
void i8042_mm_init(qemu_irq kbd_irq, qemu_irq mouse_irq,
                   target_phys_addr_t base, ram_addr_t size,
                   target_phys_addr_t mask);

/* mc146818rtc.c */

typedef struct RTCState RTCState;

RTCState *rtc_init(int base_year);
void rtc_set_memory(RTCState *s, int addr, int val);
void rtc_set_date(RTCState *s, const struct tm *tm);
void cmos_set_s3_resume(void);

/* pc.c */
extern int fd_bootchk;

void ioport_set_a20(int enable);
int ioport_get_a20(void);

/* acpi.c */
extern int acpi_enabled;
extern char *acpi_tables;
extern size_t acpi_tables_len;

void acpi_bios_init(void);
int acpi_table_add(const char *table_desc);

/* acpi_piix.c */
i2c_bus *piix4_pm_init(PCIBus *bus, int devfn, uint32_t smb_io_base,
                       qemu_irq sci_irq);
void piix4_smbus_register_device(SMBusDevice *dev, uint8_t addr);
void piix4_acpi_system_hot_add_init(PCIBus *bus);

/* hpet.c */
extern int no_hpet;

/* pcspk.c */
void pcspk_init(PITState *);
int pcspk_audio_init(qemu_irq *pic);

/* piix_pci.c */
struct PCII440FXState;
typedef struct PCII440FXState PCII440FXState;

PCIBus *i440fx_init(PCII440FXState **pi440fx_state, int *piix_devfn, qemu_irq *pic);
void i440fx_set_smm(PCII440FXState *d, int val);
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

int isa_vga_init(void);
int pci_vga_init(PCIBus *bus,
                 unsigned long vga_bios_offset, int vga_bios_size);
int isa_vga_mm_init(target_phys_addr_t vram_base,
                    target_phys_addr_t ctrl_base, int it_shift);

/* cirrus_vga.c */
void pci_cirrus_vga_init(PCIBus *bus);
void isa_cirrus_vga_init(void);

/* ne2000.c */

void isa_ne2000_init(int base, int irq, NICInfo *nd);

int cpu_is_bsp(CPUState *env);

/* e820 types */
#define E820_RAM        1
#define E820_RESERVED   2
#define E820_ACPI       3
#define E820_NVS        4
#define E820_UNUSABLE   5

int e820_add_entry(uint64_t, uint64_t, uint32_t);

#endif
