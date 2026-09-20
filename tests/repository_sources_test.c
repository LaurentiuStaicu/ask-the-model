#include "repository_sources.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>
#include <unistd.h>

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
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-source-catalog-test-XXXXXX",
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);

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
        "  \"required_paths\": [\"STATUS.md\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [\"src\"],\n"
        "    \"evidence\": [\"data\"],\n"
        "    \"tabular\": [\"data\"],\n"
        "    \"implementation\": [\"src\"],\n"
        "    \"exclude\": [\"data/raw\"]\n"
        "  }\n"
        "}\n"
    );

    write_text (root, "STATUS.md", "# Status\n");
    write_text (root, "README.md", "# Not selected\n");
    write_text (root, "src/model.py", "print('model')\n");
    write_text (root, "data/z.json", "{\"z\":1}\n");
    write_text (root, "data/a.csv", "year,value\n2025,1\n");
    write_text (root, "data/raw/secret.csv", "ignore\n");

    return root;
}

static AtmSourceRecord *
record_at (AtmSourceCatalog *catalog, guint index)
{
    return g_ptr_array_index (catalog->files, index);
}

static void
test_catalog_roles_hashes_and_order (void)
{
    char *root = new_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (catalog);
    g_assert_nonnull (catalog->manifest_sha256);
    g_assert_cmpuint (
        strlen (catalog->manifest_sha256),
        ==,
        64
    );

    g_assert_cmpuint (catalog->files->len, ==, 4);

    AtmSourceRecord *r0 = record_at (catalog, 0);
    AtmSourceRecord *r1 = record_at (catalog, 1);
    AtmSourceRecord *r2 = record_at (catalog, 2);
    AtmSourceRecord *r3 = record_at (catalog, 3);

    g_assert_cmpstr (r0->path, ==, "STATUS.md");
    g_assert_cmpstr (r1->path, ==, "data/a.csv");
    g_assert_cmpstr (r2->path, ==, "data/z.json");
    g_assert_cmpstr (r3->path, ==, "src/model.py");

    g_assert_cmpuint (
        r0->roles,
        ==,
        ATM_SOURCE_ROLE_CANONICAL
    );
    g_assert_cmpuint (
        r1->roles,
        ==,
        ATM_SOURCE_ROLE_EVIDENCE |
        ATM_SOURCE_ROLE_TABULAR
    );
    g_assert_cmpuint (
        r2->roles,
        ==,
        ATM_SOURCE_ROLE_EVIDENCE |
        ATM_SOURCE_ROLE_TABULAR
    );
    g_assert_cmpuint (
        r3->roles,
        ==,
        ATM_SOURCE_ROLE_STRUCTURAL |
        ATM_SOURCE_ROLE_IMPLEMENTATION
    );

    g_assert_cmpstr (r1->media_type, ==, "text/csv");
    g_assert_cmpstr (r2->media_type, ==, "application/json");
    g_assert_cmpstr (r3->media_type, ==, "text/x-python");

    for (guint i = 0; i < catalog->files->len; i++) {
        AtmSourceRecord *record = record_at (catalog, i);
        g_assert_nonnull (record->sha256);
        g_assert_cmpuint (strlen (record->sha256), ==, 64);
        g_assert_cmpuint (record->byte_size, >, 0);
        g_assert_false (
            g_str_has_prefix (record->path, "data/raw/")
        );
    }

    g_assert_cmpstr (
        atm_source_role_name (ATM_SOURCE_ROLE_CANONICAL),
        ==,
        "canonical"
    );
    g_assert_cmpstr (
        atm_source_role_name (ATM_SOURCE_ROLE_TABULAR),
        ==,
        "tabular"
    );

    atm_source_catalog_free (catalog);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_wrong_repository_identity_rejected (void)
{
    char *root = new_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_repository_source_catalog_build (
            root,
            "rmd",
            &catalog,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SOURCE_CATALOG_ERROR,
        ATM_SOURCE_CATALOG_ERROR_IDENTITY
    );
    g_assert_null (catalog);

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_symlink_rejected (void)
{
    char *root = new_snapshot ();
    char *link_path = g_build_filename (
        root,
        "data",
        "link.csv",
        NULL
    );
    AtmSourceCatalog *catalog = NULL;
    GError *error = NULL;

    g_assert_cmpint (
        symlink ("/tmp", link_path),
        ==,
        0
    );

    g_assert_false (
        atm_repository_source_catalog_build (
            root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_SOURCE_CATALOG_ERROR,
        ATM_SOURCE_CATALOG_ERROR_UNSAFE_ENTRY
    );
    g_assert_null (catalog);

    g_clear_error (&error);
    g_free (link_path);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/source-catalog/roles-hashes-order",
        test_catalog_roles_hashes_and_order
    );
    g_test_add_func (
        "/source-catalog/wrong-identity",
        test_wrong_repository_identity_rejected
    );
    g_test_add_func (
        "/source-catalog/symlink-rejected",
        test_symlink_rejected
    );

    return g_test_run ();
}
