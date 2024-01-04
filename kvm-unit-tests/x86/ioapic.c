#include "libcflat.h"
#include "apic.h"
#include "vm.h"
#include "smp.h"
#include "desc.h"
#include "isr.h"

#define EDGE_TRIGGERED 0
#define LEVEL_TRIGGERED 1

static void set_ioapic_redir(unsigned line, unsigned vec, unsigned trig_mode)
{
	ioapic_redir_entry_t e = {
		.vector = vec,
		.delivery_mode = 0,
		.trig_mode = trig_mode,
	};

	ioapic_write_redir(line, e);
}

static void set_irq_line(unsigned line, int val)
{
	asm volatile("out %0, %1" : : "a"((u8)val), "d"((u16)(0x2000 + line)));
}

static void toggle_irq_line(unsigned line)
{
	set_irq_line(line, 1);
	set_irq_line(line, 0);
}

static void ioapic_reg_version(void)
{
	u8 version_offset;
	uint32_t data_read, data_write;

	version_offset = 0x01;
	data_read = ioapic_read_reg(version_offset);
	data_write = data_read ^ 0xffffffff;

	ioapic_write_reg(version_offset, data_write);
	report("version register read only test",
	       data_read == ioapic_read_reg(version_offset));
}

static void ioapic_reg_id(void)
{
	u8 id_offset;
	uint32_t data_read, data_write, diff;

	id_offset = 0x0;
	data_read = ioapic_read_reg(id_offset);
	data_write = data_read ^ 0xffffffff;

	ioapic_write_reg(id_offset, data_write);

	diff = data_read ^ ioapic_read_reg(id_offset);
	report("id register only bits [24:27] writable",
	       diff == 0x0f000000);
}

static void ioapic_arbitration_id(void)
{
	u8 id_offset, arb_offset;
	uint32_t write;

	id_offset = 0x0;
	arb_offset = 0x2;
	write = 0x0f000000;

	ioapic_write_reg(id_offset, write);
	report("arbitration register set by id",
	       ioapic_read_reg(arb_offset) == write);

	ioapic_write_reg(arb_offset, 0x0);
	report("arbtration register read only",
               ioapic_read_reg(arb_offset) == write);
}

static volatile int g_isr_76;

static void ioapic_isr_76(isr_regs_t *regs)
{
	++g_isr_76;
	eoi();
}

static void test_ioapic_edge_intr(void)
{
	handle_irq(0x76, ioapic_isr_76);
	set_ioapic_redir(0x0e, 0x76, EDGE_TRIGGERED);
	toggle_irq_line(0x0e);
	asm volatile ("nop");
	report("edge triggered intr", g_isr_76 == 1);
}

static volatile int g_isr_77;

static void ioapic_isr_77(isr_regs_t *regs)
{
	++g_isr_77;
	set_irq_line(0x0e, 0);
	eoi();
}

static void test_ioapic_level_intr(void)
{
	handle_irq(0x77, ioapic_isr_77);
	set_ioapic_redir(0x0e, 0x77, LEVEL_TRIGGERED);
	set_irq_line(0x0e, 1);
	asm volatile ("nop");
	report("level triggered intr", g_isr_77 == 1);
}

static int g_78, g_66, g_66_after_78;
static ulong g_66_rip, g_78_rip;

static void ioapic_isr_78(isr_regs_t *regs)
{
	++g_78;
	g_78_rip = regs->rip;
	eoi();
}

static void ioapic_isr_66(isr_regs_t *regs)
{
	++g_66;
	if (g_78)
		++g_66_after_78;
	g_66_rip = regs->rip;
	eoi();
}

static void test_ioapic_simultaneous(void)
{
	handle_irq(0x78, ioapic_isr_78);
	handle_irq(0x66, ioapic_isr_66);
	set_ioapic_redir(0x0e, 0x78, EDGE_TRIGGERED);
	set_ioapic_redir(0x0f, 0x66, EDGE_TRIGGERED);
	irq_disable();
	toggle_irq_line(0x0f);
	toggle_irq_line(0x0e);
	irq_enable();
	asm volatile ("nop");
	report("ioapic simultaneous edge interrupts",
	       g_66 && g_78 && g_66_after_78 && g_66_rip == g_78_rip);
}

static int g_isr_98;

static void ioapic_isr_98(isr_regs_t *regs)
{
	++g_isr_98;
	if (g_isr_98 == 1) {
		set_irq_line(0x0e, 0);
		set_irq_line(0x0e, 1);
	}
	set_irq_line(0x0e, 0);
	eoi();
}

static void test_ioapic_level_coalesce(void)
{
	handle_irq(0x98, ioapic_isr_98);
	set_ioapic_redir(0x0e, 0x98, LEVEL_TRIGGERED);
	set_irq_line(0x0e, 1);
	asm volatile ("nop");
	report("coalesce simultaneous level interrupts", g_isr_98 == 1);
}

static int g_isr_99;

static void ioapic_isr_99(isr_regs_t *regs)
{
	++g_isr_99;
	set_irq_line(0x0e, 0);
	eoi();
}

static void test_ioapic_level_sequential(void)
{
	handle_irq(0x99, ioapic_isr_99);
	set_ioapic_redir(0x0e, 0x99, LEVEL_TRIGGERED);
	set_irq_line(0x0e, 1);
	set_irq_line(0x0e, 1);
	asm volatile ("nop");
	report("sequential level interrupts", g_isr_99 == 2);
}

static volatile int g_isr_81;

static void ioapic_isr_81(isr_regs_t *regs)
{
	++g_isr_81;
	set_irq_line(0x0e, 0);
	eoi();
}

static void test_ioapic_edge_mask(void)
{
	handle_irq(0x81, ioapic_isr_81);
	set_ioapic_redir(0x0e, 0x81, EDGE_TRIGGERED);

	set_mask(0x0e, true);
	set_irq_line(0x0e, 1);
	set_irq_line(0x0e, 0);

	asm volatile ("nop");
	report("masked level interrupt", g_isr_81 == 0);

	set_mask(0x0e, false);
	set_irq_line(0x0e, 1);

	asm volatile ("nop");
	report("unmasked level interrupt", g_isr_81 == 1);
}

static volatile int g_isr_82;

static void ioapic_isr_82(isr_regs_t *regs)
{
	++g_isr_82;
	set_irq_line(0x0e, 0);
	eoi();
}

static void test_ioapic_level_mask(void)
{
	handle_irq(0x82, ioapic_isr_82);
	set_ioapic_redir(0x0e, 0x82, LEVEL_TRIGGERED);

	set_mask(0x0e, true);
	set_irq_line(0x0e, 1);

	asm volatile ("nop");
	report("masked level interrupt", g_isr_82 == 0);

	set_mask(0x0e, false);

	asm volatile ("nop");
	report("unmasked level interrupt", g_isr_82 == 1);
}


int main(void)
{
	setup_vm();
	smp_init();
	setup_idt();

	mask_pic_interrupts();
	enable_apic();

	irq_enable();

	ioapic_reg_version();
	ioapic_reg_id();
	ioapic_arbitration_id();

	test_ioapic_edge_intr();
	test_ioapic_level_intr();
	test_ioapic_simultaneous();
	test_ioapic_level_coalesce();
	test_ioapic_level_sequential();

	test_ioapic_edge_mask();
	test_ioapic_level_mask();

	return report_summary();
}
