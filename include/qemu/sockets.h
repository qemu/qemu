/* headers to use the BSD sockets */

#ifndef QEMU_SOCKETS_H
#define QEMU_SOCKETS_H

#ifdef _WIN32

int inet_aton(const char *cp, struct in_addr *ia);

#endif /* !_WIN32 */

#include "qapi/qapi-types-sockets.h"

/* misc helpers */
bool fd_is_socket(int fd);
int qemu_socket(int domain, int type, int protocol);

/**
 * qemu_socketpair:
 * @domain: specifies a communication domain, such as PF_UNIX
 * @type: specifies the socket type.
 * @protocol: specifies a particular protocol to be used with the  socket
 * @sv: an array to store the pair of socket created
 *
 * Creates an unnamed pair of connected sockets in the specified domain,
 * of the specified type, and using the optionally specified protocol.
 * And automatically set the close-on-exec flags on the returned sockets
 *
 * Return 0 on success.
 */
int qemu_socketpair(int domain, int type, int protocol, int sv[2]);

int qemu_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
/*
 * A variant of send(2) which handles partial send.
 *
 * Return the number of bytes transferred over the socket.
 * Set errno if fewer than `count' bytes are sent.
 *
 * This function don't work with non-blocking socket's.
 * Any of the possibilities with non-blocking socket's is bad:
 *   - return a short write (then name is wrong)
 *   - busy wait adding (errno == EAGAIN) to the loop
 */
ssize_t qemu_send_full(int s, const void *buf, size_t count)
    G_GNUC_WARN_UNUSED_RESULT;
int socket_set_cork(int fd, int v);
int socket_set_nodelay(int fd);
void qemu_socket_set_block(int fd);
int qemu_socket_try_set_nonblock(int fd);
void qemu_socket_set_nonblock(int fd);
int socket_set_fast_reuse(int fd);

#ifdef WIN32
/* Windows has different names for the same constants with the same values */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
#endif

int inet_ai_family_from_address(InetSocketAddress *addr,
                                Error **errp);
int inet_parse(InetSocketAddress *addr, const char *str, Error **errp);
int inet_connect(const char *str, Error **errp);
int inet_connect_saddr(InetSocketAddress *saddr, Error **errp);

NetworkAddressFamily inet_netfamily(int family);

int unix_listen(const char *path, Error **errp);
int unix_connect(const char *path, Error **errp);

char *socket_uri(SocketAddress *addr);
SocketAddress *socket_parse(const char *str, Error **errp);
int socket_connect(SocketAddress *addr, Error **errp);
int socket_listen(SocketAddress *addr, int num, Error **errp);
void socket_listen_cleanup(int fd, Error **errp);
int socket_dgram(SocketAddress *remote, SocketAddress *local, Error **errp);

/* Old, ipv4 only bits.  Don't use for new code. */
int convert_host_port(struct sockaddr_in *saddr, const char *host,
                      const char *port, Error **errp);
int parse_host_port(struct sockaddr_in *saddr, const char *str,
                    Error **errp);
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

/**
 * socket_address_parse_named_fd:
 *
 * Modify @addr, replacing a named fd by its corresponding number.
 * Needed for callers that plan to pass @addr to a context where the
 * current monitor is not available.
 *
 * Return 0 on success.
 */
int socket_address_parse_named_fd(SocketAddress *addr, Error **errp);
#endif /* QEMU_SOCKETS_H */
