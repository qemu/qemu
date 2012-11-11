#ifndef _ASM_IO_H
#define _ASM_IO_H
extern int printf(const char *fmt, ...);
extern unsigned long get_ticks(void);

#define _read(source)                \
({ unsigned long __res;                \
    __asm__ __volatile__(            \
        "mfc0\t%0, " #source "\n\t"    \
        : "=r" (__res));        \
    __res;                    \
})

#define __read(source)                \
({ unsigned long __res;                \
    __asm__ __volatile__(            \
        "move\t%0, " #source "\n\t"    \
        : "=r" (__res));        \
    __res;                    \
})

#endif
