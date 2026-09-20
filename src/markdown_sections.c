#include "markdown_sections.h"

#include <glib/gstdio.h>

#include <string.h>

GQuark
atm_markdown_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-markdown-error-quark"
    );
}

void
atm_document_section_free (AtmDocumentSection *section)
{
    if (section == NULL) {
        return;
    }

    g_free (section->heading_path);
    g_free (section->title);
    g_free (section->body);
    g_free (section);
}

static gboolean
parse_atx_heading (
    const char *line,
    guint *out_level,
    char **out_title
)
{
    guint level = 0;
    const char *cursor;
    char *title;
    gsize length;

    *out_level = 0;
    *out_title = NULL;

    if (line == NULL) {
        return FALSE;
    }

    cursor = line;

    while (*cursor == '#' && level < 6) {
        level++;
        cursor++;
    }

    if (level == 0 || *cursor != ' ') {
        return FALSE;
    }

    while (*cursor == ' ') {
        cursor++;
    }

    title = g_strdup (cursor);
    g_strstrip (title);
    length = strlen (title);

    while (length > 0 && title[length - 1] == '#') {
        title[length - 1] = '\0';
        g_strchomp (title);
        length = strlen (title);
    }

    if (title[0] == '\0') {
        g_free (title);
        return FALSE;
    }

    *out_level = level;
    *out_title = title;
    return TRUE;
}

static gboolean
line_opens_or_closes_fence (
    const char *line,
    char *fence_char,
    guint *fence_length,
    gboolean *in_fence
)
{
    const char *cursor = line;
    char marker;
    guint length = 0;

    while (*cursor == ' ' && cursor - line < 4) {
        cursor++;
    }

    marker = *cursor;

    if (marker != '`' && marker != '~') {
        return FALSE;
    }

    while (*cursor == marker) {
        length++;
        cursor++;
    }

    if (length < 3) {
        return FALSE;
    }

    if (!*in_fence) {
        *in_fence = TRUE;
        *fence_char = marker;
        *fence_length = length;
        return TRUE;
    }

    if (marker == *fence_char && length >= *fence_length) {
        *in_fence = FALSE;
        *fence_char = '\0';
        *fence_length = 0;
        return TRUE;
    }

    return FALSE;
}

static char *
build_heading_path (char **heading_stack)
{
    GString *path = g_string_new (NULL);

    for (guint i = 0; i < 6; i++) {
        if (heading_stack[i] == NULL) {
            continue;
        }

        if (path->len > 0) {
            g_string_append (path, " > ");
        }

        g_string_append (path, heading_stack[i]);
    }

    return g_string_free (path, FALSE);
}

static gboolean
string_has_non_whitespace (const char *text)
{
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (!g_ascii_isspace (*cursor)) {
            return TRUE;
        }
    }

    return FALSE;
}

static void
append_section (
    GPtrArray *sections,
    guint start_line,
    guint end_line,
    const char *heading_path,
    const char *title,
    GString *body
)
{
    AtmDocumentSection *section;

    if (body == NULL || !string_has_non_whitespace (body->str)) {
        return;
    }

    section = g_new0 (AtmDocumentSection, 1);
    section->ordinal = sections->len;
    section->start_line = start_line;
    section->end_line = end_line;
    section->heading_path = g_strdup (
        heading_path != NULL ? heading_path : ""
    );
    section->title = g_strdup (title);
    section->body = g_strdup (body->str);

    g_ptr_array_add (sections, section);
}

gboolean
atm_markdown_extract_sections (
    const char *path,
    GPtrArray **out_sections,
    GError **error
)
{
    char *contents = NULL;
    gsize length = 0;
    char **lines = NULL;
    GPtrArray *sections = NULL;
    char *heading_stack[6] = { 0 };
    GString *current_body = NULL;
    char *current_heading_path = NULL;
    char *current_title = NULL;
    guint current_start_line = 1;
    gboolean in_fence = FALSE;
    char fence_char = '\0';
    guint fence_length = 0;
    gboolean ok = FALSE;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (out_sections != NULL, FALSE);
    g_return_val_if_fail (*out_sections == NULL, FALSE);

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            error
        )) {
        return FALSE;
    }

    if (length > ATM_MARKDOWN_MAX_BYTES) {
        g_set_error (
            error,
            ATM_MARKDOWN_ERROR,
            ATM_MARKDOWN_ERROR_TOO_LARGE,
            "Markdown source exceeds the %u MiB parser limit.",
            (guint) (ATM_MARKDOWN_MAX_BYTES / 1024 / 1024)
        );
        goto out;
    }

    if (!g_utf8_validate (contents, length, NULL)) {
        g_set_error_literal (
            error,
            ATM_MARKDOWN_ERROR,
            ATM_MARKDOWN_ERROR_ENCODING,
            "Markdown source is not valid UTF-8."
        );
        goto out;
    }

    sections = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_document_section_free
    );
    lines = g_strsplit (contents, "\n", -1);
    current_body = g_string_new (NULL);

    for (guint index = 0; lines[index] != NULL; index++) {
        const char *line = lines[index];
        guint line_number = index + 1;
        guint heading_level = 0;
        char *heading_title = NULL;

        if (line_opens_or_closes_fence (
                line,
                &fence_char,
                &fence_length,
                &in_fence
            )) {
            g_string_append (current_body, line);
            if (lines[index + 1] != NULL) {
                g_string_append_c (current_body, '\n');
            }
            continue;
        }

        if (!in_fence &&
            parse_atx_heading (
                line,
                &heading_level,
                &heading_title
            )) {
            append_section (
                sections,
                current_start_line,
                line_number > 1 ? line_number - 1 : 1,
                current_heading_path,
                current_title,
                current_body
            );

            g_string_set_size (current_body, 0);

            for (guint level = heading_level;
                 level < 6;
                 level++) {
                g_clear_pointer (
                    &heading_stack[level],
                    g_free
                );
            }

            g_clear_pointer (
                &heading_stack[heading_level - 1],
                g_free
            );
            heading_stack[heading_level - 1] =
                g_strdup (heading_title);

            g_clear_pointer (&current_heading_path, g_free);
            current_heading_path = build_heading_path (
                heading_stack
            );
            g_clear_pointer (&current_title, g_free);
            current_title = g_strdup (heading_title);
            current_start_line = line_number;

            g_free (heading_title);
            continue;
        }

        g_string_append (current_body, line);
        if (lines[index + 1] != NULL) {
            g_string_append_c (current_body, '\n');
        }
    }

    if (lines[0] != NULL) {
        guint line_count = 0;

        while (lines[line_count] != NULL) {
            line_count++;
        }

        append_section (
            sections,
            current_start_line,
            line_count,
            current_heading_path,
            current_title,
            current_body
        );
    }

    *out_sections = g_steal_pointer (&sections);
    ok = TRUE;

out:
    for (guint i = 0; i < 6; i++) {
        g_free (heading_stack[i]);
    }

    g_clear_pointer (&current_title, g_free);
    g_clear_pointer (&current_heading_path, g_free);
    g_clear_pointer (&current_body, g_string_free);
    g_clear_pointer (&sections, g_ptr_array_unref);
    g_strfreev (lines);
    g_free (contents);

    return ok;
}
