#include "retrieval_index_lifecycle.h"
#include "retrieval_index.h"
#include "coordination_lease.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#include <sys/stat.h>

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) || S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (path, 0, &error);

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (path, name, NULL);
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static char *
new_temp_root (const char *prefix)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (prefix, &error);

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_text (
    const char *root,
    const char *relative,
    const char *contents
)
{
    GError *error = NULL;
    char *path = g_build_filename (root, relative, NULL);
    char *parent = g_path_get_dirname (path);

    g_assert_cmpint (
        g_mkdir_with_parents (parent, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (parent);
    g_free (path);
}

static char *
new_snapshot (void)
{
    char *root = new_temp_root (
        "atm-index-lifecycle-snapshot-XXXXXX"
    );

    write_text (
        root,
        ".atm/repository.json",
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"ewd\",\n"
        "  \"acronym\": \"EWD\",\n"
        "  \"display_name\": \"Empirical World3 Dynamics\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": ["
        "\"CITATION.cff\", \"STATUS.md\", \"model/core.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [\"model\"],\n"
        "    \"evidence\": [\"data\"],\n"
        "    \"tabular\": [\"data\"],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n"
    );

    write_text (
        root,
        "CITATION.cff",
        "cff-version: 1.2.0\n"
        "message: cite this\n"
        "type: software\n"
        "title: Test EWD\n"
        "version: 0.1.0\n"
    );

    write_text (
        root,
        "STATUS.md",
        "# Scientific status\n"
        "## Release status\n"
        "Current baseline.\n"
    );

    write_text (
        root,
        "model/core.json",
        "{\"variables\":["
        "{\"id\":\"food_per_capita\","
        "\"label\":{\"en\":\"Food per capita\"}}"
        "]}\n"
    );

    write_text (
        root,
        "data/series.csv",
        "year,value\n2025,1\n"
    );

    return root;
}

static const char *
snapshot_sha (void)
{
    return "0123456789abcdef0123456789abcdef01234567";
}

typedef struct {
    const char *state_root;
    const char *cache_root;
    const char *snapshot_root;
    GMutex mutex;
    GCond cond;
    gboolean started;
    gboolean finished;
    gboolean ok;
    char *index_path;
    char *version;
    AtmRetrievalEnsureResult result;
    GError *error;
} CoordinatedEnsureThread;

static gpointer
coordinated_ensure_thread_run (
    gpointer user_data
)
{
    CoordinatedEnsureThread *state = user_data;

    g_mutex_lock (&state->mutex);
    state->started = TRUE;
    g_cond_broadcast (&state->cond);
    g_mutex_unlock (&state->mutex);

    state->ok =
        atm_retrieval_index_ensure_for_snapshot_coordinated (
            state->state_root,
            state->cache_root,
            state->snapshot_root,
            "ewd",
            snapshot_sha (),
            &state->index_path,
            &state->version,
            &state->result,
            &state->error
        );

    g_mutex_lock (&state->mutex);
    state->finished = TRUE;
    g_cond_broadcast (&state->cond);
    g_mutex_unlock (&state->mutex);
    return NULL;
}

static void
coordinated_thread_init (
    CoordinatedEnsureThread *state,
    const char *state_root,
    const char *cache_root,
    const char *snapshot_root
)
{
    memset (state, 0, sizeof (*state));
    state->state_root = state_root;
    state->cache_root = cache_root;
    state->snapshot_root = snapshot_root;
    g_mutex_init (&state->mutex);
    g_cond_init (&state->cond);
}

static void
coordinated_thread_clear (
    CoordinatedEnsureThread *state
)
{
    g_clear_error (&state->error);
    g_clear_pointer (&state->index_path, g_free);
    g_clear_pointer (&state->version, g_free);
    g_cond_clear (&state->cond);
    g_mutex_clear (&state->mutex);
}

static char *
single_flight_lock_path (
    const char *state_root
)
{
    char *directory = g_build_filename (
        state_root,
        "retrieval-index-locks",
        "ewd",
        NULL
    );
    char *filename = g_strdup_printf (
        "%s.lock",
        snapshot_sha ()
    );
    char *path;

    g_assert_cmpint (
        g_mkdir_with_parents (
            directory,
            0700
        ),
        ==,
        0
    );

    path = g_build_filename (
        directory,
        filename,
        NULL
    );
    g_free (filename);
    g_free (directory);
    return path;
}

static gint
hold_single_flight_lock (
    const char *state_root
)
{
    char *path = single_flight_lock_path (
        state_root
    );
    gint fd = -1;
    gboolean contended = FALSE;
    gint64 wait_us = 0;
    GError *error = NULL;

    g_assert_true (
        atm_coordination_lease_acquire (
            path,
            FALSE,
            &fd,
            &contended,
            &wait_us,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (contended);
    g_assert_cmpint (fd, >=, 0);

    g_free (path);
    return fd;
}

static void
wait_until_thread_started (
    CoordinatedEnsureThread *state
)
{
    gint64 deadline =
        g_get_monotonic_time () +
        (2 * G_TIME_SPAN_SECOND);

    g_mutex_lock (&state->mutex);
    while (!state->started) {
        if (!g_cond_wait_until (
                &state->cond,
                &state->mutex,
                deadline
            )) {
            break;
        }
    }
    g_assert_true (state->started);
    g_mutex_unlock (&state->mutex);
}

static gboolean
thread_finishes_before (
    CoordinatedEnsureThread *state,
    gint64 timeout_us
)
{
    gint64 deadline =
        g_get_monotonic_time () +
        timeout_us;
    gboolean finished;

    g_mutex_lock (&state->mutex);
    while (!state->finished) {
        if (!g_cond_wait_until (
                &state->cond,
                &state->mutex,
                deadline
            )) {
            break;
        }
    }
    finished = state->finished;
    g_mutex_unlock (&state->mutex);
    return finished;
}

static void
test_missing_index_is_built_and_then_reused (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );
    g_assert_cmpstr (version, ==, "0.1.0");
    g_assert_true (
        g_file_test (index_path, G_FILE_TEST_IS_REGULAR)
    );

    char *first_path = g_strdup (index_path);
    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REUSED
    );
    g_assert_cmpstr (index_path, ==, first_path);
    g_assert_cmpstr (version, ==, "0.1.0");

    g_free (first_path);
    g_free (index_path);
    g_free (version);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_deleted_cache_is_rebuilt_from_snapshot (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (g_remove (index_path), ==, 0);
    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );
    g_assert_true (
        g_file_test (index_path, G_FILE_TEST_IS_REGULAR)
    );

    g_free (index_path);
    g_free (version);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_old_schema_index_is_rebuilt (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    sqlite3 *db = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READWRITE,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_exec (
            db,
            "PRAGMA user_version = 999;",
            NULL,
            NULL,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );

    g_assert_true (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &error
        )
    );
    g_assert_no_error (error);

    g_free (index_path);
    g_free (version);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_corrupt_index_is_rebuilt (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = atm_retrieval_index_path (
        cache_root,
        "ewd",
        snapshot_sha ()
    );
    char *parent = g_path_get_dirname (index_path);
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (parent, 0700),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            index_path,
            "not sqlite",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_clear_pointer (&index_path, g_free);

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );

    g_assert_true (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &error
        )
    );
    g_assert_no_error (error);

    g_free (version);
    g_free (parent);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_non_regular_cache_path_is_not_replaced (void)
{
    char *cache_root = new_temp_root (
        "atm-index-lifecycle-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = atm_retrieval_index_path (
        cache_root,
        "ewd",
        snapshot_sha ()
    );
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (index_path, 0700),
        ==,
        0
    );
    g_clear_pointer (&index_path, g_free);

    g_assert_false (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_LIFECYCLE_ERROR,
        ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE
    );
    g_assert_null (index_path);
    g_assert_null (version);

    g_clear_error (&error);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_coordinated_waiter_joins_without_touching_staging (void)
{
    char *state_root = new_temp_root (
        "atm-index-single-flight-state-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-index-single-flight-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *staging_path =
        atm_retrieval_index_staging_path (
            cache_root,
            "ewd",
            snapshot_sha ()
        );
    gint held_fd = hold_single_flight_lock (
        state_root
    );
    CoordinatedEnsureThread call;
    GThread *thread;

    atm_retrieval_index_single_flight_test_reset ();
    coordinated_thread_init (
        &call,
        state_root,
        cache_root,
        snapshot_root
    );

    thread = g_thread_new (
        "index-waiter",
        coordinated_ensure_thread_run,
        &call
    );
    wait_until_thread_started (&call);

    g_assert_false (
        thread_finishes_before (
            &call,
            150 * G_TIME_SPAN_MILLISECOND
        )
    );
    g_assert_false (
        g_file_test (
            staging_path,
            G_FILE_TEST_EXISTS
        )
    );

    atm_coordination_lease_release (
        held_fd
    );
    g_thread_join (thread);

    g_assert_true (call.ok);
    g_assert_no_error (call.error);
    g_assert_cmpint (
        call.result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );
    g_assert_cmpint (
        atm_retrieval_index_single_flight_test_build_entries (),
        ==,
        1
    );

    coordinated_thread_clear (&call);
    g_free (staging_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
    remove_tree_best_effort (state_root);
    g_free (state_root);
}

static void
test_coordinated_two_callers_build_once (void)
{
    char *state_root = new_temp_root (
        "atm-index-single-flight-state-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-index-single-flight-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    CoordinatedEnsureThread first;
    CoordinatedEnsureThread second;
    GThread *first_thread;
    GThread *second_thread;

    atm_retrieval_index_single_flight_test_reset ();
    coordinated_thread_init (
        &first,
        state_root,
        cache_root,
        snapshot_root
    );
    coordinated_thread_init (
        &second,
        state_root,
        cache_root,
        snapshot_root
    );

    first_thread = g_thread_new (
        "index-builder-a",
        coordinated_ensure_thread_run,
        &first
    );
    second_thread = g_thread_new (
        "index-builder-b",
        coordinated_ensure_thread_run,
        &second
    );

    g_thread_join (first_thread);
    g_thread_join (second_thread);

    g_assert_true (first.ok);
    g_assert_true (second.ok);
    g_assert_no_error (first.error);
    g_assert_no_error (second.error);
    g_assert_cmpint (
        atm_retrieval_index_single_flight_test_build_entries (),
        ==,
        1
    );
    g_assert_true (
        (first.result == ATM_RETRIEVAL_ENSURE_REBUILT &&
         second.result == ATM_RETRIEVAL_ENSURE_REUSED) ||
        (first.result == ATM_RETRIEVAL_ENSURE_REUSED &&
         second.result == ATM_RETRIEVAL_ENSURE_REBUILT)
    );
    g_assert_cmpstr (
        first.index_path,
        ==,
        second.index_path
    );

    coordinated_thread_clear (&first);
    coordinated_thread_clear (&second);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
    remove_tree_best_effort (state_root);
    g_free (state_root);
}

static void
test_coordinated_abandoned_staging_is_recovered (void)
{
    char *state_root = new_temp_root (
        "atm-index-single-flight-state-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-index-single-flight-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *staging_path =
        atm_retrieval_index_staging_path (
            cache_root,
            "ewd",
            snapshot_sha ()
        );
    char *parent = g_path_get_dirname (
        staging_path
    );
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            parent,
            0700
        ),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            staging_path,
            "abandoned partial sqlite",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    atm_retrieval_index_single_flight_test_reset ();

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot_coordinated (
            state_root,
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );
    g_assert_cmpint (
        atm_retrieval_index_single_flight_test_build_entries (),
        ==,
        1
    );
    g_assert_false (
        g_file_test (
            staging_path,
            G_FILE_TEST_EXISTS
        )
    );
    g_assert_true (
        g_file_test (
            index_path,
            G_FILE_TEST_IS_REGULAR
        )
    );

    g_free (version);
    g_free (index_path);
    g_free (parent);
    g_free (staging_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
    remove_tree_best_effort (state_root);
    g_free (state_root);
}

static void
test_coordinated_non_regular_staging_is_refused (void)
{
    char *state_root = new_temp_root (
        "atm-index-single-flight-state-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-index-single-flight-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *staging_path =
        atm_retrieval_index_staging_path (
            cache_root,
            "ewd",
            snapshot_sha ()
        );
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir_with_parents (
            staging_path,
            0700
        ),
        ==,
        0
    );

    g_assert_false (
        atm_retrieval_index_ensure_for_snapshot_coordinated (
            state_root,
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_LIFECYCLE_ERROR,
        ATM_RETRIEVAL_LIFECYCLE_ERROR_CACHE
    );
    g_assert_true (
        g_file_test (
            staging_path,
            G_FILE_TEST_IS_DIR
        )
    );
    g_assert_null (index_path);
    g_assert_null (version);

    g_clear_error (&error);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
    remove_tree_best_effort (state_root);
    g_free (state_root);
}

static void
test_coordinated_valid_fast_path_does_not_wait (void)
{
    char *state_root = new_temp_root (
        "atm-index-single-flight-state-XXXXXX"
    );
    char *cache_root = new_temp_root (
        "atm-index-single-flight-cache-XXXXXX"
    );
    char *snapshot_root = new_snapshot ();
    char *index_path = NULL;
    char *version = NULL;
    AtmRetrievalEnsureResult result;
    GError *error = NULL;
    gint held_fd;
    CoordinatedEnsureThread call;
    GThread *thread;
    gboolean finished_before_release;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            "ewd",
            snapshot_sha (),
            &index_path,
            &version,
            &result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );
    g_clear_pointer (&index_path, g_free);
    g_clear_pointer (&version, g_free);

    held_fd = hold_single_flight_lock (
        state_root
    );

    atm_retrieval_index_single_flight_test_reset ();
    coordinated_thread_init (
        &call,
        state_root,
        cache_root,
        snapshot_root
    );
    thread = g_thread_new (
        "index-fast-path",
        coordinated_ensure_thread_run,
        &call
    );
    wait_until_thread_started (&call);

    finished_before_release =
        thread_finishes_before (
            &call,
            1 * G_TIME_SPAN_SECOND
        );

    atm_coordination_lease_release (
        held_fd
    );
    g_thread_join (thread);

    g_assert_true (finished_before_release);
    g_assert_true (call.ok);
    g_assert_no_error (call.error);
    g_assert_cmpint (
        call.result,
        ==,
        ATM_RETRIEVAL_ENSURE_REUSED
    );
    g_assert_cmpint (
        atm_retrieval_index_single_flight_test_build_entries (),
        ==,
        0
    );

    coordinated_thread_clear (&call);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
    remove_tree_best_effort (state_root);
    g_free (state_root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-lifecycle/missing-build-reuse",
        test_missing_index_is_built_and_then_reused
    );
    g_test_add_func (
        "/retrieval-lifecycle/cache-loss-rebuild",
        test_deleted_cache_is_rebuilt_from_snapshot
    );
    g_test_add_func (
        "/retrieval-lifecycle/old-schema-rebuild",
        test_old_schema_index_is_rebuilt
    );
    g_test_add_func (
        "/retrieval-lifecycle/corrupt-index-rebuild",
        test_corrupt_index_is_rebuilt
    );
    g_test_add_func (
        "/retrieval-lifecycle/non-regular-cache-refused",
        test_non_regular_cache_path_is_not_replaced
    );
    g_test_add_func (
        "/retrieval-lifecycle/coordinated-waiter-joins",
        test_coordinated_waiter_joins_without_touching_staging
    );
    g_test_add_func (
        "/retrieval-lifecycle/coordinated-two-callers-one-build",
        test_coordinated_two_callers_build_once
    );
    g_test_add_func (
        "/retrieval-lifecycle/coordinated-stale-staging-recovery",
        test_coordinated_abandoned_staging_is_recovered
    );
    g_test_add_func (
        "/retrieval-lifecycle/coordinated-malicious-staging-refused",
        test_coordinated_non_regular_staging_is_refused
    );
    g_test_add_func (
        "/retrieval-lifecycle/coordinated-valid-fast-path",
        test_coordinated_valid_fast_path_does_not_wait
    );

    return g_test_run ();
}
