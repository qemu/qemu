/*
 * STM32 Independent watchdog.
 *
 * Copyright (C) 2016
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "hw/sysbus.h"
#include "hw/arm/stm32.h"
#include "qemu/timer.h"
#include "sysemu/watchdog.h"

/*#define IWDG_DEBUG 1*/

#ifdef IWDG_DEBUG
#define iwdg_debug(fs,...) \
    fprintf(stderr,"iwdg: %s: "fs,__func__,##__VA_ARGS__)
#else
#define iwdg_debug(fs,...)
#endif

#define R_IWDG_KR	0x00
#define R_IWDG_PR	0x04
#define R_IWDG_RLR	0x08
#define R_IWDG_SR	0x0C

#define RCC_CSR_IWDGRSTF_BIT		29


/* Device state. */
struct Stm32Iwdg {
    SysBusDevice busdev;
    MemoryRegion iomem;
    int reboot_enabled;         /* "Reboot" on timer expiry.  The real action
				    * performed depends on the -watchdog-action
				    * param passed on QEMU command line.
				    */                                 
	/* Properties */
    stm32_periph_t periph;
    Stm32Rcc *stm32_rcc;
    void *stm32_rcc_prop;

    int enabled;                /* If true, watchdog is enabled. */
    QEMUTimer *timer;           /* The actual watchdog timer. */
    uint32_t timer_reload;    /* Values preloaded into timer1 */
    uint32_t prescaler;

    int unlock_state;
    int previous_reboot_flag;   /* If the watchdog caused the previous
				    * reboot, this flag will be set.
				    */
				    
    /* Register Values */
    uint32_t
	IWDG_KR,
	IWDG_PR,
	IWDG_RLR,
	IWDG_SR;
};

typedef struct Stm32Iwdg Stm32Iwdg;

#define TYPE_WATCHDOG_IWDG_DEVICE "stm32_iwdg"
#define WATCHDOG_IWDG_DEVICE(obj) \
    OBJECT_CHECK(Stm32Iwdg, (obj), TYPE_WATCHDOG_IWDG_DEVICE)

/**
 * @fn uint32_t tim_period(Stm32Iwdg *s)
 * @brief Calculate the equivalent time in nanoseconds
 * @param s pointer to struct Stm32Iwdg
 * @return time calculated
 * @remarks This function calculate the equivalent recharge time of IWDG in nanoseconds
 * The calculated time depends on the frequency LSI, prescaler value and
 * the reload Register RLR.
*/    
static uint32_t
tim_period(Stm32Iwdg *s)
{   
    /* LSI frequency = 37~40kHz 
     * LSI frequency can range from 37kHz to 40kHz.
     * This frequency can be measured on the board, through the Timer10.
     * When the measurement is made, the value is near to 38KHz. 
     * However, with 40kHz, the watchdog timer accuracy is closer 
     * to the real value. 
     */
    uint32_t period = (1000000 * s->prescaler) / 40;
    return ((period * s->IWDG_RLR)); // time in nanoseconds
}

/**
 * @fn uint32_t tim_next_transition(Stm32Iwdg *s, int64_t current_time)
 * @brief Calculate the equivalent time in nanoseconds
 * @param s pointer to struct Stm32Iwdg
 * @param current_time Current time of system
 * @return time calculated
 * @remarks This function return the equivalent recharge time of IWDG
 * to prevent a reset.
*/
static int64_t
tim_next_transition(Stm32Iwdg *s, int64_t current_time)
{   
    return current_time + tim_period(s);
}

/**
 * @fn void iwdg_restart_timer(Stm32Iwdg *s)
 * @brief Restart watchdog timer to prevent reset
 * @param s pointer to struct Stm32Iwdg
 * @return none
 * @remarks This function is called when the watchdog has either been enabled
 * (hence it starts counting down) or has been keep-alived.
*/
static void iwdg_restart_timer(Stm32Iwdg *d)
{
    if (!d->enabled)
        return;
    
    timer_mod(d->timer, tim_next_transition(d, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)));
}

/**
 * @fn void iwdg_disable_timer(Stm32Iwdg *s)
 * @brief Disable watchdog timer
 * @param s pointer to struct Stm32Iwdg
 * @return none
 * @remarks This is called when the guest disables the watchdog.
*/
static void iwdg_disable_timer(Stm32Iwdg *d)
{
    //iwdg_debug("timer disabled\n");

    timer_del(d->timer);
}

/**
 * @fn void iwdg_reset(DeviceState *dev)
 * @brief Reset function
 * @param dev pointer to struct DeviceState
 * @return none
 * @remarks This function is called when the machine is initialized.
*/
static void iwdg_reset(DeviceState *dev)
{
   Stm32Iwdg *d = STM32_IWDG(dev);

    iwdg_disable_timer(d);

    d->reboot_enabled = 0;
    d->enabled = 0;
    d->prescaler = 4;
    d->timer_reload = 0xfff;
    d->IWDG_RLR = 0xfff;
    d->unlock_state = 0;
}

/**
 * @fn void iwdg_timer_expired(void *vp)
 * @brief Reset function
 * @param vp pointer to void
 * @return none
 * @remarks This function is called when the watchdog expires.
*/
static void iwdg_timer_expired(void *vp)
{
    Stm32Iwdg *d = vp;
    
    if (d->reboot_enabled) {
	d->previous_reboot_flag = 1;
	/* Set bit indicating reset reason (IWDG) */
	stm32_RCC_CSR_write((Stm32Rcc *)d->stm32_rcc, 1<<RCC_CSR_IWDGRSTF_BIT, 0);
	/* This reboots, exits, etc */
	watchdog_perform_action();
	iwdg_reset((DeviceState *)d);
    }
}

static uint64_t iwdg_read(void *arg, hwaddr offset, unsigned int size)
{
    uint32_t data = 0;
    Stm32Iwdg *d = arg;

    iwdg_debug("addr = %x\n", (int) addr);

    switch (offset) {
	case R_IWDG_KR:    
	    break;
	    
	case R_IWDG_PR:    
	    data = d->IWDG_PR;
	    break;
	    
	case R_IWDG_RLR:
	    data = d->IWDG_RLR;    
	    break;
	    
	case R_IWDG_SR:
	    data = 0;
	    break;
    }

    return data;
}

static void iwdg_write(void *arg, hwaddr offset, uint64_t data, unsigned int size)
{
    Stm32Iwdg *s = arg;

    iwdg_debug ("addr = %x, val = %x\n", (int) addr, val);
    
    switch (offset) {
	case R_IWDG_KR:
	    s->IWDG_KR = data & 0xFFFF;
	    /* Start watchdog counting */
	    if (s->IWDG_KR == 0xCCCC)
	    {
		s->enabled = 1;
		s->reboot_enabled = 1;
		timer_mod(s->timer, tim_next_transition(s, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)));
	    } else /* IWDG_RLR value is reloaded in the counter */
	    if (s->IWDG_KR == 0xAAAA)
	    {
		s->timer_reload = s->IWDG_RLR;
		iwdg_restart_timer(s);
	    } else /* Enable write access to the IWDG_PR and IWDG_RLR registers */
	    if (s->IWDG_KR == 0x5555)
	    {
		s->unlock_state = 1;
	    }
	    break;
	    
	case R_IWDG_PR:
	    if (s->unlock_state == 1)
	    {
		s->IWDG_PR = data & 0x07;
		s->prescaler = 4 << s->IWDG_PR;
	    }
	    break;
	    
	case R_IWDG_RLR:
	    if (s->unlock_state == 1)
	    {
		s->IWDG_RLR = data & 0x07FF;
	    }
	    break;
	    
	case R_IWDG_SR:
	    break;
    }
}

static const MemoryRegionOps iwdg_ops = {
    .read = iwdg_read,
    .write = iwdg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4, /* XXX actually 1 */
        .max_access_size = 4
    }
};

static const VMStateDescription vmstate_iwdg = {
    .name = "stm32_iwdg",
    /* With this VMSD's introduction, version_id/minimum_version_id were
     * erroneously set to sizeof(Stm32Iwdg), causing a somewhat random
     * version_id to be set for every build. This eventually broke
     * migration.
     *
     * To correct this without breaking old->new migration for older
     * versions of QEMU, we've set version_id to a value high enough
     * to exceed all past values of sizeof(Stm32Iwdg) across various
     * build environments, and have reset minimum_version_id to 1,
     * since this VMSD has never changed and thus can accept all past
     * versions.
     *
     * For future changes we can treat these values as we normally would.
     */
    .version_id = 10000,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(reboot_enabled, Stm32Iwdg),
        VMSTATE_INT32(enabled, Stm32Iwdg),
        VMSTATE_TIMER(timer, Stm32Iwdg),
        VMSTATE_UINT32(timer_reload, Stm32Iwdg),
        VMSTATE_INT32(unlock_state, Stm32Iwdg),
        VMSTATE_INT32(previous_reboot_flag, Stm32Iwdg),
        VMSTATE_END_OF_LIST()
    }
};

static int iwdg_init(SysBusDevice *dev)
{
    Stm32Iwdg *s = STM32_IWDG(dev);
    //qemu_irq *clk_irq;

    s->stm32_rcc = (Stm32Rcc *)s->stm32_rcc_prop;
    
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, iwdg_timer_expired, s);
    s->previous_reboot_flag = 0;

    memory_region_init_io(&s->iomem, OBJECT(s), &iwdg_ops, s, "iwdg", 0x3FF);    
    sysbus_init_mmio(dev, &s->iomem);
    
    //clk_irq = qemu_allocate_irqs(iwdg_clk_irq_handler, (void *)s, 1);
    //stm32_rcc_set_periph_clk_irq((Stm32Rcc *)s->stm32_rcc, s->periph, clk_irq[0]);
    
    return 0;
}

static WatchdogTimerModel model = {
    .wdt_name = "stm32_iwdg",
    .wdt_description = "Independent watchdog",
};

static Property iwdg_properties[] = {
    DEFINE_PROP_PERIPH_T("periph", Stm32Iwdg, periph, STM32_PERIPH_UNDEFINED),
    DEFINE_PROP_PTR("stm32_rcc", Stm32Iwdg, stm32_rcc_prop),
    DEFINE_PROP_END_OF_LIST(),
};

static void iwdg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sc = SYS_BUS_DEVICE_CLASS(klass);    
    
    sc->init = iwdg_init;
    dc->reset = iwdg_reset;
    dc->vmsd = &vmstate_iwdg;
    dc->props = iwdg_properties;
}

static const TypeInfo iwdg_info = {
    .name          = "stm32_iwdg",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Stm32Iwdg),
    .class_init    = iwdg_class_init,
};

static void iwdg_register_types(void)
{
    watchdog_add_model(&model);
    type_register_static(&iwdg_info);
}

type_init(iwdg_register_types)
