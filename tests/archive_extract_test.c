#include "archive_extract.h"

#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *path;
    mode_t filetype;
    const char *content;
    const char *symlink_target;
    const char *hardlink_target;
} FixtureEntry;

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode)) {
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

static void
write_fixture (
    const char *archive_path,
    const FixtureEntry *entries,
    gsize count
)
{
    struct archive *writer = archive_write_new ();

    g_assert_nonnull (writer);
    g_assert_cmpint (
        archive_write_set_format_pax_restricted (writer),
        ==,
        ARCHIVE_OK
    );
    g_assert_cmpint (
        archive_write_add_filter_gzip (writer),
        ==,
        ARCHIVE_OK
    );
    g_assert_cmpint (
        archive_write_open_filename (writer, archive_path),
        ==,
        ARCHIVE_OK
    );

    for (gsize i = 0; i < count; i++) {
        const FixtureEntry *fixture = &entries[i];
        struct archive_entry *entry = archive_entry_new ();
        gsize length =
            fixture->content != NULL ? strlen (fixture->content) : 0;

        archive_entry_set_pathname (entry, fixture->path);
        archive_entry_set_filetype (entry, fixture->filetype);
        archive_entry_set_perm (
            entry,
            fixture->filetype == AE_IFDIR ? 0700 : 0600
        );

        if (fixture->symlink_target != NULL) {
            archive_entry_set_symlink (
                entry,
                fixture->symlink_target
            );
        }

        if (fixture->hardlink_target != NULL) {
            archive_entry_set_hardlink (
                entry,
                fixture->hardlink_target
            );
        }

        if (fixture->filetype == AE_IFREG &&
            fixture->hardlink_target == NULL) {
            archive_entry_set_size (entry, (int64_t) length);
        } else {
            archive_entry_set_size (entry, 0);
        }

        g_assert_cmpint (
            archive_write_header (writer, entry),
            ==,
            ARCHIVE_OK
        );

        if (length > 0 &&
            fixture->filetype == AE_IFREG &&
            fixture->hardlink_target == NULL) {
            g_assert_cmpint (
                archive_write_data (
                    writer,
                    fixture->content,
                    length
                ),
                ==,
                (la_ssize_t) length
            );
        }

        archive_entry_free (entry);
    }

    g_assert_cmpint (archive_write_close (writer), ==, ARCHIVE_OK);
    g_assert_cmpint (archive_write_free (writer), ==, ARCHIVE_OK);
}

static char *
new_temp_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-archive-extract-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static AtmArchiveLimits
default_limits (void)
{
    AtmArchiveLimits limits = {
        .max_entries = 100,
        .max_file_bytes = 1024 * 1024,
        .max_total_bytes = 4 * 1024 * 1024
    };

    return limits;
}

static void
test_valid_archive (void)
{
    char *root = new_temp_root ();
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *destination = g_build_filename (
        root,
        "extract",
        NULL
    );
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/CITATION.cff", AE_IFREG,
          "version: 0.1.0\n", NULL, NULL },
        { "repo-sha/model/core.txt", AE_IFREG,
          "core\n", NULL, NULL }
    };
    AtmArchiveLimits limits = default_limits ();
    guint64 extracted_entries = 0;
    guint64 extracted_bytes = 0;
    GError *error = NULL;

    write_fixture (
        archive_path,
        entries,
        G_N_ELEMENTS (entries)
    );

    g_assert_true (
        atm_archive_extract_snapshot (
            archive_path,
            destination,
            &limits,
            &extracted_entries,
            &extracted_bytes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        extracted_entries,
        ==,
        G_N_ELEMENTS (entries)
    );

    char *citation = g_build_filename (
        destination,
        "CITATION.cff",
        NULL
    );
    char *model_file = g_build_filename (
        destination,
        "model",
        "core.txt",
        NULL
    );
    char *citation_contents = NULL;
    char *model_contents = NULL;

    g_assert_true (
        g_file_get_contents (
            citation,
            &citation_contents,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (citation_contents, ==, "version: 0.1.0\n");

    g_assert_true (
        g_file_get_contents (
            model_file,
            &model_contents,
            NULL,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (model_contents, ==, "core\n");
    g_assert_cmpuint (
        extracted_bytes,
        ==,
        strlen ("version: 0.1.0\n") + strlen ("core\n")
    );

    g_free (citation_contents);
    g_free (model_contents);
    g_free (citation);
    g_free (model_file);
    remove_tree_best_effort (root);
    g_free (archive_path);
    g_free (destination);
    g_free (root);
}

static void
assert_archive_rejected (
    const FixtureEntry *entries,
    gsize count,
    AtmArchiveLimits limits,
    AtmArchiveError expected_code
)
{
    char *root = new_temp_root ();
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    char *destination = g_build_filename (
        root,
        "extract",
        NULL
    );
    GError *error = NULL;

    write_fixture (archive_path, entries, count);

    g_assert_false (
        atm_archive_extract_snapshot (
            archive_path,
            destination,
            &limits,
            NULL,
            NULL,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_ARCHIVE_ERROR,
        expected_code
    );
    g_assert_false (g_file_test (destination, G_FILE_TEST_EXISTS));

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (archive_path);
    g_free (destination);
    g_free (root);
}

static void
test_traversal_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/../escape.txt", AE_IFREG,
          "bad", NULL, NULL }
    };
    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_UNSAFE_PATH
    );
}

static void
test_absolute_path_rejected (void)
{
    FixtureEntry entries[] = {
        { "/tmp/escape.txt", AE_IFREG, "bad", NULL, NULL }
    };
    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_UNSAFE_PATH
    );
}

static void
test_symlink_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/link", AE_IFLNK, NULL, "/tmp", NULL }
    };
    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY
    );
}

static void
test_hardlink_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/link", AE_IFREG, NULL, NULL,
          "../outside" }
    };
    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY
    );
}

static void
test_fifo_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/pipe", AE_IFIFO, NULL, NULL, NULL }
    };
    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY
    );
}

static void
test_mixed_prefix_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-one/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-two/file.txt", AE_IFREG, "bad", NULL, NULL }
    };
    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_PREFIX
    );
}

static void
test_entry_limit (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/a.txt", AE_IFREG, "a", NULL, NULL }
    };
    AtmArchiveLimits limits = default_limits ();
    limits.max_entries = 1;

    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        limits,
        ATM_ARCHIVE_ERROR_LIMIT
    );
}

static void
test_file_size_limit (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/a.txt", AE_IFREG, "abcd", NULL, NULL }
    };
    AtmArchiveLimits limits = default_limits ();
    limits.max_file_bytes = 3;

    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        limits,
        ATM_ARCHIVE_ERROR_LIMIT
    );
}

static void
test_total_size_limit (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/a.txt", AE_IFREG, "abc", NULL, NULL },
        { "repo-sha/b.txt", AE_IFREG, "def", NULL, NULL }
    };
    AtmArchiveLimits limits = default_limits ();
    limits.max_total_bytes = 5;

    assert_archive_rejected (
        entries,
        G_N_ELEMENTS (entries),
        limits,
        ATM_ARCHIVE_ERROR_LIMIT
    );
}

static void
test_inspect_valid_archive (void)
{
    char *root = new_temp_root ();
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/CITATION.cff", AE_IFREG,
          "version: 0.1.0\n", NULL, NULL },
        { "repo-sha/model/core.txt", AE_IFREG,
          "core\n", NULL, NULL }
    };
    AtmArchiveLimits limits = default_limits ();
    AtmArchiveInspection inspection;
    GError *error = NULL;

    write_fixture (
        archive_path,
        entries,
        G_N_ELEMENTS (entries)
    );

    g_assert_true (
        atm_archive_inspect_snapshot (
            archive_path,
            &limits,
            &inspection,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        inspection.archive_entries,
        ==,
        3
    );
    g_assert_cmpuint (
        inspection.regular_files,
        ==,
        2
    );
    g_assert_cmpuint (
        inspection.directories,
        ==,
        1
    );
    g_assert_cmpuint (
        inspection.materialized_entries,
        ==,
        4
    );
    g_assert_cmpuint (
        inspection.logical_regular_bytes,
        ==,
        strlen ("version: 0.1.0\n") +
        strlen ("core\n")
    );
    g_assert_cmpuint (
        inspection.largest_regular_file_bytes,
        ==,
        strlen ("version: 0.1.0\n")
    );

    remove_tree_best_effort (root);
    g_free (archive_path);
    g_free (root);
}

static void
test_inspect_explicit_directory_not_double_counted (void)
{
    char *root = new_temp_root ();
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/model/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/model/core.txt", AE_IFREG,
          "core\n", NULL, NULL }
    };
    AtmArchiveLimits limits = default_limits ();
    AtmArchiveInspection inspection;
    GError *error = NULL;

    write_fixture (
        archive_path,
        entries,
        G_N_ELEMENTS (entries)
    );

    g_assert_true (
        atm_archive_inspect_snapshot (
            archive_path,
            &limits,
            &inspection,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        inspection.directories,
        ==,
        1
    );
    g_assert_cmpuint (
        inspection.regular_files,
        ==,
        1
    );
    g_assert_cmpuint (
        inspection.materialized_entries,
        ==,
        3
    );

    remove_tree_best_effort (root);
    g_free (archive_path);
    g_free (root);
}

static void
assert_inspection_rejected (
    const FixtureEntry *entries,
    gsize count,
    AtmArchiveLimits limits,
    AtmArchiveError expected_code
)
{
    char *root = new_temp_root ();
    char *archive_path = g_build_filename (
        root,
        "snapshot.tar.gz",
        NULL
    );
    AtmArchiveInspection inspection;
    GError *error = NULL;

    write_fixture (
        archive_path,
        entries,
        count
    );

    g_assert_false (
        atm_archive_inspect_snapshot (
            archive_path,
            &limits,
            &inspection,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_ARCHIVE_ERROR,
        expected_code
    );

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (archive_path);
    g_free (root);
}

static void
test_inspect_duplicate_file_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/a.txt", AE_IFREG, "a", NULL, NULL },
        { "repo-sha/a.txt", AE_IFREG, "b", NULL, NULL }
    };

    assert_inspection_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_FORMAT
    );
}

static void
test_inspect_symlink_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/link", AE_IFLNK, NULL, "/tmp", NULL }
    };

    assert_inspection_rejected (
        entries,
        G_N_ELEMENTS (entries),
        default_limits (),
        ATM_ARCHIVE_ERROR_UNSUPPORTED_ENTRY
    );
}

static void
test_inspect_total_limit_rejected (void)
{
    FixtureEntry entries[] = {
        { "repo-sha/", AE_IFDIR, NULL, NULL, NULL },
        { "repo-sha/a.txt", AE_IFREG, "abc", NULL, NULL },
        { "repo-sha/b.txt", AE_IFREG, "def", NULL, NULL }
    };
    AtmArchiveLimits limits = default_limits ();
    limits.max_total_bytes = 5;

    assert_inspection_rejected (
        entries,
        G_N_ELEMENTS (entries),
        limits,
        ATM_ARCHIVE_ERROR_LIMIT
    );
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/archive/valid", test_valid_archive);
    g_test_add_func (
        "/archive/traversal-rejected",
        test_traversal_rejected
    );
    g_test_add_func (
        "/archive/absolute-path-rejected",
        test_absolute_path_rejected
    );
    g_test_add_func (
        "/archive/symlink-rejected",
        test_symlink_rejected
    );
    g_test_add_func (
        "/archive/hardlink-rejected",
        test_hardlink_rejected
    );
    g_test_add_func ("/archive/fifo-rejected", test_fifo_rejected);
    g_test_add_func (
        "/archive/mixed-prefix-rejected",
        test_mixed_prefix_rejected
    );
    g_test_add_func ("/archive/entry-limit", test_entry_limit);
    g_test_add_func (
        "/archive/file-size-limit",
        test_file_size_limit
    );
    g_test_add_func (
        "/archive/total-size-limit",
        test_total_size_limit
    );
    g_test_add_func (
        "/archive/inspect-valid",
        test_inspect_valid_archive
    );
    g_test_add_func (
        "/archive/inspect-explicit-directory",
        test_inspect_explicit_directory_not_double_counted
    );
    g_test_add_func (
        "/archive/inspect-duplicate-file-rejected",
        test_inspect_duplicate_file_rejected
    );
    g_test_add_func (
        "/archive/inspect-symlink-rejected",
        test_inspect_symlink_rejected
    );
    g_test_add_func (
        "/archive/inspect-total-limit-rejected",
        test_inspect_total_limit_rejected
    );

    return g_test_run ();
}
