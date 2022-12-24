/*
 * Synopsys DesignWareCore for USB OTG.
 *
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "hw/platform-bus.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/arm/ipod_touch_usb_otg.h"

static inline size_t synopsys_usb_tx_fifo_start(synopsys_usb_state *_state, uint32_t _fifo)
{
	if(_fifo == 0)
		return _state->gnptxfsiz >> 16;
	else
		return _state->dptxfsiz[_fifo-1] >> 16;
}

static inline size_t synopsys_usb_tx_fifo_size(synopsys_usb_state *_state, uint32_t _fifo)
{
	if(_fifo == 0)
		return _state->gnptxfsiz & 0xFFFF;
	else
		return _state->dptxfsiz[_fifo-1] & 0xFFFF;
}

static void synopsys_usb_update_irq(synopsys_usb_state *_state)
{
	_state->daintsts = 0;
	_state->gintsts &=~ (GINTMSK_OEP | GINTMSK_INEP | GINTMSK_OTG);

	if(_state->gotgint)
		_state->gintsts |= GINTMSK_OTG;

	int i;
	for(i = 0; i < USB_NUM_ENDPOINTS; i++)
	{
		if(_state->out_eps[i].interrupt_status & _state->doepmsk)
		{
			_state->daintsts |= 1 << (i+DAINT_OUT_SHIFT);
			if(_state->daintmsk & (1 << (i+DAINT_OUT_SHIFT)))
				_state->gintsts |= GINTMSK_OEP;
		}

		if(_state->in_eps[i].interrupt_status & _state->diepmsk)
		{
			_state->daintsts |= 1 << (i+DAINT_IN_SHIFT);
			if(_state->daintmsk & (1 << (i+DAINT_IN_SHIFT)))
				_state->gintsts |= GINTMSK_INEP;
		}
	}
	
	if((_state->pcgcctl & 3) == 0 && _state->gintmsk & _state->gintsts)
	{
		//printf("USB: IRQ triggered 0x%08x & 0x%08x.\n", _state->gintsts, _state->gintmsk);
		qemu_irq_raise(_state->irq);
	}
	else
		qemu_irq_lower(_state->irq);
}

static void synopsys_usb_update_ep(synopsys_usb_state *_state, synopsys_usb_ep_state *_ep)
{
	if(_ep->control & USB_EPCON_SETNAK)
	{
		_ep->control |= USB_EPCON_NAKSTS;
		_ep->interrupt_status |= USB_EPINT_INEPNakEff;
		_ep->control &=~ USB_EPCON_SETNAK;
	}

	if(_ep->control & USB_EPCON_DISABLE)
	{
		_ep->interrupt_status |= USB_EPINT_EPDisbld;
		_ep->control &=~ (USB_EPCON_DISABLE | USB_EPCON_ENABLE);
	}
}

static void synopsys_usb_update_in_ep(synopsys_usb_state *_state, uint8_t _ep)
{
	synopsys_usb_ep_state *eps = &_state->in_eps[_ep];
	synopsys_usb_update_ep(_state, eps);

	if(eps->control & USB_EPCON_ENABLE)
		;//printf("USB: IN transfer queued on %d.\n", _ep);
}

static void synopsys_usb_update_out_ep(synopsys_usb_state *_state, uint8_t _ep)
{
	synopsys_usb_ep_state *eps = &_state->out_eps[_ep];
	synopsys_usb_update_ep(_state, eps);

	if(eps->control & USB_EPCON_ENABLE)
		;//printf("USB: OUT transfer queued on %d.\n", _ep);
}

static uint32_t synopsys_usb_in_ep_read(synopsys_usb_state *_state, uint8_t _ep, hwaddr _addr)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Tried to read from disabled EP %d.\n", _ep);
		return 0;
	}

    switch (_addr)
	{
    case 0x00:
        return _state->in_eps[_ep].control;

    case 0x08:
        return _state->in_eps[_ep].interrupt_status;

    case 0x10:
        return _state->in_eps[_ep].tx_size;

    case 0x14:
        return _state->in_eps[_ep].dma_address;

    case 0x1C:
        return _state->in_eps[_ep].dma_buffer;

    default:
        hw_error("usb_synopsys: bad ep read offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }

	return 0;
}

static uint32_t synopsys_usb_out_ep_read(synopsys_usb_state *_state, int _ep, hwaddr _addr)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Tried to read from disabled EP %d.\n", _ep);
		return 0;
	}

    switch (_addr)
	{
    case 0x00:
        return _state->out_eps[_ep].control;

    case 0x08:
        return _state->out_eps[_ep].interrupt_status;

    case 0x10:
        return _state->out_eps[_ep].tx_size;

    case 0x14:
        return _state->out_eps[_ep].dma_address;

    case 0x1C:
        return _state->out_eps[_ep].dma_buffer;

    default:
        hw_error("usb_synopsys: bad ep read offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }

	return 0;
}

static uint64_t synopsys_usb_read(void *opaque, hwaddr _addr, unsigned size)
{
	synopsys_usb_state *state = (synopsys_usb_state *)opaque;
	
	//printf("USB: Read 0x%08x.\n", _addr);

	switch(_addr)
	{
	case PCGCCTL:
		return state->pcgcctl;

	case GOTGCTL:
		return state->gotgctl;

	case GOTGINT:
		return state->gotgint;

	case GRSTCTL:
		return state->grstctl;

	case GHWCFG1:
		return state->ghwcfg1;

	case GHWCFG2:
		return state->ghwcfg2;

	case GHWCFG3:
		return state->ghwcfg3;

	case GHWCFG4:
		return state->ghwcfg4;

	case GAHBCFG:
		return state->gahbcfg;

	case GUSBCFG:
		return state->gusbcfg;

	case GINTMSK:
		return state->gintmsk;

	case GINTSTS:
		return state->gintsts;

	case DIEPMSK:
		return state->diepmsk;

	case DOEPMSK:
		return state->doepmsk;

	case DAINTMSK:
		return state->daintmsk;
	
	case DAINTSTS:
		return state->daintsts;

	case DCTL:
		return state->dctl;

	case DCFG:
		return state->dcfg;

	case DSTS:
		return state->dsts;

	case GRXSTSR:
	case GRXSTSP:
		return 0; // TODO: Do something about this?

	case GNPTXFSTS:
		return 0xFFFFFFFF;

	case GRXFSIZ:
		return state->grxfsiz;

	case GNPTXFSIZ:
		return state->gnptxfsiz;

	case DIEPTXF(1) ... DIEPTXF(USB_NUM_FIFOS+1):
		_addr -= DIEPTXF(1);
		_addr >>= 2;
		return state->dptxfsiz[_addr];

	case USB_INREGS ... (USB_INREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_INREGS;
		return synopsys_usb_in_ep_read(state, _addr >> 5, _addr & 0x1f);

	case USB_OUTREGS ... (USB_OUTREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_OUTREGS;
		return synopsys_usb_out_ep_read(state, _addr >> 5, _addr & 0x1f);

	case USB_FIFO_START ... USB_FIFO_END-4:
		_addr -= USB_FIFO_START;
		return *((uint32_t*)(&state->fifos[_addr]));

	default:
		hw_error("USB: Unhandled read address 0x%08x!\n", _addr);
	}

	return 0;
}

static void synopsys_usb_in_ep_write(synopsys_usb_state *_state, int _ep, hwaddr _addr, uint32_t _val)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Wrote to disabled EP %d.\n", _ep);
		return;
	}

    switch (_addr)
	{
    case 0x00:
		_state->in_eps[_ep].control = _val;
		synopsys_usb_update_in_ep(_state, _ep);
		return;

    case 0x08:
        _state->in_eps[_ep].interrupt_status &=~ _val;
		synopsys_usb_update_irq(_state);
		return;

    case 0x10:
        _state->in_eps[_ep].tx_size = _val;
		return;

    case 0x14:
        _state->in_eps[_ep].dma_address = _val;
		return;

    case 0x1C:
        _state->in_eps[_ep].dma_buffer = _val;
		return;

    default:
        hw_error("usb_synopsys: bad ep write offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }
}

static void synopsys_usb_out_ep_write(synopsys_usb_state *_state, int _ep, hwaddr _addr, uint32_t _val)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Wrote to disabled EP %d.\n", _ep);
		return;
	}

    switch (_addr)
	{
	case 0x00:
        _state->out_eps[_ep].control = _val;
		synopsys_usb_update_out_ep(_state, _ep);
		return;

    case 0x08:
        _state->out_eps[_ep].interrupt_status &=~ _val;
		synopsys_usb_update_irq(_state);
		return;

    case 0x10:
        _state->out_eps[_ep].tx_size = _val;
		return;

    case 0x14:
        _state->out_eps[_ep].dma_address = _val;
		return;

    case 0x1C:
        _state->out_eps[_ep].dma_buffer = _val;
		return;

    default:
        hw_error("usb_synopsys: bad ep write offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }
}

static void synopsys_usb_write(void *opaque, hwaddr _addr, uint64_t _val, unsigned size)
{
	synopsys_usb_state *state = (synopsys_usb_state *)opaque;
	
	//printf("USB: Write 0x%08x to 0x%08x.\n", _val, _addr);

	switch(_addr)
	{
	case PCGCCTL:
		state->pcgcctl = _val;
		synopsys_usb_update_irq(state);
		return;

	case GOTGCTL:
		state->gotgctl = _val;
		break;

	case GOTGINT:
		state->gotgint &=~ _val;
		synopsys_usb_update_irq(state);
		return;

	case GRSTCTL:
		if(_val & GRSTCTL_CORESOFTRESET)
		{
			state->grstctl = GRSTCTL_CORESOFTRESET;

			// Do reset stuff
			// if(state->server_host)
			// {
			// 	tcp_usb_cleanup(&state->tcp_state);
			// 	tcp_usb_init(&state->tcp_state, synopsys_usb_tcp_callback, NULL, state);

			// 	printf("Connecting to USB server at %s:%d...\n",
			// 			state->server_host, state->server_port);

			// 	int ret = tcp_usb_connect(&state->tcp_state, state->server_host, state->server_port);
			// 	if(ret < 0)
			// 		hw_error("Failed to connect to USB server (%d).\n", ret);

			// 	printf("Connected to USB server.\n");
			// }

			state->grstctl &= ~GRSTCTL_CORESOFTRESET;
			state->grstctl |= GRSTCTL_AHBIDLE;
			state->gintsts |= GINTMSK_RESET;
			synopsys_usb_update_irq(state);
		}
		else if(_val == 0)
			state->grstctl = _val;

		return;

	case GINTMSK:
		state->gintmsk = _val;
		synopsys_usb_update_irq(state);
		break;

	case GINTSTS:
		state->gintsts &=~ _val;
		synopsys_usb_update_irq(state);
		return;

	case DOEPMSK:
		state->doepmsk = _val;
		synopsys_usb_update_irq(state);
		return;

	case DIEPMSK:
		state->diepmsk = _val;
		synopsys_usb_update_irq(state);
		return;

	case DAINTMSK:
		state->daintmsk = _val;
		synopsys_usb_update_irq(state);
		return;
	
	case DAINTSTS:
		state->daintsts &=~ _val;
		synopsys_usb_update_irq(state);
		return;

	case GAHBCFG:
		state->gahbcfg = _val;
		return;

	case GUSBCFG:
		state->gusbcfg = _val;
		return;

	case DCTL:
		if((_val & DCTL_SGNPINNAK) != (state->dctl & DCTL_SGNPINNAK)
				&& (_val & DCTL_SGNPINNAK))
		{
			state->gintsts |= GINTMSK_GINNAKEFF;
			_val &=~ DCTL_SGNPINNAK;
		}

		if((_val & DCTL_SGOUTNAK) != (state->dctl & DCTL_SGOUTNAK)
				&& (_val & DCTL_SGOUTNAK))
		{
			state->gintsts |= GINTMSK_GOUTNAKEFF;
			_val &=~ DCTL_SGOUTNAK;
		}

		state->dctl = _val;
		synopsys_usb_update_irq(state);
		return;

	case DCFG:
		//printf("USB: dcfg = 0x%08x.\n", _val);
		state->dcfg = _val;
		return;

	case GRXFSIZ:
		state->grxfsiz = _val;
		return;

	case GNPTXFSIZ:
		state->gnptxfsiz = _val;
		return;

	case DIEPTXF(1) ... DIEPTXF(USB_NUM_FIFOS+1):
		_addr -= DIEPTXF(1);
		_addr >>= 2;
		state->dptxfsiz[_addr] = _val;
		return;

	case USB_INREGS ... (USB_INREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_INREGS;
		synopsys_usb_in_ep_write(state, _addr >> 5, _addr & 0x1f, _val);
		return;

	case USB_OUTREGS ... (USB_OUTREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_OUTREGS;
		synopsys_usb_out_ep_write(state, _addr >> 5, _addr & 0x1f, _val);
		return;

	case USB_FIFO_START ... USB_FIFO_END-4:
		_addr -= USB_FIFO_START;
		*((uint32_t*)(&state->fifos[_addr])) = _val;
		return;

	default:
		hw_error("USB: Unhandled write address 0x%08x!\n", _addr);
	}
}

static const MemoryRegionOps usb_otg_ops = {
    .read = synopsys_usb_read,
    .write = synopsys_usb_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8900_usb_otg_reset(DeviceState *d)
{
	synopsys_usb_state *state = S5L8900USBOTG(d);

	printf("USB: cfg = 0x%08x, 0x%08x, 0x%08x, 0x%08x.\n",
			state->ghwcfg1,
			state->ghwcfg2,
			state->ghwcfg3,
			state->ghwcfg4);

	state->pcgcctl = 3;

	state->gahbcfg = 0;
	state->gusbcfg = 0;

	state->dctl = 0;
	state->dcfg = 0;
	state->dsts = 0;

	state->gotgctl = 0;
	state->gotgint = 0;

	state->gintmsk = 0;
	state->gintsts = 0;

	state->daintmsk = 0;
	state->daintsts = 0;

	state->diepmsk = 0;
	state->doepmsk = 0;

	state->grxfsiz = 0x100;
	state->gnptxfsiz = (0x100 << 16) | 0x100;

	uint32_t counter = 0x200;
	int i;
	for(i = 0; i < USB_NUM_FIFOS; i++)
	{
		state->dptxfsiz[i] = (counter << 16) | 0x100;
		counter += 0x100;
	}

	for(i = 0; i < USB_NUM_ENDPOINTS; i++)
	{
		synopsys_usb_ep_state *in = &state->in_eps[i];
		in->control = 0;
		in->dma_address = 0;
		in->fifo = 0;
		in->tx_size = 0;

		synopsys_usb_ep_state *out = &state->out_eps[i];
		out->control = 0;
		out->dma_address = 0;
		out->fifo = 0;
		out->tx_size = 0;
	}

	synopsys_usb_update_irq(state);
}

static void s5l8900_usb_otg_init1(Object *obj)
{
	DeviceState *dev = DEVICE(obj);
    synopsys_usb_state *s = S5L8900USBOTG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &usb_otg_ops, s, "usb_otg", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

// Helper for adding to a machine
DeviceState *ipod_touch_init_usb_otg(qemu_irq _irq, uint32_t _hwcfg[4])
{
	DeviceState *dev = qdev_new(TYPE_S5L8900USBOTG);
	synopsys_usb_state *state = S5L8900USBOTG(dev);

	state->ghwcfg1 = _hwcfg[0];
	state->ghwcfg2 = _hwcfg[1];
	state->ghwcfg3 = _hwcfg[2];
	state->ghwcfg4 = _hwcfg[3];

	SysBusDevice *sdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(sdev, 0, _irq);

    return dev;
}

static void s5l8900_usb_otg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = s5l8900_usb_otg_reset;
}

static const TypeInfo s5l8900_usb_otg_info = {
    .name          = TYPE_S5L8900USBOTG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(synopsys_usb_state),
    .instance_init = s5l8900_usb_otg_init1,
    .class_init    = s5l8900_usb_otg_class_init,
};

static void s5l8900_usb_otg_register_types(void)
{
    type_register_static(&s5l8900_usb_otg_info);
}

type_init(s5l8900_usb_otg_register_types)