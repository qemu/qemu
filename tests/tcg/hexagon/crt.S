#define SYS_exit_group 94

    .text
    .globl pass
pass:
    r0 = #0
    r6 = #SYS_exit_group
    trap0(#1)

    .globl fail
fail:
    r0 = #1
    r6 = #SYS_exit_group
    trap0(#1)
