#include "libcflat.h"

struct rsdp_descriptor {        /* Root System Descriptor Pointer */
    u64 signature;              /* ACPI signature, contains "RSD PTR " */
    u8  checksum;               /* To make sum of struct == 0 */
    u8  oem_id [6];             /* OEM identification */
    u8  revision;               /* Must be 0 for 1.0, 2 for 2.0 */
    u32 rsdt_physical_address;  /* 32-bit physical address of RSDT */
    u32 length;                 /* XSDT Length in bytes including hdr */
    u64 xsdt_physical_address;  /* 64-bit physical address of XSDT */
    u8  extended_checksum;      /* Checksum of entire table */
    u8  reserved [3];           /* Reserved field must be 0 */
};

#define ACPI_TABLE_HEADER_DEF   /* ACPI common table header */ \
    u32 signature;          /* ACPI signature (4 ASCII characters) */ \
    u32 length;                 /* Length of table, in bytes, including header */ \
    u8  revision;               /* ACPI Specification minor version # */ \
    u8  checksum;               /* To make sum of entire table == 0 */ \
    u8  oem_id [6];             /* OEM identification */ \
    u8  oem_table_id [8];       /* OEM table identification */ \
    u32 oem_revision;           /* OEM revision number */ \
    u8  asl_compiler_id [4];    /* ASL compiler vendor ID */ \
    u32 asl_compiler_revision;  /* ASL compiler revision number */

#define RSDT_SIGNATURE 0x54445352
struct rsdt_descriptor_rev1 {
    ACPI_TABLE_HEADER_DEF
    u32 table_offset_entry[0];
};

#define FACP_SIGNATURE 0x50434146 // FACP
struct fadt_descriptor_rev1
{
    ACPI_TABLE_HEADER_DEF     /* ACPI common table header */
    u32 firmware_ctrl;          /* Physical address of FACS */
    u32 dsdt;                   /* Physical address of DSDT */
    u8  model;                  /* System Interrupt Model */
    u8  reserved1;              /* Reserved */
    u16 sci_int;                /* System vector of SCI interrupt */
    u32 smi_cmd;                /* Port address of SMI command port */
    u8  acpi_enable;            /* Value to write to smi_cmd to enable ACPI */
    u8  acpi_disable;           /* Value to write to smi_cmd to disable ACPI */
    u8  S4bios_req;             /* Value to write to SMI CMD to enter S4BIOS state */
    u8  reserved2;              /* Reserved - must be zero */
    u32 pm1a_evt_blk;           /* Port address of Power Mgt 1a acpi_event Reg Blk */
    u32 pm1b_evt_blk;           /* Port address of Power Mgt 1b acpi_event Reg Blk */
    u32 pm1a_cnt_blk;           /* Port address of Power Mgt 1a Control Reg Blk */
    u32 pm1b_cnt_blk;           /* Port address of Power Mgt 1b Control Reg Blk */
    u32 pm2_cnt_blk;            /* Port address of Power Mgt 2 Control Reg Blk */
    u32 pm_tmr_blk;             /* Port address of Power Mgt Timer Ctrl Reg Blk */
    u32 gpe0_blk;               /* Port addr of General Purpose acpi_event 0 Reg Blk */
    u32 gpe1_blk;               /* Port addr of General Purpose acpi_event 1 Reg Blk */
    u8  pm1_evt_len;            /* Byte length of ports at pm1_x_evt_blk */
    u8  pm1_cnt_len;            /* Byte length of ports at pm1_x_cnt_blk */
    u8  pm2_cnt_len;            /* Byte Length of ports at pm2_cnt_blk */
    u8  pm_tmr_len;             /* Byte Length of ports at pm_tm_blk */
    u8  gpe0_blk_len;           /* Byte Length of ports at gpe0_blk */
    u8  gpe1_blk_len;           /* Byte Length of ports at gpe1_blk */
    u8  gpe1_base;              /* Offset in gpe model where gpe1 events start */
    u8  reserved3;              /* Reserved */
    u16 plvl2_lat;              /* Worst case HW latency to enter/exit C2 state */
    u16 plvl3_lat;              /* Worst case HW latency to enter/exit C3 state */
    u16 flush_size;             /* Size of area read to flush caches */
    u16 flush_stride;           /* Stride used in flushing caches */
    u8  duty_offset;            /* Bit location of duty cycle field in p_cnt reg */
    u8  duty_width;             /* Bit width of duty cycle field in p_cnt reg */
    u8  day_alrm;               /* Index to day-of-month alarm in RTC CMOS RAM */
    u8  mon_alrm;               /* Index to month-of-year alarm in RTC CMOS RAM */
    u8  century;                /* Index to century in RTC CMOS RAM */
    u8  reserved4;              /* Reserved */
    u8  reserved4a;             /* Reserved */
    u8  reserved4b;             /* Reserved */
};

#define FACS_SIGNATURE 0x53434146 // FACS
struct facs_descriptor_rev1
{
    u32 signature;           /* ACPI Signature */
    u32 length;                 /* Length of structure, in bytes */
    u32 hardware_signature;     /* Hardware configuration signature */
    u32 firmware_waking_vector; /* ACPI OS waking vector */
    u32 global_lock;            /* Global Lock */
    u32 S4bios_f        : 1;    /* Indicates if S4BIOS support is present */
    u32 reserved1       : 31;   /* Must be 0 */
    u8  resverved3 [40];        /* Reserved - must be zero */
};

u32* find_resume_vector_addr(void)
{
    unsigned long addr;
    struct rsdp_descriptor *rsdp;
    struct rsdt_descriptor_rev1 *rsdt;
    void *end;
    int i;

    for(addr = 0xf0000; addr < 0x100000; addr += 16) {
	rsdp = (void*)addr;
	if (rsdp->signature == 0x2052545020445352LL)
          break;
    }
    if (addr == 0x100000) {
        printf("Can't find RSDP\n");
        return 0;
    }

    printf("RSDP is at %x\n", rsdp);
    rsdt = (void*)(ulong)rsdp->rsdt_physical_address;
    if (!rsdt || rsdt->signature != RSDT_SIGNATURE)
        return 0;

    printf("RSDT is at %x\n", rsdt);

    end = (void*)rsdt + rsdt->length;
    for (i=0; (void*)&rsdt->table_offset_entry[i] < end; i++) {
        struct fadt_descriptor_rev1 *fadt = (void*)(ulong)rsdt->table_offset_entry[i];
        struct facs_descriptor_rev1 *facs;
        if (!fadt || fadt->signature != FACP_SIGNATURE)
            continue;
        printf("FADT is at %x\n", fadt);
        facs = (void*)(ulong)fadt->firmware_ctrl;
        if (!facs || facs->signature != FACS_SIGNATURE)
            return 0;
        printf("FACS is at %x\n", facs);
        return &facs->firmware_waking_vector;
    }
   return 0;
}

#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS_ALARM         5
#define RTC_ALARM_DONT_CARE     0xC0

#define RTC_REG_A               10
#define RTC_REG_B               11
#define RTC_REG_C               12

#define REG_A_UIP               0x80
#define REG_B_AIE               0x20

static inline int rtc_in(u8 reg)
{
    u8 x = reg;
    asm volatile("outb %b1, $0x70; inb $0x71, %b0"
		 : "=a"(x) : "0"(x));
    return x;
}

static inline void rtc_out(u8 reg, u8 val)
{
    asm volatile("outb %b1, $0x70; mov %b2, %b1; outb %b1, $0x71"
		 : "=a"(reg) : "0"(reg), "ri"(val));
}

extern char resume_start, resume_end;

int main(int argc, char **argv)
{
	volatile u32 *resume_vector_ptr = find_resume_vector_addr();
	char *addr, *resume_vec = (void*)0x1000;

	*resume_vector_ptr = (u32)(ulong)resume_vec;

	printf("resume vector addr is %x\n", resume_vector_ptr);
	for (addr = &resume_start; addr < &resume_end; addr++)
		*resume_vec++ = *addr;
	printf("copy resume code from %x\n", &resume_start);

	/* Setup RTC alarm to wake up on the next second.  */
	while ((rtc_in(RTC_REG_A) & REG_A_UIP) == 0);
	while ((rtc_in(RTC_REG_A) & REG_A_UIP) != 0);
	rtc_in(RTC_REG_C);
	rtc_out(RTC_SECONDS_ALARM, RTC_ALARM_DONT_CARE);
	rtc_out(RTC_MINUTES_ALARM, RTC_ALARM_DONT_CARE);
	rtc_out(RTC_HOURS_ALARM, RTC_ALARM_DONT_CARE);
	rtc_out(RTC_REG_B, rtc_in(RTC_REG_B) | REG_B_AIE);

	*(volatile int*)0 = 0;
	asm volatile("outw %0, %1" :: "a"((short)0x2400), "d"((short)0xb004):"memory");
	while(1)
		*(volatile int*)0 = 1;

	return 0;
}

asm (
        ".global resume_start\n"
	".global resume_end\n"
	".code16\n"
	"resume_start:\n"
	"mov 0x0, %eax\n"
	"mov $0xf4, %dx\n"
	"out %eax, %dx\n"
	"1: hlt\n"
	"jmp 1b\n"
	"resume_end:\n"
#ifdef __i386__
	".code32\n"
#else
	".code64\n"
#endif
    );
