#ifndef QEMU_UNICODE_H
#define QEMU_UNICODE_H

int mod_utf8_codepoint(const char *s, size_t n, char **end);
ssize_t mod_utf8_encode(char buf[], size_t bufsz, int codepoint);

#endif
