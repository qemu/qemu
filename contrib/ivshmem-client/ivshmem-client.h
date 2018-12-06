/*
 * Copyright 6WIND S.A., 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef IVSHMEM_CLIENT_H
#define IVSHMEM_CLIENT_H

/**
 * This file provides helper to implement an ivshmem client. It is used
 * on the host to ask QEMU to send an interrupt to an ivshmem PCI device in a
 * guest. QEMU also implements an ivshmem client similar to this one, they both
 * connect to an ivshmem server.
 *
 * A standalone ivshmem client based on this file is provided for debug/test
 * purposes.
 */

#include <sys/select.h>

#include "qemu/queue.h"
#include "hw/misc/ivshmem.h"

/**
 * Maximum number of notification vectors supported by the client
 */
#define IVSHMEM_CLIENT_MAX_VECTORS 64

/**
 * Structure storing a peer
 *
 * Each time a client connects to an ivshmem server, it is advertised to
 * all connected clients through the unix socket. When our ivshmem
 * client receives a notification, it creates a IvshmemClientPeer
 * structure to store the infos of this peer.
 *
 * This structure is also used to store the information of our own
 * client in (IvshmemClient)->local.
 */
typedef struct IvshmemClientPeer {
    QTAILQ_ENTRY(IvshmemClientPeer) next;    /**< next in list*/
    int64_t id;                              /**< the id of the peer */
    int vectors[IVSHMEM_CLIENT_MAX_VECTORS]; /**< one fd per vector */
    unsigned vectors_count;                  /**< number of vectors */
} IvshmemClientPeer;

typedef struct IvshmemClient IvshmemClient;

/**
 * Typedef of callback function used when our IvshmemClient receives a
 * notification from a peer.
 */
typedef void (*IvshmemClientNotifCb)(
    const IvshmemClient *client,
    const IvshmemClientPeer *peer,
    unsigned vect, void *arg);

/**
 * Structure describing an ivshmem client
 *
 * This structure stores all information related to our client: the name
 * of the server unix socket, the list of peers advertised by the
 * server, our own client information, and a pointer the notification
 * callback function used when we receive a notification from a peer.
 */
struct IvshmemClient {
    char unix_sock_path[PATH_MAX];      /**< path to unix sock */
    int sock_fd;                        /**< unix sock filedesc */
    int shm_fd;                         /**< shm file descriptor */

    QTAILQ_HEAD(, IvshmemClientPeer) peer_list;    /**< list of peers */
    IvshmemClientPeer local;            /**< our own infos */

    IvshmemClientNotifCb notif_cb;      /**< notification callback */
    void *notif_arg;                    /**< notification argument */

    bool verbose;                       /**< true to enable debug */
};

/**
 * Initialize an ivshmem client
 *
 * @client:         A pointer to an uninitialized IvshmemClient structure
 * @unix_sock_path: The pointer to the unix socket file name
 * @notif_cb:       If not NULL, the pointer to the function to be called when
 *                  our IvshmemClient receives a notification from a peer
 * @notif_arg:      Opaque pointer given as-is to the notification callback
 *                  function
 * @verbose:        True to enable debug
 *
 * Returns:         0 on success, or a negative value on error
 */
int ivshmem_client_init(IvshmemClient *client, const char *unix_sock_path,
                        IvshmemClientNotifCb notif_cb, void *notif_arg,
                        bool verbose);

/**
 * Connect to the server
 *
 * Connect to the server unix socket, and read the first initial
 * messages sent by the server, giving the ID of the client and the file
 * descriptor of the shared memory.
 *
 * @client: The ivshmem client
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_client_connect(IvshmemClient *client);

/**
 * Close connection to the server and free all peer structures
 *
 * @client: The ivshmem client
 */
void ivshmem_client_close(IvshmemClient *client);

/**
 * Fill a fd_set with file descriptors to be monitored
 *
 * This function will fill a fd_set with all file descriptors
 * that must be polled (unix server socket and peers eventfd). The
 * function will not initialize the fd_set, it is up to the caller
 * to do this.
 *
 * @client: The ivshmem client
 * @fds:    The fd_set to be updated
 * @maxfd:  Must be set to the max file descriptor + 1 in fd_set. This value is
 *          updated if this function adds a greater fd in fd_set.
 */
void ivshmem_client_get_fds(const IvshmemClient *client, fd_set *fds,
                            int *maxfd);

/**
 * Read and handle new messages
 *
 * Given a fd_set filled by select(), handle incoming messages from
 * server or peers.
 *
 * @client: The ivshmem client
 * @fds:    The fd_set containing the file descriptors to be checked. Note
 *          that file descriptors that are not related to our client are
 *          ignored.
 * @maxfd:  The maximum fd in fd_set, plus one.
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_client_handle_fds(IvshmemClient *client, fd_set *fds, int maxfd);

/**
 * Send a notification to a vector of a peer
 *
 * @client: The ivshmem client
 * @peer:   The peer to be notified
 * @vector: The number of the vector
 *
 * Returns: 0 on success, or a negative value on error
 */
int ivshmem_client_notify(const IvshmemClient *client,
                          const IvshmemClientPeer *peer, unsigned vector);

/**
 * Send a notification to all vectors of a peer
 *
 * @client: The ivshmem client
 * @peer:   The peer to be notified
 *
 * Returns: 0 on success, or a negative value on error (at least one
 *          notification failed)
 */
int ivshmem_client_notify_all_vects(const IvshmemClient *client,
                                    const IvshmemClientPeer *peer);

/**
 * Broadcat a notification to all vectors of all peers
 *
 * @client: The ivshmem client
 *
 * Returns: 0 on success, or a negative value on error (at least one
 *          notification failed)
 */
int ivshmem_client_notify_broadcast(const IvshmemClient *client);

/**
 * Search a peer from its identifier
 *
 * Return the peer structure from its peer_id. If the given peer_id is
 * the local id, the function returns the local peer structure.
 *
 * @client:  The ivshmem client
 * @peer_id: The identifier of the peer structure
 *
 * Returns:  The peer structure, or NULL if not found
 */
IvshmemClientPeer *
ivshmem_client_search_peer(IvshmemClient *client, int64_t peer_id);

/**
 * Dump information of this ivshmem client on stdout
 *
 * Dump the id and the vectors of the given ivshmem client and the list
 * of its peers and their vectors on stdout.
 *
 * @client: The ivshmem client
 */
void ivshmem_client_dump(const IvshmemClient *client);

#endif /* IVSHMEM_CLIENT_H */
