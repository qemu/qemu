/* AXI DMA connection. Used until qdev provides a generic way.  */
typedef void (*DMAPushFn)(void *opaque,
                            unsigned char *buf, size_t len, uint32_t *app);

struct XilinxDMAConnection {
    void *dma;
    void *client;

    DMAPushFn to_dma;
    DMAPushFn to_client;
};

static inline void xlx_dma_connect_client(struct XilinxDMAConnection *dmach,
                                          void *c, DMAPushFn f)
{
    dmach->client = c;
    dmach->to_client = f;
}

static inline void xlx_dma_connect_dma(struct XilinxDMAConnection *dmach,
                                       void *d, DMAPushFn f)
{
    dmach->dma = d;
    dmach->to_dma = f;
}

static inline
void xlx_dma_push_to_dma(struct XilinxDMAConnection *dmach,
                         uint8_t *buf, size_t len, uint32_t *app)
{
    dmach->to_dma(dmach->dma, buf, len, app);
}
static inline
void xlx_dma_push_to_client(struct XilinxDMAConnection *dmach,
                            uint8_t *buf, size_t len, uint32_t *app)
{
    dmach->to_client(dmach->client, buf, len, app);
}

