Nokia N800 and N810 tablets (``n800``, ``n810``)
================================================

Nokia N800 and N810 internet tablets (known also as RX-34 and RX-44 /
48) emulation supports the following elements:

-  Texas Instruments OMAP2420 System-on-chip (ARM1136 core)

-  RAM and non-volatile OneNAND Flash memories

-  Display connected to EPSON remote framebuffer chip and OMAP on-chip
   display controller and a LS041y3 MIPI DBI-C controller

-  TI TSC2301 (in N800) and TI TSC2005 (in N810) touchscreen
   controllers driven through SPI bus

-  National Semiconductor LM8323-controlled qwerty keyboard driven
   through |I2C| bus

-  Secure Digital card connected to OMAP MMC/SD host

-  Three OMAP on-chip UARTs and on-chip STI debugging console

-  Mentor Graphics \"Inventra\" dual-role USB controller embedded in a
   TI TUSB6010 chip - only USB host mode is supported

-  TI TMP105 temperature sensor driven through |I2C| bus

-  TI TWL92230C power management companion with an RTC on
   |I2C| bus

-  Nokia RETU and TAHVO multi-purpose chips with an RTC, connected
   through CBUS
