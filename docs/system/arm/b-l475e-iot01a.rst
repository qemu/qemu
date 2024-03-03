B-L475E-IOT01A IoT Node (``b-l475e-iot01a``)
============================================

The B-L475E-IOT01A IoT Node uses the STM32L475VG SoC which is based on
ARM Cortex-M4F core. It is part of STMicroelectronics
:doc:`STM32 boards </system/arm/stm32>` and more specifically the STM32L4
ultra-low power series. The STM32L4x5 chip runs at up to 80 MHz and
integrates 128 KiB of SRAM and up to 1MiB of Flash. The B-L475E-IOT01A board
namely features 64 Mibit QSPI Flash, BT, WiFi and RF connectivity,
USART, I2C, SPI, CAN and USB OTG, as well as a variety of sensors.

Supported devices
"""""""""""""""""

Currently B-L475E-IOT01A machine's only supports the following devices:

- Cortex-M4F based STM32L4x5 SoC
- STM32L4x5 EXTI (Extended interrupts and events controller)
- STM32L4x5 SYSCFG (System configuration controller)
- STM32L4x5 RCC (Reset and clock control)

Missing devices
"""""""""""""""

The B-L475E-IOT01A does *not* support the following devices:

- Serial ports (UART)
- General-purpose I/Os (GPIO)
- Analog to Digital Converter (ADC)
- SPI controller
- Timer controller (TIMER)

See the complete list of unimplemented peripheral devices
in the STM32L4x5 module : ``./hw/arm/stm32l4x5_soc.c``

Boot options
""""""""""""

The B-L475E-IOT01A machine can be started using the ``-kernel``
option to load a firmware. Example:

.. code-block:: bash

  $ qemu-system-arm -M b-l475e-iot01a -kernel firmware.bin

