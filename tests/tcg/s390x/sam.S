/* DAT on, home-space mode, 64-bit mode */
#define DAT_PSWM 0x400c00180000000
#define VIRTUAL_BASE 0x123456789abcd000

    .org 0x8e
program_interruption_code:
    .org 0x150
program_old_psw:
    .org 0x1d0                         /* program new PSW */
    .quad 0,pgm_handler
    .org 0x200                         /* lowcore padding */

    .globl _start
_start:
    lctlg %c13,%c13,hasce
    lpswe dat_psw
start_dat:
    sam24
sam24_suppressed:
    /* sam24 should fail */
fail:
    basr %r12,%r0
    lpswe failure_psw-.(%r12)
pgm_handler:
    chhsi program_interruption_code,6  /* specification exception? */
    jne fail
    clc suppressed_psw(16),program_old_psw  /* correct location? */
    jne fail
    lpswe success_psw

    .align 8
dat_psw:
    .quad DAT_PSWM,VIRTUAL_BASE+start_dat
suppressed_psw:
    .quad DAT_PSWM,VIRTUAL_BASE+sam24_suppressed
success_psw:
    .quad 0x2000000000000,0xfff        /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0            /* disabled wait */
hasce:
    /* DT = 0b11 (region-first-table), TL = 3 (2k entries) */
    .quad region_first_table + (3 << 2) + 3
    .align 0x1000
region_first_table:
    .org region_first_table + ((VIRTUAL_BASE >> 53) & 0x7ff) * 8
    /* TT = 0b11 (region-first-table), TL = 3 (2k entries) */
    .quad region_second_table + (3 << 2) + 3
    .org region_first_table + 0x800 * 8
region_second_table:
    .org region_second_table + ((VIRTUAL_BASE >> 42) & 0x7ff) * 8
    /* TT = 0b10 (region-second-table), TL = 3 (2k entries) */
    .quad region_third_table + (2 << 2) + 3
    .org region_second_table + 0x800 * 8
region_third_table:
    .org region_third_table + ((VIRTUAL_BASE >> 31) & 0x7ff) * 8
    /* TT = 0b01 (region-third-table), TL = 3 (2k entries) */
    .quad segment_table + (1 << 2) + 3
    .org region_third_table + 0x800 * 8
segment_table:
    .org segment_table + ((VIRTUAL_BASE >> 20) & 0x7ff) * 8
    /* TT = 0b00 (segment-table) */
    .quad page_table
    .org segment_table + 0x800 * 8
page_table:
    .org page_table + ((VIRTUAL_BASE >> 12) & 0xff) * 8
    .quad 0
    .org page_table + 0x100 * 8
