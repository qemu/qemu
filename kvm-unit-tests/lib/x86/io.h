#ifndef IO_H
#define IO_H

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    asm volatile("inb %w1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short value;
    asm volatile("inw %w1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static inline unsigned int inl(unsigned short port)
{
    unsigned int value;
    asm volatile("inl %w1, %0" : "=a" (value) : "Nd" (port));
    return value;
}

static inline void outb(unsigned char value, unsigned short port)
{
    asm volatile("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

static inline void outw(unsigned short value, unsigned short port)
{
    asm volatile("outw %w0, %w1" : : "a"(value), "Nd"(port));
}

static inline void outl(unsigned int value, unsigned short port)
{
    asm volatile("outl %0, %w1" : : "a"(value), "Nd"(port));
}

#endif
