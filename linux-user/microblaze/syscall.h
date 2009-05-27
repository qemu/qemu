#define UNAME_MACHINE "microblaze"

/* We use microblaze_reg_t to keep things similar to the kernel sources.  */
typedef uint32_t microblaze_reg_t;

struct target_pt_regs {
        microblaze_reg_t r0;
        microblaze_reg_t r1;
        microblaze_reg_t r2;
        microblaze_reg_t r3;
        microblaze_reg_t r4;
        microblaze_reg_t r5;
        microblaze_reg_t r6;
        microblaze_reg_t r7;
        microblaze_reg_t r8;
        microblaze_reg_t r9;
        microblaze_reg_t r10;
        microblaze_reg_t r11;
        microblaze_reg_t r12;
        microblaze_reg_t r13;
        microblaze_reg_t r14;
        microblaze_reg_t r15;
        microblaze_reg_t r16;
        microblaze_reg_t r17;
        microblaze_reg_t r18;
        microblaze_reg_t r19;
        microblaze_reg_t r20;
        microblaze_reg_t r21;
        microblaze_reg_t r22;
        microblaze_reg_t r23;
        microblaze_reg_t r24;
        microblaze_reg_t r25;
        microblaze_reg_t r26;
        microblaze_reg_t r27;
        microblaze_reg_t r28;
        microblaze_reg_t r29;
        microblaze_reg_t r30;
        microblaze_reg_t r31;
        microblaze_reg_t pc;
        microblaze_reg_t msr;
        microblaze_reg_t ear;
        microblaze_reg_t esr;
        microblaze_reg_t fsr;
        uint32_t kernel_mode;
};
