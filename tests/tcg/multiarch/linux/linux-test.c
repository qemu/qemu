/*
 *  linux and CPU test
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <utime.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sched.h>
#include <dirent.h>
#include <setjmp.h>
#include <sys/shm.h>
#include <assert.h>

#define STACK_SIZE 16384

static void error1(const char *filename, int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s:%d: ", filename, line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static int __chk_error(const char *filename, int line, int ret)
{
    if (ret < 0) {
        error1(filename, line, "%m (ret=%d, errno=%d/%s)",
               ret, errno, strerror(errno));
    }
    return ret;
}

#define error(fmt, ...) error1(__FILE__, __LINE__, fmt, ## __VA_ARGS__)

#define chk_error(ret) __chk_error(__FILE__, __LINE__, (ret))

/*******************************************************/

#define FILE_BUF_SIZE 300

static void test_file(void)
{
    int fd, i, len, ret;
    uint8_t buf[FILE_BUF_SIZE];
    uint8_t buf2[FILE_BUF_SIZE];
    uint8_t buf3[FILE_BUF_SIZE];
    char cur_dir[1024];
    struct stat st;
    struct utimbuf tbuf;
    struct iovec vecs[2];
    DIR *dir;
    struct dirent64 *de;
    /* TODO: make common tempdir creation for tcg tests */
    char template[] = "/tmp/linux-test-XXXXXX";
    char *tmpdir = mkdtemp(template);

    assert(tmpdir);

    if (getcwd(cur_dir, sizeof(cur_dir)) == NULL)
        error("getcwd");

    chk_error(chdir(tmpdir));

    /* open/read/write/close/readv/writev/lseek */

    fd = chk_error(open("file1", O_WRONLY | O_TRUNC | O_CREAT, 0644));
    for(i=0;i < FILE_BUF_SIZE; i++)
        buf[i] = i;
    len = chk_error(write(fd, buf, FILE_BUF_SIZE / 2));
    if (len != (FILE_BUF_SIZE / 2))
        error("write");
    vecs[0].iov_base = buf + (FILE_BUF_SIZE / 2);
    vecs[0].iov_len = 16;
    vecs[1].iov_base = buf + (FILE_BUF_SIZE / 2) + 16;
    vecs[1].iov_len = (FILE_BUF_SIZE / 2) - 16;
    len = chk_error(writev(fd, vecs, 2));
    if (len != (FILE_BUF_SIZE / 2))
     error("writev");
    chk_error(close(fd));

    chk_error(rename("file1", "file2"));

    fd = chk_error(open("file2", O_RDONLY));

    len = chk_error(read(fd, buf2, FILE_BUF_SIZE));
    if (len != FILE_BUF_SIZE)
        error("read");
    if (memcmp(buf, buf2, FILE_BUF_SIZE) != 0)
        error("memcmp");

#define FOFFSET 16
    ret = chk_error(lseek(fd, FOFFSET, SEEK_SET));
    if (ret != 16)
        error("lseek");
    vecs[0].iov_base = buf3;
    vecs[0].iov_len = 32;
    vecs[1].iov_base = buf3 + 32;
    vecs[1].iov_len = FILE_BUF_SIZE - FOFFSET - 32;
    len = chk_error(readv(fd, vecs, 2));
    if (len != FILE_BUF_SIZE - FOFFSET)
        error("readv");
    if (memcmp(buf + FOFFSET, buf3, FILE_BUF_SIZE - FOFFSET) != 0)
        error("memcmp");

    chk_error(close(fd));

    /* access */
    chk_error(access("file2", R_OK));

    /* stat/chmod/utime/truncate */

    chk_error(chmod("file2", 0600));
    tbuf.actime = 1001;
    tbuf.modtime = 1000;
    chk_error(truncate("file2", 100));
    chk_error(utime("file2", &tbuf));
    chk_error(stat("file2", &st));
    if (st.st_size != 100)
        error("stat size");
    if (!S_ISREG(st.st_mode))
        error("stat mode");
    if ((st.st_mode & 0777) != 0600)
        error("stat mode2");
    if (st.st_atime != 1001 ||
        st.st_mtime != 1000)
        error("stat time");

    chk_error(stat(tmpdir, &st));
    if (!S_ISDIR(st.st_mode))
        error("stat mode");

    /* fstat */
    fd = chk_error(open("file2", O_RDWR));
    chk_error(ftruncate(fd, 50));
    chk_error(fstat(fd, &st));
    chk_error(close(fd));

    if (st.st_size != 50)
        error("stat size");
    if (!S_ISREG(st.st_mode))
        error("stat mode");

    /* symlink/lstat */
    chk_error(symlink("file2", "file3"));
    chk_error(lstat("file3", &st));
    if (!S_ISLNK(st.st_mode))
        error("stat mode");

    /* getdents */
    dir = opendir(tmpdir);
    if (!dir)
        error("opendir");
    len = 0;
    for(;;) {
        de = readdir64(dir);
        if (!de)
            break;
        if (strcmp(de->d_name, ".") != 0 &&
            strcmp(de->d_name, "..") != 0 &&
            strcmp(de->d_name, "file2") != 0 &&
            strcmp(de->d_name, "file3") != 0)
            error("readdir");
        len++;
    }
    closedir(dir);
    if (len != 4)
        error("readdir");

    chk_error(unlink("file3"));
    chk_error(unlink("file2"));
    chk_error(chdir(cur_dir));
    chk_error(rmdir(tmpdir));
}

static void test_fork(void)
{
    int pid, status;

    pid = chk_error(fork());
    if (pid == 0) {
        /* child */
        sleep(2);
        exit(2);
    }
    chk_error(waitpid(pid, &status, 0));
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 2)
        error("waitpid status=0x%x", status);
}

static void test_time(void)
{
    struct timeval tv, tv2;
    struct timespec ts, rem;
    struct rusage rusg1, rusg2;
    int ti, i;

    chk_error(gettimeofday(&tv, NULL));
    rem.tv_sec = 1;
    ts.tv_sec = 0;
    ts.tv_nsec = 20 * 1000000;
    chk_error(nanosleep(&ts, &rem));
    if (rem.tv_sec != 1)
        error("nanosleep");
    chk_error(gettimeofday(&tv2, NULL));
    ti = tv2.tv_sec - tv.tv_sec;
    if (ti >= 2)
        error("gettimeofday");

    chk_error(getrusage(RUSAGE_SELF, &rusg1));
    for(i = 0;i < 10000; i++);
    chk_error(getrusage(RUSAGE_SELF, &rusg2));
    if ((rusg2.ru_utime.tv_sec - rusg1.ru_utime.tv_sec) < 0 ||
        (rusg2.ru_stime.tv_sec - rusg1.ru_stime.tv_sec) < 0)
        error("getrusage");
}

static int server_socket(void)
{
    int val, fd;
    struct sockaddr_in sockaddr = {};

    /* server socket */
    fd = chk_error(socket(PF_INET, SOCK_STREAM, 0));

    val = 1;
    chk_error(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)));

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(0); /* choose random ephemeral port) */
    sockaddr.sin_addr.s_addr = 0;
    chk_error(bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)));
    chk_error(listen(fd, 1));
    return fd;

}

static int client_socket(uint16_t port)
{
    int fd;
    struct sockaddr_in sockaddr = {};

    /* server socket */
    fd = chk_error(socket(PF_INET, SOCK_STREAM, 0));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    inet_aton("127.0.0.1", &sockaddr.sin_addr);
    chk_error(connect(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)));
    return fd;
}

static const char socket_msg[] = "hello socket\n";

static void test_socket(void)
{
    int server_fd, client_fd, fd, pid, ret, val;
    struct sockaddr_in sockaddr;
    struct sockaddr_in server_addr;
    socklen_t len, socklen;
    uint16_t server_port;
    char buf[512];

    server_fd = server_socket();
    /* find out what port we got */
    socklen = sizeof(server_addr);
    ret = getsockname(server_fd, (struct sockaddr *)&server_addr, &socklen);
    chk_error(ret);
    server_port = ntohs(server_addr.sin_port);

    /* test a few socket options */
    len = sizeof(val);
    chk_error(getsockopt(server_fd, SOL_SOCKET, SO_TYPE, &val, &len));
    if (val != SOCK_STREAM)
        error("getsockopt");

    pid = chk_error(fork());
    if (pid == 0) {
        client_fd = client_socket(server_port);
        send(client_fd, socket_msg, sizeof(socket_msg), 0);
        close(client_fd);
        exit(0);
    }
    len = sizeof(sockaddr);
    fd = chk_error(accept(server_fd, (struct sockaddr *)&sockaddr, &len));

    ret = chk_error(recv(fd, buf, sizeof(buf), 0));
    if (ret != sizeof(socket_msg))
        error("recv");
    if (memcmp(buf, socket_msg, sizeof(socket_msg)) != 0)
        error("socket_msg");
    chk_error(close(fd));
    chk_error(close(server_fd));
}

#define WCOUNT_MAX 512

static void test_pipe(void)
{
    fd_set rfds, wfds;
    int fds[2], fd_max, ret;
    uint8_t ch;
    int wcount, rcount;

    chk_error(pipe(fds));
    chk_error(fcntl(fds[0], F_SETFL, O_NONBLOCK));
    chk_error(fcntl(fds[1], F_SETFL, O_NONBLOCK));
    wcount = 0;
    rcount = 0;
    for(;;) {
        FD_ZERO(&rfds);
        fd_max = fds[0];
        FD_SET(fds[0], &rfds);

        FD_ZERO(&wfds);
        FD_SET(fds[1], &wfds);
        if (fds[1] > fd_max)
            fd_max = fds[1];

        ret = chk_error(select(fd_max + 1, &rfds, &wfds, NULL, NULL));
        if (ret > 0) {
            if (FD_ISSET(fds[0], &rfds)) {
                chk_error(read(fds[0], &ch, 1));
                rcount++;
                if (rcount >= WCOUNT_MAX)
                    break;
            }
            if (FD_ISSET(fds[1], &wfds)) {
                ch = 'a';
                chk_error(write(fds[1], &ch, 1));
                wcount++;
            }
        }
    }
    chk_error(close(fds[0]));
    chk_error(close(fds[1]));
}

static int thread1_res;
static int thread2_res;

static int thread1_func(void *arg)
{
    int i;
    for(i=0;i<5;i++) {
        thread1_res++;
        usleep(10 * 1000);
    }
    return 0;
}

static int thread2_func(void *arg)
{
    int i;
    for(i=0;i<6;i++) {
        thread2_res++;
        usleep(10 * 1000);
    }
    return 0;
}

static void wait_for_child(pid_t pid)
{
    int status;
    chk_error(waitpid(pid, &status, 0));
}

/* For test_clone we must match the clone flags used by glibc, see
 * CLONE_THREAD_FLAGS in the QEMU source code.
 */
static void test_clone(void)
{
    uint8_t *stack1, *stack2;
    pid_t pid1, pid2;

    stack1 = malloc(STACK_SIZE);
    pid1 = chk_error(clone(thread1_func, stack1 + STACK_SIZE,
                           CLONE_VM | CLONE_FS | CLONE_FILES |
                           CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM,
                            "hello1"));

    stack2 = malloc(STACK_SIZE);
    pid2 = chk_error(clone(thread2_func, stack2 + STACK_SIZE,
                           CLONE_VM | CLONE_FS | CLONE_FILES |
                           CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM,
                           "hello2"));

    wait_for_child(pid1);
    free(stack1);
    wait_for_child(pid2);
    free(stack2);

    if (thread1_res != 5 ||
        thread2_res != 6)
        error("clone");
}

/***********************************/

volatile int alarm_count;
jmp_buf jmp_env;

static void sig_alarm(int sig)
{
    if (sig != SIGALRM)
        error("signal");
    alarm_count++;
}

static void sig_segv(int sig, siginfo_t *info, void *puc)
{
    if (sig != SIGSEGV)
        error("signal");
    longjmp(jmp_env, 1);
}

static void test_signal(void)
{
    struct sigaction act;
    struct itimerval it, oit;

    /* timer test */

    alarm_count = 0;

    act.sa_handler = sig_alarm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    chk_error(sigaction(SIGALRM, &act, NULL));

    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 10 * 1000;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 10 * 1000;
    chk_error(setitimer(ITIMER_REAL, &it, NULL));
    chk_error(getitimer(ITIMER_REAL, &oit));

    while (alarm_count < 5) {
        usleep(10 * 1000);
        getitimer(ITIMER_REAL, &oit);
    }

    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    memset(&oit, 0xff, sizeof(oit));
    chk_error(setitimer(ITIMER_REAL, &it, &oit));

    /* SIGSEGV test */
    act.sa_sigaction = sig_segv;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    chk_error(sigaction(SIGSEGV, &act, NULL));
    if (setjmp(jmp_env) == 0) {
        /*
         * clang requires volatile or it will turn this into a
         * call to abort() instead of forcing a SIGSEGV.
         */
        *(volatile uint8_t *)0 = 0;
    }

    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    chk_error(sigaction(SIGSEGV, &act, NULL));

    if (sigaction(SIGKILL, &act, NULL) == 0) {
        error("sigaction(SIGKILL, &act, NULL) must not succeed");
    }
    if (sigaction(SIGSTOP, &act, NULL) == 0) {
        error("sigaction(SIGSTOP, &act, NULL) must not succeed");
    }
    chk_error(sigaction(SIGKILL, NULL, &act));
    chk_error(sigaction(SIGSTOP, NULL, &act));
}

#define SHM_SIZE 32768

static void test_shm(void)
{
    void *ptr;
    int shmid;

    shmid = chk_error(shmget(IPC_PRIVATE, SHM_SIZE, IPC_CREAT | 0777));
    ptr = shmat(shmid, NULL, 0);
    if (ptr == (void *)-1) {
        error("shmat");
    }

    memset(ptr, 0, SHM_SIZE);

    chk_error(shmctl(shmid, IPC_RMID, 0));
    chk_error(shmdt(ptr));
}

int main(int argc, char **argv)
{
    test_file();
    test_pipe();
    test_fork();
    test_time();
    test_socket();

    if (argc > 1) {
        printf("test_clone still considered buggy\n");
        test_clone();
    }

    test_signal();
    test_shm();
    return 0;
}
