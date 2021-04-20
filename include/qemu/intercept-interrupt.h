#ifndef INTERRUPT_INTERCEPTION
#define INTERRUPT_INTERCEPTION
bool is_module_inserted;
unsigned int irq_line = 0;
bool is_interrupt_raised;
QemuMutex interrupt_raised_mutex;
#endif