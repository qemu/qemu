/*
 * QTest testcase for STML4X5_USART
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/misc/stm32l4x5_rcc_internals.h"
#include "hw/registerfields.h"
#include "stm32l4x5.h"

#define RCC_BASE_ADDR 0x40021000
/* Use USART 1 ADDR, assume the others work the same */
#define USART1_BASE_ADDR 0x40013800

/* See stm32l4x5_usart for definitions */
REG32(CR1, 0x00)
    FIELD(CR1, M1, 28, 1)
    FIELD(CR1, OVER8, 15, 1)
    FIELD(CR1, M0, 12, 1)
    FIELD(CR1, PCE, 10, 1)
    FIELD(CR1, TXEIE, 7, 1)
    FIELD(CR1, RXNEIE, 5, 1)
    FIELD(CR1, TE, 3, 1)
    FIELD(CR1, RE, 2, 1)
    FIELD(CR1, UE, 0, 1)
REG32(CR2, 0x04)
REG32(CR3, 0x08)
    FIELD(CR3, OVRDIS, 12, 1)
REG32(BRR, 0x0C)
REG32(GTPR, 0x10)
REG32(RTOR, 0x14)
REG32(RQR, 0x18)
REG32(ISR, 0x1C)
    FIELD(ISR, REACK, 22, 1)
    FIELD(ISR, TEACK, 21, 1)
    FIELD(ISR, TXE, 7, 1)
    FIELD(ISR, RXNE, 5, 1)
    FIELD(ISR, ORE, 3, 1)
REG32(ICR, 0x20)
REG32(RDR, 0x24)
REG32(TDR, 0x28)

#define NVIC_ISPR1 0XE000E204
#define NVIC_ICPR1 0xE000E284
#define USART1_IRQ 37

static bool check_nvic_pending(QTestState *qts, unsigned int n)
{
    /* No USART interrupts are less than 32 */
    assert(n > 32);
    n -= 32;
    return qtest_readl(qts, NVIC_ISPR1) & (1 << n);
}

static bool clear_nvic_pending(QTestState *qts, unsigned int n)
{
    /* No USART interrupts are less than 32 */
    assert(n > 32);
    n -= 32;
    qtest_writel(qts, NVIC_ICPR1, (1 << n));
    return true;
}

/*
 * Wait indefinitely for the flag to be updated.
 * If this is run on a slow CI runner,
 * the meson harness will timeout after 10 minutes for us.
 */
static bool usart_wait_for_flag(QTestState *qts, uint32_t event_addr,
                                uint32_t flag)
{
    while (true) {
        if ((qtest_readl(qts, event_addr) & flag)) {
            return true;
        }
        g_usleep(1000);
    }

    return false;
}

static void usart_receive_string(QTestState *qts, int sock_fd, const char *in,
                                 char *out)
{
    int i, in_len = strlen(in);

    g_assert_true(send(sock_fd, in, in_len, 0) == in_len);
    for (i = 0; i < in_len; i++) {
        g_assert_true(usart_wait_for_flag(qts,
            USART1_BASE_ADDR + A_ISR, R_ISR_RXNE_MASK));
        out[i] = qtest_readl(qts, USART1_BASE_ADDR + A_RDR);
    }
    out[i] = '\0';
}

static void usart_send_string(QTestState *qts, const char *in)
{
    int i, in_len = strlen(in);

    for (i = 0; i < in_len; i++) {
        qtest_writel(qts, USART1_BASE_ADDR + A_TDR, in[i]);
        g_assert_true(usart_wait_for_flag(qts,
            USART1_BASE_ADDR + A_ISR, R_ISR_TXE_MASK));
    }
}

/* Init the RCC clocks to run at 80 MHz */
static void init_clocks(QTestState *qts)
{
    uint32_t value;

    /* MSIRANGE can be set only when MSI is OFF or READY */
    qtest_writel(qts, (RCC_BASE_ADDR + A_CR), R_CR_MSION_MASK);

    /* Clocking from MSI, in case MSI was not the default source */
    qtest_writel(qts, (RCC_BASE_ADDR + A_CFGR), 0);

    /*
     * Update PLL and set MSI as the source clock.
     * PLLM = 1 --> 000
     * PLLN = 40 --> 40
     * PPLLR = 2 --> 00
     * PLLDIV = unused, PLLP = unused (SAI3), PLLQ = unused (48M1)
     * SRC = MSI --> 01
     */
    qtest_writel(qts, (RCC_BASE_ADDR + A_PLLCFGR), R_PLLCFGR_PLLREN_MASK |
            (40 << R_PLLCFGR_PLLN_SHIFT) |
            (0b01 << R_PLLCFGR_PLLSRC_SHIFT));

    /* PLL activation */

    value = qtest_readl(qts, (RCC_BASE_ADDR + A_CR));
    qtest_writel(qts, (RCC_BASE_ADDR + A_CR), value | R_CR_PLLON_MASK);

    /* RCC_CFGR is OK by defaut */
    qtest_writel(qts, (RCC_BASE_ADDR + A_CFGR), 0);

    /* CCIPR : no periph clock by default */
    qtest_writel(qts, (RCC_BASE_ADDR + A_CCIPR), 0);

    /* Switches on the PLL clock source */
    value = qtest_readl(qts, (RCC_BASE_ADDR + A_CFGR));
    qtest_writel(qts, (RCC_BASE_ADDR + A_CFGR), (value & ~R_CFGR_SW_MASK) |
        (0b11 << R_CFGR_SW_SHIFT));

    /* Enable SYSCFG clock enabled */
    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2ENR), R_APB2ENR_SYSCFGEN_MASK);

    /* Enable the IO port B clock (See p.252) */
    qtest_writel(qts, (RCC_BASE_ADDR + A_AHB2ENR), R_AHB2ENR_GPIOBEN_MASK);

    /* Enable the clock for USART1 (cf p.259) */
    /* We rewrite SYSCFGEN to not disable it */
    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2ENR),
                 R_APB2ENR_SYSCFGEN_MASK | R_APB2ENR_USART1EN_MASK);

    /* TODO: Enable usart via gpio */

    /* Set PCLK as the clock for USART1(cf p.272) i.e. reset both bits */
    qtest_writel(qts, (RCC_BASE_ADDR + A_CCIPR), 0);

    /* Reset USART1 (see p.249) */
    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2RSTR), 1 << 14);
    qtest_writel(qts, (RCC_BASE_ADDR + A_APB2RSTR), 0);
}

static void init_uart(QTestState *qts)
{
    uint32_t cr1;

    init_clocks(qts);

    /*
     * For 115200 bauds, see p.1349.
     * The clock has a frequency of 80Mhz,
     * for 115200, we have to put a divider of 695 = 0x2B7.
     */
    qtest_writel(qts, (USART1_BASE_ADDR + A_BRR), 0x2B7);

    /*
     * Set the oversampling by 16,
     * disable the parity control and
     * set the word length to 8. (cf p.1377)
     */
    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));
    cr1 &= ~(R_CR1_M1_MASK | R_CR1_M0_MASK | R_CR1_OVER8_MASK | R_CR1_PCE_MASK);
    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1), cr1);

    /* Enable the transmitter, the receiver and the USART. */
    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1),
        cr1 | R_CR1_UE_MASK | R_CR1_RE_MASK | R_CR1_TE_MASK);
}

static void test_write_read(void)
{
    QTestState *qts = qtest_init("-M b-l475e-iot01a");

    /* Test that we can write and retrieve a value from the device */
    qtest_writel(qts, USART1_BASE_ADDR + A_TDR, 0xFFFFFFFF);
    const uint32_t tdr = qtest_readl(qts, USART1_BASE_ADDR + A_TDR);
    g_assert_cmpuint(tdr, ==, 0x000001FF);

    qtest_quit(qts);
}

static void test_receive_char(void)
{
    int sock_fd;
    uint32_t cr1;
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    /* Try without initializing IRQ */
    g_assert_true(send(sock_fd, "a", 1, 0) == 1);
    usart_wait_for_flag(qts, USART1_BASE_ADDR + A_ISR, R_ISR_RXNE_MASK);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + A_RDR), ==, 'a');
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    /* Now with the IRQ */
    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));
    cr1 |= R_CR1_RXNEIE_MASK;
    qtest_writel(qts, USART1_BASE_ADDR + A_CR1, cr1);
    g_assert_true(send(sock_fd, "b", 1, 0) == 1);
    usart_wait_for_flag(qts, USART1_BASE_ADDR + A_ISR, R_ISR_RXNE_MASK);
    g_assert_cmphex(qtest_readl(qts, USART1_BASE_ADDR + A_RDR), ==, 'b');
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));
    clear_nvic_pending(qts, USART1_IRQ);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_send_char(void)
{
    int sock_fd;
    char s[1];
    uint32_t cr1;
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    /* Try without initializing IRQ */
    qtest_writel(qts, USART1_BASE_ADDR + A_TDR, 'c');
    g_assert_true(recv(sock_fd, s, 1, 0) == 1);
    g_assert_cmphex(s[0], ==, 'c');
    g_assert_false(check_nvic_pending(qts, USART1_IRQ));

    /* Now with the IRQ */
    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));
    cr1 |= R_CR1_TXEIE_MASK;
    qtest_writel(qts, USART1_BASE_ADDR + A_CR1, cr1);
    qtest_writel(qts, USART1_BASE_ADDR + A_TDR, 'd');
    g_assert_true(recv(sock_fd, s, 1, 0) == 1);
    g_assert_cmphex(s[0], ==, 'd');
    g_assert_true(check_nvic_pending(qts, USART1_IRQ));
    clear_nvic_pending(qts, USART1_IRQ);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_receive_str(void)
{
    int sock_fd;
    char s[10];
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    usart_receive_string(qts, sock_fd, "hello", s);
    g_assert_true(memcmp(s, "hello", 5) == 0);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_send_str(void)
{
    int sock_fd;
    char s[10];
    QTestState *qts = qtest_init_with_serial("-M b-l475e-iot01a", &sock_fd);

    init_uart(qts);

    usart_send_string(qts, "world");
    g_assert_true(recv(sock_fd, s, 10, 0) == 5);
    g_assert_true(memcmp(s, "world", 5) == 0);

    close(sock_fd);

    qtest_quit(qts);
}

static void test_ack(void)
{
    uint32_t cr1;
    uint32_t isr;
    QTestState *qts = qtest_init("-M b-l475e-iot01a");

    init_uart(qts);

    cr1 = qtest_readl(qts, (USART1_BASE_ADDR + A_CR1));

    /* Disable the transmitter and receiver. */
    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1),
        cr1 & ~(R_CR1_RE_MASK | R_CR1_TE_MASK));

    /* Test ISR ACK for transmitter and receiver disabled */
    isr = qtest_readl(qts, (USART1_BASE_ADDR + A_ISR));
    g_assert_false(isr & R_ISR_TEACK_MASK);
    g_assert_false(isr & R_ISR_REACK_MASK);

    /* Enable the transmitter and receiver. */
    qtest_writel(qts, (USART1_BASE_ADDR + A_CR1),
        cr1 | (R_CR1_RE_MASK | R_CR1_TE_MASK));

    /* Test ISR ACK for transmitter and receiver disabled */
    isr = qtest_readl(qts, (USART1_BASE_ADDR + A_ISR));
    g_assert_true(isr & R_ISR_TEACK_MASK);
    g_assert_true(isr & R_ISR_REACK_MASK);

    qtest_quit(qts);
}

static void check_clock(QTestState *qts, const char *path, uint32_t rcc_reg,
                        uint32_t reg_offset)
{
    g_assert_cmpuint(get_clock_period(qts, path), ==, 0);
    qtest_writel(qts, rcc_reg, qtest_readl(qts, rcc_reg) | (0x1 << reg_offset));
    g_assert_cmpuint(get_clock_period(qts, path), ==, SYSCLK_PERIOD);
}

static void test_clock_enable(void)
{
    /*
     * For each USART device, enable its clock in RCC
     * and check that its clock frequency is SYSCLK_PERIOD
     */
    QTestState *qts = qtest_init("-M b-l475e-iot01a");

    check_clock(qts, "machine/soc/usart[0]/clk", RCC_APB2ENR, 14);
    check_clock(qts, "machine/soc/usart[1]/clk", RCC_APB1ENR1, 17);
    check_clock(qts, "machine/soc/usart[2]/clk", RCC_APB1ENR1, 18);
    check_clock(qts, "machine/soc/uart[0]/clk", RCC_APB1ENR1, 19);
    check_clock(qts, "machine/soc/uart[1]/clk", RCC_APB1ENR1, 20);
    check_clock(qts, "machine/soc/lpuart1/clk", RCC_APB1ENR2, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("stm32l4x5/usart/write_read", test_write_read);
    qtest_add_func("stm32l4x5/usart/receive_char", test_receive_char);
    qtest_add_func("stm32l4x5/usart/send_char", test_send_char);
    qtest_add_func("stm32l4x5/usart/receive_str", test_receive_str);
    qtest_add_func("stm32l4x5/usart/send_str", test_send_str);
    qtest_add_func("stm32l4x5/usart/ack", test_ack);
    qtest_add_func("stm32l4x5/usart/clock_enable", test_clock_enable);
    return g_test_run();
}

