#ifndef GDBSTUB_H
#define GDBSTUB_H

#define DEFAULT_GDBSTUB_PORT 1234

#ifdef CONFIG_USER_ONLY
int gdb_handlesig (CPUState *, int);
void gdb_exit(CPUState *, int);
#endif
int gdbserver_start(int);

#endif
