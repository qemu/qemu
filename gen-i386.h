static inline void gen_start(void)
{
}

static inline void gen_end(void)
{
    *gen_code_ptr++ = 0xc3; /* ret */
}
