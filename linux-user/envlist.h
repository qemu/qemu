#ifndef ENVLIST_H
#define ENVLIST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct envlist envlist_t;

extern	envlist_t *envlist_create(void);
extern	void envlist_free(envlist_t *);
extern	int envlist_setenv(envlist_t *, const char *);
extern	int envlist_unsetenv(envlist_t *, const char *);
extern	int envlist_parse_set(envlist_t *, const char *);
extern	int envlist_parse_unset(envlist_t *, const char *);
extern	char **envlist_to_environ(const envlist_t *, size_t *);

#ifdef __cplusplus
}
#endif

#endif /* ENVLIST_H */
