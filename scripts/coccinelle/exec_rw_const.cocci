/*
  Usage:

    spatch \
           --macro-file scripts/cocci-macro-file.h \
           --sp-file scripts/coccinelle/exec_rw_const.cocci \
           --keep-comments \
           --in-place \
           --dir .
*/

// Convert to boolean
@@
expression E1, E2, E3, E4, E5;
@@
(
- address_space_rw(E1, E2, E3, E4, E5, 0)
+ address_space_rw(E1, E2, E3, E4, E5, false)
|
- address_space_rw(E1, E2, E3, E4, E5, 1)
+ address_space_rw(E1, E2, E3, E4, E5, true)
|

- cpu_physical_memory_rw(E1, E2, E3, 0)
+ cpu_physical_memory_rw(E1, E2, E3, false)
|
- cpu_physical_memory_rw(E1, E2, E3, 1)
+ cpu_physical_memory_rw(E1, E2, E3, true)
|

- cpu_physical_memory_map(E1, E2, 0)
+ cpu_physical_memory_map(E1, E2, false)
|
- cpu_physical_memory_map(E1, E2, 1)
+ cpu_physical_memory_map(E1, E2, true)
)

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

// Avoid uses of address_space_rw() with a constant is_write argument.
@@
expression E1, E2, E3, E4, E5;
symbol true, false;
@@
(
- address_space_rw(E1, E2, E3, E4, E5, false)
+ address_space_read(E1, E2, E3, E4, E5)
|
- address_space_rw(E1, E2, E3, E4, E5, true)
+ address_space_write(E1, E2, E3, E4, E5)
)

// Avoid uses of cpu_physical_memory_rw() with a constant is_write argument.
@@
expression E1, E2, E3;
@@
(
- cpu_physical_memory_rw(E1, E2, E3, false)
+ cpu_physical_memory_read(E1, E2, E3)
|
- cpu_physical_memory_rw(E1, E2, E3, true)
+ cpu_physical_memory_write(E1, E2, E3)
)

// Remove useless cast
@@
expression E1, E2, E3, E4, E5, E6;
type T;
@@
(
- address_space_rw(E1, E2, E3, (T *)(E4), E5, E6)
+ address_space_rw(E1, E2, E3, E4, E5, E6)
|
- address_space_read(E1, E2, E3, (T *)(E4), E5)
+ address_space_read(E1, E2, E3, E4, E5)
|
- address_space_write(E1, E2, E3, (T *)(E4), E5)
+ address_space_write(E1, E2, E3, E4, E5)
|
- address_space_write_rom(E1, E2, E3, (T *)(E4), E5)
+ address_space_write_rom(E1, E2, E3, E4, E5)
|

- cpu_physical_memory_rw(E1, (T *)(E2), E3, E4)
+ cpu_physical_memory_rw(E1, E2, E3, E4)
|
- cpu_physical_memory_read(E1, (T *)(E2), E3)
+ cpu_physical_memory_read(E1, E2, E3)
|
- cpu_physical_memory_write(E1, (T *)(E2), E3)
+ cpu_physical_memory_write(E1, E2, E3)
|

- dma_memory_read(E1, E2, (T *)(E3), E4)
+ dma_memory_read(E1, E2, E3, E4)
|
- dma_memory_write(E1, E2, (T *)(E3), E4)
+ dma_memory_write(E1, E2, E3, E4)
)
