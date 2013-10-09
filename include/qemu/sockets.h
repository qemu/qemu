/* headers to use the BSD sockets */
#ifndef QEMU_SOCKET_H
#define QEMU_SOCKET_H

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define socket_error() WSAGetLastError()

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

#include "qemu/option.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"

extern QemuOptsList socket_optslist;

/* misc helpers */
int qemu_socket(int domain, int type, int protocol);
int qemu_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int socket_set_cork(int fd, int v);
int socket_set_nodelay(int fd);
void qemu_set_block(int fd);
void qemu_set_nonblock(int fd);
int socket_set_fast_reuse(int fd);
int send_all(int fd, const void *buf, int len1);
int recv_all(int fd, void *buf, int len1, bool single_read);

/* callback function for nonblocking connect
 * valid fd on success, negative error code on failure
 */
typedef void NonBlockingConnectHandler(int fd, void *opaque);

InetSocketAddress *inet_parse(const char *str, Error **errp);
int inet_listen_opts(QemuOpts *opts, int port_offset, Error **errp);
int inet_listen(const char *str, char *ostr, int olen,
                int socktype, int port_offset, Error **errp);
int inet_connect_opts(QemuOpts *opts, Error **errp,
                      NonBlockingConnectHandler *callback, void *opaque);
int inet_connect(const char *str, Error **errp);
int inet_nonblocking_connect(const char *str,
                             NonBlockingConnectHandler *callback,
                             void *opaque, Error **errp);

int inet_dgram_opts(QemuOpts *opts, Error **errp);
const char *inet_strfamily(int family);

int unix_listen_opts(QemuOpts *opts, Error **errp);
int unix_listen(const char *path, char *ostr, int olen, Error **errp);
int unix_connect_opts(QemuOpts *opts, Error **errp,
                      NonBlockingConnectHandler *callback, void *opaque);
int unix_connect(const char *path, Error **errp);
int unix_nonblocking_connect(const char *str,
                             NonBlockingConnectHandler *callback,
                             void *opaque, Error **errp);

SocketAddress *socket_parse(const char *str, Error **errp);
int socket_connect(SocketAddress *addr, Error **errp,
                   NonBlockingConnectHandler *callback, void *opaque);
int socket_listen(SocketAddress *addr, Error **errp);
int socket_dgram(SocketAddress *remote, SocketAddress *local, Error **errp);

/* Old, ipv4 only bits.  Don't use for new code. */
int parse_host_port(struct sockaddr_in *saddr, const char *str);
int socket_init(void);

#endif /* QEMU_SOCKET_H */
