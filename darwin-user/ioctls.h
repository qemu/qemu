     /* emulated ioctl list */

     IOCTL(TIOCGETA, IOC_R, MK_PTR(MK_STRUCT(STRUCT_termios)))
     IOCTL(TIOCSETA, IOC_W, MK_PTR(MK_STRUCT(STRUCT_termios)))
