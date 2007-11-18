/* es1370.c */
int es1370_init (PCIBus *bus, AudioState *s);

/* sb16.c */
int SB16_init (AudioState *s, qemu_irq *pic);

/* adlib.c */
int Adlib_init (AudioState *s, qemu_irq *pic);

/* gus.c */
int GUS_init (AudioState *s, qemu_irq *pic);

