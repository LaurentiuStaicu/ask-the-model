#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define ATM_CSV_MAX_BYTES ((gsize) 32 * 1024 * 1024)
#define ATM_CSV_MAX_COLUMNS 512
#define ATM_CSV_MAX_ROWS 200000

typedef enum {
    ATM_CSV_ERROR_IO,
    ATM_CSV_ERROR_TOO_LARGE,
    ATM_CSV_ERROR_ENCODING,
    ATM_CSV_ERROR_SYNTAX,
    ATM_CSV_ERROR_HEADER,
    ATM_CSV_ERROR_SHAPE,
    ATM_CSV_ERROR_LIMIT
} AtmCsvError;

#define ATM_CSV_ERROR (atm_csv_error_quark ())

typedef struct {
    guint ordinal;
    guint start_line;
    guint end_line;
    GPtrArray *fields;
} AtmCsvRow;

typedef struct {
    GPtrArray *headers;
    GPtrArray *rows;
} AtmCsvTable;

GQuark atm_csv_error_quark (void);

gboolean atm_csv_parse_file (
    const char *path,
    AtmCsvTable **out_table,
    GError **error
);

void atm_csv_row_free (AtmCsvRow *row);
void atm_csv_table_free (AtmCsvTable *table);

G_END_DECLS
