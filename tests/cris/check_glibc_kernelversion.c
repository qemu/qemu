/*
 * Check the lz insn.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"

#define __LINUX_KERNEL_VERSION 131584

#define DL_SYSDEP_OSCHECK(FATAL) \
  do {                                                                        \
    /* Test whether the kernel is new enough.  This test is only              \
       performed if the library is not compiled to run on all                 \
       kernels.  */                                                           \
    if (__LINUX_KERNEL_VERSION > 0)                                           \
      {                                                                       \
        char bufmem[64];                                                      \
        char *buf = bufmem;                                                   \
        unsigned int version;                                                 \
        int parts;                                                            \
        char *cp;                                                             \
        struct utsname uts;                                                   \
                                                                              \
        /* Try the uname syscall */                                           \
        if (__uname (&uts))                                                   \
          {                                                                   \
            /* This was not successful.  Now try reading the /proc            \
               filesystem.  */                                                \
            ssize_t reslen;                                                   \
            int fd = __open ("/proc/sys/kernel/osrelease", O_RDONLY);         \
            if (fd == -1                                                      \
                || (reslen = __read (fd, bufmem, sizeof (bufmem))) <= 0)      \
              /* This also didn't work.  We give up since we cannot           \
                 make sure the library can actually work.  */                 \
              FATAL ("FATAL: cannot determine library version\n");            \
            __close (fd);                                                     \
            buf[MIN (reslen, (ssize_t) sizeof (bufmem) - 1)] = '\0';          \
          }                                                                   \
        else                                                                  \
          buf = uts.release;                                                  \
                                                                              \
        /* Now convert it into a number.  The string consists of at most      \
           three parts.  */                                                   \
        version = 0;                                                          \
        parts = 0;                                                            \
        cp = buf;                                                             \
        while ((*cp >= '0') && (*cp <= '9'))                                  \
          {                                                                   \
            unsigned int here = *cp++ - '0';                                  \
                                                                              \
            while ((*cp >= '0') && (*cp <= '9'))                              \
              {                                                               \
                here *= 10;                                                   \
                here += *cp++ - '0';                                          \
              }                                                               \
                                                                              \
            ++parts;                                                          \
            version <<= 8;                                                    \
            version |= here;                                                  \
                                                                              \
            if (*cp++ != '.')                                                 \
              /* Another part following?  */                                  \
              break;                                                          \
          }                                                                   \
                                                                              \
        if (parts < 3)                                                        \
          version <<= 8 * (3 - parts);                                        \
                                                                              \
        /* Now we can test with the required version.  */                     \
        if (version < __LINUX_KERNEL_VERSION)                                 \
          /* Not sufficient.  */                                               \
          FATAL ("FATAL: kernel too old\n");                                  \
                                                                              \
        _dl_osversion = version;                                              \
      }                                                                       \
  } while (0)

int main(void)
{
        char bufmem[64] = "2.6.22";
        char *buf = bufmem;
        unsigned int version;
        int parts;
        char *cp;

        version = 0;
        parts = 0;
        cp = buf;
        while ((*cp >= '0') && (*cp <= '9'))
          {
            unsigned int here = *cp++ - '0';

            while ((*cp >= '0') && (*cp <= '9'))
              {
                here *= 10;
                here += *cp++ - '0';
              }

            ++parts;
            version <<= 8;
            version |= here;

            if (*cp++ != '.')
              /* Another part following?  */
              break;
          }

        if (parts < 3)
          version <<= 8 * (3 - parts);
	if (version < __LINUX_KERNEL_VERSION)
		err();
	pass();
	exit(0);
}
