/* headers to use the BSD sockets */
#ifndef QEMU_SOCKET_H
#define QEMU_SOCKET_H

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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

int parse_unix_path(struct sockaddr_un *uaddr, const char *str);

#endif /* !_WIN32 */

void socket_set_nonblock(int fd);
int parse_host_port(struct sockaddr_in *saddr, const char *str);
int parse_host_src_port(struct sockaddr_in *haddr,
                        struct sockaddr_in *saddr,
                        const char *str);
int send_all(int fd, const uint8_t *buf, int len1);

#endif /* QEMU_SOCKET_H */
