/*
  Usage:

    spatch \
           --macro-file scripts/cocci-macro-file.h \
           --sp-file scripts/coccinelle/exec_rw_const.cocci \
           --keep-comments \
           --in-place \
           --dir .
*/

// Use address_space_write instead of casting to non-const
@@
type T;
const T *V;
expression E1, E2, E3, E4;
@@
(
- address_space_rw(E1, E2, E3, (T *)V, E4, 1)
+ address_space_write(E1, E2, E3, V, E4)
|
- address_space_rw(E1, E2, E3, (void *)V, E4, 1)
+ address_space_write(E1, E2, E3, V, E4)
)

// Remove useless cast
@@
expression E1, E2, E3, E4;
type T;
@@
(
- dma_memory_read(E1, E2, (T *)(E3), E4)
+ dma_memory_read(E1, E2, E3, E4)
|
- dma_memory_write(E1, E2, (T *)(E3), E4)
+ dma_memory_write(E1, E2, E3, E4)
)
