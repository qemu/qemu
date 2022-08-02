#ifndef QPP_SRV_H
#define QPP_SRV_H

/*
 * Prototype for the on_exit callback: callback functions should be
 * of type `void f(int, bool)`
 */
QPP_CB_PROTOTYPE(void, on_exit, int, bool);

/*
 * Prototypes for the do_add and do_sub functions. Both return an int and
 * take an int as an argument.
 */
QPP_FUN_PROTOTYPE(qpp_srv, int, do_add, int);
QPP_FUN_PROTOTYPE(qpp_srv, int, do_sub, int);

#endif /* QPP_SRV_H */
