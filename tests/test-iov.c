#include <glib.h>
#include "qemu-common.h"
#include "iov.h"

/* create a randomly-sized iovec with random vectors */
static void iov_random(struct iovec **iovp, unsigned *iov_cntp)
{
     unsigned niov = g_test_rand_int_range(3,8);
     struct iovec *iov = g_malloc(niov * sizeof(*iov));
     unsigned i;
     for (i = 0; i < niov; ++i) {
         iov[i].iov_len = g_test_rand_int_range(5,20);
         iov[i].iov_base = g_malloc(iov[i].iov_len);
     }
     *iovp = iov;
     *iov_cntp = niov;
}

static void iov_free(struct iovec *iov, unsigned niov)
{
    unsigned i;
    for (i = 0; i < niov; ++i) {
        g_free(iov[i].iov_base);
    }
    g_free(iov);
}

static void test_iov_bytes(struct iovec *iov, unsigned niov,
                           size_t offset, size_t bytes)
{
    unsigned i;
    size_t j, o;
    unsigned char *b;
    o = 0;

    /* we walk over all elements, */
    for (i = 0; i < niov; ++i) {
        b = iov[i].iov_base;
        /* over each char of each element, */
        for (j = 0; j < iov[i].iov_len; ++j) {
            /* counting each of them and
             * verifying that the ones within [offset,offset+bytes)
             * range are equal to the position number (o) */
            if (o >= offset && o < offset + bytes) {
                g_assert(b[j] == (o & 255));
            } else {
                g_assert(b[j] == 0xff);
            }
            ++o;
        }
    }
}

static void test_to_from_buf_1(void)
{
     unsigned niov;
     struct iovec *iov;
     size_t sz;
     unsigned char *ibuf, *obuf;
     unsigned i, j, n;

     iov_random(&iov, &niov);

     sz = iov_size(iov, niov);

     ibuf = g_malloc(sz + 8) + 4;
     memcpy(ibuf-4, "aaaa", 4); memcpy(ibuf + sz, "bbbb", 4);
     obuf = g_malloc(sz + 8) + 4;
     memcpy(obuf-4, "xxxx", 4); memcpy(obuf + sz, "yyyy", 4);

     /* fill in ibuf with 0123456... */
     for (i = 0; i < sz; ++i) {
         ibuf[i] = i & 255;
     }

     for (i = 0; i <= sz; ++i) {

         /* Test from/to buf for offset(i) in [0..sz] up to the end of buffer.
          * For last iteration with offset == sz, the procedure should
          * skip whole vector and process exactly 0 bytes */

         /* first set bytes [i..sz) to some "random" value */
         n = iov_memset(iov, niov, 0, 0xff, -1);
         g_assert(n == sz);

         /* next copy bytes [i..sz) from ibuf to iovec */
         n = iov_from_buf(iov, niov, i, ibuf + i, -1);
         g_assert(n == sz - i);

         /* clear part of obuf */
         memset(obuf + i, 0, sz - i);
         /* and set this part of obuf to values from iovec */
         n = iov_to_buf(iov, niov, i, obuf + i, -1);
         g_assert(n == sz - i);

         /* now compare resulting buffers */
         g_assert(memcmp(ibuf, obuf, sz) == 0);

         /* test just one char */
         n = iov_to_buf(iov, niov, i, obuf + i, 1);
         g_assert(n == (i < sz));
         if (n) {
             g_assert(obuf[i] == (i & 255));
         }

         for (j = i; j <= sz; ++j) {
             /* now test num of bytes cap up to byte no. j,
              * with j in [i..sz]. */

             /* clear iovec */
             n = iov_memset(iov, niov, 0, 0xff, -1);
             g_assert(n == sz);

             /* copy bytes [i..j) from ibuf to iovec */
             n = iov_from_buf(iov, niov, i, ibuf + i, j - i);
             g_assert(n == j - i);

             /* clear part of obuf */
             memset(obuf + i, 0, j - i);

             /* copy bytes [i..j) from iovec to obuf */
             n = iov_to_buf(iov, niov, i, obuf + i, j - i);
             g_assert(n == j - i);

             /* verify result */
             g_assert(memcmp(ibuf, obuf, sz) == 0);

             /* now actually check if the iovec contains the right data */
             test_iov_bytes(iov, niov, i, j - i);
         }
    }
    g_assert(!memcmp(ibuf-4, "aaaa", 4) && !memcmp(ibuf+sz, "bbbb", 4));
    g_free(ibuf-4);
    g_assert(!memcmp(obuf-4, "xxxx", 4) && !memcmp(obuf+sz, "yyyy", 4));
    g_free(obuf-4);
    iov_free(iov, niov);
}

static void test_to_from_buf(void)
{
    int x;
    for (x = 0; x < 4; ++x) {
        test_to_from_buf_1();
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_rand_int();
    g_test_add_func("/basic/iov/from-to-buf", test_to_from_buf);
    return g_test_run();
}
