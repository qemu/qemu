/*
 * QEMU model of the Mister IRQ polling
 *
 * Copyright (c) 2021 Mark Watson
 *
 * GPL
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/ptimer.h"
#include "hw/qdev-properties.h"
#include "sysemu/runstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/qemu-print.h"
#include <sys/ioctl.h>
#include "minimig_ioctl.h"
//#include "exec/exec-all.h"
void tb_invalidate_phys_range(unsigned long start, unsigned long end);

#define D(x)

extern void volatile * chip_addr;

int cia_written = 0;

struct mister_timer
{
    ptimer_state *ptimer;
    void *parent;
    int nr; /* for debug.  */

    unsigned long timer_div;
};

struct timerblock
{
    SysBusDevice parent_obj;

    qemu_irq irq;
    QEMUBH * irq_bh;
    struct mister_timer *timers;
    MemoryRegion hardware1;
    MemoryRegion hardware2;
    MemoryRegion hardware3;
    unsigned char volatile * irqs;

    volatile void * hardware_addr1;
    volatile void * hardware_addr2;
    volatile void * hardware_addr3;
};

static int timer_update_irq(struct timerblock *t)
{
    int current = *t->irqs;
 //   fprintf(stderr,"I");
//
//    /* All timers within the same slave share a single IRQ line.  */
//    static int last = -1;
//    if (last!=current)
//    {
//	    last = current;
if (current==15)
{
	  //  FILE *logfile = qemu_log_lock();
	//    qemu_fprintf(logfile,"IRQ on write:%d:%ld\n",current,clock());
	  //  qemu_log_unlock(logfile);
	qemu_set_irq(t->irq, 7&(~current));
}
//	    return 1;
//    }
    return 0;
}

static void irq_bh_func(void *opaque)
{
	struct timerblock * t = (struct timerblock *)(opaque);
    	int current = *t->irqs;
        qemu_set_irq(t->irq, 7&(~current));

	if (0==(8&current))
	{
		// reset!
		//fprintf(stderr,"RESET_THREAD:%d\n",gettid());
	//	qemu_bh_schedule(t->irq_bh);
		usleep(100000);
		qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
	}

	//fprintf(stderr,"RESET BOTTOM HALF:%d\n",gettid());
}

//clock_t irq_pre,irq_post;
static void * mythreadfunc(void * opaque)
{
    struct timerblock *t = opaque;
 //   static int last = -1;

    static int first = 1;
    if (first)
    {
	    first =0;
	    fprintf(stderr,"ioctl thread:%d\n",gettid());
    }

    int fd = open ("/sys/kernel/debug/minimig_irq/ioctl_dev",O_RDONLY);
    while (true)
    {
	int res = ioctl(fd, MINIMIG_IOC_WAIT_IRQ, 1);
	if (res<0)
		perror("arg:");
	//irq_pre = clock();

        qemu_mutex_lock_iothread();
	//fprintf(stderr,"i");
	  //  FILE *logfile = qemu_log_lock();
	//    qemu_fprintf(logfile,"IRQ on ioctl:%d:%ld\n",current,clock());
	   // qemu_log_unlock(logfile);
	qemu_bh_schedule(t->irq_bh);

        qemu_mutex_unlock_iothread();

	//fprintf(stderr,"IRQ:%d:%d:%d\n", current,fd,res);
    }
    return 0;
}


static uint64_t
hardware_read1(void *opaque, hwaddr addr, unsigned int size)
{
    struct timerblock *t = opaque;

    uint64_t res;
    void volatile * hardware_addr = t->hardware_addr1+addr;
    switch (size)
    {
	case 1:
		res = *((unsigned char volatile *)(hardware_addr));
	break;
	case 2:
		res = *((unsigned short volatile *)(hardware_addr));
	break;
	case 4:
		res = *((unsigned int volatile *)(hardware_addr));
	break;
	default:
		exit(-1);
    }
    return res;
}

static uint64_t
hardware_read2(void *opaque, hwaddr addr, unsigned int size)
{
    struct timerblock *t = opaque;

    uint64_t res;
    void volatile * hardware_addr = t->hardware_addr2+addr;
    switch (size)
    {
	case 1:
		res = *((unsigned char volatile *)(hardware_addr));
	break;
	case 2:
		res = *((unsigned short volatile *)(hardware_addr));
	break;
	case 4:
		res = *((unsigned int volatile *)(hardware_addr));
	break;
	default:
		exit(-1);
    }
    return res;
}

static uint64_t
hardware_read3(void *opaque, hwaddr addr, unsigned int size)
{
    struct timerblock *t = opaque;

    uint64_t res;
    void volatile * hardware_addr = t->hardware_addr3+addr;
    switch (size)
    {
	case 1:
		res = *((unsigned char volatile *)(hardware_addr));
	break;
	case 2:
		res = *((unsigned short volatile *)(hardware_addr));
	break;
	case 4:
		res = *((unsigned int volatile *)(hardware_addr));
	break;
	default:
		exit(-1);
    }
 //   fprintf(stderr,"CIAR:%04x=%04x(%d)\n",(unsigned int)addr,(unsigned int)res,size);
    return res;
}

static void
hardware_write1(void *opaque, hwaddr addr,
            uint64_t val64, unsigned int size)
{
    struct timerblock *t = opaque;

    void volatile * hardware_addr = t->hardware_addr1+addr;
    switch (size)
    {
	case 1:
		*((unsigned char volatile *)(hardware_addr)) = val64;
	break;
	case 2:
		*((unsigned short volatile *)(hardware_addr)) = val64;
	break;
	case 4:
		*((unsigned int volatile *)(hardware_addr)) = val64;
	break;
	default:
		exit(-1);
    }

    timer_update_irq(t);
}

static void
hardware_write2(void *opaque, hwaddr addr,
            uint64_t val64, unsigned int size)
{
    struct timerblock *t = opaque;

    void volatile * hardware_addr = t->hardware_addr2+addr;
    switch (size)
    {
	case 1:
		*((unsigned char volatile *)(hardware_addr)) = val64;
	break;
	case 2:
		*((unsigned short volatile *)(hardware_addr)) = val64;
	break;
	case 4:
		*((unsigned int volatile *)(hardware_addr)) = val64;
	break;
	default:
		exit(-1);
    }

    timer_update_irq(t);

    static int first = 1;
    if (first)
    {
	    first =0;
	    fprintf(stderr,"io thread:%d\n",gettid());
    }
}

static void
hardware_write3(void *opaque, hwaddr addr,
            uint64_t val64, unsigned int size)
{
    struct timerblock *t = opaque;

    void volatile * hardware_addr = t->hardware_addr3+addr;
    switch (size)
    {
	case 1:
		*((unsigned char volatile *)(hardware_addr)) = val64;
	break;
	case 2:
		*((unsigned short volatile *)(hardware_addr)) = val64;
	break;
	case 4:
		*((unsigned int volatile *)(hardware_addr)) = val64;
	break;
	default:
		exit(-1);
    }

 //   fprintf(stderr,"CIAW:%04x=%04x(%d)\n",(unsigned int)addr,(unsigned int)val64,size);

    if (cia_written==0)
    {
	    cia_written = 1;
	   // fprintf(stderr,"Invalidating!");
	    unsigned int hpsbridgeaddr = 0xc0000000;
	    tb_invalidate_phys_range(hpsbridgeaddr, (hpsbridgeaddr+0x200000));
    }
}

static const MemoryRegionOps hardware_ops1 = {
    .read = hardware_read1,
    .write = hardware_write1,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static const MemoryRegionOps hardware_ops2 = {
    .read = hardware_read2,
    .write = hardware_write2,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static const MemoryRegionOps hardware_ops3 = {
    .read = hardware_read3,
    .write = hardware_write3,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};


#define TYPE_MISTER_IRQPOLL "mister.interruptpoll"
DECLARE_INSTANCE_CHECKER(struct timerblock, MISTER_IRQPOLL,
                         TYPE_MISTER_IRQPOLL)

/*static void timer_hit(void *opaque)
{
    struct mister_timer *xt = opaque;
    struct timerblock *t = xt->parent;
    D(fprintf(stderr, "%s %d\n", __func__, xt->nr));

    //ptimer_transaction_begin(xt->ptimer);
 //   ptimer_set_count(xt->ptimer,1);
    //ptimer_transaction_commit(xt->ptimer);

    timer_update_irq(t);
}*/

static void mister_interruptpoll_realize(DeviceState *dev, Error **errp)
{
    struct timerblock *t = MISTER_IRQPOLL(dev);
    unsigned int i=0;

    unsigned int irqoffset = 0x1000000;
    int fduncached = open("/dev/mem",(O_RDWR|O_SYNC));

    unsigned int hpsbridgeaddr = 0xc0000000;
    void * irqs = mmap(NULL,1,(PROT_READ|PROT_WRITE),MAP_SHARED,fduncached,hpsbridgeaddr+irqoffset); //cached?

    fprintf(stderr,"Init interrupt polling thread (requires kernel module):%p\n",irqs);

    int hardware_bytes =  4; //13*1024*1024;
    void volatile * hardware_addr = mmap(NULL,8192,(PROT_READ|PROT_WRITE),MAP_SHARED,fduncached,hpsbridgeaddr + 0xdff000);
    t->hardware_addr1 = hardware_addr+0x1c;
    t->hardware_addr2 = hardware_addr+0x9a;

    void volatile * cia_addr = mmap(NULL,8192,(PROT_READ|PROT_WRITE),MAP_SHARED,fduncached,hpsbridgeaddr + 0xbfd000);
    t->hardware_addr3 = cia_addr;

    //sysbus_mmio_map(s, 0, 0xdff01c);
    //sysbus_mmio_map(s, 1, 0xdff09a);
    //sysbus_mmio_map(s, 2, 0xbfe000);

    memory_region_init_io(&t->hardware1, OBJECT(t), &hardware_ops1, t, "mister.minimig.hardware1", hardware_bytes);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &t->hardware1);
    memory_region_init_io(&t->hardware2, OBJECT(t), &hardware_ops2, t, "mister.minimig.hardware2", hardware_bytes);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &t->hardware2);
    memory_region_init_io(&t->hardware3, OBJECT(t), &hardware_ops3, t, "mister.minimig.hardware3", hardware_bytes);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &t->hardware3);

    /* Init all the ptimers.  */
    t->timers = g_malloc0(sizeof t->timers[0]);
    struct mister_timer *xt = &t->timers[0];

    t->irqs = irqs;
    xt->parent = t;
    xt->nr = i;
  //  xt->ptimer = ptimer_init(timer_hit, xt, PTIMER_POLICY_CONTINUOUS_TRIGGER);

  //  ptimer_transaction_begin(xt->ptimer);
  //  ptimer_set_freq(xt->ptimer, t->freq_hz);
  //  ptimer_set_count(xt->ptimer,1);
  //  ptimer_run(xt->ptimer, 0);
  //  ptimer_transaction_commit(xt->ptimer);
  
  t->irq_bh = qemu_bh_new(irq_bh_func, t);

  pthread_t * mythread = malloc(sizeof(pthread_t));
  memset(mythread,0,sizeof(pthread_t));
  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setschedpolicy (&attr, SCHED_FIFO);
  struct sched_param param;
  pthread_attr_getschedparam (&attr, &param);
  param.sched_priority = 99;
  pthread_attr_setschedparam (&attr, &param);
  pthread_create(mythread,&attr,&mythreadfunc,t);
}

static void mister_interruptpoll_init(Object *obj)
{
    struct timerblock *t = MISTER_IRQPOLL(obj);

    /* All timers share a single irq line.  */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &t->irq);
}

static Property mister_interruptpoll_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void mister_interruptpoll_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mister_interruptpoll_realize;
    device_class_set_props(dc, mister_interruptpoll_properties);
}

static const TypeInfo mister_interruptpoll_info = {
    .name          = TYPE_MISTER_IRQPOLL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct timerblock),
    .instance_init = mister_interruptpoll_init,
    .class_init    = mister_interruptpoll_class_init,
};

static void mister_interruptpoll_register_types(void)
{
    type_register_static(&mister_interruptpoll_info);
}

type_init(mister_interruptpoll_register_types)
