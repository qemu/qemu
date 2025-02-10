/*
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include "libqtest.h"

#define FILE_TEST_FILENAME "migfile"
#define FILE_TEST_OFFSET 0x1000
#define FILE_TEST_MARKER 'X'

typedef struct MigrationTestEnv {
    bool has_kvm;
    bool has_tcg;
    bool has_uffd;
    bool uffd_feature_thread_id;
    bool has_dirty_ring;
    bool is_x86;
    bool full_set;
    const char *arch;
    const char *qemu_src;
    const char *qemu_dst;
    char *tmpfs;
} MigrationTestEnv;

MigrationTestEnv *migration_get_env(void);
int migration_env_clean(MigrationTestEnv *env);

/*
 * A hook that runs after the src and dst QEMUs have been
 * created, but before the migration is started. This can
 * be used to set migration parameters and capabilities.
 *
 * Returns: NULL, or a pointer to opaque state to be
 *          later passed to the TestMigrateEndHook
 */
typedef void * (*TestMigrateStartHook)(QTestState *from,
                                       QTestState *to);

/*
 * A hook that runs after the migration has finished,
 * regardless of whether it succeeded or failed, but
 * before QEMU has terminated (unless it self-terminated
 * due to migration error)
 *
 * @opaque is a pointer to state previously returned
 * by the TestMigrateStartHook if any, or NULL.
 */
typedef void (*TestMigrateEndHook)(QTestState *from,
                                   QTestState *to,
                                   void *opaque);

/*
 * Our goal is to ensure that we run a single full migration
 * iteration, and also dirty memory, ensuring that at least
 * one further iteration is required.
 *
 * We can't directly synchronize with the start of a migration
 * so we have to apply some tricks monitoring memory that is
 * transferred.
 *
 * Initially we set the migration bandwidth to an insanely
 * low value, with tiny max downtime too. This basically
 * guarantees migration will never complete.
 *
 * This will result in a test that is unacceptably slow though,
 * so we can't let the entire migration pass run at this speed.
 * Our intent is to let it run just long enough that we can
 * prove data prior to the marker has been transferred *AND*
 * also prove this transferred data is dirty again.
 *
 * Before migration starts, we write a 64-bit magic marker
 * into a fixed location in the src VM RAM.
 *
 * Then watch dst memory until the marker appears. This is
 * proof that start_address -> MAGIC_OFFSET_BASE has been
 * transferred.
 *
 * Finally we go back to the source and read a byte just
 * before the marker until we see it flip in value. This
 * is proof that start_address -> MAGIC_OFFSET_BASE
 * is now dirty again.
 *
 * IOW, we're guaranteed at least a 2nd migration pass
 * at this point.
 *
 * We can now let migration run at full speed to finish
 * the test
 */
typedef struct {
    /*
     * QTEST_LOG=1 may override this.  When QTEST_LOG=1, we always dump errors
     * unconditionally, because it means the user would like to be verbose.
     */
    bool hide_stderr;
    bool use_shmem;
    /* only launch the target process */
    bool only_target;
    /* Use dirty ring if true; dirty logging otherwise */
    bool use_dirty_ring;
    const char *opts_source;
    const char *opts_target;
    /* suspend the src before migrating to dest. */
    bool suspend_me;
    /* enable OOB QMP capability */
    bool oob;
    /*
     * Format string for the main memory backend, containing one %s where the
     * size is plugged in.  If omitted, "-m %s" is used.
     */
    const char *memory_backend;

    /* Do not connect to target monitor and qtest sockets in qtest_init */
    bool defer_target_connect;
} MigrateStart;

typedef enum PostcopyRecoveryFailStage {
    /*
     * "no failure" must be 0 as it's the default.  OTOH, real failure
     * cases must be >0 to make sure they trigger by a "if" test.
     */
    POSTCOPY_FAIL_NONE = 0,
    POSTCOPY_FAIL_CHANNEL_ESTABLISH,
    POSTCOPY_FAIL_RECOVERY,
    POSTCOPY_FAIL_MAX
} PostcopyRecoveryFailStage;

typedef struct {
    /* Optional: fine tune start parameters */
    MigrateStart start;

    /* Required: the URI for the dst QEMU to listen on */
    const char *listen_uri;

    /*
     * Optional: the URI for the src QEMU to connect to
     * If NULL, then it will query the dst QEMU for its actual
     * listening address and use that as the connect address.
     * This allows for dynamically picking a free TCP port.
     */
    const char *connect_uri;

    /*
     * Optional: JSON-formatted list of src QEMU URIs. If a port is
     * defined as '0' in any QDict key a value of '0' will be
     * automatically converted to the correct destination port.
     */
    const char *connect_channels;

    /* Optional: the cpr migration channel, in JSON or dotted keys format */
    const char *cpr_channel;

    /* Optional: callback to run at start to set migration parameters */
    TestMigrateStartHook start_hook;
    /* Optional: callback to run at finish to cleanup */
    TestMigrateEndHook end_hook;

    /*
     * Optional: normally we expect the migration process to complete.
     *
     * There can be a variety of reasons and stages in which failure
     * can happen during tests.
     *
     * If a failure is expected to happen at time of establishing
     * the connection, then MIG_TEST_FAIL will indicate that the dst
     * QEMU is expected to stay running and accept future migration
     * connections.
     *
     * If a failure is expected to happen while processing the
     * migration stream, then MIG_TEST_FAIL_DEST_QUIT_ERR will indicate
     * that the dst QEMU is expected to quit with non-zero exit status
     */
    enum {
        /* This test should succeed, the default */
        MIG_TEST_SUCCEED = 0,
        /* This test should fail, dest qemu should keep alive */
        MIG_TEST_FAIL,
        /* This test should fail, dest qemu should fail with abnormal status */
        MIG_TEST_FAIL_DEST_QUIT_ERR,
        /* The QMP command for this migration should fail with an error */
        MIG_TEST_QMP_ERROR,
    } result;

    /*
     * Optional: set number of migration passes to wait for, if live==true.
     * If zero, then merely wait for a few MB of dirty data
     */
    unsigned int iterations;

    /*
     * Optional: whether the guest CPUs should be running during a precopy
     * migration test.  We used to always run with live but it took much
     * longer so we reduced live tests to only the ones that have solid
     * reason to be tested live-only.  For each of the new test cases for
     * precopy please provide justifications to use live explicitly (please
     * refer to existing ones with live=true), or use live=off by default.
     */
    bool live;

    /* Postcopy specific fields */
    void *postcopy_data;
    bool postcopy_preempt;
    PostcopyRecoveryFailStage postcopy_recovery_fail_stage;
} MigrateCommon;

void wait_for_serial(const char *side);
void migrate_prepare_for_dirty_mem(QTestState *from);
void migrate_wait_for_dirty_mem(QTestState *from, QTestState *to);
int migrate_start(QTestState **from, QTestState **to, const char *uri,
                  MigrateStart *args);
void migrate_end(QTestState *from, QTestState *to, bool test_dest);

void test_postcopy_common(MigrateCommon *args);
void test_postcopy_recovery_common(MigrateCommon *args);
void test_precopy_common(MigrateCommon *args);
void test_file_common(MigrateCommon *args, bool stop_src);
void *migrate_hook_start_precopy_tcp_multifd_common(QTestState *from,
                                                    QTestState *to,
                                                    const char *method);

typedef struct QTestMigrationState QTestMigrationState;
QTestMigrationState *get_src(void);

#ifdef CONFIG_GNUTLS
void migration_test_add_tls(MigrationTestEnv *env);
#else
static inline void migration_test_add_tls(MigrationTestEnv *env) {};
#endif
void migration_test_add_compression(MigrationTestEnv *env);
void migration_test_add_postcopy(MigrationTestEnv *env);
void migration_test_add_file(MigrationTestEnv *env);
void migration_test_add_precopy(MigrationTestEnv *env);
void migration_test_add_cpr(MigrationTestEnv *env);
void migration_test_add_misc(MigrationTestEnv *env);

#endif /* TEST_FRAMEWORK_H */
