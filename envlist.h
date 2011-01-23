#ifndef ENVLIST_H
#define ENVLIST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envlist envlist_t;

envlist_t *envlist_create(void);
void envlist_free(envlist_t *);
int envlist_setenv(envlist_t *, const char *);
int envlist_unsetenv(envlist_t *, const char *);
int envlist_parse_set(envlist_t *, const char *);
int envlist_parse_unset(envlist_t *, const char *);
char **envlist_to_environ(const envlist_t *, size_t *);

#ifdef __cplusplus
}
#endif

#endif /* ENVLIST_H */
