#include "qemu/osdep.h"
#include <locale.h>
#include <glib/gstdio.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "libqtest.h"

typedef struct {
    char *test_dir;
    GMainLoop *loop;
    int fd;
    GPid pid;
} TestFixture;

static int connect_qga(char *path)
{
    int s, ret, len, i = 0;
    struct sockaddr_un remote;

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert(s != -1);

    remote.sun_family = AF_UNIX;
    do {
        strcpy(remote.sun_path, path);
        len = strlen(remote.sun_path) + sizeof(remote.sun_family);
        ret = connect(s, (struct sockaddr *)&remote, len);
        if (ret == -1) {
            g_usleep(G_USEC_PER_SEC);
        }
        if (i++ == 10) {
            return -1;
        }
    } while (ret == -1);

    return s;
}

static void qga_watch(GPid pid, gint status, gpointer user_data)
{
    TestFixture *fixture = user_data;

    g_assert_cmpint(status, ==, 0);
    g_main_loop_quit(fixture->loop);
}

static void
fixture_setup(TestFixture *fixture, gconstpointer data, gchar **envp)
{
    const gchar *extra_arg = data;
    GError *error = NULL;
    gchar *cwd, *path, *cmd, **argv = NULL;

    fixture->loop = g_main_loop_new(NULL, FALSE);

    fixture->test_dir = g_strdup("/tmp/qgatest.XXXXXX");
    g_assert_nonnull(mkdtemp(fixture->test_dir));

    path = g_build_filename(fixture->test_dir, "sock", NULL);
    cwd = g_get_current_dir();
    cmd = g_strdup_printf("%s%cqemu-ga -m unix-listen -t %s -p %s %s %s",
                          cwd, G_DIR_SEPARATOR,
                          fixture->test_dir, path,
                          getenv("QTEST_LOG") ? "-v" : "",
                          extra_arg ?: "");
    g_shell_parse_argv(cmd, NULL, &argv, &error);
    g_assert_no_error(error);

    g_spawn_async(fixture->test_dir, argv, envp,
                  G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,
                  NULL, NULL, &fixture->pid, &error);
    g_assert_no_error(error);

    g_child_watch_add(fixture->pid, qga_watch, fixture);

    fixture->fd = connect_qga(path);
    g_assert_cmpint(fixture->fd, !=, -1);

    g_strfreev(argv);
    g_free(cmd);
    g_free(cwd);
    g_free(path);
}

static void
fixture_tear_down(TestFixture *fixture, gconstpointer data)
{
    gchar *tmp;

    kill(fixture->pid, SIGTERM);

    g_main_loop_run(fixture->loop);
    g_main_loop_unref(fixture->loop);

    g_spawn_close_pid(fixture->pid);

    tmp = g_build_filename(fixture->test_dir, "foo", NULL);
    g_unlink(tmp);
    g_free(tmp);

    tmp = g_build_filename(fixture->test_dir, "qga.state", NULL);
    g_unlink(tmp);
    g_free(tmp);

    tmp = g_build_filename(fixture->test_dir, "sock", NULL);
    g_unlink(tmp);
    g_free(tmp);

    g_rmdir(fixture->test_dir);
    g_free(fixture->test_dir);
}

static void qmp_assertion_message_error(const char     *domain,
                                        const char     *file,
                                        int             line,
                                        const char     *func,
                                        const char     *expr,
                                        QDict          *dict)
{
    const char *class, *desc;
    char *s;
    QDict *error;

    error = qdict_get_qdict(dict, "error");
    class = qdict_get_try_str(error, "class");
    desc = qdict_get_try_str(error, "desc");

    s = g_strdup_printf("assertion failed %s: %s %s", expr, class, desc);
    g_assertion_message(domain, file, line, func, s);
    g_free(s);
}

#define qmp_assert_no_error(err) do {                                   \
    if (qdict_haskey(err, "error")) {                                   \
        qmp_assertion_message_error(G_LOG_DOMAIN, __FILE__, __LINE__,   \
                                    G_STRFUNC, #err, err);              \
    }                                                                   \
} while (0)

static void test_qga_sync_delimited(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    guint32 v, r = g_random_int();
    unsigned char c;
    QDict *ret;
    gchar *cmd;

    cmd = g_strdup_printf("\xff{'execute': 'guest-sync-delimited',"
                          " 'arguments': {'id': %u } }", r);
    qmp_fd_send(fixture->fd, cmd);
    g_free(cmd);

    /*
     * Read and ignore garbage until resynchronized.
     *
     * Note that the full reset sequence would involve checking the
     * response of guest-sync-delimited and repeating the loop if
     * 'id' field of the response does not match the 'id' field of
     * the request. Testing this fully would require inserting
     * garbage in the response stream and is left as a future test
     * to implement.
     *
     * TODO: The server shouldn't emit so much garbage (among other
     * things, it loudly complains about the client's \xff being
     * invalid JSON, even though it is a documented part of the
     * handshake.
     */
    do {
        v = read(fixture->fd, &c, 1);
        g_assert_cmpint(v, ==, 1);
    } while (c != 0xff);

    ret = qmp_fd_receive(fixture->fd);
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    v = qdict_get_int(ret, "return");
    g_assert_cmpint(r, ==, v);

    QDECREF(ret);
}

static void test_qga_sync(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    guint32 v, r = g_random_int();
    QDict *ret;
    gchar *cmd;

    /*
     * TODO guest-sync is inherently limited: we cannot distinguish
     * failure caused by reacting to garbage on the wire prior to this
     * command, from failure of this actual command. Clients are
     * supposed to be able to send a raw '\xff' byte to at least
     * re-synchronize the server's parser prior to this command, but
     * we are not in a position to test that here because (at least
     * for now) it causes the server to issue an error message about
     * invalid JSON. Testing of '\xff' handling is done in
     * guest-sync-delimited instead.
     */
    cmd = g_strdup_printf("{'execute': 'guest-sync',"
                          " 'arguments': {'id': %u } }", r);
    ret = qmp_fd(fixture->fd, cmd);
    g_free(cmd);

    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    v = qdict_get_int(ret, "return");
    g_assert_cmpint(r, ==, v);

    QDECREF(ret);
}

static void test_qga_ping(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-ping'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    QDECREF(ret);
}

static void test_qga_invalid_args(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret, *error;
    const gchar *class, *desc;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-ping', "
                 "'arguments': {'foo': 42 }}");
    g_assert_nonnull(ret);

    error = qdict_get_qdict(ret, "error");
    class = qdict_get_try_str(error, "class");
    desc = qdict_get_try_str(error, "desc");

    g_assert_cmpstr(class, ==, "GenericError");
    g_assert_cmpstr(desc, ==, "Parameter 'foo' is unexpected");

    QDECREF(ret);
}

static void test_qga_invalid_cmd(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret, *error;
    const gchar *class, *desc;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-invalid-cmd'}");
    g_assert_nonnull(ret);

    error = qdict_get_qdict(ret, "error");
    class = qdict_get_try_str(error, "class");
    desc = qdict_get_try_str(error, "desc");

    g_assert_cmpstr(class, ==, "CommandNotFound");
    g_assert_cmpint(strlen(desc), >, 0);

    QDECREF(ret);
}

static void test_qga_info(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret, *val;
    const gchar *version;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-info'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    val = qdict_get_qdict(ret, "return");
    version = qdict_get_try_str(val, "version");
    g_assert_cmpstr(version, ==, QEMU_VERSION);

    QDECREF(ret);
}

static void test_qga_get_vcpus(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    QList *list;
    const QListEntry *entry;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-get-vcpus'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    /* check there is at least a cpu */
    list = qdict_get_qlist(ret, "return");
    entry = qlist_first(list);
    g_assert(qdict_haskey(qobject_to_qdict(entry->value), "online"));
    g_assert(qdict_haskey(qobject_to_qdict(entry->value), "logical-id"));

    QDECREF(ret);
}

static void test_qga_get_fsinfo(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    QList *list;
    const QListEntry *entry;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-get-fsinfo'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    /* sanity-check the response if there are any filesystems */
    list = qdict_get_qlist(ret, "return");
    entry = qlist_first(list);
    if (entry) {
        g_assert(qdict_haskey(qobject_to_qdict(entry->value), "name"));
        g_assert(qdict_haskey(qobject_to_qdict(entry->value), "mountpoint"));
        g_assert(qdict_haskey(qobject_to_qdict(entry->value), "type"));
        g_assert(qdict_haskey(qobject_to_qdict(entry->value), "disk"));
    }

    QDECREF(ret);
}

static void test_qga_get_memory_block_info(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret, *val;
    int64_t size;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-get-memory-block-info'}");
    g_assert_nonnull(ret);

    /* some systems might not expose memory block info in sysfs */
    if (!qdict_haskey(ret, "error")) {
        /* check there is at least some memory */
        val = qdict_get_qdict(ret, "return");
        size = qdict_get_int(val, "size");
        g_assert_cmpint(size, >, 0);
    }

    QDECREF(ret);
}

static void test_qga_get_memory_blocks(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    QList *list;
    const QListEntry *entry;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-get-memory-blocks'}");
    g_assert_nonnull(ret);

    /* some systems might not expose memory block info in sysfs */
    if (!qdict_haskey(ret, "error")) {
        list = qdict_get_qlist(ret, "return");
        entry = qlist_first(list);
        /* newer versions of qga may return empty list without error */
        if (entry) {
            g_assert(qdict_haskey(qobject_to_qdict(entry->value), "phys-index"));
            g_assert(qdict_haskey(qobject_to_qdict(entry->value), "online"));
        }
    }

    QDECREF(ret);
}

static void test_qga_network_get_interfaces(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    QList *list;
    const QListEntry *entry;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-network-get-interfaces'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    /* check there is at least an interface */
    list = qdict_get_qlist(ret, "return");
    entry = qlist_first(list);
    g_assert(qdict_haskey(qobject_to_qdict(entry->value), "name"));

    QDECREF(ret);
}

static void test_qga_file_ops(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    const unsigned char helloworld[] = "Hello World!\n";
    const char *b64;
    gchar *cmd, *path, *enc;
    unsigned char *dec;
    QDict *ret, *val;
    int64_t id, eof;
    gsize count;
    FILE *f;
    char tmp[100];

    /* open */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-file-open',"
                 " 'arguments': { 'path': 'foo', 'mode': 'w+' } }");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    id = qdict_get_int(ret, "return");
    QDECREF(ret);

    enc = g_base64_encode(helloworld, sizeof(helloworld));
    /* write */
    cmd = g_strdup_printf("{'execute': 'guest-file-write',"
                          " 'arguments': { 'handle': %" PRId64 ","
                          " 'buf-b64': '%s' } }", id, enc);
    ret = qmp_fd(fixture->fd, cmd);
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "count");
    eof = qdict_get_bool(val, "eof");
    g_assert_cmpint(count, ==, sizeof(helloworld));
    g_assert_cmpint(eof, ==, 0);
    QDECREF(ret);
    g_free(cmd);

    /* flush */
    cmd = g_strdup_printf("{'execute': 'guest-file-flush',"
                          " 'arguments': {'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    QDECREF(ret);
    g_free(cmd);

    /* close */
    cmd = g_strdup_printf("{'execute': 'guest-file-close',"
                          " 'arguments': {'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    QDECREF(ret);
    g_free(cmd);

    /* check content */
    path = g_build_filename(fixture->test_dir, "foo", NULL);
    f = fopen(path, "r");
    g_free(path);
    g_assert_nonnull(f);
    count = fread(tmp, 1, sizeof(tmp), f);
    g_assert_cmpint(count, ==, sizeof(helloworld));
    tmp[count] = 0;
    g_assert_cmpstr(tmp, ==, (char *)helloworld);
    fclose(f);

    /* open */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-file-open',"
                 " 'arguments': { 'path': 'foo', 'mode': 'r' } }");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    id = qdict_get_int(ret, "return");
    QDECREF(ret);

    /* read */
    cmd = g_strdup_printf("{'execute': 'guest-file-read',"
                          " 'arguments': { 'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "count");
    eof = qdict_get_bool(val, "eof");
    b64 = qdict_get_str(val, "buf-b64");
    g_assert_cmpint(count, ==, sizeof(helloworld));
    g_assert(eof);
    g_assert_cmpstr(b64, ==, enc);

    QDECREF(ret);
    g_free(cmd);
    g_free(enc);

    /* read eof */
    cmd = g_strdup_printf("{'execute': 'guest-file-read',"
                          " 'arguments': { 'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "count");
    eof = qdict_get_bool(val, "eof");
    b64 = qdict_get_str(val, "buf-b64");
    g_assert_cmpint(count, ==, 0);
    g_assert(eof);
    g_assert_cmpstr(b64, ==, "");
    QDECREF(ret);
    g_free(cmd);

    /* seek */
    cmd = g_strdup_printf("{'execute': 'guest-file-seek',"
                          " 'arguments': { 'handle': %" PRId64 ", "
                          " 'offset': %d, 'whence': '%s' } }",
                          id, 6, "set");
    ret = qmp_fd(fixture->fd, cmd);
    qmp_assert_no_error(ret);
    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "position");
    eof = qdict_get_bool(val, "eof");
    g_assert_cmpint(count, ==, 6);
    g_assert(!eof);
    QDECREF(ret);
    g_free(cmd);

    /* partial read */
    cmd = g_strdup_printf("{'execute': 'guest-file-read',"
                          " 'arguments': { 'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "count");
    eof = qdict_get_bool(val, "eof");
    b64 = qdict_get_str(val, "buf-b64");
    g_assert_cmpint(count, ==, sizeof(helloworld) - 6);
    g_assert(eof);
    dec = g_base64_decode(b64, &count);
    g_assert_cmpint(count, ==, sizeof(helloworld) - 6);
    g_assert_cmpmem(dec, count, helloworld + 6, sizeof(helloworld) - 6);
    g_free(dec);

    QDECREF(ret);
    g_free(cmd);

    /* close */
    cmd = g_strdup_printf("{'execute': 'guest-file-close',"
                          " 'arguments': {'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    QDECREF(ret);
    g_free(cmd);
}

static void test_qga_file_write_read(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    const unsigned char helloworld[] = "Hello World!\n";
    const char *b64;
    gchar *cmd, *enc;
    QDict *ret, *val;
    int64_t id, eof;
    gsize count;

    /* open */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-file-open',"
                 " 'arguments': { 'path': 'foo', 'mode': 'w+' } }");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    id = qdict_get_int(ret, "return");
    QDECREF(ret);

    enc = g_base64_encode(helloworld, sizeof(helloworld));
    /* write */
    cmd = g_strdup_printf("{'execute': 'guest-file-write',"
                          " 'arguments': { 'handle': %" PRId64 ","
                          " 'buf-b64': '%s' } }", id, enc);
    ret = qmp_fd(fixture->fd, cmd);
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "count");
    eof = qdict_get_bool(val, "eof");
    g_assert_cmpint(count, ==, sizeof(helloworld));
    g_assert_cmpint(eof, ==, 0);
    QDECREF(ret);
    g_free(cmd);

    /* read (check implicit flush) */
    cmd = g_strdup_printf("{'execute': 'guest-file-read',"
                          " 'arguments': { 'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "count");
    eof = qdict_get_bool(val, "eof");
    b64 = qdict_get_str(val, "buf-b64");
    g_assert_cmpint(count, ==, 0);
    g_assert(eof);
    g_assert_cmpstr(b64, ==, "");
    QDECREF(ret);
    g_free(cmd);

    /* seek to 0 */
    cmd = g_strdup_printf("{'execute': 'guest-file-seek',"
                          " 'arguments': { 'handle': %" PRId64 ", "
                          " 'offset': %d, 'whence': '%s' } }",
                          id, 0, "set");
    ret = qmp_fd(fixture->fd, cmd);
    qmp_assert_no_error(ret);
    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "position");
    eof = qdict_get_bool(val, "eof");
    g_assert_cmpint(count, ==, 0);
    g_assert(!eof);
    QDECREF(ret);
    g_free(cmd);

    /* read */
    cmd = g_strdup_printf("{'execute': 'guest-file-read',"
                          " 'arguments': { 'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    val = qdict_get_qdict(ret, "return");
    count = qdict_get_int(val, "count");
    eof = qdict_get_bool(val, "eof");
    b64 = qdict_get_str(val, "buf-b64");
    g_assert_cmpint(count, ==, sizeof(helloworld));
    g_assert(eof);
    g_assert_cmpstr(b64, ==, enc);
    QDECREF(ret);
    g_free(cmd);
    g_free(enc);

    /* close */
    cmd = g_strdup_printf("{'execute': 'guest-file-close',"
                          " 'arguments': {'handle': %" PRId64 "} }",
                          id);
    ret = qmp_fd(fixture->fd, cmd);
    QDECREF(ret);
    g_free(cmd);
}

static void test_qga_get_time(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    int64_t time;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-get-time'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    time = qdict_get_int(ret, "return");
    g_assert_cmpint(time, >, 0);

    QDECREF(ret);
}

static void test_qga_set_time(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    int64_t current, time;
    gchar *cmd;

    /* get current time */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-get-time'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    current = qdict_get_int(ret, "return");
    g_assert_cmpint(current, >, 0);
    QDECREF(ret);

    /* set some old time */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-set-time',"
                 " 'arguments': { 'time': 1000 } }");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    QDECREF(ret);

    /* check old time */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-get-time'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    time = qdict_get_int(ret, "return");
    g_assert_cmpint(time / 1000, <, G_USEC_PER_SEC * 10);
    QDECREF(ret);

    /* set back current time */
    cmd = g_strdup_printf("{'execute': 'guest-set-time',"
                          " 'arguments': { 'time': %" PRId64 " } }",
                          current + time * 1000);
    ret = qmp_fd(fixture->fd, cmd);
    g_free(cmd);
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    QDECREF(ret);
}

static void test_qga_fstrim(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    QList *list;
    const QListEntry *entry;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-fstrim',"
                 " arguments: { minimum: 4194304 } }");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    list = qdict_get_qlist(ret, "return");
    entry = qlist_first(list);
    g_assert(qdict_haskey(qobject_to_qdict(entry->value), "paths"));

    QDECREF(ret);
}

static void test_qga_blacklist(gconstpointer data)
{
    TestFixture fix;
    QDict *ret, *error;
    const gchar *class, *desc;

    fixture_setup(&fix, "-b guest-ping,guest-get-time", NULL);

    /* check blacklist */
    ret = qmp_fd(fix.fd, "{'execute': 'guest-ping'}");
    g_assert_nonnull(ret);
    error = qdict_get_qdict(ret, "error");
    class = qdict_get_try_str(error, "class");
    desc = qdict_get_try_str(error, "desc");
    g_assert_cmpstr(class, ==, "GenericError");
    g_assert_nonnull(g_strstr_len(desc, -1, "has been disabled"));
    QDECREF(ret);

    ret = qmp_fd(fix.fd, "{'execute': 'guest-get-time'}");
    g_assert_nonnull(ret);
    error = qdict_get_qdict(ret, "error");
    class = qdict_get_try_str(error, "class");
    desc = qdict_get_try_str(error, "desc");
    g_assert_cmpstr(class, ==, "GenericError");
    g_assert_nonnull(g_strstr_len(desc, -1, "has been disabled"));
    QDECREF(ret);

    /* check something work */
    ret = qmp_fd(fix.fd, "{'execute': 'guest-get-fsinfo'}");
    qmp_assert_no_error(ret);
    QDECREF(ret);

    fixture_tear_down(&fix, NULL);
}

static void test_qga_config(gconstpointer data)
{
    GError *error = NULL;
    char *cwd, *cmd, *out, *err, *str, **strv, **argv = NULL;
    char *env[2];
    int status;
    gsize n;
    GKeyFile *kf;

    cwd = g_get_current_dir();
    cmd = g_strdup_printf("%s%cqemu-ga -D",
                          cwd, G_DIR_SEPARATOR);
    g_free(cwd);
    g_shell_parse_argv(cmd, NULL, &argv, &error);
    g_free(cmd);
    g_assert_no_error(error);

    env[0] = g_strdup_printf("QGA_CONF=tests%cdata%ctest-qga-config",
                             G_DIR_SEPARATOR, G_DIR_SEPARATOR);
    env[1] = NULL;
    g_spawn_sync(NULL, argv, env, 0,
                 NULL, NULL, &out, &err, &status, &error);
    g_strfreev(argv);

    g_assert_no_error(error);
    g_assert_cmpstr(err, ==, "");
    g_assert_cmpint(status, ==, 0);

    kf = g_key_file_new();
    g_key_file_load_from_data(kf, out, -1, G_KEY_FILE_NONE, &error);
    g_assert_no_error(error);

    str = g_key_file_get_start_group(kf);
    g_assert_cmpstr(str, ==, "general");
    g_free(str);

    g_assert_false(g_key_file_get_boolean(kf, "general", "daemon", &error));
    g_assert_no_error(error);

    str = g_key_file_get_string(kf, "general", "method", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(str, ==, "virtio-serial");
    g_free(str);

    str = g_key_file_get_string(kf, "general", "path", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(str, ==, "/path/to/org.qemu.guest_agent.0");
    g_free(str);

    str = g_key_file_get_string(kf, "general", "pidfile", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(str, ==, "/var/foo/qemu-ga.pid");
    g_free(str);

    str = g_key_file_get_string(kf, "general", "statedir", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(str, ==, "/var/state");
    g_free(str);

    g_assert_true(g_key_file_get_boolean(kf, "general", "verbose", &error));
    g_assert_no_error(error);

    strv = g_key_file_get_string_list(kf, "general", "blacklist", &n, &error);
    g_assert_cmpint(n, ==, 2);
#if GLIB_CHECK_VERSION(2, 44, 0)
    g_assert_true(g_strv_contains((const char * const *)strv,
                                  "guest-ping"));
    g_assert_true(g_strv_contains((const char * const *)strv,
                                  "guest-get-time"));
#endif
    g_assert_no_error(error);
    g_strfreev(strv);

    g_free(out);
    g_free(err);
    g_free(env[0]);
    g_key_file_free(kf);
}

static void test_qga_fsfreeze_status(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    const gchar *status;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-fsfreeze-status'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    status = qdict_get_try_str(ret, "return");
    g_assert_cmpstr(status, ==, "thawed");

    QDECREF(ret);
}

static void test_qga_fsfreeze_and_thaw(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret;
    const gchar *status;

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-fsfreeze-freeze'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    QDECREF(ret);

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-fsfreeze-status'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    status = qdict_get_try_str(ret, "return");
    g_assert_cmpstr(status, ==, "frozen");
    QDECREF(ret);

    ret = qmp_fd(fixture->fd, "{'execute': 'guest-fsfreeze-thaw'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    QDECREF(ret);
}

static void test_qga_guest_exec(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret, *val;
    const gchar *out;
    guchar *decoded;
    int64_t pid, now, exitcode;
    gsize len;
    bool exited;
    char *cmd;

    /* exec 'echo foo bar' */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-exec', 'arguments': {"
                 " 'path': '/bin/echo', 'arg': [ '-n', '\" test_str \"' ],"
                 " 'capture-output': true } }");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);
    val = qdict_get_qdict(ret, "return");
    pid = qdict_get_int(val, "pid");
    g_assert_cmpint(pid, >, 0);
    QDECREF(ret);

    /* wait for completion */
    now = g_get_monotonic_time();
    cmd = g_strdup_printf("{'execute': 'guest-exec-status',"
                          " 'arguments': { 'pid': %" PRId64 " } }", pid);
    do {
        ret = qmp_fd(fixture->fd, cmd);
        g_assert_nonnull(ret);
        val = qdict_get_qdict(ret, "return");
        exited = qdict_get_bool(val, "exited");
        if (!exited) {
            QDECREF(ret);
        }
    } while (!exited &&
             g_get_monotonic_time() < now + 5 * G_TIME_SPAN_SECOND);
    g_assert(exited);
    g_free(cmd);

    /* check stdout */
    exitcode = qdict_get_int(val, "exitcode");
    g_assert_cmpint(exitcode, ==, 0);
    out = qdict_get_str(val, "out-data");
    decoded = g_base64_decode(out, &len);
    g_assert_cmpint(len, ==, 12);
    g_assert_cmpstr((char *)decoded, ==, "\" test_str \"");
    g_free(decoded);
    QDECREF(ret);
}

static void test_qga_guest_exec_invalid(gconstpointer fix)
{
    const TestFixture *fixture = fix;
    QDict *ret, *error;
    const gchar *class, *desc;

    /* invalid command */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-exec', 'arguments': {"
                 " 'path': '/bin/invalid-cmd42' } }");
    g_assert_nonnull(ret);
    error = qdict_get_qdict(ret, "error");
    g_assert_nonnull(error);
    class = qdict_get_str(error, "class");
    desc = qdict_get_str(error, "desc");
    g_assert_cmpstr(class, ==, "GenericError");
    g_assert_cmpint(strlen(desc), >, 0);
    QDECREF(ret);

    /* invalid pid */
    ret = qmp_fd(fixture->fd, "{'execute': 'guest-exec-status',"
                 " 'arguments': { 'pid': 0 } }");
    g_assert_nonnull(ret);
    error = qdict_get_qdict(ret, "error");
    g_assert_nonnull(error);
    class = qdict_get_str(error, "class");
    desc = qdict_get_str(error, "desc");
    g_assert_cmpstr(class, ==, "GenericError");
    g_assert_cmpint(strlen(desc), >, 0);
    QDECREF(ret);
}

static void test_qga_guest_get_osinfo(gconstpointer data)
{
    TestFixture fixture;
    const gchar *str;
    gchar *cwd, *env[2];
    QDict *ret, *val;

    cwd = g_get_current_dir();
    env[0] = g_strdup_printf(
        "QGA_OS_RELEASE=%s%ctests%cdata%ctest-qga-os-release",
        cwd, G_DIR_SEPARATOR, G_DIR_SEPARATOR, G_DIR_SEPARATOR);
    env[1] = NULL;
    g_free(cwd);
    fixture_setup(&fixture, NULL, env);

    ret = qmp_fd(fixture.fd, "{'execute': 'guest-get-osinfo'}");
    g_assert_nonnull(ret);
    qmp_assert_no_error(ret);

    val = qdict_get_qdict(ret, "return");

    str = qdict_get_try_str(val, "id");
    g_assert_nonnull(str);
    g_assert_cmpstr(str, ==, "qemu-ga-test");

    str = qdict_get_try_str(val, "name");
    g_assert_nonnull(str);
    g_assert_cmpstr(str, ==, "QEMU-GA");

    str = qdict_get_try_str(val, "pretty-name");
    g_assert_nonnull(str);
    g_assert_cmpstr(str, ==, "QEMU Guest Agent test");

    str = qdict_get_try_str(val, "version");
    g_assert_nonnull(str);
    g_assert_cmpstr(str, ==, "Test 1");

    str = qdict_get_try_str(val, "version-id");
    g_assert_nonnull(str);
    g_assert_cmpstr(str, ==, "1");

    str = qdict_get_try_str(val, "variant");
    g_assert_nonnull(str);
    g_assert_cmpstr(str, ==, "Unit test \"'$`\\ and \\\\ etc.");

    str = qdict_get_try_str(val, "variant-id");
    g_assert_nonnull(str);
    g_assert_cmpstr(str, ==, "unit-test");

    QDECREF(ret);
    g_free(env[0]);
    fixture_tear_down(&fixture, NULL);
}

int main(int argc, char **argv)
{
    TestFixture fix;
    int ret;

    setlocale (LC_ALL, "");
    g_test_init(&argc, &argv, NULL);
    fixture_setup(&fix, NULL, NULL);

    g_test_add_data_func("/qga/sync-delimited", &fix, test_qga_sync_delimited);
    g_test_add_data_func("/qga/sync", &fix, test_qga_sync);
    g_test_add_data_func("/qga/ping", &fix, test_qga_ping);
    g_test_add_data_func("/qga/info", &fix, test_qga_info);
    g_test_add_data_func("/qga/network-get-interfaces", &fix,
                         test_qga_network_get_interfaces);
    if (!access("/sys/devices/system/cpu/cpu0", F_OK)) {
        g_test_add_data_func("/qga/get-vcpus", &fix, test_qga_get_vcpus);
    }
    g_test_add_data_func("/qga/get-fsinfo", &fix, test_qga_get_fsinfo);
    g_test_add_data_func("/qga/get-memory-block-info", &fix,
                         test_qga_get_memory_block_info);
    g_test_add_data_func("/qga/get-memory-blocks", &fix,
                         test_qga_get_memory_blocks);
    g_test_add_data_func("/qga/file-ops", &fix, test_qga_file_ops);
    g_test_add_data_func("/qga/file-write-read", &fix, test_qga_file_write_read);
    g_test_add_data_func("/qga/get-time", &fix, test_qga_get_time);
    g_test_add_data_func("/qga/invalid-cmd", &fix, test_qga_invalid_cmd);
    g_test_add_data_func("/qga/invalid-args", &fix, test_qga_invalid_args);
    g_test_add_data_func("/qga/fsfreeze-status", &fix,
                         test_qga_fsfreeze_status);

    g_test_add_data_func("/qga/blacklist", NULL, test_qga_blacklist);
    g_test_add_data_func("/qga/config", NULL, test_qga_config);
    g_test_add_data_func("/qga/guest-exec", &fix, test_qga_guest_exec);
    g_test_add_data_func("/qga/guest-exec-invalid", &fix,
                         test_qga_guest_exec_invalid);
    g_test_add_data_func("/qga/guest-get-osinfo", &fix,
                         test_qga_guest_get_osinfo);

    if (g_getenv("QGA_TEST_SIDE_EFFECTING")) {
        g_test_add_data_func("/qga/fsfreeze-and-thaw", &fix,
                             test_qga_fsfreeze_and_thaw);
        g_test_add_data_func("/qga/set-time", &fix, test_qga_set_time);
        g_test_add_data_func("/qga/fstrim", &fix, test_qga_fstrim);
    }

    ret = g_test_run();

    fixture_tear_down(&fix, NULL);

    return ret;
}
