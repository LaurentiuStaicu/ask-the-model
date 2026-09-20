#include "retrieval_index.h"
#include "markdown_sections.h"
#include "csv_table.h"
#include "structured_json.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

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
new_cache_root (void)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        "atm-retrieval-index-test-XXXXXX",
        &error
    );

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
new_source_snapshot (void)
{
    char *root = new_cache_root ();

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
        "STATUS.md",
        "# Scientific status\n"
        "## Release status\n"
        "Stare curentă verificată.\n"
        "## Boundary\n"
        "Model core only.\n"
    );
    write_text (
        root,
        "model/core.json",
        "{"
        "\"variables\":["
            "{\"id\":\"food_per_capita\","
             "\"label\":{\"en\":\"Food per capita\"}},"
            "{\"id\":\"population\","
             "\"label\":{\"en\":\"Population\"}}"
        "],"
        "\"links\":[{"
            "\"id\":\"LINK.FOOD.POP\","
            "\"source\":\"food_per_capita\","
            "\"target\":\"population\","
            "\"relation_type\":\"INFLUENCE\""
        "}],"
        "\"loops\":[{"
            "\"id\":\"government_refinancing_interest_loop\","
            "\"label\":{\"en\":\"Government refinancing loop\"},"
            "\"path\":[{"
                "\"from\":\"food_per_capita\","
                "\"to\":\"population\","
                "\"sign\":\"+\""
            "}]"
        "}]"
        "}\n"
    );
    write_text (root, "data/series.csv", "year,value\n2025,1\n");

    return root;
}

static int
query_int (
    sqlite3 *db,
    const char *sql
)
{
    sqlite3_stmt *statement = NULL;
    int value;

    g_assert_cmpint (
        sqlite3_prepare_v2 (
            db,
            sql,
            -1,
            &statement,
            NULL
        ),
        ==,
        SQLITE_OK
    );
    g_assert_cmpint (
        sqlite3_step (statement),
        ==,
        SQLITE_ROW
    );

    value = sqlite3_column_int (statement, 0);
    sqlite3_finalize (statement);
    return value;
}

static AtmRetrievalIndexMetadata
valid_metadata (void)
{
    AtmRetrievalIndexMetadata metadata = {
        .repository_id = "ewd",
        .repository_version = "0.1.0",
        .snapshot_sha =
            "0123456789abcdef0123456789abcdef01234567",
        .manifest_schema_version = 1,
        .manifest_sha256 =
            "0123456789abcdef0123456789abcdef"
            "0123456789abcdef0123456789abcdef",
        .created_at_utc = "2026-09-20T00:00:00Z"
    };

    return metadata;
}

static void
test_create_validate_and_refuse_overwrite (void)
{
    char *root = new_cache_root ();
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (index_path);
    g_assert_true (
        g_file_test (index_path, G_FILE_TEST_IS_REGULAR)
    );

    char *expected = atm_retrieval_index_path (
        root,
        metadata.repository_id,
        metadata.snapshot_sha
    );
    char *staging = atm_retrieval_index_staging_path (
        root,
        metadata.repository_id,
        metadata.snapshot_sha
    );

    g_assert_cmpstr (index_path, ==, expected);
    g_assert_false (g_file_test (staging, G_FILE_TEST_EXISTS));

    g_assert_true (
        atm_retrieval_index_validate_identity (
            index_path,
            metadata.repository_id,
            metadata.snapshot_sha,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_retrieval_index_validate_identity (
            index_path,
            metadata.repository_id,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY
    );
    g_clear_error (&error);

    char *second_path = NULL;
    g_assert_false (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &second_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_EXISTS
    );
    g_assert_null (second_path);

    g_clear_error (&error);
    g_free (staging);
    g_free (expected);
    g_free (index_path);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_source_catalog_is_committed_before_promotion (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    sqlite3 *db = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (catalog);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_sources (
            cache_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (index_path);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM source_files;"
        ),
        ==,
        3
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM source_roles;"
        ),
        ==,
        4
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM source_roles "
            "WHERE role = 'tabular';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM source_files "
            "WHERE logical_source_id = "
            "'ewd:file:data/series.csv' "
            "AND length(sha256) = 64;"
        ),
        ==,
        1
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_markdown_sections_and_fts_are_committed (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    sqlite3 *db = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_documents (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (index_path);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM document_sections;"
        ),
        ==,
        2
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM document_sections "
            "WHERE heading_path = "
            "'Scientific status > Release status' "
            "AND locator = 'lines:2-3' "
            "AND logical_source_id = "
            "'ewd:section:STATUS.md:lines:2-3';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM search_fts "
            "WHERE search_fts MATCH 'curenta';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM search_fts "
            "WHERE evidence_kind = 'section';"
        ),
        ==,
        2
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_invalid_markdown_never_promotes (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    char *status_path = g_build_filename (
        snapshot_root,
        "STATUS.md",
        NULL
    );
    const char invalid[] = { (char) 0xff };
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            status_path,
            invalid,
            1,
            &error
        )
    );
    g_assert_no_error (error);

    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_false (
        atm_retrieval_index_create_with_documents (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MARKDOWN_ERROR,
        ATM_MARKDOWN_ERROR_ENCODING
    );
    g_assert_null (index_path);

    char *final_path = atm_retrieval_index_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );
    char *staging_path = atm_retrieval_index_staging_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );

    g_assert_false (
        g_file_test (final_path, G_FILE_TEST_EXISTS)
    );
    g_assert_false (
        g_file_test (staging_path, G_FILE_TEST_EXISTS)
    );

    g_free (staging_path);
    g_free (final_path);
    g_clear_error (&error);
    atm_source_catalog_free (catalog);
    g_free (status_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_csv_datasets_rows_and_fts_are_committed (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    sqlite3 *db = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (index_path);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM datasets "
            "WHERE logical_source_id = "
            "'ewd:dataset:data/series.csv' "
            "AND locator = 'file:data/series.csv';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM datasets "
            "WHERE metadata_json LIKE "
            "'%\"columns\":[\"year\",\"value\"]%' "
            "AND metadata_json LIKE '%\"row_count\":1%';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM dataset_rows "
            "WHERE ordinal = 0 "
            "AND row_key = '2025' "
            "AND locator = 'lines:2-2' "
            "AND payload_json LIKE '%\"year\":\"2025\"%' "
            "AND payload_json LIKE '%\"value\":\"1\"%';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM search_fts "
            "WHERE evidence_kind = 'dataset_row' "
            "AND logical_source_id = "
            "'ewd:dataset-row:data/series.csv:0' "
            "AND search_fts MATCH '2025';"
        ),
        ==,
        1
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_invalid_csv_never_promotes_content_index (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    char *csv_path = g_build_filename (
        snapshot_root,
        "data",
        "series.csv",
        NULL
    );
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            csv_path,
            "year,value\n2025\n",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_false (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CSV_ERROR,
        ATM_CSV_ERROR_SHAPE
    );
    g_assert_null (index_path);

    char *final_path = atm_retrieval_index_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );
    char *staging_path = atm_retrieval_index_staging_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );

    g_assert_false (
        g_file_test (final_path, G_FILE_TEST_EXISTS)
    );
    g_assert_false (
        g_file_test (staging_path, G_FILE_TEST_EXISTS)
    );

    g_free (staging_path);
    g_free (final_path);
    g_clear_error (&error);
    atm_source_catalog_free (catalog);
    g_free (csv_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_structured_json_entities_relations_and_fts_are_committed (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    sqlite3 *db = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (index_path);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM structured_entities;"
        ),
        ==,
        3
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM structured_relations;"
        ),
        ==,
        2
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM structured_entities "
            "WHERE native_id = 'food_per_capita' "
            "AND entity_type = 'variable' "
            "AND logical_source_id = "
            "'ewd:entity:variable:food_per_capita' "
            "AND locator = 'json:/variables/0';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM structured_relations "
            "WHERE native_id = 'LINK.FOOD.POP' "
            "AND relation_type = 'INFLUENCE' "
            "AND logical_source_id = "
            "'ewd:entity:relation:LINK.FOOD.POP' "
            "AND from_logical_source_id = "
            "'ewd:entity:variable:food_per_capita' "
            "AND to_logical_source_id = "
            "'ewd:entity:variable:population';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM structured_relations "
            "WHERE native_id IS NULL "
            "AND relation_type = 'path_edge' "
            "AND locator = 'json:/loops/0/path/0';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM search_fts "
            "WHERE evidence_kind = 'entity' "
            "AND search_fts MATCH 'refinancing';"
        ),
        ==,
        1
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM search_fts "
            "WHERE evidence_kind = 'relation' "
            "AND search_fts MATCH 'LINK';"
        ),
        ==,
        1
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_ambiguous_native_entity_does_not_create_false_relation_link (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    sqlite3 *db = NULL;
    GError *error = NULL;

    write_text (
        snapshot_root,
        "model/duplicate.json",
        "[{\"id\":\"food_per_capita\","
        "\"label\":\"Duplicate\"}]\n"
    );

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpint (
        sqlite3_open_v2 (
            index_path,
            &db,
            SQLITE_OPEN_READONLY,
            NULL
        ),
        ==,
        SQLITE_OK
    );

    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM structured_entities "
            "WHERE native_id = 'food_per_capita';"
        ),
        ==,
        2
    );
    g_assert_cmpint (
        query_int (
            db,
            "SELECT count(*) FROM structured_relations "
            "WHERE native_id = 'LINK.FOOD.POP' "
            "AND from_logical_source_id IS NULL "
            "AND to_logical_source_id = "
            "'ewd:entity:variable:population';"
        ),
        ==,
        1
    );

    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_invalid_structured_json_never_promotes_content_index (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    char *json_path = g_build_filename (
        snapshot_root,
        "model",
        "core.json",
        NULL
    );
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        g_file_set_contents (
            json_path,
            "{\"id\":",
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_false (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STRUCTURED_JSON_ERROR,
        ATM_STRUCTURED_JSON_ERROR_PARSE
    );
    g_assert_null (index_path);

    char *final_path = atm_retrieval_index_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );
    char *staging_path = atm_retrieval_index_staging_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );

    g_assert_false (
        g_file_test (final_path, G_FILE_TEST_EXISTS)
    );
    g_assert_false (
        g_file_test (staging_path, G_FILE_TEST_EXISTS)
    );

    g_free (staging_path);
    g_free (final_path);
    g_clear_error (&error);
    atm_source_catalog_free (catalog);
    g_free (json_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_final_index_matches_pinned_snapshot_sources (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            metadata.snapshot_sha,
            &error
        )
    );
    g_assert_no_error (error);

    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_snapshot_file_hash_mismatch_is_detected (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);

    write_text (
        snapshot_root,
        "STATUS.md",
        "# Scientific status\n"
        "Modified after indexing.\n"
    );

    g_assert_false (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            metadata.snapshot_sha,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY
    );

    g_clear_error (&error);
    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_manifest_byte_change_is_detected (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    char *manifest_path = NULL;
    char *manifest_contents = NULL;
    gsize manifest_length = 0;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);

    manifest_path = g_build_filename (
        snapshot_root,
        ".atm",
        "repository.json",
        NULL
    );
    g_assert_true (
        g_file_get_contents (
            manifest_path,
            &manifest_contents,
            &manifest_length,
            &error
        )
    );
    g_assert_no_error (error);

    char *changed = g_strconcat (
        manifest_contents,
        "\n",
        NULL
    );
    g_assert_true (
        g_file_set_contents (
            manifest_path,
            changed,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            metadata.snapshot_sha,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY
    );

    g_clear_error (&error);
    g_free (changed);
    g_free (manifest_contents);
    g_free (manifest_path);
    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_index_source_role_tamper_is_detected (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    sqlite3 *db = NULL;
    char *sqlite_error = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 = catalog->manifest_sha256;

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
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
            "PRAGMA foreign_keys = ON;"
            "DELETE FROM source_roles "
            "WHERE source_id = ("
                "SELECT id FROM source_files "
                "WHERE path = 'data/series.csv'"
            ") AND role = 'tabular';",
            NULL,
            NULL,
            &sqlite_error
        ),
        ==,
        SQLITE_OK
    );
    sqlite3_free (sqlite_error);
    sqlite_error = NULL;
    g_assert_cmpint (sqlite3_close (db), ==, SQLITE_OK);
    db = NULL;

    g_assert_false (
        atm_retrieval_index_validate_snapshot_sources (
            index_path,
            snapshot_root,
            "ewd",
            metadata.snapshot_sha,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY
    );

    g_clear_error (&error);
    atm_source_catalog_free (catalog);
    g_free (index_path);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_manifest_hash_mismatch_never_promotes (void)
{
    char *cache_root = new_cache_root ();
    char *snapshot_root = new_source_snapshot ();
    AtmSourceCatalog *catalog = NULL;
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            "ewd",
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    metadata.manifest_sha256 =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    g_assert_false (
        atm_retrieval_index_create_with_sources (
            cache_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INTEGRITY
    );
    g_assert_null (index_path);

    char *final_path = atm_retrieval_index_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );
    char *staging_path = atm_retrieval_index_staging_path (
        cache_root,
        metadata.repository_id,
        metadata.snapshot_sha
    );

    g_assert_false (
        g_file_test (final_path, G_FILE_TEST_EXISTS)
    );
    g_assert_false (
        g_file_test (staging_path, G_FILE_TEST_EXISTS)
    );

    g_free (staging_path);
    g_free (final_path);
    g_clear_error (&error);
    atm_source_catalog_free (catalog);
    remove_tree_best_effort (snapshot_root);
    g_free (snapshot_root);
    remove_tree_best_effort (cache_root);
    g_free (cache_root);
}

static void
test_invalid_identity_is_rejected_before_creation (void)
{
    char *root = new_cache_root ();
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    metadata.repository_id = "other";

    g_assert_false (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INVALID_ID
    );
    g_assert_null (index_path);

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (root);
}

static void
test_invalid_manifest_hash_is_rejected (void)
{
    char *root = new_cache_root ();
    AtmRetrievalIndexMetadata metadata = valid_metadata ();
    char *index_path = NULL;
    GError *error = NULL;

    metadata.manifest_sha256 = "not-a-hash";

    g_assert_false (
        atm_retrieval_index_create_empty (
            root,
            &metadata,
            &index_path,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_INDEX_ERROR,
        ATM_RETRIEVAL_INDEX_ERROR_INVALID_MANIFEST_HASH
    );
    g_assert_null (index_path);

    g_clear_error (&error);
    remove_tree_best_effort (root);
    g_free (root);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-index/create-validate",
        test_create_validate_and_refuse_overwrite
    );
    g_test_add_func (
        "/retrieval-index/source-catalog-committed",
        test_source_catalog_is_committed_before_promotion
    );
    g_test_add_func (
        "/retrieval-index/markdown-sections-fts",
        test_markdown_sections_and_fts_are_committed
    );
    g_test_add_func (
        "/retrieval-index/invalid-markdown-rollback",
        test_invalid_markdown_never_promotes
    );
    g_test_add_func (
        "/retrieval-index/csv-datasets-fts",
        test_csv_datasets_rows_and_fts_are_committed
    );
    g_test_add_func (
        "/retrieval-index/invalid-csv-rollback",
        test_invalid_csv_never_promotes_content_index
    );
    g_test_add_func (
        "/retrieval-index/structured-json-content",
        test_structured_json_entities_relations_and_fts_are_committed
    );
    g_test_add_func (
        "/retrieval-index/structured-json-ambiguous-native-id",
        test_ambiguous_native_entity_does_not_create_false_relation_link
    );
    g_test_add_func (
        "/retrieval-index/invalid-structured-json-rollback",
        test_invalid_structured_json_never_promotes_content_index
    );
    g_test_add_func (
        "/retrieval-index/final-snapshot-provenance",
        test_final_index_matches_pinned_snapshot_sources
    );
    g_test_add_func (
        "/retrieval-index/snapshot-hash-mismatch",
        test_snapshot_file_hash_mismatch_is_detected
    );
    g_test_add_func (
        "/retrieval-index/manifest-byte-mismatch",
        test_manifest_byte_change_is_detected
    );
    g_test_add_func (
        "/retrieval-index/source-role-tamper",
        test_index_source_role_tamper_is_detected
    );
    g_test_add_func (
        "/retrieval-index/manifest-hash-mismatch",
        test_manifest_hash_mismatch_never_promotes
    );
    g_test_add_func (
        "/retrieval-index/invalid-id",
        test_invalid_identity_is_rejected_before_creation
    );
    g_test_add_func (
        "/retrieval-index/invalid-manifest-hash",
        test_invalid_manifest_hash_is_rejected
    );

    return g_test_run ();
}
