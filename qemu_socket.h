/* headers to use the BSD sockets */
#ifndef QEMU_SOCKET_H
#define QEMU_SOCKET_H

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define socket_error() WSAGetLastError()
#undef EINTR
#define EWOULDBLOCK WSAEWOULDBLOCK
#define EINTR       WSAEINTR
#define EINPROGRESS WSAEINPROGRESS

int inet_aton(const char *cp, struct in_addr *ia);

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>

#define socket_error() errno
#define closesocket(s) close(s)

#endif /* !_WIN32 */

#include "qemu-option.h"

/* misc helpers */
int qemu_socket(int domain, int type, int protocol);
int qemu_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
void socket_set_nonblock(int fd);
int send_all(int fd, const void *buf, int len1);

/* New, ipv6-ready socket helper functions, see qemu-sockets.c */
int inet_listen_opts(QemuOpts *opts, int port_offset);
int inet_listen(const char *str, char *ostr, int olen,
                int socktype, int port_offset);
int inet_connect_opts(QemuOpts *opts);
int inet_connect(const char *str, int socktype);
int inet_dgram_opts(QemuOpts *opts);
const char *inet_strfamily(int family);

int unix_listen_opts(QemuOpts *opts);
int unix_listen(const char *path, char *ostr, int olen);
int unix_connect_opts(QemuOpts *opts);
int unix_connect(const char *path);

/* Old, ipv4 only bits.  Don't use for new code. */
int parse_host_port(struct sockaddr_in *saddr, const char *str);
int socket_init(void);

#endif /* QEMU_SOCKET_H */
