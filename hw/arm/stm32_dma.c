/*-
 * Copyright (c) 2013
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/*
 * QEMU DMA controller device model
 */
#include "hw/sysbus.h"
#include "hw/arm/stm32.h"

//#define DEBUG_STM32_DMA
#ifdef DEBUG_STM32_DMA

// NOTE: The usleep() helps the MacOS stdout from freezing when we have a lot of print out
#define DPRINTF(fmt, ...)                                       \
	do { printf("STM32_DMA: " fmt , ## __VA_ARGS__); \
		 usleep(1000); \
	} while (0)
#else
#define DPRINTF(fmt, ...)
#endif


/* Common interrupt status / clear registers. */
#define R_DMA_ISR            (0x00) //r
#define R_DMA_IFCR           (0x04) //w

/* Per-stream registers. */
#define DMA_Stream_Count               (8)
#define DMA_Perstream_Reg_Num          (4)
#define DMA_Perstream_Reg_Size         (4)
#define DMA_Perstream_Reg_Total_Size   (DMA_Perstream_Reg_Num*DMA_Perstream_Reg_Size)
#define DMA_Stream_Reg_Total_Size      (DMA_Stream_Count*DMA_Perstream_Reg_Total_Size)

#define DMA_Register_Count 2
#define DMA_Register_Size  4
#define DMA_Total_Size     (DMA_Register_Count*DMA_Register_Size)

#define DMA_Reg_Total_Size             (DMA_Stream_Reg_Total_Size+DMA_Total_Size)


/* Circular mode delay */
#define DMA_Circular_delay 1e6

//#define R_DMA_Sx             (0x8 / 4)
//#define R_DMA_Sx_COUNT           8
//#define R_DMA_Sx_REGS            4
#define R_DMA_SxCR           (0x00)
#define R_DMA_SxCR_EN   0x00000001
#define R_DMA_SxNDTR         (0x04)
////#define R_DMA_SxNDTR_EN 0x00000001
#define R_DMA_SxPAR          (0x08)
#define R_DMA_SxMAR          (0x0c)

#define R_DMA_MAX            (0xd0 / 4)

/* Inturrupts definitions */
#define DMA_ISR_GIF  0x0001 //Channel global inturrupt flag
#define DMA_ISR_TCIF 0x0002 //Channel transfer complete flag
#define DMA_ISR_HTIF 0x0004 //Channel half transfer flag
#define DMA_ISR_TEIF 0x0008 //Channel transfer error flag

/* CR definitions */
// 1 bit
#define DMA_CCR_EN      0x0001 //Enable DMA channel
#define DMA_CCR_TCIE    0x0002 //Enable Transfer Complete Inturrupt
#define DMA_CCR_HTIE    0x0004 //Enable Half Transfer Inturrupt
#define DMA_CCR_TEIE    0x0008 //Enable Transfer Error Inturrupt
#define DMA_CCR_DIR     0x0010 //Transfer Direction (0 means Peripheral to Memory, 1 means Memory to Peripheral)
#define DMA_CCR_CIRC    0x0020 //Circular mode (0 for Normal and 1 for circular)
#define DMA_CCR_PINC    0x0040 //Peripheral increment mode
#define DMA_CCR_MINC    0x0080 //Memory increment mode
// 2 bits
#define DMA_CCR_PSIZE   0x0300 //Peripheral Size (0x00 for byte, 0x01 for half word, 0x02 for word)
#define DMA_CCR_MSIZE   0x0C00 //Memory Size     (0x00 for byte, 0x01 for half word, 0x02 for word)
#define DMA_CCR_PL      0x3000 //Channel Priority
// 1 bit
#define DMA_CCR_MEM2MEM 0x4000 //Memory to memory mode

typedef struct stm32_dma_stream {
	qemu_irq irq;

	uint32_t cr;
	uint32_t ndtr; //length & remaining bytes
	uint32_t par; //source
	uint32_t mar; //destination

	uint32_t circular_par;    //original source for circular mode
	uint32_t circular_mar;    //original destination for circular mode
	uint32_t circular_ndtr;   //original (length & remaining bytes) for circular mode
} stm32_dma_stream;

//static int msize_table[] = {1, 2, 4, 0};
static int msize_table[] = {1, 0, 2, 0}; //in bytes

typedef struct stm32_dma {
	SysBusDevice busdev;
	MemoryRegion iomem;
	struct QEMUTimer *circular_timer;
	uint32_t chan_circular_mode;

	uint32_t isr; //read only
	uint32_t ifcr; //write only
	stm32_dma_stream stream[DMA_Stream_Count]; 
} stm32_dma;

qemu_irq *stm32_DMA1_irq;
static void stm32_dma_stream_start_once(stm32_dma *s, uint32_t stream_no, bool skip_enabled_check);
static void stm32_dma_stream_start_whole(stm32_dma *s, uint32_t stream_no, bool skip_enabled_check);
void printall(stm32_dma *s);

static void stm32_dma_stream_circular_timer(void *opaque) {
	stm32_dma *s = (stm32_dma *)opaque;
	stm32_dma_stream *s_stream = &(s->stream[s->chan_circular_mode-1]);

	uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

	stm32_dma_stream_start_once(s, s->chan_circular_mode, false);

	if (s_stream->ndtr == 0) {
		//if in circular mode, reset the addreses and the length
		if (s_stream->cr & DMA_CCR_CIRC) {
			s_stream->mar = s_stream->circular_mar;
			s_stream->par = s_stream->circular_par;
			s_stream->ndtr = s_stream->circular_ndtr;

			//Enable the chennel, Because it is disabled in stm32_dma_stream_start_once
			s_stream->cr |= DMA_CCR_EN;
		}
	}

	if ((s_stream->cr & DMA_CCR_EN) != 0)
		timer_mod(s->circular_timer, curr_time + DMA_Circular_delay);
}

//Find the channel from destination address
static int dma_find_channel(stm32_dma *s, uint32_t address, uint8_t search_in_dest) {
	uint32_t reg_contents;
	uint32_t i;

	for (i=0; i<DMA_Stream_Count; i++) {
		if (search_in_dest != 0) { //Not zero
			reg_contents = s->stream[i].mar;
		} else { //Zero
			reg_contents = s->stream[i].par;
		}

		if (reg_contents == address) {
			return i+1; //We start the index fom zero and zero is not a valid dma channel
		}
	}

	return -1;
}

void printall(stm32_dma *s) {
    uint32_t i;
	for (i=0; i<DMA_Stream_Count; i++) {
		printf("ch%d: %08x\n", i , s->stream[i].par);
	}
}

/* Inturrupt routines */
static void dma_irq_handler(void *opaque, int n, int level) {
	int channel;
	stm32_dma *s = (stm32_dma *) opaque;

	/*printall(s);*/

	switch (level) {
		case 0x00: //Reset the level
			channel = -2;
		break;

		case 0x10: //USART 1 Read
			channel = dma_find_channel(s, 0x40013800+0x04, 1);
			if (channel == -1) {
				hw_error("Cant Find channel for USART1 RX\n");
				exit(1);
			}
		break;

		case 0x11: //USART 1 Write
			channel = dma_find_channel(s, 0x40013800+0x04, 0);
			if (channel == -1) {
				hw_error("Cant Find channel for USART1 TX\n");
				exit(1);
			}
		break;

	    default:
	        hw_error("Invalid level");
	}

	if (channel == -1) {
		hw_error("Cant Find channel for USART1 RX\n");
		exit(1);
	} if (channel == -2) {
		//Nothing to do
	} else {
		stm32_dma_stream_start_once(s, channel, false);
		//printf("Done transfer to channel %d\n", channel);
	}

}

/* Start a DMA transfer for a given stream. */
static void
stm32_dma_stream_start_once(stm32_dma *s, uint32_t stream_no, bool skip_enabled_check)
{
	uint32_t src,dest,src_size,dest_size;
	uint8_t buf[2];
	stm32_dma_stream *s_stream = &(s->stream[stream_no-1]);
	int psize = msize_table[(s_stream->cr >> 8) & 0x3];
	int msize = msize_table[(s_stream->cr >> 10) & 0x3];

	DPRINTF("%s: stream: %d\n", __func__, stream_no);

	if (psize == 0 || msize == 0) {
		hw_error("Error: Halfword is NOT implemented or invalid size!\n");
		return;
	}

	if (skip_enabled_check == false) {
		if (s_stream->cr & DMA_CCR_EN) { //Channel enabled
		} else {
			//printf("Warning: skipping DMA transfer because channel %zu isnt enabled!\n", stream_no);
			return;
		}
	}

	if (s_stream->cr & DMA_CCR_DIR) {
		src = s_stream->mar;
		dest = s_stream->par;

		src_size = msize;
		dest_size = psize;
	} else { //peripheral to memory OR memort to memory
		src = s_stream->par;
		dest = s_stream->mar;

		src_size = psize;
		dest_size = msize;
	}

	/* XXX Skip USART, as pacing control is not yet in place. */
	/*if (s->par == 0x40011004) {
		qemu_log_mask(LOG_UNIMP, "stm32 dma: skipping USART\n");
		return;
	}*/

	DPRINTF("%s: transferring from 0x%08x - %d byte(s) to 0x%08x - %d byte(s)\n", __func__, src,
		          src_size, dest, dest_size);

	s_stream->ndtr--;
	cpu_physical_memory_read(src, buf, src_size); //read from peripheral
	cpu_physical_memory_write(dest, buf, dest_size); //write to memory

	if (s_stream->cr & DMA_CCR_PINC)
		s_stream->par += psize;

	if (s_stream->cr & DMA_CCR_MINC) {
		s_stream->mar += msize;
	}

	if (s_stream->ndtr == 0) {
		/* Transfer complete. */

		if ((s_stream->cr & DMA_CCR_CIRC) == 0) {
			//Disable the stream
			s_stream->cr &= ~DMA_CCR_EN;
		}

		//Set Transfer complete flag
		s->isr |= (DMA_ISR_TCIF << ((stream_no-1) * 4));

		if (s_stream->cr & DMA_CCR_TCIE) {
			if ((s_stream->cr & DMA_CCR_CIRC) == 0) {
				//Do the actual inturrupt
				qemu_irq_pulse(s->stream[stream_no-1].irq);
			} else {
				printf("Warning: Skipping an inturrupt, because the inturrupt with circular mode will crash the emulator");
			}
		}

		//printf("DMA channel %zu Transfer Completed!\n", stream_no);

	}
}

/* Start a DMA Whole transfer for a given stream. */
static void
stm32_dma_stream_start_whole(stm32_dma *s, uint32_t stream_no, bool skip_enabled_check)
{
	while(s->stream[stream_no-1].ndtr != 0) {
		stm32_dma_stream_start_once(s, stream_no, skip_enabled_check);
	}

}

/* Per-stream read. */
static uint32_t
stm32_dma_stream_read(stm32_dma_stream *s, int stream_no, uint32_t reg)
{
	DPRINTF("\n\nSTREAM READ! %d %d\n\n", stream_no, reg);
	switch (reg) {
	case R_DMA_SxCR:
		DPRINTF("   %s: stream: %d, register CR\n", __func__, stream_no);
		return s->cr;
	case R_DMA_SxNDTR:
		DPRINTF("   %s: stream: %d, register NDTR (UNIMPLEMENTED)\n", __func__, stream_no);
		qemu_log_mask(LOG_UNIMP, "stm32 dma unimp read reg NDTR\n");
		return 0;
	case R_DMA_SxPAR:
		DPRINTF("   %s: stream: %d, register PAR (UNIMPLEMENTED)\n", __func__, stream_no);
		qemu_log_mask(LOG_UNIMP, "stm32 dma unimp read reg PAR\n");
		return 0;
	case R_DMA_SxMAR:
		DPRINTF("   %s: stream: %d, register M0AR (UNIMPLEMENTED)\n", __func__, stream_no);
		qemu_log_mask(LOG_UNIMP, "stm32 dma unimp read reg M0AR\n");
		return 0;
	default:
		DPRINTF("   %s: stream: %d, register 0x%02x\n", __func__, stream_no, reg<<2);
		qemu_log_mask(LOG_UNIMP, "stm32 dma unimp read stream reg 0x%02x\n",
		  (unsigned int)reg<<2);
	}
	return 0;
}

/* Register read. */
static uint64_t
stm32_dma_read(void *arg, hwaddr addr, unsigned int size)
{
	stm32_dma *s = arg;
	int offset = addr & 0x3;
	uint64_t result;
	int stream_no = (addr - 8) >> (DMA_Perstream_Reg_Total_Size/DMA_Perstream_Reg_Size);

	DPRINTF("%s: addr: 0x%llx, size:%d...\n", __func__, addr, size);

	if (size != 4) {
		hw_error("stm32 crc only supports 4-byte reads\n");
		return 0;
	}

	if (offset != 0) {
		hw_error("stm32 dma: Adreess is not 4 byte Alligned write!\n");
	}

	if (addr >= R_DMA_MAX) {
		hw_error("invalid read stm32 dma register 0x%02x\n",
					  (unsigned int)addr << 2);
		result = 0;
	} else {
		switch(addr) {
		case R_DMA_ISR:
			DPRINTF("   %s: register ISR\n", __func__);
			result = s->isr;
			break;
		case R_DMA_IFCR:
			DPRINTF("   %s: register IFCR\n", __func__);
			result = s->ifcr;
			break;

		default:
			/* Only per-stream registers remain. */
			result = stm32_dma_stream_read(&s->stream[stream_no], stream_no, (addr - DMA_Total_Size) % DMA_Perstream_Reg_Total_Size);
			break;
		}
	}

	DPRINTF("    %s: result:0x%llx\n", __func__, result);
	return result;
}

/* Per-stream register write. */
static void
stm32_dma_stream_write(stm32_dma_stream *s, stm32_dma *s_o, int stream_no, uint32_t addr, uint32_t data)
{
	uint64_t curr_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

	switch (addr) {
	case R_DMA_SxCR:
		DPRINTF("%s: stream: %d, register CR, data:0x%x\n", __func__, stream_no, data);
		if ((s->cr & DMA_CCR_EN) == 0 && (data & DMA_CCR_EN) != 0) {
			if ((data & DMA_CCR_MEM2MEM) != 0) {
				//Do the whole stream only in memory to memory mode
				//Here, stream_no is starting from zero, and stm32_dma_stream_start_whole is expecting 1 or more
				stm32_dma_stream_start_whole(s_o, stream_no+1, true);
			} else if (data & DMA_CCR_CIRC) {
				//Continuosly transfer in dma circular mode
				if (data & DMA_CCR_DIR)
					hw_error("stm32 dma Circular mode is not supported in Memory to Peripheral mode\n");

				if (s_o->chan_circular_mode != 0)
					hw_error("stm32 dma Circular mode: Currently only one channel supports circular mode!\n");

				s_o->chan_circular_mode = stream_no+1;
				timer_mod(s_o->circular_timer, curr_time + DMA_Circular_delay);
			}
		}
		s->cr = data;
		break;
	case R_DMA_SxNDTR:
		DPRINTF("%s: stream: %d, register NDTR, data:0x%x\n", __func__, stream_no, data);
		if (s->cr & DMA_CCR_EN) {
			hw_error("stm32 dma write to NDTR while enabled\n");
			return;
		}
		s->ndtr = s->circular_ndtr = data;
		break;
	case R_DMA_SxPAR:
		DPRINTF("%s: stream: %d, register PAR, data:0x%x\n", __func__, stream_no, data);
		s->par = s->circular_par = data;
		break;
	case R_DMA_SxMAR:
		DPRINTF("%s: stream: %d, register MAR, data:0x%x\n", __func__, stream_no, data);
		s->mar = s->circular_mar = data;
		break;
	}
}

/* Register write. */
static void
stm32_dma_write(void *arg, hwaddr addr, uint64_t data, unsigned int size)
{
	stm32_dma *s = arg;
	int offset = addr & 0x3;
	int stream_no = (addr - 8) >> (DMA_Perstream_Reg_Total_Size/DMA_Perstream_Reg_Size);
	// Using the following variable prevents "comparison of unsigned expression >= 0 is always true"
	// compile error. This is a hack, but there does not seem to be a better way
	// without either disabling the warning altogether, or removing the check.
	// See: https://stackoverflow.com/questions/7542857/suppress-comparison-always-true-warning-for-macros
	hwaddr r_dma_isr = R_DMA_ISR;

	(void)offset;

	/* XXX Check DMA peripheral clock enable. */
	if (size != 4) {
		hw_error("stm32 dma only supports 4-byte writes\n");
		return;
	}

	if (offset != 0) {
		hw_error("stm32 dma: Adreess is not 4 byte Alligned write!\n");
	}

	switch(addr) {
		case R_DMA_ISR:
		DPRINTF("%s: register ISR (READ-ONLY), data: 0x%llx\n", __func__, data);
		hw_error("stm32 dma: invalid write to ISR\n");
		break;
	case R_DMA_IFCR:
		DPRINTF("%s: register IFCR, data: 0x%llx\n", __func__, data);
		// Any interrupt clear write to stream x clears all interrupts for that stream
		s->ifcr = data;
		break;
	default:
		if (addr >= r_dma_isr && addr < DMA_Reg_Total_Size) {
			//printf("Write to stream %d\n", stream_no);
			stm32_dma_stream_write(&s->stream[stream_no], s, stream_no, (addr - DMA_Total_Size) % DMA_Perstream_Reg_Total_Size, data);
		} else {
			hw_error("stm32 dma unimpl write reg 0x%08x\n", (unsigned int)addr);
		}
	}
}

static const MemoryRegionOps stm32_dma_ops = {
	.read = stm32_dma_read,
	.write = stm32_dma_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
	.impl = {
		.min_access_size = 1,
		.max_access_size = 4,
	}
};

static int
stm32_dma_init(SysBusDevice *dev)
{
	stm32_dma *s = STM32_DMA(dev);
	int i;

	memory_region_init_io(&s->iomem, OBJECT(s), &stm32_dma_ops, s, "dma", 0x400);
	sysbus_init_mmio(dev, &s->iomem);

	for (i = 0; i < DMA_Stream_Count; i++) {
		sysbus_init_irq(dev, &s->stream[i].irq);
	}

	stm32_DMA1_irq = qemu_allocate_irqs(dma_irq_handler, (void *)s, 1);

	s->circular_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, (QEMUTimerCB *)stm32_dma_stream_circular_timer, s);

	return 0;
}

static void
stm32_dma_reset(DeviceState *ds)
{
	stm32_dma *s = STM32_DMA(ds);

	memset(&s->ifcr, 0, sizeof(s->ifcr));

	s->chan_circular_mode = 0;

	int i;
	for (i=0; i < DMA_Stream_Count; i++) {
		qemu_irq save = s->stream[i].irq;
		memset(&s->stream[i], 0, sizeof(stm32_dma_stream));
		s->stream[i].irq = save;
	}
}

static Property stm32_dma_properties[] = {
	DEFINE_PROP_END_OF_LIST(),
};

static void
stm32_dma_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SysBusDeviceClass *sc = SYS_BUS_DEVICE_CLASS(klass);
	sc->init = stm32_dma_init;
	dc->reset = stm32_dma_reset;
	//TODO: fix this: dc->no_user = 1;
	dc->props = stm32_dma_properties;
}

static const TypeInfo
stm32_dma_info = {
	.name          = "stm32_dma",
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(stm32_dma),
	.class_init    = stm32_dma_class_init,
};

static void
stm32_dma_register_types(void)
{
	type_register_static(&stm32_dma_info);
}

type_init(stm32_dma_register_types)
