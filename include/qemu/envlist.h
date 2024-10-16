#ifndef ENVLIST_H
#define ENVLIST_H

typedef struct envlist envlist_t;

envlist_t *envlist_create(void);
void envlist_free(envlist_t *);
int envlist_setenv(envlist_t *, const char *);
int envlist_unsetenv(envlist_t *, const char *);
char **envlist_to_environ(const envlist_t *, size_t *);

#endif /* ENVLIST_H */
