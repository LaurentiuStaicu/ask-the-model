#include "csv_table.h"

#include <glib.h>
#include <glib/gstdio.h>

static char *
new_csv_file (const char *contents, gssize length)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp (
        "atm-csv-test-XXXXXX",
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (directory);

    char *path = g_build_filename (
        directory,
        "table.csv",
        NULL
    );

    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            length,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (directory);
    return path;
}

static void
remove_csv_file (char *path)
{
    char *directory = g_path_get_dirname (path);

    g_remove (path);
    g_rmdir (directory);

    g_free (directory);
    g_free (path);
}

static const char *
header_at (AtmCsvTable *table, guint index)
{
    return g_ptr_array_index (table->headers, index);
}

static AtmCsvRow *
row_at (AtmCsvTable *table, guint index)
{
    return g_ptr_array_index (table->rows, index);
}

static const char *
field_at (AtmCsvRow *row, guint index)
{
    return g_ptr_array_index (row->fields, index);
}

static void
test_quoted_fields_empty_fields_and_lines (void)
{
    const char *contents =
        "id,label,note,missing\r\n"
        "1,\"Alpha, Beta\",\"line one\r\nline two\",\r\n"
        "2,\"He said \"\"yes\"\"\",plain,";
    char *path = new_csv_file (contents, -1);
    AtmCsvTable *table = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_csv_parse_file (
            path,
            &table,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (table);

    g_assert_cmpuint (table->headers->len, ==, 4);
    g_assert_cmpstr (header_at (table, 0), ==, "id");
    g_assert_cmpstr (header_at (table, 3), ==, "missing");
    g_assert_cmpuint (table->rows->len, ==, 2);

    AtmCsvRow *first = row_at (table, 0);
    AtmCsvRow *second = row_at (table, 1);

    g_assert_cmpuint (first->ordinal, ==, 0);
    g_assert_cmpuint (first->start_line, ==, 2);
    g_assert_cmpuint (first->end_line, ==, 3);
    g_assert_cmpstr (field_at (first, 0), ==, "1");
    g_assert_cmpstr (field_at (first, 1), ==, "Alpha, Beta");
    g_assert_cmpstr (
        field_at (first, 2),
        ==,
        "line one\r\nline two"
    );
    g_assert_cmpstr (field_at (first, 3), ==, "");

    g_assert_cmpuint (second->ordinal, ==, 1);
    g_assert_cmpuint (second->start_line, ==, 4);
    g_assert_cmpuint (second->end_line, ==, 4);
    g_assert_cmpstr (
        field_at (second, 1),
        ==,
        "He said \"yes\""
    );
    g_assert_cmpstr (field_at (second, 3), ==, "");

    atm_csv_table_free (table);
    remove_csv_file (path);
}

static void
test_utf8_bom_is_removed_from_first_header (void)
{
    const char contents[] =
        "\xef\xbb\xbfid,value\n"
        "1,2\n";
    char *path = new_csv_file (
        contents,
        sizeof contents - 1
    );
    AtmCsvTable *table = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_csv_parse_file (
            path,
            &table,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (header_at (table, 0), ==, "id");
    g_assert_cmpuint (table->rows->len, ==, 1);

    atm_csv_table_free (table);
    remove_csv_file (path);
}

static void
test_shape_mismatch_is_rejected (void)
{
    const char *contents =
        "a,b,c\n"
        "1,2\n";
    char *path = new_csv_file (contents, -1);
    AtmCsvTable *table = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_csv_parse_file (
            path,
            &table,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CSV_ERROR,
        ATM_CSV_ERROR_SHAPE
    );
    g_assert_null (table);

    g_clear_error (&error);
    remove_csv_file (path);
}

static void
test_duplicate_header_is_rejected (void)
{
    const char *contents =
        "id,value,id\n"
        "1,2,3\n";
    char *path = new_csv_file (contents, -1);
    AtmCsvTable *table = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_csv_parse_file (
            path,
            &table,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CSV_ERROR,
        ATM_CSV_ERROR_HEADER
    );
    g_assert_null (table);

    g_clear_error (&error);
    remove_csv_file (path);
}

static void
test_unterminated_quote_is_rejected (void)
{
    const char *contents =
        "id,note\n"
        "1,\"unfinished\n";
    char *path = new_csv_file (contents, -1);
    AtmCsvTable *table = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_csv_parse_file (
            path,
            &table,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CSV_ERROR,
        ATM_CSV_ERROR_SYNTAX
    );
    g_assert_null (table);

    g_clear_error (&error);
    remove_csv_file (path);
}

static void
test_quote_inside_unquoted_field_is_rejected (void)
{
    const char *contents =
        "id,note\n"
        "1,bad\"quote\n";
    char *path = new_csv_file (contents, -1);
    AtmCsvTable *table = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_csv_parse_file (
            path,
            &table,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CSV_ERROR,
        ATM_CSV_ERROR_SYNTAX
    );
    g_assert_null (table);

    g_clear_error (&error);
    remove_csv_file (path);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/csv/quoted-empty-lines",
        test_quoted_fields_empty_fields_and_lines
    );
    g_test_add_func (
        "/csv/utf8-bom",
        test_utf8_bom_is_removed_from_first_header
    );
    g_test_add_func (
        "/csv/shape-mismatch",
        test_shape_mismatch_is_rejected
    );
    g_test_add_func (
        "/csv/duplicate-header",
        test_duplicate_header_is_rejected
    );
    g_test_add_func (
        "/csv/unterminated-quote",
        test_unterminated_quote_is_rejected
    );
    g_test_add_func (
        "/csv/quote-in-unquoted-field",
        test_quote_inside_unquoted_field_is_rejected
    );

    return g_test_run ();
}
