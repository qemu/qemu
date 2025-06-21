#define UART0_BASE 0x101f1000
#define UARTFR     (*(volatile unsigned int *)(UART0_BASE + 0x18))
#define UARTDR     (*(volatile unsigned int *)(UART0_BASE + 0x00))

void uart_putc(char c) {
    while (UARTFR & (1 << 5)); // Wait until TXFF = 0 (FIFO not full)
    if (c == '\n') uart_putc('\r');
    UARTDR = c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

void main(void) {
    uart_puts("Hello, QEMU UART!\n");
}
