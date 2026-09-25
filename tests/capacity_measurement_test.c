#include "capacity_measurement.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

static void
remove_tree_best_effort (
    const char *path
)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (
        path,
        0,
        &error
    );

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static char *
new_temp_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-capacity-measurement-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_text (
    const char *path,
    const char *contents
)
{
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);
}

static void
test_filesystem_measurement_uses_existing_ancestor (void)
{
    char *root = new_temp_root ();
    char *missing = g_build_filename (
        root,
        "not-created",
        "child",
        NULL
    );
    AtmFilesystemCapacityMeasurement root_measurement;
    AtmFilesystemCapacityMeasurement missing_measurement;
    GError *error = NULL;

    g_assert_true (
        atm_capacity_measure_filesystem (
            root,
            &root_measurement,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        root_measurement.fragment_size,
        >,
        0
    );
    g_assert_cmpuint (
        root_measurement.available_bytes,
        >,
        0
    );

    g_assert_true (
        atm_capacity_measure_filesystem (
            missing,
            &missing_measurement,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        missing_measurement.device_id,
        ==,
        root_measurement.device_id
    );
    g_assert_cmpuint (
        missing_measurement.fragment_size,
        ==,
        root_measurement.fragment_size
    );
    g_assert_cmpint (
        missing_measurement.inode_budget_known,
        ==,
        root_measurement.inode_budget_known
    );

    g_free (missing);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_regular_file_measurement_separates_logical_and_allocated (void)
{
    char *root = new_temp_root ();
    char *path = g_build_filename (
        root,
        "sample.bin",
        NULL
    );
    AtmFileCapacityMeasurement measurement;
    GError *error = NULL;

    write_text (path, "12345");

    g_assert_true (
        atm_capacity_measure_regular_file (
            path,
            &measurement,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        measurement.logical_bytes,
        ==,
        5
    );
    g_assert_cmpuint (
        measurement.allocated_bytes % 512,
        ==,
        0
    );

    g_free (path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_tree_measurement_counts_entries_and_regular_bytes (void)
{
    char *root = new_temp_root ();
    char *file_a = g_build_filename (
        root,
        "a.txt",
        NULL
    );
    char *subdir = g_build_filename (
        root,
        "sub",
        NULL
    );
    char *file_b = g_build_filename (
        subdir,
        "b.txt",
        NULL
    );
    AtmTreeCapacityMeasurement measurement;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir (subdir, 0700),
        ==,
        0
    );
    write_text (file_a, "abc");
    write_text (file_b, "12345");

    g_assert_true (
        atm_capacity_measure_tree (
            root,
            &measurement,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        measurement.logical_regular_bytes,
        ==,
        8
    );
    g_assert_cmpuint (
        measurement.entries,
        ==,
        3
    );
    g_assert_cmpuint (
        measurement.regular_files,
        ==,
        2
    );
    g_assert_cmpuint (
        measurement.directories,
        ==,
        1
    );
    g_assert_cmpuint (
        measurement.allocated_tree_bytes % 512,
        ==,
        0
    );

    g_free (file_b);
    g_free (subdir);
    g_free (file_a);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_tree_measurement_refuses_symlink (void)
{
    char *root = new_temp_root ();
    char *target = g_build_filename (
        root,
        "target.txt",
        NULL
    );
    char *link_path = g_build_filename (
        root,
        "link.txt",
        NULL
    );
    AtmTreeCapacityMeasurement measurement;
    GError *error = NULL;

    write_text (target, "data");
    g_assert_cmpint (
        symlink ("target.txt", link_path),
        ==,
        0
    );

    g_assert_false (
        atm_capacity_measure_tree (
            root,
            &measurement,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CAPACITY_MEASUREMENT_ERROR,
        ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT
    );

    g_clear_error (&error);
    g_free (link_path);
    g_free (target);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_filesystem_measurement_refuses_symlink_anchor (void)
{
    char *root = new_temp_root ();
    char *target = g_build_filename (
        root,
        "target",
        NULL
    );
    char *link_path = g_build_filename (
        root,
        "link",
        NULL
    );
    AtmFilesystemCapacityMeasurement measurement;
    GError *error = NULL;

    g_assert_cmpint (
        g_mkdir (target, 0700),
        ==,
        0
    );
    g_assert_cmpint (
        symlink ("target", link_path),
        ==,
        0
    );

    g_assert_false (
        atm_capacity_measure_filesystem (
            link_path,
            &measurement,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CAPACITY_MEASUREMENT_ERROR,
        ATM_CAPACITY_MEASUREMENT_ERROR_INVALID_OBJECT
    );

    g_clear_error (&error);
    g_free (link_path);
    g_free (target);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/capacity/filesystem-existing-ancestor",
        test_filesystem_measurement_uses_existing_ancestor
    );
    g_test_add_func (
        "/capacity/file-logical-vs-allocated",
        test_regular_file_measurement_separates_logical_and_allocated
    );
    g_test_add_func (
        "/capacity/tree-counts",
        test_tree_measurement_counts_entries_and_regular_bytes
    );
    g_test_add_func (
        "/capacity/tree-refuses-symlink",
        test_tree_measurement_refuses_symlink
    );
    g_test_add_func (
        "/capacity/filesystem-refuses-symlink",
        test_filesystem_measurement_refuses_symlink_anchor
    );

    return g_test_run ();
}
