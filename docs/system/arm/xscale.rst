Sharp XScale-based PDA models (``akita``, ``borzoi``, ``spitz``, ``terrier``, ``tosa``)
=======================================================================================

The Sharp Zaurus are PDAs based on XScale, able to run Linux ('SL series').

The SL-6000 (\"Tosa\"), released in 2005, uses a PXA255 System-on-chip.

The SL-C3000 (\"Spitz\"), SL-C1000 (\"Akita\"), SL-C3100 (\"Borzoi\") and
SL-C3200 (\"Terrier\") use a PXA270.

The clamshell PDA models emulation includes the following peripherals:

-  Intel PXA255/PXA270 System-on-chip (ARMv5TE core)

-  NAND Flash memory - not in \"Tosa\"

-  IBM/Hitachi DSCM microdrive in a PXA PCMCIA slot - not in \"Akita\"

-  On-chip OHCI USB controller - not in \"Tosa\"

-  On-chip LCD controller

-  On-chip Real Time Clock

-  TI ADS7846 touchscreen controller on SSP bus

-  Maxim MAX1111 analog-digital converter on |I2C| bus

-  GPIO-connected keyboard controller and LEDs

-  Secure Digital card connected to PXA MMC/SD host

-  Three on-chip UARTs

-  WM8750 audio CODEC on |I2C| and |I2S| busses
