#include "hw.h"
#include "sh.h"
#include "loader.h"

#define CE1  0x0100
#define CE2  0x0200
#define RE   0x0400
#define WE   0x0800
#define ALE  0x1000
#define CLE  0x2000
#define RDY1 0x4000
#define RDY2 0x8000
#define RDY(n) ((n) == 0 ? RDY1 : RDY2)

typedef enum { WAIT, READ1, READ2, READ3 } state_t;

typedef struct {
    uint8_t *flash_contents;
    state_t state;
    uint32_t address;
    uint8_t address_cycle;
} tc58128_dev;

static tc58128_dev tc58128_devs[2];

#define FLASH_SIZE (16*1024*1024)

static void init_dev(tc58128_dev * dev, const char *filename)
{
    int ret, blocks;

    dev->state = WAIT;
    dev->flash_contents = qemu_mallocz(FLASH_SIZE);
    memset(dev->flash_contents, 0xff, FLASH_SIZE);
    if (!dev->flash_contents) {
	fprintf(stderr, "could not alloc memory for flash\n");
	exit(1);
    }
    if (filename) {
	/* Load flash image skipping the first block */
	ret = load_image(filename, dev->flash_contents + 528 * 32);
	if (ret < 0) {
	    fprintf(stderr, "ret=%d\n", ret);
	    fprintf(stderr, "qemu: could not load flash image %s\n",
		    filename);
	    exit(1);
	} else {
	    /* Build first block with number of blocks */
	    blocks = (ret + 528 * 32 - 1) / (528 * 32);
	    dev->flash_contents[0] = blocks & 0xff;
	    dev->flash_contents[1] = (blocks >> 8) & 0xff;
	    dev->flash_contents[2] = (blocks >> 16) & 0xff;
	    dev->flash_contents[3] = (blocks >> 24) & 0xff;
	    fprintf(stderr, "loaded %d bytes for %s into flash\n", ret,
		    filename);
	}
    }
}

static void handle_command(tc58128_dev * dev, uint8_t command)
{
    switch (command) {
    case 0xff:
	fprintf(stderr, "reset flash device\n");
	dev->state = WAIT;
	break;
    case 0x00:
	fprintf(stderr, "read mode 1\n");
	dev->state = READ1;
	dev->address_cycle = 0;
	break;
    case 0x01:
	fprintf(stderr, "read mode 2\n");
	dev->state = READ2;
	dev->address_cycle = 0;
	break;
    case 0x50:
	fprintf(stderr, "read mode 3\n");
	dev->state = READ3;
	dev->address_cycle = 0;
	break;
    default:
	fprintf(stderr, "unknown flash command 0x%02x\n", command);
        abort();
    }
}

static void handle_address(tc58128_dev * dev, uint8_t data)
{
    switch (dev->state) {
    case READ1:
    case READ2:
    case READ3:
	switch (dev->address_cycle) {
	case 0:
	    dev->address = data;
	    if (dev->state == READ2)
		dev->address |= 0x100;
	    else if (dev->state == READ3)
		dev->address |= 0x200;
	    break;
	case 1:
	    dev->address += data * 528 * 0x100;
	    break;
	case 2:
	    dev->address += data * 528;
	    fprintf(stderr, "address pointer in flash: 0x%08x\n",
		    dev->address);
	    break;
	default:
	    /* Invalid data */
            abort();
	}
	dev->address_cycle++;
	break;
    default:
        abort();
    }
}

static uint8_t handle_read(tc58128_dev * dev)
{
#if 0
    if (dev->address % 0x100000 == 0)
	fprintf(stderr, "reading flash at address 0x%08x\n", dev->address);
#endif
    return dev->flash_contents[dev->address++];
}

/* We never mark the device as busy, so interrupts cannot be triggered
   XXXXX */

static int tc58128_cb(uint16_t porta, uint16_t portb,
                      uint16_t * periph_pdtra, uint16_t * periph_portadir,
                      uint16_t * periph_pdtrb, uint16_t * periph_portbdir)
{
    int dev;

    if ((porta & CE1) == 0)
	dev = 0;
    else if ((porta & CE2) == 0)
	dev = 1;
    else
	return 0;		/* No device selected */

    if ((porta & RE) && (porta & WE)) {
	/* Nothing to do, assert ready and return to input state */
	*periph_portadir &= 0xff00;
	*periph_portadir |= RDY(dev);
	*periph_pdtra |= RDY(dev);
	return 1;
    }

    if (porta & CLE) {
	/* Command */
	assert((porta & WE) == 0);
	handle_command(&tc58128_devs[dev], porta & 0x00ff);
    } else if (porta & ALE) {
	assert((porta & WE) == 0);
	handle_address(&tc58128_devs[dev], porta & 0x00ff);
    } else if ((porta & RE) == 0) {
	*periph_portadir |= 0x00ff;
	*periph_pdtra &= 0xff00;
	*periph_pdtra |= handle_read(&tc58128_devs[dev]);
    } else {
        abort();
    }
    return 1;
}

static sh7750_io_device tc58128 = {
    RE | WE,			/* Port A triggers */
    0,				/* Port B triggers */
    tc58128_cb			/* Callback */
};

int tc58128_init(struct SH7750State *s, const char *zone1, const char *zone2)
{
    init_dev(&tc58128_devs[0], zone1);
    init_dev(&tc58128_devs[1], zone2);
    return sh7750_register_io_device(s, &tc58128);
}
