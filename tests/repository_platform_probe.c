#include <archive.h>
#include <archive_entry.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static gboolean
require_true (gboolean condition, const char *message)
{
    if (!condition) {
        g_printerr ("FAIL: %s\n", message);
        return FALSE;
    }

    return TRUE;
}

static gboolean
sqlite_exec_checked (sqlite3 *db, const char *sql)
{
    char *error = NULL;
    int rc = sqlite3_exec (db, sql, NULL, NULL, &error);

    if (rc != SQLITE_OK) {
        g_printerr ("FAIL: SQLite statement failed: %s\nSQL: %s\n",
                    error != NULL ? error : sqlite3_errmsg (db),
                    sql);
        sqlite3_free (error);
        return FALSE;
    }

    return TRUE;
}

static gboolean
probe_sqlite (void)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    gboolean ok = FALSE;

    if (!require_true (sqlite3_open (":memory:", &db) == SQLITE_OK,
                       "SQLite in-memory database could not be opened")) {
        goto out;
    }

    if (!sqlite_exec_checked (
            db,
            "PRAGMA foreign_keys = ON;"
            "PRAGMA trusted_schema = OFF;"
            "CREATE VIRTUAL TABLE docs USING fts5("
            "content, tokenize='unicode61 remove_diacritics 2');"
            "INSERT INTO docs(content) VALUES('fără pădure');")) {
        goto out;
    }

    if (!require_true (
            sqlite3_prepare_v2 (
                db,
                "SELECT json_extract('{\"value\":42}', '$.value');",
                -1,
                &statement,
                NULL) == SQLITE_OK,
            "SQLite JSON SQL function could not be prepared")) {
        goto out;
    }

    if (!require_true (sqlite3_step (statement) == SQLITE_ROW &&
                       sqlite3_column_int (statement, 0) == 42,
                       "SQLite JSON SQL function did not return the expected value")) {
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (!require_true (
            sqlite3_prepare_v2 (
                db,
                "SELECT count(*) FROM docs WHERE docs MATCH ?1;",
                -1,
                &statement,
                NULL) == SQLITE_OK,
            "SQLite FTS5 query could not be prepared")) {
        goto out;
    }

    if (!require_true (
            sqlite3_bind_text (statement, 1, "fara", -1, SQLITE_STATIC) == SQLITE_OK,
            "SQLite prepared-statement binding failed")) {
        goto out;
    }

    if (!require_true (sqlite3_step (statement) == SQLITE_ROW &&
                       sqlite3_column_int (statement, 0) == 1,
                       "FTS5 unicode61/remove_diacritics did not match Romanian text")) {
        goto out;
    }

    sqlite3_finalize (statement);
    statement = NULL;

    if (!sqlite_exec_checked (db, "PRAGMA query_only = ON;")) {
        goto out;
    }

    if (!require_true (
            sqlite3_prepare_v2 (db, "SELECT 1;", -1, &statement, NULL) == SQLITE_OK &&
            sqlite3_step (statement) == SQLITE_ROW &&
            sqlite3_column_int (statement, 0) == 1,
            "SQLite read-only retrieval connection probe failed")) {
        goto out;
    }

    ok = TRUE;

out:
    if (statement != NULL) {
        sqlite3_finalize (statement);
    }

    if (db != NULL) {
        sqlite3_close (db);
    }

    if (ok) {
        g_print ("PASS: SQLite JSON, FTS5 unicode61, prepared statements and defensive pragmas\n");
    }

    return ok;
}

static int
archive_write_probe_entry (const char *pathname)
{
    struct archive *disk = archive_write_disk_new ();
    struct archive_entry *entry = archive_entry_new ();
    int flags =
        ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS |
        ARCHIVE_EXTRACT_SECURE_NODOTDOT |
        ARCHIVE_EXTRACT_SECURE_SYMLINKS;
    int result;

    if (disk == NULL || entry == NULL) {
        if (entry != NULL) {
            archive_entry_free (entry);
        }
        if (disk != NULL) {
            archive_write_free (disk);
        }
        return ARCHIVE_FATAL;
    }

    result = archive_write_disk_set_options (disk, flags);
    if (result != ARCHIVE_OK) {
        archive_entry_free (entry);
        archive_write_free (disk);
        return result;
    }

    archive_entry_set_pathname (entry, pathname);
    archive_entry_set_filetype (entry, AE_IFREG);
    archive_entry_set_perm (entry, 0600);
    archive_entry_set_size (entry, 0);

    result = archive_write_header (disk, entry);
    if (result == ARCHIVE_OK) {
        archive_write_finish_entry (disk);
    }

    archive_entry_free (entry);
    archive_write_close (disk);
    archive_write_free (disk);

    return result;
}

static gboolean
probe_libarchive (void)
{
    GError *error = NULL;
    char *old_cwd = g_get_current_dir ();
    char *temp_dir = g_dir_make_tmp ("atm-platform-probe-XXXXXX", &error);
    char absolute_path[256] = { 0 };
    char escaped_path[256] = { 0 };
    char symlink_escape_path[256] = { 0 };
    gboolean ok = FALSE;
    struct archive_entry *metadata_entry = NULL;

    if (!require_true (temp_dir != NULL,
                       error != NULL ? error->message : "temporary directory creation failed")) {
        g_clear_error (&error);
        g_free (old_cwd);
        return FALSE;
    }

    if (!require_true (g_chdir (temp_dir) == 0,
                       "could not enter temporary archive-probe directory")) {
        goto out;
    }

    g_snprintf (absolute_path,
                sizeof absolute_path,
                "/tmp/atm-platform-probe-absolute-%ld",
                (long) getpid ());
    g_snprintf (escaped_path,
                sizeof escaped_path,
                "/tmp/atm-platform-probe-dotdot-%ld",
                (long) getpid ());
    g_snprintf (symlink_escape_path,
                sizeof symlink_escape_path,
                "/tmp/atm-platform-probe-symlink-%ld",
                (long) getpid ());

    if (!require_true (archive_write_probe_entry ("safe-file") == ARCHIVE_OK,
                       "libarchive could not extract a valid regular-file entry")) {
        goto out;
    }
    g_remove ("safe-file");

    if (!require_true (archive_write_probe_entry (absolute_path) != ARCHIVE_OK,
                       "libarchive accepted an absolute extraction path")) {
        goto out;
    }

    {
        char dotdot_path[256];
        g_snprintf (dotdot_path,
                    sizeof dotdot_path,
                    "../atm-platform-probe-dotdot-%ld",
                    (long) getpid ());

        if (!require_true (archive_write_probe_entry (dotdot_path) != ARCHIVE_OK,
                           "libarchive accepted a '..' extraction path")) {
            goto out;
        }
    }

    if (!require_true (symlink ("..", "escape-link") == 0,
                       "could not create symlink fixture for secure extraction probe")) {
        goto out;
    }

    {
        char symlink_path[256];
        g_snprintf (symlink_path,
                    sizeof symlink_path,
                    "escape-link/atm-platform-probe-symlink-%ld",
                    (long) getpid ());

        if (!require_true (archive_write_probe_entry (symlink_path) != ARCHIVE_OK,
                           "libarchive followed an unsafe symlink during extraction")) {
            goto out;
        }
    }

    metadata_entry = archive_entry_new ();
    if (!require_true (metadata_entry != NULL,
                       "libarchive entry metadata could not be allocated")) {
        goto out;
    }

    archive_entry_set_size (metadata_entry, 1234);
    archive_entry_set_filetype (metadata_entry, AE_IFREG);
    archive_entry_set_hardlink (metadata_entry, "../outside-hardlink-target");

    if (!require_true (archive_entry_size_is_set (metadata_entry) &&
                       archive_entry_size (metadata_entry) == 1234 &&
                       archive_entry_filetype (metadata_entry) == AE_IFREG &&
                       archive_entry_hardlink (metadata_entry) != NULL,
                       "libarchive entry size/type/link metadata is unavailable")) {
        goto out;
    }

    ok = TRUE;

out:
    if (metadata_entry != NULL) {
        archive_entry_free (metadata_entry);
    }

    g_remove ("safe-file");
    g_remove ("escape-link");

    if (old_cwd != NULL) {
        g_chdir (old_cwd);
    }

    if (absolute_path[0] != '\0') {
        g_remove (absolute_path);
    }
    if (escaped_path[0] != '\0') {
        g_remove (escaped_path);
    }
    if (symlink_escape_path[0] != '\0') {
        g_remove (symlink_escape_path);
    }
    g_rmdir (temp_dir);

    g_clear_error (&error);
    g_free (temp_dir);
    g_free (old_cwd);

    if (ok) {
        g_print ("PASS: libarchive secure extraction flags and entry metadata\n");
    }

    return ok;
}

static gboolean
probe_writable_directory (const char *directory, const char *label)
{
    GError *error = NULL;
    char *probe_dir = g_build_filename (directory, "atm-platform-probe", NULL);
    char *probe_file = g_build_filename (probe_dir, "write-test", NULL);
    gboolean ok = FALSE;

    if (!require_true (g_mkdir_with_parents (probe_dir, 0700) == 0,
                       "could not create XDG probe directory")) {
        goto out;
    }

    if (!require_true (
            g_file_set_contents (probe_file, "ok\n", -1, &error),
            error != NULL ? error->message : "could not write XDG probe file")) {
        goto out;
    }

    g_print ("PASS: %s is writable (%s)\n", label, directory);
    ok = TRUE;

out:
    g_remove (probe_file);
    g_rmdir (probe_dir);
    g_clear_error (&error);
    g_free (probe_file);
    g_free (probe_dir);

    return ok;
}

static gboolean
probe_xdg_and_manifest (void)
{
    const char *manifest_path = g_getenv ("ATM_FLATPAK_MANIFEST");
    char *manifest = NULL;
    gsize manifest_length = 0;
    GError *error = NULL;
    gboolean ok = FALSE;

    if (!probe_writable_directory (g_get_user_data_dir (), "XDG data directory") ||
        !probe_writable_directory (g_get_user_cache_dir (), "XDG cache directory") ||
        !probe_writable_directory (g_get_user_state_dir (), "XDG state directory")) {
        return FALSE;
    }

    if (!require_true (manifest_path != NULL && *manifest_path != '\0',
                       "ATM_FLATPAK_MANIFEST was not provided to the test")) {
        return FALSE;
    }

    if (!require_true (
            g_file_get_contents (manifest_path, &manifest, &manifest_length, &error),
            error != NULL ? error->message : "Flatpak manifest could not be read")) {
        goto out;
    }

    if (!require_true (strstr (manifest, "runtime-version: '8'") != NULL,
                       "platform probe is not using the documented elementary runtime version")) {
        goto out;
    }

    if (!require_true (strstr (manifest, "sdk: io.elementary.Sdk") != NULL,
                       "platform probe is not using io.elementary.Sdk")) {
        goto out;
    }

    if (!require_true (strstr (manifest, "--filesystem=") == NULL,
                       "general host filesystem access was added to the Flatpak manifest")) {
        goto out;
    }

    g_print ("PASS: Flatpak manifest keeps the elementary OS 8 baseline without general filesystem access\n");
    ok = TRUE;

out:
    g_clear_error (&error);
    g_free (manifest);
    return ok;
}

int
main (void)
{
    if (!probe_sqlite ()) {
        return 1;
    }

    if (!probe_libarchive ()) {
        return 1;
    }

    if (!probe_xdg_and_manifest ()) {
        return 1;
    }

    g_print ("PASS: repository platform capability gate G-P0\n");
    return 0;
}
