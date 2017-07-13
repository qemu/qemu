/* headers to use the BSD sockets */

#ifndef QEMU_SOCKETS_H
#define QEMU_SOCKETS_H

#ifdef _WIN32

int inet_aton(const char *cp, struct in_addr *ia);

#endif /* !_WIN32 */

#include "qapi-types.h"

/* misc helpers */
int qemu_socket(int domain, int type, int protocol);
int qemu_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int socket_set_cork(int fd, int v);
int socket_set_nodelay(int fd);
void qemu_set_block(int fd);
void qemu_set_nonblock(int fd);
int socket_set_fast_reuse(int fd);

#ifdef WIN32
/* Windows has different names for the same constants with the same values */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
#endif

/* callback function for nonblocking connect
 * valid fd on success, negative error code on failure
 */
typedef void NonBlockingConnectHandler(int fd, Error *err, void *opaque);

int inet_ai_family_from_address(InetSocketAddress *addr,
                                Error **errp);
int inet_parse(InetSocketAddress *addr, const char *str, Error **errp);
int inet_connect(const char *str, Error **errp);
int inet_connect_saddr(InetSocketAddress *saddr,
                       NonBlockingConnectHandler *callback, void *opaque,
                       Error **errp);

NetworkAddressFamily inet_netfamily(int family);

int unix_listen(const char *path, char *ostr, int olen, Error **errp);
int unix_connect(const char *path, Error **errp);

SocketAddress *socket_parse(const char *str, Error **errp);
int socket_connect(SocketAddress *addr, NonBlockingConnectHandler *callback,
                   void *opaque, Error **errp);
int socket_listen(SocketAddress *addr, Error **errp);
void socket_listen_cleanup(int fd, Error **errp);
int socket_dgram(SocketAddress *remote, SocketAddress *local, Error **errp);

/* Old, ipv4 only bits.  Don't use for new code. */
int parse_host_port(struct sockaddr_in *saddr, const char *str);
int socket_init(void);

/**
 * socket_sockaddr_to_address:
 * @sa: socket address struct
 * @salen: size of @sa struct
 * @errp: pointer to uninitialized error object
 *
 * Get the string representation of the socket
 * address. A pointer to the allocated address information
 * struct will be returned, which the caller is required to
 * release with a call qapi_free_SocketAddress() when no
 * longer required.
 *
 * Returns: the socket address struct, or NULL on error
 */
SocketAddress *
socket_sockaddr_to_address(struct sockaddr_storage *sa,
                           socklen_t salen,
                           Error **errp);

/**
 * socket_local_address:
 * @fd: the socket file handle
 * @errp: pointer to uninitialized error object
 *
 * Get the string representation of the local socket
 * address. A pointer to the allocated address information
 * struct will be returned, which the caller is required to
 * release with a call qapi_free_SocketAddress() when no
 * longer required.
 *
 * Returns: the socket address struct, or NULL on error
 */
SocketAddress *socket_local_address(int fd, Error **errp);

/**
 * socket_remote_address:
 * @fd: the socket file handle
 * @errp: pointer to uninitialized error object
 *
 * Get the string representation of the remote socket
 * address. A pointer to the allocated address information
 * struct will be returned, which the caller is required to
 * release with a call qapi_free_SocketAddress() when no
 * longer required.
 *
 * Returns: the socket address struct, or NULL on error
 */
SocketAddress *socket_remote_address(int fd, Error **errp);

/**
 * socket_address_flatten:
 * @addr: the socket address to flatten
 *
 * Convert SocketAddressLegacy to SocketAddress.  Caller is responsible
 * for freeing with qapi_free_SocketAddress().
 *
 * Returns: the argument converted to SocketAddress.
 */
SocketAddress *socket_address_flatten(SocketAddressLegacy *addr);

#endif /* QEMU_SOCKETS_H */
