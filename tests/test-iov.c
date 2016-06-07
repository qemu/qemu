#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"

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

static void test_io(void)
{
#ifndef _WIN32
/* socketpair(PF_UNIX) which does not exist on windows */

    int sv[2];
    int r;
    unsigned i, j, k, s, t;
    fd_set fds;
    unsigned niov;
    struct iovec *iov, *siov;
    unsigned char *buf;
    size_t sz;

    iov_random(&iov, &niov);
    sz = iov_size(iov, niov);
    buf = g_malloc(sz);
    for (i = 0; i < sz; ++i) {
        buf[i] = i & 255;
    }
    iov_from_buf(iov, niov, 0, buf, sz);

    siov = g_malloc(sizeof(*iov) * niov);
    memcpy(siov, iov, sizeof(*iov) * niov);

    if (socketpair(PF_UNIX, SOCK_STREAM, 0, sv) < 0) {
       perror("socketpair");
       exit(1);
    }

    FD_ZERO(&fds);

    t = 0;
    if (fork() == 0) {
       /* writer */

       close(sv[0]);
       FD_SET(sv[1], &fds);
       fcntl(sv[1], F_SETFL, O_RDWR|O_NONBLOCK);
       r = g_test_rand_int_range(sz / 2, sz);
       setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &r, sizeof(r));

       for (i = 0; i <= sz; ++i) {
           for (j = i; j <= sz; ++j) {
               k = i;
               do {
                   s = g_test_rand_int_range(0, j - k + 1);
                   r = iov_send(sv[1], iov, niov, k, s);
                   g_assert(memcmp(iov, siov, sizeof(*iov)*niov) == 0);
                   if (r >= 0) {
                       k += r;
                       t += r;
                       usleep(g_test_rand_int_range(0, 30));
                   } else if (errno == EAGAIN) {
                       select(sv[1]+1, NULL, &fds, NULL, NULL);
                       continue;
                   } else {
                       perror("send");
                       exit(1);
                   }
               } while(k < j);
           }
       }
       exit(0);

    } else {
       /* reader & verifier */

       close(sv[1]);
       FD_SET(sv[0], &fds);
       fcntl(sv[0], F_SETFL, O_RDWR|O_NONBLOCK);
       r = g_test_rand_int_range(sz / 2, sz);
       setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &r, sizeof(r));
       usleep(500000);

       for (i = 0; i <= sz; ++i) {
           for (j = i; j <= sz; ++j) {
               k = i;
               iov_memset(iov, niov, 0, 0xff, -1);
               do {
                   s = g_test_rand_int_range(0, j - k + 1);
                   r = iov_recv(sv[0], iov, niov, k, s);
                   g_assert(memcmp(iov, siov, sizeof(*iov)*niov) == 0);
                   if (r > 0) {
                       k += r;
                       t += r;
                   } else if (!r) {
                       if (s) {
                           break;
                       }
                   } else if (errno == EAGAIN) {
                       select(sv[0]+1, &fds, NULL, NULL, NULL);
                       continue;
                   } else {
                       perror("recv");
                       exit(1);
                   }
               } while(k < j);
               test_iov_bytes(iov, niov, i, j - i);
           }
        }
     }
#endif
}

static void test_discard_front(void)
{
    struct iovec *iov;
    struct iovec *iov_tmp;
    unsigned int iov_cnt;
    unsigned int iov_cnt_tmp;
    void *old_base;
    size_t size;
    size_t ret;

    /* Discard zero bytes */
    iov_random(&iov, &iov_cnt);
    iov_tmp = iov;
    iov_cnt_tmp = iov_cnt;
    ret = iov_discard_front(&iov_tmp, &iov_cnt_tmp, 0);
    g_assert(ret == 0);
    g_assert(iov_tmp == iov);
    g_assert(iov_cnt_tmp == iov_cnt);
    iov_free(iov, iov_cnt);

    /* Discard more bytes than vector size */
    iov_random(&iov, &iov_cnt);
    iov_tmp = iov;
    iov_cnt_tmp = iov_cnt;
    size = iov_size(iov, iov_cnt);
    ret = iov_discard_front(&iov_tmp, &iov_cnt_tmp, size + 1);
    g_assert(ret == size);
    g_assert(iov_cnt_tmp == 0);
    iov_free(iov, iov_cnt);

    /* Discard entire vector */
    iov_random(&iov, &iov_cnt);
    iov_tmp = iov;
    iov_cnt_tmp = iov_cnt;
    size = iov_size(iov, iov_cnt);
    ret = iov_discard_front(&iov_tmp, &iov_cnt_tmp, size);
    g_assert(ret == size);
    g_assert(iov_cnt_tmp == 0);
    iov_free(iov, iov_cnt);

    /* Discard within first element */
    iov_random(&iov, &iov_cnt);
    iov_tmp = iov;
    iov_cnt_tmp = iov_cnt;
    old_base = iov->iov_base;
    size = g_test_rand_int_range(1, iov->iov_len);
    ret = iov_discard_front(&iov_tmp, &iov_cnt_tmp, size);
    g_assert(ret == size);
    g_assert(iov_tmp == iov);
    g_assert(iov_cnt_tmp == iov_cnt);
    g_assert(iov_tmp->iov_base == old_base + size);
    iov_tmp->iov_base = old_base; /* undo before g_free() */
    iov_free(iov, iov_cnt);

    /* Discard entire first element */
    iov_random(&iov, &iov_cnt);
    iov_tmp = iov;
    iov_cnt_tmp = iov_cnt;
    ret = iov_discard_front(&iov_tmp, &iov_cnt_tmp, iov->iov_len);
    g_assert(ret == iov->iov_len);
    g_assert(iov_tmp == iov + 1);
    g_assert(iov_cnt_tmp == iov_cnt - 1);
    iov_free(iov, iov_cnt);

    /* Discard within second element */
    iov_random(&iov, &iov_cnt);
    iov_tmp = iov;
    iov_cnt_tmp = iov_cnt;
    old_base = iov[1].iov_base;
    size = iov->iov_len + g_test_rand_int_range(1, iov[1].iov_len);
    ret = iov_discard_front(&iov_tmp, &iov_cnt_tmp, size);
    g_assert(ret == size);
    g_assert(iov_tmp == iov + 1);
    g_assert(iov_cnt_tmp == iov_cnt - 1);
    g_assert(iov_tmp->iov_base == old_base + (size - iov->iov_len));
    iov_tmp->iov_base = old_base; /* undo before g_free() */
    iov_free(iov, iov_cnt);
}

static void test_discard_back(void)
{
    struct iovec *iov;
    unsigned int iov_cnt;
    unsigned int iov_cnt_tmp;
    void *old_base;
    size_t size;
    size_t ret;

    /* Discard zero bytes */
    iov_random(&iov, &iov_cnt);
    iov_cnt_tmp = iov_cnt;
    ret = iov_discard_back(iov, &iov_cnt_tmp, 0);
    g_assert(ret == 0);
    g_assert(iov_cnt_tmp == iov_cnt);
    iov_free(iov, iov_cnt);

    /* Discard more bytes than vector size */
    iov_random(&iov, &iov_cnt);
    iov_cnt_tmp = iov_cnt;
    size = iov_size(iov, iov_cnt);
    ret = iov_discard_back(iov, &iov_cnt_tmp, size + 1);
    g_assert(ret == size);
    g_assert(iov_cnt_tmp == 0);
    iov_free(iov, iov_cnt);

    /* Discard entire vector */
    iov_random(&iov, &iov_cnt);
    iov_cnt_tmp = iov_cnt;
    size = iov_size(iov, iov_cnt);
    ret = iov_discard_back(iov, &iov_cnt_tmp, size);
    g_assert(ret == size);
    g_assert(iov_cnt_tmp == 0);
    iov_free(iov, iov_cnt);

    /* Discard within last element */
    iov_random(&iov, &iov_cnt);
    iov_cnt_tmp = iov_cnt;
    old_base = iov[iov_cnt - 1].iov_base;
    size = g_test_rand_int_range(1, iov[iov_cnt - 1].iov_len);
    ret = iov_discard_back(iov, &iov_cnt_tmp, size);
    g_assert(ret == size);
    g_assert(iov_cnt_tmp == iov_cnt);
    g_assert(iov[iov_cnt - 1].iov_base == old_base);
    iov_free(iov, iov_cnt);

    /* Discard entire last element */
    iov_random(&iov, &iov_cnt);
    iov_cnt_tmp = iov_cnt;
    old_base = iov[iov_cnt - 1].iov_base;
    size = iov[iov_cnt - 1].iov_len;
    ret = iov_discard_back(iov, &iov_cnt_tmp, size);
    g_assert(ret == size);
    g_assert(iov_cnt_tmp == iov_cnt - 1);
    iov_free(iov, iov_cnt);

    /* Discard within second-to-last element */
    iov_random(&iov, &iov_cnt);
    iov_cnt_tmp = iov_cnt;
    old_base = iov[iov_cnt - 2].iov_base;
    size = iov[iov_cnt - 1].iov_len +
           g_test_rand_int_range(1, iov[iov_cnt - 2].iov_len);
    ret = iov_discard_back(iov, &iov_cnt_tmp, size);
    g_assert(ret == size);
    g_assert(iov_cnt_tmp == iov_cnt - 1);
    g_assert(iov[iov_cnt - 2].iov_base == old_base);
    iov_free(iov, iov_cnt);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_rand_int();
    g_test_add_func("/basic/iov/from-to-buf", test_to_from_buf);
    g_test_add_func("/basic/iov/io", test_io);
    g_test_add_func("/basic/iov/discard-front", test_discard_front);
    g_test_add_func("/basic/iov/discard-back", test_discard_back);
    return g_test_run();
}
