CPUState *panda_current_cpu(int index);
CPUState *panda_cpu_in_translate(void);
TranslationBlock *panda_get_tb(void);
int panda_get_memcb_status(void);