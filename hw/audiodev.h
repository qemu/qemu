/* es1370.c */
int es1370_init(PCIBus *bus);

/* sb16.c */
int SB16_init(ISABus *bus);

/* adlib.c */
int Adlib_init(ISABus *bus);

/* gus.c */
int GUS_init(ISABus *bus);

/* ac97.c */
int ac97_init(PCIBus *bus);

/* cs4231a.c */
int cs4231a_init(ISABus *bus);

/* intel-hda.c + hda-audio.c */
int intel_hda_and_codec_init(PCIBus *bus);
