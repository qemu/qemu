#ifndef HW_ETRAXFS_DMA_H
#define HW_ETRAXFS_DMA_H

#include "exec/hwaddr.h"

struct dma_context_metadata {
	/* data descriptor md */
	uint16_t metadata;
};

struct etraxfs_dma_client
{
	/* DMA controller. */
	int channel;
	void *ctrl;

	/* client.  */
	struct {
		int (*push)(void *opaque, unsigned char *buf,
		            int len, bool eop);
		void (*pull)(void *opaque);
		void (*metadata_push)(void *opaque,
		                      const struct dma_context_metadata *md);
		void *opaque;
	} client;
};

void *etraxfs_dmac_init(hwaddr base, int nr_channels);
void etraxfs_dmac_connect(void *opaque, int channel, qemu_irq *line,
			  int input);
void etraxfs_dmac_connect_client(void *opaque, int c, 
				 struct etraxfs_dma_client *cl);
int etraxfs_dmac_input(struct etraxfs_dma_client *client, 
		       void *buf, int len, int eop);

#endif
