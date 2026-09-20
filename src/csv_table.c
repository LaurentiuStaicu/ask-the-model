#include "csv_table.h"

#include <glib/gstdio.h>

#include <string.h>

GQuark
atm_csv_error_quark (void)
{
    return g_quark_from_static_string ("atm-csv-error-quark");
}

void
atm_csv_row_free (AtmCsvRow *row)
{
    if (row == NULL) {
        return;
    }

    g_clear_pointer (&row->fields, g_ptr_array_unref);
    g_free (row);
}

void
atm_csv_table_free (AtmCsvTable *table)
{
    if (table == NULL) {
        return;
    }

    g_clear_pointer (&table->headers, g_ptr_array_unref);
    g_clear_pointer (&table->rows, g_ptr_array_unref);
    g_free (table);
}

static GPtrArray *
new_string_array (void)
{
    return g_ptr_array_new_with_free_func (g_free);
}

static void
finish_field (
    GPtrArray *record,
    GString *field
)
{
    g_ptr_array_add (
        record,
        g_strdup (field->str)
    );
    g_string_set_size (field, 0);
}

static gboolean
validate_header (
    GPtrArray *headers,
    GError **error
)
{
    GHashTable *seen;

    if (headers->len == 0 ||
        headers->len > ATM_CSV_MAX_COLUMNS) {
        g_set_error_literal (
            error,
            ATM_CSV_ERROR,
            ATM_CSV_ERROR_HEADER,
            "CSV header has an unsupported number of columns."
        );
        return FALSE;
    }

    seen = g_hash_table_new (
        g_str_hash,
        g_str_equal
    );

    for (guint i = 0; i < headers->len; i++) {
        const char *header = g_ptr_array_index (
            headers,
            i
        );

        if (header[0] == '\0') {
            g_hash_table_unref (seen);
            g_set_error_literal (
                error,
                ATM_CSV_ERROR,
                ATM_CSV_ERROR_HEADER,
                "CSV header contains an empty column name."
            );
            return FALSE;
        }

        if (g_hash_table_contains (seen, header)) {
            g_hash_table_unref (seen);
            g_set_error (
                error,
                ATM_CSV_ERROR,
                ATM_CSV_ERROR_HEADER,
                "CSV header contains duplicate column '%s'.",
                header
            );
            return FALSE;
        }

        g_hash_table_add (seen, (gpointer) header);
    }

    g_hash_table_unref (seen);
    return TRUE;
}

static gboolean
commit_record (
    AtmCsvTable *table,
    GPtrArray **record_ptr,
    guint start_line,
    guint end_line,
    GError **error
)
{
    GPtrArray *record = *record_ptr;

    if (table->headers == NULL) {
        if (!validate_header (record, error)) {
            return FALSE;
        }

        table->headers = record;
        *record_ptr = new_string_array ();
        return TRUE;
    }

    if (record->len != table->headers->len) {
        g_set_error (
            error,
            ATM_CSV_ERROR,
            ATM_CSV_ERROR_SHAPE,
            "CSV record at lines %u-%u has %u columns; expected %u.",
            start_line,
            end_line,
            record->len,
            table->headers->len
        );
        return FALSE;
    }

    if (table->rows->len >= ATM_CSV_MAX_ROWS) {
        g_set_error (
            error,
            ATM_CSV_ERROR,
            ATM_CSV_ERROR_LIMIT,
            "CSV exceeds the %u-row parser limit.",
            ATM_CSV_MAX_ROWS
        );
        return FALSE;
    }

    AtmCsvRow *row = g_new0 (AtmCsvRow, 1);
    row->ordinal = table->rows->len;
    row->start_line = start_line;
    row->end_line = end_line;
    row->fields = record;

    g_ptr_array_add (table->rows, row);
    *record_ptr = new_string_array ();
    return TRUE;
}

gboolean
atm_csv_parse_file (
    const char *path,
    AtmCsvTable **out_table,
    GError **error
)
{
    char *contents = NULL;
    gsize length = 0;
    gsize offset = 0;
    AtmCsvTable *table = NULL;
    GPtrArray *record = NULL;
    GString *field = NULL;
    guint line = 1;
    guint record_start_line = 1;
    gboolean in_quotes = FALSE;
    gboolean field_started = FALSE;
    gboolean quote_closed = FALSE;
    gboolean ok = FALSE;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (out_table != NULL, FALSE);
    g_return_val_if_fail (*out_table == NULL, FALSE);

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            error
        )) {
        return FALSE;
    }

    if (length > ATM_CSV_MAX_BYTES) {
        g_set_error (
            error,
            ATM_CSV_ERROR,
            ATM_CSV_ERROR_TOO_LARGE,
            "CSV source exceeds the %u MiB parser limit.",
            (guint) (ATM_CSV_MAX_BYTES / 1024 / 1024)
        );
        goto out;
    }

    if (memchr (contents, '\0', length) != NULL ||
        !g_utf8_validate (contents, length, NULL)) {
        g_set_error_literal (
            error,
            ATM_CSV_ERROR,
            ATM_CSV_ERROR_ENCODING,
            "CSV source is not valid NUL-free UTF-8."
        );
        goto out;
    }

    if (length >= 3 &&
        (guint8) contents[0] == 0xef &&
        (guint8) contents[1] == 0xbb &&
        (guint8) contents[2] == 0xbf) {
        offset = 3;
    }

    table = g_new0 (AtmCsvTable, 1);
    table->rows = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_csv_row_free
    );
    record = new_string_array ();
    field = g_string_new (NULL);

    for (gsize i = offset; i < length; i++) {
        char c = contents[i];

        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < length &&
                    contents[i + 1] == '"') {
                    g_string_append_c (field, '"');
                    i++;
                    continue;
                }

                in_quotes = FALSE;
                quote_closed = TRUE;
                continue;
            }

            if (c == '\r' &&
                i + 1 < length &&
                contents[i + 1] == '\n') {
                g_string_append_c (field, '\r');
                g_string_append_c (field, '\n');
                i++;
                line++;
                continue;
            }

            if (c == '\n' || c == '\r') {
                g_string_append_c (field, c);
                line++;
                continue;
            }

            g_string_append_c (field, c);
            continue;
        }

        if (quote_closed) {
            if (c == ',') {
                finish_field (record, field);
                field_started = FALSE;
                quote_closed = FALSE;
                continue;
            }

            if (c == '\r' || c == '\n') {
                finish_field (record, field);

                if (!commit_record (
                        table,
                        &record,
                        record_start_line,
                        line,
                        error
                    )) {
                    goto out;
                }

                if (c == '\r' &&
                    i + 1 < length &&
                    contents[i + 1] == '\n') {
                    i++;
                }

                line++;
                record_start_line = line;
                field_started = FALSE;
                quote_closed = FALSE;
                continue;
            }

            g_set_error (
                error,
                ATM_CSV_ERROR,
                ATM_CSV_ERROR_SYNTAX,
                "Unexpected character after closing quote at line %u.",
                line
            );
            goto out;
        }

        if (!field_started && c == '"') {
            in_quotes = TRUE;
            field_started = TRUE;
            continue;
        }

        if (c == '"') {
            g_set_error (
                error,
                ATM_CSV_ERROR,
                ATM_CSV_ERROR_SYNTAX,
                "Quote appears inside an unquoted field at line %u.",
                line
            );
            goto out;
        }

        if (c == ',') {
            finish_field (record, field);
            field_started = FALSE;

            if (record->len > ATM_CSV_MAX_COLUMNS) {
                g_set_error (
                    error,
                    ATM_CSV_ERROR,
                    ATM_CSV_ERROR_LIMIT,
                    "CSV record exceeds the %u-column parser limit.",
                    ATM_CSV_MAX_COLUMNS
                );
                goto out;
            }

            continue;
        }

        if (c == '\r' || c == '\n') {
            finish_field (record, field);

            if (!commit_record (
                    table,
                    &record,
                    record_start_line,
                    line,
                    error
                )) {
                goto out;
            }

            if (c == '\r' &&
                i + 1 < length &&
                contents[i + 1] == '\n') {
                i++;
            }

            line++;
            record_start_line = line;
            field_started = FALSE;
            continue;
        }

        g_string_append_c (field, c);
        field_started = TRUE;
    }

    if (in_quotes) {
        g_set_error (
            error,
            ATM_CSV_ERROR,
            ATM_CSV_ERROR_SYNTAX,
            "CSV ends inside a quoted field that began at line %u.",
            record_start_line
        );
        goto out;
    }

    if (quote_closed ||
        field_started ||
        record->len > 0 ||
        field->len > 0) {
        finish_field (record, field);

        if (!commit_record (
                table,
                &record,
                record_start_line,
                line,
                error
            )) {
            goto out;
        }
    }

    if (table->headers == NULL) {
        g_set_error_literal (
            error,
            ATM_CSV_ERROR,
            ATM_CSV_ERROR_HEADER,
            "CSV contains no header row."
        );
        goto out;
    }

    *out_table = g_steal_pointer (&table);
    ok = TRUE;

out:
    g_clear_pointer (&record, g_ptr_array_unref);

    if (field != NULL) {
        g_string_free (field, TRUE);
    }

    g_clear_pointer (&table, atm_csv_table_free);
    g_free (contents);
    return ok;
}
