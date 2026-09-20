#include "markdown_sections.h"

#include <glib.h>
#include <glib/gstdio.h>

static char *
new_temp_file (const char *contents, gssize length)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp (
        "atm-markdown-test-XXXXXX",
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (directory);

    char *path = g_build_filename (
        directory,
        "document.md",
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
remove_temp_file (char *path)
{
    char *directory = g_path_get_dirname (path);

    g_remove (path);
    g_rmdir (directory);

    g_free (directory);
    g_free (path);
}

static AtmDocumentSection *
section_at (GPtrArray *sections, guint index)
{
    return g_ptr_array_index (sections, index);
}

static void
test_heading_hierarchy_and_fences (void)
{
    const char *contents =
        "Intro line\n"
        "\n"
        "# Top\n"
        "Top body\n"
        "```text\n"
        "## not a heading\n"
        "```\n"
        "## Child\n"
        "Child body\n"
        "### Grand\n"
        "Grand body\n"
        "## Sibling\n"
        "Sibling body";
    char *path = new_temp_file (contents, -1);
    GPtrArray *sections = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_markdown_extract_sections (
            path,
            &sections,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (sections);
    g_assert_cmpuint (sections->len, ==, 5);

    AtmDocumentSection *preamble = section_at (sections, 0);
    AtmDocumentSection *top = section_at (sections, 1);
    AtmDocumentSection *child = section_at (sections, 2);
    AtmDocumentSection *grand = section_at (sections, 3);
    AtmDocumentSection *sibling = section_at (sections, 4);

    g_assert_cmpuint (preamble->ordinal, ==, 0);
    g_assert_cmpuint (preamble->start_line, ==, 1);
    g_assert_cmpuint (preamble->end_line, ==, 2);
    g_assert_cmpstr (preamble->heading_path, ==, "");
    g_assert_null (preamble->title);
    g_assert_cmpstr (preamble->body, ==, "Intro line\n\n");

    g_assert_cmpuint (top->start_line, ==, 3);
    g_assert_cmpuint (top->end_line, ==, 7);
    g_assert_cmpstr (top->heading_path, ==, "Top");
    g_assert_cmpstr (top->title, ==, "Top");
    g_assert_cmpstr (
        top->body,
        ==,
        "Top body\n"
        "```text\n"
        "## not a heading\n"
        "```\n"
    );

    g_assert_cmpstr (
        child->heading_path,
        ==,
        "Top > Child"
    );
    g_assert_cmpuint (child->start_line, ==, 8);
    g_assert_cmpuint (child->end_line, ==, 9);
    g_assert_cmpstr (child->body, ==, "Child body\n");

    g_assert_cmpstr (
        grand->heading_path,
        ==,
        "Top > Child > Grand"
    );
    g_assert_cmpuint (grand->start_line, ==, 10);
    g_assert_cmpuint (grand->end_line, ==, 11);

    g_assert_cmpstr (
        sibling->heading_path,
        ==,
        "Top > Sibling"
    );
    g_assert_cmpuint (sibling->start_line, ==, 12);
    g_assert_cmpuint (sibling->end_line, ==, 13);
    g_assert_cmpstr (sibling->body, ==, "Sibling body");

    g_ptr_array_unref (sections);
    remove_temp_file (path);
}

static void
test_document_without_atx_heading_is_one_section (void)
{
    const char *contents =
        "<h1 align=\"center\">Title</h1>\n"
        "\n"
        "Paragraph one.\n"
        "Paragraph two.";
    char *path = new_temp_file (contents, -1);
    GPtrArray *sections = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_markdown_extract_sections (
            path,
            &sections,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (sections->len, ==, 1);

    AtmDocumentSection *section = section_at (sections, 0);

    g_assert_cmpuint (section->start_line, ==, 1);
    g_assert_cmpuint (section->end_line, ==, 4);
    g_assert_cmpstr (section->heading_path, ==, "");
    g_assert_null (section->title);
    g_assert_cmpstr (section->body, ==, contents);

    g_ptr_array_unref (sections);
    remove_temp_file (path);
}

static void
test_trailing_heading_markers_are_trimmed (void)
{
    const char *contents =
        "# Release status ###\n"
        "Body.";
    char *path = new_temp_file (contents, -1);
    GPtrArray *sections = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_markdown_extract_sections (
            path,
            &sections,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (sections->len, ==, 1);

    AtmDocumentSection *section = section_at (sections, 0);
    g_assert_cmpstr (section->title, ==, "Release status");
    g_assert_cmpstr (
        section->heading_path,
        ==,
        "Release status"
    );
    g_assert_cmpstr (section->body, ==, "Body.");

    g_ptr_array_unref (sections);
    remove_temp_file (path);
}

static void
test_atx_edge_cases (void)
{
    const char *contents =
        "   ## Indented heading\n"
        "Body one\n"
        "### C#\n"
        "Body two\n"
        "```\n"
        "```not-a-closing-fence\n"
        "# still code\n"
        "```\n"
        "## After code\n"
        "Body three";
    char *path = new_temp_file (contents, -1);
    GPtrArray *sections = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_markdown_extract_sections (
            path,
            &sections,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (sections->len, ==, 3);

    AtmDocumentSection *first = section_at (sections, 0);
    AtmDocumentSection *second = section_at (sections, 1);
    AtmDocumentSection *third = section_at (sections, 2);

    g_assert_cmpstr (first->title, ==, "Indented heading");
    g_assert_cmpstr (
        first->heading_path,
        ==,
        "Indented heading"
    );

    g_assert_cmpstr (second->title, ==, "C#");
    g_assert_cmpstr (
        second->heading_path,
        ==,
        "Indented heading > C#"
    );
    g_assert_true (
        strstr (second->body, "# still code") != NULL
    );

    g_assert_cmpstr (third->title, ==, "After code");
    g_assert_cmpstr (
        third->heading_path,
        ==,
        "After code"
    );

    g_ptr_array_unref (sections);
    remove_temp_file (path);
}

static void
test_invalid_utf8_is_rejected (void)
{
    const char invalid[] = { (char) 0xff };
    char *path = new_temp_file (invalid, 1);
    GPtrArray *sections = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_markdown_extract_sections (
            path,
            &sections,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_MARKDOWN_ERROR,
        ATM_MARKDOWN_ERROR_ENCODING
    );
    g_assert_null (sections);

    g_clear_error (&error);
    remove_temp_file (path);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/markdown/hierarchy-and-fences",
        test_heading_hierarchy_and_fences
    );
    g_test_add_func (
        "/markdown/no-atx-heading",
        test_document_without_atx_heading_is_one_section
    );
    g_test_add_func (
        "/markdown/trailing-markers",
        test_trailing_heading_markers_are_trimmed
    );
    g_test_add_func (
        "/markdown/atx-edge-cases",
        test_atx_edge_cases
    );
    g_test_add_func (
        "/markdown/invalid-utf8",
        test_invalid_utf8_is_rejected
    );

    return g_test_run ();
}
