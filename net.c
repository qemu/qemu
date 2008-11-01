/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/usb.h"
#include "hw/pcmcia.h"
#include "hw/pc.h"
#include "hw/audiodev.h"
#include "hw/isa.h"
#include "hw/baum.h"
#include "hw/bt.h"
#include "net.h"
#include "console.h"
#include "sysemu.h"
#include "gdbstub.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "block.h"
#include "audio/audio.h"
#include "migration.h"

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <zlib.h>

#ifndef _WIN32
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/select.h>
#include <arpa/inet.h>
#ifdef _BSD
#include <sys/stat.h>
#if !defined(__APPLE__) && !defined(__OpenBSD__)
#include <libutil.h>
#endif
#ifdef __OpenBSD__
#include <net/if.h>
#endif
#elif defined (__GLIBC__) && defined (__FreeBSD_kernel__)
#include <freebsd/stdlib.h>
#else
#ifdef __linux__
#include <linux/if.h>
#include <linux/if_tun.h>
#include <pty.h>
#include <malloc.h>
#include <linux/rtc.h>

/* For the benefit of older linux systems which don't supply it,
   we use a local copy of hpet.h. */
/* #include <linux/hpet.h> */
#include "hpet.h"

#include <linux/ppdev.h>
#include <linux/parport.h>
#endif
#ifdef __sun__
#include <sys/stat.h>
#include <sys/ethernet.h>
#include <sys/sockio.h>
#include <netinet/arp.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h> // must come after ip.h
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <syslog.h>
#include <stropts.h>
#endif
#endif
#endif

#include "qemu_socket.h"

#if defined(CONFIG_SLIRP)
#include "libslirp.h"
#endif

#if defined(__OpenBSD__)
#include <util.h>
#endif

#if defined(CONFIG_VDE)
#include <libvdeplug.h>
#endif

#ifdef _WIN32
#include <malloc.h>
#include <sys/timeb.h>
#include <mmsystem.h>
#define getopt_long_only getopt_long
#define memalign(align, size) malloc(size)
#endif

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"
#ifdef __sun__
#define SMBD_COMMAND "/usr/sfw/sbin/smbd"
#else
#define SMBD_COMMAND "/usr/sbin/smbd"
#endif

static VLANState *first_vlan;

/***********************************************************/
/* network device redirectors */

#if defined(DEBUG_NET) || defined(DEBUG_SLIRP)
static void hex_dump(FILE *f, const uint8_t *buf, int size)
{
    int len, i, j, c;

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        fprintf(f, "%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                fprintf(f, " %02x", buf[i+j]);
            else
                fprintf(f, "   ");
        }
        fprintf(f, " ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            fprintf(f, "%c", c);
        }
        fprintf(f, "\n");
    }
}
#endif

static int parse_macaddr(uint8_t *macaddr, const char *p)
{
    int i;
    char *last_char;
    long int offset;

    errno = 0;
    offset = strtol(p, &last_char, 0);    
    if (0 == errno && '\0' == *last_char &&
            offset >= 0 && offset <= 0xFFFFFF) {
        macaddr[3] = (offset & 0xFF0000) >> 16;
        macaddr[4] = (offset & 0xFF00) >> 8;
        macaddr[5] = offset & 0xFF;
        return 0;
    } else {
        for(i = 0; i < 6; i++) {
            macaddr[i] = strtol(p, (char **)&p, 16);
            if (i == 5) {
                if (*p != '\0')
                    return -1;
            } else {
                if (*p != ':' && *p != '-')
                    return -1;
                p++;
            }
        }
        return 0;    
    }

    return -1;
}

static int get_str_sep(char *buf, int buf_size, const char **pp, int sep)
{
    const char *p, *p1;
    int len;
    p = *pp;
    p1 = strchr(p, sep);
    if (!p1)
        return -1;
    len = p1 - p;
    p1++;
    if (buf_size > 0) {
        if (len > buf_size - 1)
            len = buf_size - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
    }
    *pp = p1;
    return 0;
}

int parse_host_src_port(struct sockaddr_in *haddr,
                        struct sockaddr_in *saddr,
                        const char *input_str)
{
    char *str = strdup(input_str);
    char *host_str = str;
    char *src_str;
    const char *src_str2;
    char *ptr;

    /*
     * Chop off any extra arguments at the end of the string which
     * would start with a comma, then fill in the src port information
     * if it was provided else use the "any address" and "any port".
     */
    if ((ptr = strchr(str,',')))
        *ptr = '\0';

    if ((src_str = strchr(input_str,'@'))) {
        *src_str = '\0';
        src_str++;
    }

    if (parse_host_port(haddr, host_str) < 0)
        goto fail;

    src_str2 = src_str;
    if (!src_str || *src_str == '\0')
        src_str2 = ":0";

    if (parse_host_port(saddr, src_str2) < 0)
        goto fail;

    free(str);
    return(0);

fail:
    free(str);
    return -1;
}

int parse_host_port(struct sockaddr_in *saddr, const char *str)
{
    char buf[512];
    struct hostent *he;
    const char *p, *r;
    int port;

    p = str;
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        return -1;
    saddr->sin_family = AF_INET;
    if (buf[0] == '\0') {
        saddr->sin_addr.s_addr = 0;
    } else {
        if (isdigit(buf[0])) {
            if (!inet_aton(buf, &saddr->sin_addr))
                return -1;
        } else {
            if ((he = gethostbyname(buf)) == NULL)
                return - 1;
            saddr->sin_addr = *(struct in_addr *)he->h_addr;
        }
    }
    port = strtol(p, (char **)&r, 0);
    if (r == p)
        return -1;
    saddr->sin_port = htons(port);
    return 0;
}

#ifndef _WIN32
int parse_unix_path(struct sockaddr_un *uaddr, const char *str)
{
    const char *p;
    int len;

    len = MIN(108, strlen(str));
    p = strchr(str, ',');
    if (p)
	len = MIN(len, p - str);

    memset(uaddr, 0, sizeof(*uaddr));

    uaddr->sun_family = AF_UNIX;
    memcpy(uaddr->sun_path, str, len);

    return 0;
}
#endif

VLANClientState *qemu_new_vlan_client(VLANState *vlan,
                                      IOReadHandler *fd_read,
                                      IOCanRWHandler *fd_can_read,
                                      void *opaque)
{
    VLANClientState *vc, **pvc;
    vc = qemu_mallocz(sizeof(VLANClientState));
    if (!vc)
        return NULL;
    vc->fd_read = fd_read;
    vc->fd_can_read = fd_can_read;
    vc->opaque = opaque;
    vc->vlan = vlan;

    vc->next = NULL;
    pvc = &vlan->first_client;
    while (*pvc != NULL)
        pvc = &(*pvc)->next;
    *pvc = vc;
    return vc;
}

void qemu_del_vlan_client(VLANClientState *vc)
{
    VLANClientState **pvc = &vc->vlan->first_client;

    while (*pvc != NULL)
        if (*pvc == vc) {
            *pvc = vc->next;
            free(vc);
            break;
        } else
            pvc = &(*pvc)->next;
}

int qemu_can_send_packet(VLANClientState *vc1)
{
    VLANState *vlan = vc1->vlan;
    VLANClientState *vc;

    for(vc = vlan->first_client; vc != NULL; vc = vc->next) {
        if (vc != vc1) {
            if (vc->fd_can_read && vc->fd_can_read(vc->opaque))
                return 1;
        }
    }
    return 0;
}

void qemu_send_packet(VLANClientState *vc1, const uint8_t *buf, int size)
{
    VLANState *vlan = vc1->vlan;
    VLANClientState *vc;

#ifdef DEBUG_NET
    printf("vlan %d send:\n", vlan->id);
    hex_dump(stdout, buf, size);
#endif
    for(vc = vlan->first_client; vc != NULL; vc = vc->next) {
        if (vc != vc1) {
            vc->fd_read(vc->opaque, buf, size);
        }
    }
}

#if defined(CONFIG_SLIRP)

/* slirp network adapter */

static int slirp_inited;
static VLANClientState *slirp_vc;

int slirp_can_output(void)
{
    return !slirp_vc || qemu_can_send_packet(slirp_vc);
}

void slirp_output(const uint8_t *pkt, int pkt_len)
{
#ifdef DEBUG_SLIRP
    printf("slirp output:\n");
    hex_dump(stdout, pkt, pkt_len);
#endif
    if (!slirp_vc)
        return;
    qemu_send_packet(slirp_vc, pkt, pkt_len);
}

int slirp_is_inited(void)
{
    return slirp_inited;
}

static void slirp_receive(void *opaque, const uint8_t *buf, int size)
{
#ifdef DEBUG_SLIRP
    printf("slirp input:\n");
    hex_dump(stdout, buf, size);
#endif
    slirp_input(buf, size);
}

static int net_slirp_init(VLANState *vlan)
{
    if (!slirp_inited) {
        slirp_inited = 1;
        slirp_init();
    }
    slirp_vc = qemu_new_vlan_client(vlan,
                                    slirp_receive, NULL, NULL);
    snprintf(slirp_vc->info_str, sizeof(slirp_vc->info_str), "user redirector");
    return 0;
}

void net_slirp_redir(const char *redir_str)
{
    int is_udp;
    char buf[256], *r;
    const char *p;
    struct in_addr guest_addr;
    int host_port, guest_port;

    if (!slirp_inited) {
        slirp_inited = 1;
        slirp_init();
    }

    p = redir_str;
    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        goto fail;
    if (!strcmp(buf, "tcp")) {
        is_udp = 0;
    } else if (!strcmp(buf, "udp")) {
        is_udp = 1;
    } else {
        goto fail;
    }

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        goto fail;
    host_port = strtol(buf, &r, 0);
    if (r == buf)
        goto fail;

    if (get_str_sep(buf, sizeof(buf), &p, ':') < 0)
        goto fail;
    if (buf[0] == '\0') {
        pstrcpy(buf, sizeof(buf), "10.0.2.15");
    }
    if (!inet_aton(buf, &guest_addr))
        goto fail;

    guest_port = strtol(p, &r, 0);
    if (r == p)
        goto fail;

    if (slirp_redir(is_udp, host_port, guest_addr, guest_port) < 0) {
        fprintf(stderr, "qemu: could not set up redirection\n");
        exit(1);
    }
    return;
 fail:
    fprintf(stderr, "qemu: syntax: -redir [tcp|udp]:host-port:[guest-host]:guest-port\n");
    exit(1);
}

#ifndef _WIN32

static char smb_dir[1024];

static void erase_dir(char *dir_name)
{
    DIR *d;
    struct dirent *de;
    char filename[1024];

    /* erase all the files in the directory */
    if ((d = opendir(dir_name)) != 0) {
        for(;;) {
            de = readdir(d);
            if (!de)
                break;
            if (strcmp(de->d_name, ".") != 0 &&
                strcmp(de->d_name, "..") != 0) {
                snprintf(filename, sizeof(filename), "%s/%s",
                         smb_dir, de->d_name);
                if (unlink(filename) != 0)  /* is it a directory? */
                    erase_dir(filename);
            }
        }
        closedir(d);
        rmdir(dir_name);
    }
}

/* automatic user mode samba server configuration */
static void smb_exit(void)
{
    erase_dir(smb_dir);
}

/* automatic user mode samba server configuration */
void net_slirp_smb(const char *exported_dir)
{
    char smb_conf[1024];
    char smb_cmdline[1024];
    FILE *f;

    if (!slirp_inited) {
        slirp_inited = 1;
        slirp_init();
    }

    /* XXX: better tmp dir construction */
    snprintf(smb_dir, sizeof(smb_dir), "/tmp/qemu-smb.%d", getpid());
    if (mkdir(smb_dir, 0700) < 0) {
        fprintf(stderr, "qemu: could not create samba server dir '%s'\n", smb_dir);
        exit(1);
    }
    snprintf(smb_conf, sizeof(smb_conf), "%s/%s", smb_dir, "smb.conf");

    f = fopen(smb_conf, "w");
    if (!f) {
        fprintf(stderr, "qemu: could not create samba server configuration file '%s'\n", smb_conf);
        exit(1);
    }
    fprintf(f,
            "[global]\n"
            "private dir=%s\n"
            "smb ports=0\n"
            "socket address=127.0.0.1\n"
            "pid directory=%s\n"
            "lock directory=%s\n"
            "log file=%s/log.smbd\n"
            "smb passwd file=%s/smbpasswd\n"
            "security = share\n"
            "[qemu]\n"
            "path=%s\n"
            "read only=no\n"
            "guest ok=yes\n",
            smb_dir,
            smb_dir,
            smb_dir,
            smb_dir,
            smb_dir,
            exported_dir
            );
    fclose(f);
    atexit(smb_exit);

    snprintf(smb_cmdline, sizeof(smb_cmdline), "%s -s %s",
             SMBD_COMMAND, smb_conf);

    slirp_add_exec(0, smb_cmdline, 4, 139);
}

#endif /* !defined(_WIN32) */
void do_info_slirp(void)
{
    slirp_stats();
}

#endif /* CONFIG_SLIRP */

#if !defined(_WIN32)

typedef struct TAPState {
    VLANClientState *vc;
    int fd;
    char down_script[1024];
} TAPState;

static void tap_receive(void *opaque, const uint8_t *buf, int size)
{
    TAPState *s = opaque;
    int ret;
    for(;;) {
        ret = write(s->fd, buf, size);
        if (ret < 0 && (errno == EINTR || errno == EAGAIN)) {
        } else {
            break;
        }
    }
}

static void tap_send(void *opaque)
{
    TAPState *s = opaque;
    uint8_t buf[4096];
    int size;

#ifdef __sun__
    struct strbuf sbuf;
    int f = 0;
    sbuf.maxlen = sizeof(buf);
    sbuf.buf = buf;
    size = getmsg(s->fd, NULL, &sbuf, &f) >=0 ? sbuf.len : -1;
#else
    size = read(s->fd, buf, sizeof(buf));
#endif
    if (size > 0) {
        qemu_send_packet(s->vc, buf, size);
    }
}

/* fd support */

static TAPState *net_tap_fd_init(VLANState *vlan, int fd)
{
    TAPState *s;

    s = qemu_mallocz(sizeof(TAPState));
    if (!s)
        return NULL;
    s->fd = fd;
    s->vc = qemu_new_vlan_client(vlan, tap_receive, NULL, s);
    qemu_set_fd_handler(s->fd, tap_send, NULL, s);
    snprintf(s->vc->info_str, sizeof(s->vc->info_str), "tap: fd=%d", fd);
    return s;
}

#if defined (_BSD) || defined (__FreeBSD_kernel__)
static int tap_open(char *ifname, int ifname_size)
{
    int fd;
    char *dev;
    struct stat s;

    TFR(fd = open("/dev/tap", O_RDWR));
    if (fd < 0) {
        fprintf(stderr, "warning: could not open /dev/tap: no virtual network emulation\n");
        return -1;
    }

    fstat(fd, &s);
    dev = devname(s.st_rdev, S_IFCHR);
    pstrcpy(ifname, ifname_size, dev);

    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
#elif defined(__sun__)
#define TUNNEWPPA       (('T'<<16) | 0x0001)
/*
 * Allocate TAP device, returns opened fd.
 * Stores dev name in the first arg(must be large enough).
 */
int tap_alloc(char *dev, size_t dev_size)
{
    int tap_fd, if_fd, ppa = -1;
    static int ip_fd = 0;
    char *ptr;

    static int arp_fd = 0;
    int ip_muxid, arp_muxid;
    struct strioctl  strioc_if, strioc_ppa;
    int link_type = I_PLINK;;
    struct lifreq ifr;
    char actual_name[32] = "";

    memset(&ifr, 0x0, sizeof(ifr));

    if( *dev ){
       ptr = dev;
       while( *ptr && !isdigit((int)*ptr) ) ptr++;
       ppa = atoi(ptr);
    }

    /* Check if IP device was opened */
    if( ip_fd )
       close(ip_fd);

    TFR(ip_fd = open("/dev/udp", O_RDWR, 0));
    if (ip_fd < 0) {
       syslog(LOG_ERR, "Can't open /dev/ip (actually /dev/udp)");
       return -1;
    }

    TFR(tap_fd = open("/dev/tap", O_RDWR, 0));
    if (tap_fd < 0) {
       syslog(LOG_ERR, "Can't open /dev/tap");
       return -1;
    }

    /* Assign a new PPA and get its unit number. */
    strioc_ppa.ic_cmd = TUNNEWPPA;
    strioc_ppa.ic_timout = 0;
    strioc_ppa.ic_len = sizeof(ppa);
    strioc_ppa.ic_dp = (char *)&ppa;
    if ((ppa = ioctl (tap_fd, I_STR, &strioc_ppa)) < 0)
       syslog (LOG_ERR, "Can't assign new interface");

    TFR(if_fd = open("/dev/tap", O_RDWR, 0));
    if (if_fd < 0) {
       syslog(LOG_ERR, "Can't open /dev/tap (2)");
       return -1;
    }
    if(ioctl(if_fd, I_PUSH, "ip") < 0){
       syslog(LOG_ERR, "Can't push IP module");
       return -1;
    }

    if (ioctl(if_fd, SIOCGLIFFLAGS, &ifr) < 0)
	syslog(LOG_ERR, "Can't get flags\n");

    snprintf (actual_name, 32, "tap%d", ppa);
    pstrcpy(ifr.lifr_name, sizeof(ifr.lifr_name), actual_name);

    ifr.lifr_ppa = ppa;
    /* Assign ppa according to the unit number returned by tun device */

    if (ioctl (if_fd, SIOCSLIFNAME, &ifr) < 0)
        syslog (LOG_ERR, "Can't set PPA %d", ppa);
    if (ioctl(if_fd, SIOCGLIFFLAGS, &ifr) <0)
        syslog (LOG_ERR, "Can't get flags\n");
    /* Push arp module to if_fd */
    if (ioctl (if_fd, I_PUSH, "arp") < 0)
        syslog (LOG_ERR, "Can't push ARP module (2)");

    /* Push arp module to ip_fd */
    if (ioctl (ip_fd, I_POP, NULL) < 0)
        syslog (LOG_ERR, "I_POP failed\n");
    if (ioctl (ip_fd, I_PUSH, "arp") < 0)
        syslog (LOG_ERR, "Can't push ARP module (3)\n");
    /* Open arp_fd */
    TFR(arp_fd = open ("/dev/tap", O_RDWR, 0));
    if (arp_fd < 0)
       syslog (LOG_ERR, "Can't open %s\n", "/dev/tap");

    /* Set ifname to arp */
    strioc_if.ic_cmd = SIOCSLIFNAME;
    strioc_if.ic_timout = 0;
    strioc_if.ic_len = sizeof(ifr);
    strioc_if.ic_dp = (char *)&ifr;
    if (ioctl(arp_fd, I_STR, &strioc_if) < 0){
        syslog (LOG_ERR, "Can't set ifname to arp\n");
    }

    if((ip_muxid = ioctl(ip_fd, I_LINK, if_fd)) < 0){
       syslog(LOG_ERR, "Can't link TAP device to IP");
       return -1;
    }

    if ((arp_muxid = ioctl (ip_fd, link_type, arp_fd)) < 0)
        syslog (LOG_ERR, "Can't link TAP device to ARP");

    close (if_fd);

    memset(&ifr, 0x0, sizeof(ifr));
    pstrcpy(ifr.lifr_name, sizeof(ifr.lifr_name), actual_name);
    ifr.lifr_ip_muxid  = ip_muxid;
    ifr.lifr_arp_muxid = arp_muxid;

    if (ioctl (ip_fd, SIOCSLIFMUXID, &ifr) < 0)
    {
      ioctl (ip_fd, I_PUNLINK , arp_muxid);
      ioctl (ip_fd, I_PUNLINK, ip_muxid);
      syslog (LOG_ERR, "Can't set multiplexor id");
    }

    snprintf(dev, dev_size, "tap%d", ppa);
    return tap_fd;
}

static int tap_open(char *ifname, int ifname_size)
{
    char  dev[10]="";
    int fd;
    if( (fd = tap_alloc(dev, sizeof(dev))) < 0 ){
       fprintf(stderr, "Cannot allocate TAP device\n");
       return -1;
    }
    pstrcpy(ifname, ifname_size, dev);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
#else
static int tap_open(char *ifname, int ifname_size)
{
    struct ifreq ifr;
    int fd, ret;

    TFR(fd = open("/dev/net/tun", O_RDWR));
    if (fd < 0) {
        fprintf(stderr, "warning: could not open /dev/net/tun: no virtual network emulation\n");
        return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (ifname[0] != '\0')
        pstrcpy(ifr.ifr_name, IFNAMSIZ, ifname);
    else
        pstrcpy(ifr.ifr_name, IFNAMSIZ, "tap%d");
    ret = ioctl(fd, TUNSETIFF, (void *) &ifr);
    if (ret != 0) {
        fprintf(stderr, "warning: could not configure /dev/net/tun: no virtual network emulation\n");
        close(fd);
        return -1;
    }
    pstrcpy(ifname, ifname_size, ifr.ifr_name);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
#endif

static int launch_script(const char *setup_script, const char *ifname, int fd)
{
    int pid, status;
    char *args[3];
    char **parg;

        /* try to launch network script */
        pid = fork();
        if (pid >= 0) {
            if (pid == 0) {
                int open_max = sysconf (_SC_OPEN_MAX), i;
                for (i = 0; i < open_max; i++)
                    if (i != STDIN_FILENO &&
                        i != STDOUT_FILENO &&
                        i != STDERR_FILENO &&
                        i != fd)
                        close(i);

                parg = args;
                *parg++ = (char *)setup_script;
                *parg++ = (char *)ifname;
                *parg++ = NULL;
                execv(setup_script, args);
                _exit(1);
            }
            while (waitpid(pid, &status, 0) != pid);
            if (!WIFEXITED(status) ||
                WEXITSTATUS(status) != 0) {
                fprintf(stderr, "%s: could not launch network script\n",
                        setup_script);
                return -1;
            }
        }
    return 0;
}

static int net_tap_init(VLANState *vlan, const char *ifname1,
                        const char *setup_script, const char *down_script)
{
    TAPState *s;
    int fd;
    char ifname[128];

    if (ifname1 != NULL)
        pstrcpy(ifname, sizeof(ifname), ifname1);
    else
        ifname[0] = '\0';
    TFR(fd = tap_open(ifname, sizeof(ifname)));
    if (fd < 0)
        return -1;

    if (!setup_script || !strcmp(setup_script, "no"))
        setup_script = "";
    if (setup_script[0] != '\0') {
	if (launch_script(setup_script, ifname, fd))
	    return -1;
    }
    s = net_tap_fd_init(vlan, fd);
    if (!s)
        return -1;
    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "tap: ifname=%s setup_script=%s", ifname, setup_script);
    if (down_script && strcmp(down_script, "no"))
        snprintf(s->down_script, sizeof(s->down_script), "%s", down_script);
    return 0;
}

#endif /* !_WIN32 */

#if defined(CONFIG_VDE)
typedef struct VDEState {
    VLANClientState *vc;
    VDECONN *vde;
} VDEState;

static void vde_to_qemu(void *opaque)
{
    VDEState *s = opaque;
    uint8_t buf[4096];
    int size;

    size = vde_recv(s->vde, buf, sizeof(buf), 0);
    if (size > 0) {
        qemu_send_packet(s->vc, buf, size);
    }
}

static void vde_from_qemu(void *opaque, const uint8_t *buf, int size)
{
    VDEState *s = opaque;
    int ret;
    for(;;) {
        ret = vde_send(s->vde, buf, size, 0);
        if (ret < 0 && errno == EINTR) {
        } else {
            break;
        }
    }
}

static int net_vde_init(VLANState *vlan, const char *sock, int port,
                        const char *group, int mode)
{
    VDEState *s;
    char *init_group = strlen(group) ? (char *)group : NULL;
    char *init_sock = strlen(sock) ? (char *)sock : NULL;

    struct vde_open_args args = {
        .port = port,
        .group = init_group,
        .mode = mode,
    };

    s = qemu_mallocz(sizeof(VDEState));
    if (!s)
        return -1;
    s->vde = vde_open(init_sock, "QEMU", &args);
    if (!s->vde){
        free(s);
        return -1;
    }
    s->vc = qemu_new_vlan_client(vlan, vde_from_qemu, NULL, s);
    qemu_set_fd_handler(vde_datafd(s->vde), vde_to_qemu, NULL, s);
    snprintf(s->vc->info_str, sizeof(s->vc->info_str), "vde: sock=%s fd=%d",
             sock, vde_datafd(s->vde));
    return 0;
}
#endif

/* network connection */
typedef struct NetSocketState {
    VLANClientState *vc;
    int fd;
    int state; /* 0 = getting length, 1 = getting data */
    int index;
    int packet_len;
    uint8_t buf[4096];
    struct sockaddr_in dgram_dst; /* contains inet host and port destination iff connectionless (SOCK_DGRAM) */
} NetSocketState;

typedef struct NetSocketListenState {
    VLANState *vlan;
    int fd;
} NetSocketListenState;

/* XXX: we consider we can send the whole packet without blocking */
static void net_socket_receive(void *opaque, const uint8_t *buf, int size)
{
    NetSocketState *s = opaque;
    uint32_t len;
    len = htonl(size);

    send_all(s->fd, (const uint8_t *)&len, sizeof(len));
    send_all(s->fd, buf, size);
}

static void net_socket_receive_dgram(void *opaque, const uint8_t *buf, int size)
{
    NetSocketState *s = opaque;
    sendto(s->fd, buf, size, 0,
           (struct sockaddr *)&s->dgram_dst, sizeof(s->dgram_dst));
}

static void net_socket_send(void *opaque)
{
    NetSocketState *s = opaque;
    int l, size, err;
    uint8_t buf1[4096];
    const uint8_t *buf;

    size = recv(s->fd, buf1, sizeof(buf1), 0);
    if (size < 0) {
        err = socket_error();
        if (err != EWOULDBLOCK)
            goto eoc;
    } else if (size == 0) {
        /* end of connection */
    eoc:
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        closesocket(s->fd);
        return;
    }
    buf = buf1;
    while (size > 0) {
        /* reassemble a packet from the network */
        switch(s->state) {
        case 0:
            l = 4 - s->index;
            if (l > size)
                l = size;
            memcpy(s->buf + s->index, buf, l);
            buf += l;
            size -= l;
            s->index += l;
            if (s->index == 4) {
                /* got length */
                s->packet_len = ntohl(*(uint32_t *)s->buf);
                s->index = 0;
                s->state = 1;
            }
            break;
        case 1:
            l = s->packet_len - s->index;
            if (l > size)
                l = size;
            memcpy(s->buf + s->index, buf, l);
            s->index += l;
            buf += l;
            size -= l;
            if (s->index >= s->packet_len) {
                qemu_send_packet(s->vc, s->buf, s->packet_len);
                s->index = 0;
                s->state = 0;
            }
            break;
        }
    }
}

static void net_socket_send_dgram(void *opaque)
{
    NetSocketState *s = opaque;
    int size;

    size = recv(s->fd, s->buf, sizeof(s->buf), 0);
    if (size < 0)
        return;
    if (size == 0) {
        /* end of connection */
        qemu_set_fd_handler(s->fd, NULL, NULL, NULL);
        return;
    }
    qemu_send_packet(s->vc, s->buf, size);
}

static int net_socket_mcast_create(struct sockaddr_in *mcastaddr)
{
    struct ip_mreq imr;
    int fd;
    int val, ret;
    if (!IN_MULTICAST(ntohl(mcastaddr->sin_addr.s_addr))) {
	fprintf(stderr, "qemu: error: specified mcastaddr \"%s\" (0x%08x) does not contain a multicast address\n",
		inet_ntoa(mcastaddr->sin_addr),
                (int)ntohl(mcastaddr->sin_addr.s_addr));
	return -1;

    }
    fd = socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket(PF_INET, SOCK_DGRAM)");
        return -1;
    }

    val = 1;
    ret=setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&val, sizeof(val));
    if (ret < 0) {
	perror("setsockopt(SOL_SOCKET, SO_REUSEADDR)");
	goto fail;
    }

    ret = bind(fd, (struct sockaddr *)mcastaddr, sizeof(*mcastaddr));
    if (ret < 0) {
        perror("bind");
        goto fail;
    }

    /* Add host to multicast group */
    imr.imr_multiaddr = mcastaddr->sin_addr;
    imr.imr_interface.s_addr = htonl(INADDR_ANY);

    ret = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     (const char *)&imr, sizeof(struct ip_mreq));
    if (ret < 0) {
	perror("setsockopt(IP_ADD_MEMBERSHIP)");
	goto fail;
    }

    /* Force mcast msgs to loopback (eg. several QEMUs in same host */
    val = 1;
    ret=setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                   (const char *)&val, sizeof(val));
    if (ret < 0) {
	perror("setsockopt(SOL_IP, IP_MULTICAST_LOOP)");
	goto fail;
    }

    socket_set_nonblock(fd);
    return fd;
fail:
    if (fd >= 0)
        closesocket(fd);
    return -1;
}

static NetSocketState *net_socket_fd_init_dgram(VLANState *vlan, int fd,
                                          int is_connected)
{
    struct sockaddr_in saddr;
    int newfd;
    socklen_t saddr_len;
    NetSocketState *s;

    /* fd passed: multicast: "learn" dgram_dst address from bound address and save it
     * Because this may be "shared" socket from a "master" process, datagrams would be recv()
     * by ONLY ONE process: we must "clone" this dgram socket --jjo
     */

    if (is_connected) {
	if (getsockname(fd, (struct sockaddr *) &saddr, &saddr_len) == 0) {
	    /* must be bound */
	    if (saddr.sin_addr.s_addr==0) {
		fprintf(stderr, "qemu: error: init_dgram: fd=%d unbound, cannot setup multicast dst addr\n",
			fd);
		return NULL;
	    }
	    /* clone dgram socket */
	    newfd = net_socket_mcast_create(&saddr);
	    if (newfd < 0) {
		/* error already reported by net_socket_mcast_create() */
		close(fd);
		return NULL;
	    }
	    /* clone newfd to fd, close newfd */
	    dup2(newfd, fd);
	    close(newfd);

	} else {
	    fprintf(stderr, "qemu: error: init_dgram: fd=%d failed getsockname(): %s\n",
		    fd, strerror(errno));
	    return NULL;
	}
    }

    s = qemu_mallocz(sizeof(NetSocketState));
    if (!s)
        return NULL;
    s->fd = fd;

    s->vc = qemu_new_vlan_client(vlan, net_socket_receive_dgram, NULL, s);
    qemu_set_fd_handler(s->fd, net_socket_send_dgram, NULL, s);

    /* mcast: save bound address as dst */
    if (is_connected) s->dgram_dst=saddr;

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
	    "socket: fd=%d (%s mcast=%s:%d)",
	    fd, is_connected? "cloned" : "",
	    inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return s;
}

static void net_socket_connect(void *opaque)
{
    NetSocketState *s = opaque;
    qemu_set_fd_handler(s->fd, net_socket_send, NULL, s);
}

static NetSocketState *net_socket_fd_init_stream(VLANState *vlan, int fd,
                                          int is_connected)
{
    NetSocketState *s;
    s = qemu_mallocz(sizeof(NetSocketState));
    if (!s)
        return NULL;
    s->fd = fd;
    s->vc = qemu_new_vlan_client(vlan,
                                 net_socket_receive, NULL, s);
    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "socket: fd=%d", fd);
    if (is_connected) {
        net_socket_connect(s);
    } else {
        qemu_set_fd_handler(s->fd, NULL, net_socket_connect, s);
    }
    return s;
}

static NetSocketState *net_socket_fd_init(VLANState *vlan, int fd,
                                          int is_connected)
{
    int so_type=-1, optlen=sizeof(so_type);

    if(getsockopt(fd, SOL_SOCKET, SO_TYPE, (char *)&so_type,
        (socklen_t *)&optlen)< 0) {
	fprintf(stderr, "qemu: error: getsockopt(SO_TYPE) for fd=%d failed\n", fd);
	return NULL;
    }
    switch(so_type) {
    case SOCK_DGRAM:
        return net_socket_fd_init_dgram(vlan, fd, is_connected);
    case SOCK_STREAM:
        return net_socket_fd_init_stream(vlan, fd, is_connected);
    default:
        /* who knows ... this could be a eg. a pty, do warn and continue as stream */
        fprintf(stderr, "qemu: warning: socket type=%d for fd=%d is not SOCK_DGRAM or SOCK_STREAM\n", so_type, fd);
        return net_socket_fd_init_stream(vlan, fd, is_connected);
    }
    return NULL;
}

static void net_socket_accept(void *opaque)
{
    NetSocketListenState *s = opaque;
    NetSocketState *s1;
    struct sockaddr_in saddr;
    socklen_t len;
    int fd;

    for(;;) {
        len = sizeof(saddr);
        fd = accept(s->fd, (struct sockaddr *)&saddr, &len);
        if (fd < 0 && errno != EINTR) {
            return;
        } else if (fd >= 0) {
            break;
        }
    }
    s1 = net_socket_fd_init(s->vlan, fd, 1);
    if (!s1) {
        closesocket(fd);
    } else {
        snprintf(s1->vc->info_str, sizeof(s1->vc->info_str),
                 "socket: connection from %s:%d",
                 inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    }
}

static int net_socket_listen_init(VLANState *vlan, const char *host_str)
{
    NetSocketListenState *s;
    int fd, val, ret;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;

    s = qemu_mallocz(sizeof(NetSocketListenState));
    if (!s)
        return -1;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    socket_set_nonblock(fd);

    /* allow fast reuse */
    val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

    ret = bind(fd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (ret < 0) {
        perror("bind");
        return -1;
    }
    ret = listen(fd, 0);
    if (ret < 0) {
        perror("listen");
        return -1;
    }
    s->vlan = vlan;
    s->fd = fd;
    qemu_set_fd_handler(fd, net_socket_accept, NULL, s);
    return 0;
}

static int net_socket_connect_init(VLANState *vlan, const char *host_str)
{
    NetSocketState *s;
    int fd, connected, ret, err;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    socket_set_nonblock(fd);

    connected = 0;
    for(;;) {
        ret = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
        if (ret < 0) {
            err = socket_error();
            if (err == EINTR || err == EWOULDBLOCK) {
            } else if (err == EINPROGRESS) {
                break;
#ifdef _WIN32
            } else if (err == WSAEALREADY) {
                break;
#endif
            } else {
                perror("connect");
                closesocket(fd);
                return -1;
            }
        } else {
            connected = 1;
            break;
        }
    }
    s = net_socket_fd_init(vlan, fd, connected);
    if (!s)
        return -1;
    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "socket: connect to %s:%d",
             inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return 0;
}

static int net_socket_mcast_init(VLANState *vlan, const char *host_str)
{
    NetSocketState *s;
    int fd;
    struct sockaddr_in saddr;

    if (parse_host_port(&saddr, host_str) < 0)
        return -1;


    fd = net_socket_mcast_create(&saddr);
    if (fd < 0)
	return -1;

    s = net_socket_fd_init(vlan, fd, 0);
    if (!s)
        return -1;

    s->dgram_dst = saddr;

    snprintf(s->vc->info_str, sizeof(s->vc->info_str),
             "socket: mcast=%s:%d",
             inet_ntoa(saddr.sin_addr), ntohs(saddr.sin_port));
    return 0;

}

/* find or alloc a new VLAN */
VLANState *qemu_find_vlan(int id)
{
    VLANState **pvlan, *vlan;
    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        if (vlan->id == id)
            return vlan;
    }
    vlan = qemu_mallocz(sizeof(VLANState));
    if (!vlan)
        return NULL;
    vlan->id = id;
    vlan->next = NULL;
    pvlan = &first_vlan;
    while (*pvlan != NULL)
        pvlan = &(*pvlan)->next;
    *pvlan = vlan;
    return vlan;
}

int net_client_init(const char *device, const char *p)
{
    char buf[1024];
    int vlan_id, ret;
    VLANState *vlan;

    vlan_id = 0;
    if (get_param_value(buf, sizeof(buf), "vlan", p)) {
        vlan_id = strtol(buf, NULL, 0);
    }
    vlan = qemu_find_vlan(vlan_id);
    if (!vlan) {
        fprintf(stderr, "Could not create vlan %d\n", vlan_id);
        return -1;
    }
    if (!strcmp(device, "nic")) {
        NICInfo *nd;
        uint8_t *macaddr;

        if (nb_nics >= MAX_NICS) {
            fprintf(stderr, "Too Many NICs\n");
            return -1;
        }
        nd = &nd_table[nb_nics];
        macaddr = nd->macaddr;
        macaddr[0] = 0x52;
        macaddr[1] = 0x54;
        macaddr[2] = 0x00;
        macaddr[3] = 0x12;
        macaddr[4] = 0x34;
        macaddr[5] = 0x56 + nb_nics;

        if (get_param_value(buf, sizeof(buf), "macaddr", p)) {
            if (parse_macaddr(macaddr, buf) < 0) {
                fprintf(stderr, "invalid syntax for ethernet address\n");
                return -1;
            }
        }
        if (get_param_value(buf, sizeof(buf), "model", p)) {
            nd->model = strdup(buf);
        }
        nd->vlan = vlan;
        nb_nics++;
        vlan->nb_guest_devs++;
        ret = 0;
    } else
    if (!strcmp(device, "none")) {
        /* does nothing. It is needed to signal that no network cards
           are wanted */
        ret = 0;
    } else
#ifdef CONFIG_SLIRP
    if (!strcmp(device, "user")) {
        if (get_param_value(buf, sizeof(buf), "hostname", p)) {
            pstrcpy(slirp_hostname, sizeof(slirp_hostname), buf);
        }
        vlan->nb_host_devs++;
        ret = net_slirp_init(vlan);
    } else
#endif
#ifdef _WIN32
    if (!strcmp(device, "tap")) {
        char ifname[64];
        if (get_param_value(ifname, sizeof(ifname), "ifname", p) <= 0) {
            fprintf(stderr, "tap: no interface name\n");
            return -1;
        }
        vlan->nb_host_devs++;
        ret = tap_win32_init(vlan, ifname);
    } else
#else
    if (!strcmp(device, "tap")) {
        char ifname[64];
        char setup_script[1024], down_script[1024];
        int fd;
        vlan->nb_host_devs++;
        if (get_param_value(buf, sizeof(buf), "fd", p) > 0) {
            fd = strtol(buf, NULL, 0);
            fcntl(fd, F_SETFL, O_NONBLOCK);
            ret = -1;
            if (net_tap_fd_init(vlan, fd))
                ret = 0;
        } else {
            if (get_param_value(ifname, sizeof(ifname), "ifname", p) <= 0) {
                ifname[0] = '\0';
            }
            if (get_param_value(setup_script, sizeof(setup_script), "script", p) == 0) {
                pstrcpy(setup_script, sizeof(setup_script), DEFAULT_NETWORK_SCRIPT);
            }
            if (get_param_value(down_script, sizeof(down_script), "downscript", p) == 0) {
                pstrcpy(down_script, sizeof(down_script), DEFAULT_NETWORK_DOWN_SCRIPT);
            }
            ret = net_tap_init(vlan, ifname, setup_script, down_script);
        }
    } else
#endif
    if (!strcmp(device, "socket")) {
        if (get_param_value(buf, sizeof(buf), "fd", p) > 0) {
            int fd;
            fd = strtol(buf, NULL, 0);
            ret = -1;
            if (net_socket_fd_init(vlan, fd, 1))
                ret = 0;
        } else if (get_param_value(buf, sizeof(buf), "listen", p) > 0) {
            ret = net_socket_listen_init(vlan, buf);
        } else if (get_param_value(buf, sizeof(buf), "connect", p) > 0) {
            ret = net_socket_connect_init(vlan, buf);
        } else if (get_param_value(buf, sizeof(buf), "mcast", p) > 0) {
            ret = net_socket_mcast_init(vlan, buf);
        } else {
            fprintf(stderr, "Unknown socket options: %s\n", p);
            return -1;
        }
        vlan->nb_host_devs++;
    } else
#ifdef CONFIG_VDE
    if (!strcmp(device, "vde")) {
        char vde_sock[1024], vde_group[512];
	int vde_port, vde_mode;
        vlan->nb_host_devs++;
        if (get_param_value(vde_sock, sizeof(vde_sock), "sock", p) <= 0) {
	    vde_sock[0] = '\0';
	}
	if (get_param_value(buf, sizeof(buf), "port", p) > 0) {
	    vde_port = strtol(buf, NULL, 10);
	} else {
	    vde_port = 0;
	}
	if (get_param_value(vde_group, sizeof(vde_group), "group", p) <= 0) {
	    vde_group[0] = '\0';
	}
	if (get_param_value(buf, sizeof(buf), "mode", p) > 0) {
	    vde_mode = strtol(buf, NULL, 8);
	} else {
	    vde_mode = 0700;
	}
	ret = net_vde_init(vlan, vde_sock, vde_port, vde_group, vde_mode);
    } else
#endif
    {
        fprintf(stderr, "Unknown network device: %s\n", device);
        return -1;
    }
    if (ret < 0) {
        fprintf(stderr, "Could not initialize device '%s'\n", device);
    }

    return ret;
}

int net_client_parse(const char *str)
{
    const char *p;
    char *q;
    char device[64];

    p = str;
    q = device;
    while (*p != '\0' && *p != ',') {
        if ((q - device) < sizeof(device) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    if (*p == ',')
        p++;

    return net_client_init(device, p);
}

void do_info_network(void)
{
    VLANState *vlan;
    VLANClientState *vc;

    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        term_printf("VLAN %d devices:\n", vlan->id);
        for(vc = vlan->first_client; vc != NULL; vc = vc->next)
            term_printf("  %s\n", vc->info_str);
    }
}

void net_cleanup(void)
{
    VLANState *vlan;

#if !defined(_WIN32)
    /* close network clients */
    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        VLANClientState *vc;

        for(vc = vlan->first_client; vc != NULL; vc = vc->next) {
            if (vc->fd_read == tap_receive) {
                char ifname[64];
                TAPState *s = vc->opaque;

                if (sscanf(vc->info_str, "tap: ifname=%63s ", ifname) == 1 &&
                    s->down_script[0])
                    launch_script(s->down_script, ifname, s->fd);
            }
#if defined(CONFIG_VDE)
            if (vc->fd_read == vde_from_qemu) {
                VDEState *s = vc->opaque;
                vde_close(s->vde);
            }
#endif
        }
    }
#endif
}

void net_client_check(void)
{
    VLANState *vlan;

    for(vlan = first_vlan; vlan != NULL; vlan = vlan->next) {
        if (vlan->nb_guest_devs == 0 && vlan->nb_host_devs == 0)
            continue;
        if (vlan->nb_guest_devs == 0)
            fprintf(stderr, "Warning: vlan %d with no nics\n", vlan->id);
        if (vlan->nb_host_devs == 0)
            fprintf(stderr,
                    "Warning: vlan %d is not connected to host network\n",
                    vlan->id);
    }
}
