/* ps2.c */
void *ps2_kbd_init(void (*update_irq)(void *, int), void *update_arg);
void *ps2_mouse_init(void (*update_irq)(void *, int), void *update_arg);
void ps2_write_mouse(void *, int val);
void ps2_write_keyboard(void *, int val);
uint32_t ps2_read_data(void *);
void ps2_queue(void *, int b);
void ps2_keyboard_set_translation(void *opaque, int mode);
void ps2_mouse_fake_event(void *opaque);

