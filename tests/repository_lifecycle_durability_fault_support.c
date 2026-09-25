#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <glib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef fsync
#undef fsync
#endif

typedef struct {
    const char *path;
    const char *content;
    mode_t filetype;
} FixtureEntry;

static char *requested_checkpoint = NULL;
static gboolean fail_next_fsync = FALSE;
static gboolean fault_triggered = FALSE;

void
atm_m12_set_fault_checkpoint (
    const char *checkpoint_id
)
{
    g_free (requested_checkpoint);
    requested_checkpoint = g_strdup (checkpoint_id);
    fail_next_fsync = FALSE;
    fault_triggered = FALSE;
}

void
atm_m12_clear_fault (void)
{
    g_clear_pointer (&requested_checkpoint, g_free);
    fail_next_fsync = FALSE;
    fault_triggered = FALSE;
}

gboolean
atm_m12_fault_triggered (void)
{
    return fault_triggered;
}

void
atm_test_fault_checkpoint (
    const char *checkpoint_id
)
{
    if (requested_checkpoint == NULL ||
        g_strcmp0 (
            requested_checkpoint,
            checkpoint_id
        ) != 0) {
        return;
    }

    fail_next_fsync = TRUE;
    fault_triggered = TRUE;
}

int
atm_m12_test_fsync (
    int fd
)
{
    if (fail_next_fsync) {
        fail_next_fsync = FALSE;
        errno = EIO;
        return -1;
    }

    return fsync (fd);
}

gboolean
atm_m12_write_ewd_archive (
    const char *archive_path
)
{
    char *manifest = g_strdup (
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
        "    \"canonical\": ["
        "\"STATUS.md\", \"README.md\", \"CITATION.cff\"],\n"
        "    \"structural\": [\"model\"],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": [\".github\", \"__pycache__\"]\n"
        "  }\n"
        "}\n"
    );
    const FixtureEntry entries[] = {
        { "repo-sha/", NULL, AE_IFDIR },
        { "repo-sha/.atm/", NULL, AE_IFDIR },
        { "repo-sha/.atm/repository.json", manifest, AE_IFREG },
        {
            "repo-sha/CITATION.cff",
            "cff-version: 1.2.0\n"
            "message: cite this\n"
            "type: software\n"
            "title: Test repository\n"
            "version: 0.1.0\n",
            AE_IFREG
        },
        { "repo-sha/STATUS.md", "# Status\n", AE_IFREG },
        { "repo-sha/README.md", "# Readme\n", AE_IFREG },
        { "repo-sha/model/", NULL, AE_IFDIR },
        { "repo-sha/model/core.json", "{}\n", AE_IFREG }
    };
    struct archive *writer = archive_write_new ();

    if (writer == NULL) {
        g_free (manifest);
        return FALSE;
    }

    if (archive_write_set_format_pax_restricted (writer) != ARCHIVE_OK ||
        archive_write_add_filter_gzip (writer) != ARCHIVE_OK ||
        archive_write_open_filename (
            writer,
            archive_path
        ) != ARCHIVE_OK) {
        archive_write_free (writer);
        g_free (manifest);
        return FALSE;
    }

    gboolean ok = TRUE;

    for (gsize i = 0;
         i < G_N_ELEMENTS (entries);
         i++) {
        const FixtureEntry *fixture = &entries[i];
        struct archive_entry *entry =
            archive_entry_new ();
        gsize length =
            fixture->content != NULL
                ? strlen (fixture->content)
                : 0;

        if (entry == NULL) {
            ok = FALSE;
            break;
        }

        archive_entry_set_pathname (
            entry,
            fixture->path
        );
        archive_entry_set_filetype (
            entry,
            fixture->filetype
        );
        archive_entry_set_perm (
            entry,
            fixture->filetype == AE_IFDIR
                ? 0700
                : 0600
        );
        archive_entry_set_size (
            entry,
            fixture->filetype == AE_IFREG
                ? (int64_t) length
                : 0
        );

        if (archive_write_header (
                writer,
                entry
            ) != ARCHIVE_OK) {
            archive_entry_free (entry);
            ok = FALSE;
            break;
        }

        if (fixture->filetype == AE_IFREG &&
            length > 0 &&
            archive_write_data (
                writer,
                fixture->content,
                length
            ) != (la_ssize_t) length) {
            archive_entry_free (entry);
            ok = FALSE;
            break;
        }

        archive_entry_free (entry);
    }

    if (archive_write_close (writer) != ARCHIVE_OK) {
        ok = FALSE;
    }

    if (archive_write_free (writer) != ARCHIVE_OK) {
        ok = FALSE;
    }

    g_free (manifest);
    return ok;
}
