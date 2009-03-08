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

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>

#define socket_error() errno
#define closesocket(s) close(s)

#endif /* !_WIN32 */

/* misc helpers */
void socket_set_nonblock(int fd);
int send_all(int fd, const void *buf, int len1);

/* New, ipv6-ready socket helper functions, see qemu-sockets.c */
int inet_listen(const char *str, char *ostr, int olen,
                int socktype, int port_offset);
int inet_connect(const char *str, int socktype);

int unix_listen(const char *path, char *ostr, int olen);
int unix_connect(const char *path);

/* Old, ipv4 only bits.  Don't use for new code. */
int parse_host_port(struct sockaddr_in *saddr, const char *str);
int parse_host_src_port(struct sockaddr_in *haddr,
                        struct sockaddr_in *saddr,
                        const char *str);

#endif /* QEMU_SOCKET_H */
