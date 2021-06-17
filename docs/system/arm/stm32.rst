STMicroelectronics STM32 boards (``netduino2``, ``netduinoplus2``, ``stm32vldiscovery``)
========================================================================================

The `STM32`_ chips are a family of 32-bit ARM-based microcontroller by
STMicroelectronics.

.. _STM32: https://www.st.com/en/microcontrollers-microprocessors/stm32-32-bit-arm-cortex-mcus.html

The STM32F1 series is based on ARM Cortex-M3 core. The following machines are
based on this chip :

- ``stm32vldiscovery``  STM32VLDISCOVERY board with STM32F100RBT6 microcontroller

The STM32F2 series is based on ARM Cortex-M3 core. The following machines are
based on this chip :

- ``netduino2``         Netduino 2 board with STM32F205RFT6 microcontroller

The STM32F4 series is based on ARM Cortex-M4F core. This series is pin-to-pin
compatible with STM32F2 series. The following machines are based on this chip :

- ``netduinoplus2``     Netduino Plus 2 board with STM32F405RGT6 microcontroller

There are many other STM32 series that are currently not supported by QEMU.

Supported devices
-----------------

 * ARM Cortex-M3, Cortex M4F
 * Analog to Digital Converter (ADC)
 * EXTI interrupt
 * Serial ports (USART)
 * SPI controller
 * System configuration (SYSCFG)
 * Timer controller (TIMER)

Missing devices
---------------

 * Camera interface (DCMI)
 * Controller Area Network (CAN)
 * Cycle Redundancy Check (CRC) calculation unit
 * Digital to Analog Converter (DAC)
 * DMA controller
 * Ethernet controller
 * Flash Interface Unit
 * GPIO controller
 * I2C controller
 * Inter-Integrated Sound (I2S) controller
 * Power supply configuration (PWR)
 * Random Number Generator (RNG)
 * Real-Time Clock (RTC) controller
 * Reset and Clock Controller (RCC)
 * Secure Digital Input/Output (SDIO) interface
 * USB OTG
 * Watchdog controller (IWDG, WWDG)

Boot options
------------

The STM32 machines can be started using the ``-kernel`` option to load a
firmware. Example:

.. code-block:: bash

  $ qemu-system-arm -M stm32vldiscovery -kernel firmware.bin
