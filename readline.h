#ifndef READLINE_H
#define READLINE_H

#include "qemu-common.h"

typedef void ReadLineFunc(Monitor *mon, const char *str, void *opaque);

void readline_add_completion(const char *str);
void readline_set_completion_index(int index);
void readline_find_completion(const char *cmdline);

const char *readline_get_history(unsigned int index);

void readline_handle_byte(int ch);

void readline_start(const char *prompt, int is_password,
                    ReadLineFunc *readline_func, void *opaque);
void readline_show_prompt(void);

#endif /* !READLINE_H */
