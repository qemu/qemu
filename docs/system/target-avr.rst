.. _AVR-System-emulator:

AVR System emulator
-------------------

Use the executable ``qemu-system-avr`` to emulate a AVR 8 bit based machine.
These can have one of the following cores: avr1, avr2, avr25, avr3, avr31,
avr35, avr4, avr5, avr51, avr6, avrtiny, xmega2, xmega3, xmega4, xmega5,
xmega6 and xmega7.

As for now it supports few Arduino boards for educational and testing purposes.
These boards use a ATmega controller, which model is limited to USART & 16-bit
timer devices, enought to run FreeRTOS based applications (like
https://github.com/seharris/qemu-avr-tests/blob/master/free-rtos/Demo/AVR_ATMega2560_GCC/demo.elf
).

Following are examples of possible usages, assuming demo.elf is compiled for
AVR cpu

 - Continuous non interrupted execution:
   ``qemu-system-avr -machine mega2560 -bios demo.elf``

 - Continuous non interrupted execution with serial output into telnet window:
   ``qemu-system-avr -machine mega2560 -bios demo.elf -serial
   tcp::5678,server,nowait -nographic``
   and then in another shell
   ``telnet localhost 5678``

 - Debugging wit GDB debugger:
   ``qemu-system-avr -machine mega2560 -bios demo.elf -s -S``
   and then in another shell
   ``avr-gdb demo.elf``
   and then within GDB shell
   ``target remote :1234``

 - Print out executed instructions:
   ``qemu-system-avr -machine mega2560 -bios demo.elf -d in_asm``
