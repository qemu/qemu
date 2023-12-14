    .org 0x8d
ilc:
    .org 0x8e
program_interruption_code:
    .org 0x94
monitor_class:
    .org 0xb0
monitor_code:
    .org 0x150
program_old_psw:
    .org 0x1d0                              /* program new PSW */
    .quad 0x180000000,pgm                   /* 64-bit mode */
    .org 0x200                              /* lowcore padding */
    .globl _start
_start:
    stctg %c8,%c8,c8                        /* enable only monitor class 1 */
    mvhhi c8+6,0x4000
    lctlg %c8,%c8,c8
mc_nop:
    mc 123,0
mc_monitor_event:
    mc 321,1
    j failure
mc_specification:
    mc 333,16
    j failure
pgm:
    lgrl %r0,program_old_psw+8              /* ilc adjustment */
    llgc %r1,ilc
    sgr %r0,%r1
    larl %r1,mc_monitor_event               /* dispatch based on old PSW */
    cgrje %r0,%r1,pgm_monitor_event
    larl %r1,mc_specification
    cgrje %r0,%r1,pgm_specification
    j failure
pgm_monitor_event:
    chhsi program_interruption_code,0x40    /* monitor event? */
    jne failure
    chhsi monitor_class,1                   /* class from mc_monitor_event? */
    jne failure
    cghsi monitor_code,321                  /* code from mc_monitor_event? */
    jne failure
    j mc_specification                      /* next test */
pgm_specification:
    chhsi program_interruption_code,6       /* specification exception? */
    jne failure
    lpswe success_psw
failure:
    lpswe failure_psw
    .align 8
c8:
    .quad 0
success_psw:
    .quad 0x2000000000000,0xfff             /* see is_special_wait_psw() */
failure_psw:
    .quad 0x2000000000000,0                 /* disabled wait */
