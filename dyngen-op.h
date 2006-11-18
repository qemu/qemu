static inline int gen_new_label(void)
{
    return nb_gen_labels++;
}

static inline void gen_set_label(int n)
{
    gen_labels[n] = gen_opc_ptr - gen_opc_buf;
}
