/*
  Usage:

    spatch \
           --macro-file scripts/cocci-macro-file.h \
           --sp-file scripts/coccinelle/exec_rw_const.cocci \
           --keep-comments \
           --in-place \
           --dir .
*/

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
