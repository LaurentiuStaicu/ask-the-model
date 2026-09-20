#include "citation_labels.h"

#include <glib.h>

static AtmGroundingSource *
new_source (
    const char *label,
    const char *repository_id,
    const char *sha,
    const char *logical_id,
    const char *source_path,
    const char *locator,
    const char *excerpt
)
{
    AtmGroundingSource *source = g_new0 (
        AtmGroundingSource,
        1
    );

    source->label = g_strdup (label);
    source->evidence_kind = g_strdup ("section");
    source->evidence_id = 7;
    source->repository_id = g_strdup (repository_id);
    source->repository_version = g_strdup ("0.1.0");
    source->snapshot_sha = g_strdup (sha);
    source->logical_source_id = g_strdup (logical_id);
    source->source_path = g_strdup (source_path);
    source->locator = g_strdup (locator);
    source->title = g_strdup ("Scientific status");
    source->excerpt = g_strdup (excerpt);
    source->source_roles = ATM_SOURCE_ROLE_CANONICAL;
    source->match_kind = ATM_EVIDENCE_MATCH_LEXICAL;

    return source;
}

static AtmGroundingContext *
new_context (void)
{
    AtmGroundingContext *context = g_new0 (
        AtmGroundingContext,
        1
    );

    context->sources = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_grounding_source_free
    );
    context->evidence_text = g_strdup ("test");
    context->evidence_bytes = 4;

    g_ptr_array_add (
        context->sources,
        new_source (
            "[S1]",
            "ewd",
            "1111111111111111111111111111111111111111",
            "ewd:section:STATUS.md:Scientific status",
            "STATUS.md",
            "lines:1-4",
            "EWD current status."
        )
    );
    g_ptr_array_add (
        context->sources,
        new_source (
            "[S2]",
            "rmd",
            "2222222222222222222222222222222222222222",
            "rmd:section:STATUS.md:Scientific status",
            "STATUS.md",
            "lines:1-5",
            "RMD current status."
        )
    );

    return context;
}

static void
test_known_unknown_and_duplicate_labels (void)
{
    AtmGroundingContext *context = new_context ();
    AtmCitationResolution *resolution = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_citation_resolve_labels (
            "RMD claim [S2]. Repeat [S2]. "
            "EWD claim [S1]. Unknown [S9] [S9].",
            context,
            &resolution,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (resolution);
    g_assert_cmpuint (resolution->citations->len, ==, 2);
    g_assert_cmpuint (resolution->unknown_labels->len, ==, 1);

    AtmCitationReference *first = g_ptr_array_index (
        resolution->citations,
        0
    );
    AtmCitationReference *second = g_ptr_array_index (
        resolution->citations,
        1
    );

    g_assert_cmpstr (first->label, ==, "[S2]");
    g_assert_cmpstr (first->repository_id, ==, "rmd");
    g_assert_cmpstr (
        first->snapshot_sha,
        ==,
        "2222222222222222222222222222222222222222"
    );
    g_assert_cmpstr (first->excerpt, ==, "RMD current status.");

    g_assert_cmpstr (second->label, ==, "[S1]");
    g_assert_cmpstr (second->repository_id, ==, "ewd");

    g_assert_cmpstr (
        g_ptr_array_index (
            resolution->unknown_labels,
            0
        ),
        ==,
        "[S9]"
    );

    atm_citation_resolution_free (resolution);
    atm_grounding_context_free (context);
}

static void
test_citation_copy_survives_turn_context_free (void)
{
    AtmGroundingContext *context = new_context ();
    AtmCitationResolution *resolution = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_citation_resolve_labels (
            "Supported statement [S1].",
            context,
            &resolution,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (resolution->citations->len, ==, 1);

    atm_grounding_context_free (context);
    context = NULL;

    AtmCitationReference *citation = g_ptr_array_index (
        resolution->citations,
        0
    );

    g_assert_cmpstr (citation->label, ==, "[S1]");
    g_assert_cmpstr (citation->repository_id, ==, "ewd");
    g_assert_cmpstr (
        citation->logical_source_id,
        ==,
        "ewd:section:STATUS.md:Scientific status"
    );
    g_assert_cmpstr (citation->source_path, ==, "STATUS.md");
    g_assert_cmpstr (citation->locator, ==, "lines:1-4");
    g_assert_cmpstr (
        citation->excerpt,
        ==,
        "EWD current status."
    );

    atm_citation_resolution_free (resolution);
}

static void
test_no_labels_is_valid_empty_resolution (void)
{
    AtmGroundingContext *context = new_context ();
    AtmCitationResolution *resolution = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_citation_resolve_labels (
            "No repository citation emitted.",
            context,
            &resolution,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (resolution->citations->len, ==, 0);
    g_assert_cmpuint (resolution->unknown_labels->len, ==, 0);

    atm_citation_resolution_free (resolution);
    atm_grounding_context_free (context);
}

static void
test_similar_text_is_not_misclassified (void)
{
    AtmGroundingContext *context = new_context ();
    AtmCitationResolution *resolution = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_citation_resolve_labels (
            "[s1] [S0] [S01] [S1x] S1 [S10000] [S2]",
            context,
            &resolution,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (resolution->citations->len, ==, 1);
    g_assert_cmpstr (
        ((AtmCitationReference *) g_ptr_array_index (
            resolution->citations,
            0
        ))->label,
        ==,
        "[S2]"
    );

    g_assert_cmpuint (resolution->unknown_labels->len, ==, 1);
    g_assert_cmpstr (
        g_ptr_array_index (
            resolution->unknown_labels,
            0
        ),
        ==,
        "[S01]"
    );

    atm_citation_resolution_free (resolution);
    atm_grounding_context_free (context);
}

static void
test_incomplete_context_provenance_fails_closed (void)
{
    AtmGroundingContext *context = new_context ();
    AtmGroundingSource *source = g_ptr_array_index (
        context->sources,
        0
    );
    AtmCitationResolution *resolution = NULL;
    GError *error = NULL;

    g_clear_pointer (&source->snapshot_sha, g_free);

    g_assert_false (
        atm_citation_resolve_labels (
            "Claim [S1].",
            context,
            &resolution,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CITATION_RESOLVE_ERROR,
        ATM_CITATION_RESOLVE_ERROR_PROVENANCE
    );
    g_assert_null (resolution);

    g_clear_error (&error);
    atm_grounding_context_free (context);
}

static void
test_invalid_utf8_output_is_rejected (void)
{
    AtmGroundingContext *context = new_context ();
    AtmCitationResolution *resolution = NULL;
    GError *error = NULL;
    const char invalid[] = {
        'x',
        (char) 0xff,
        '\0'
    };

    g_assert_false (
        atm_citation_resolve_labels (
            invalid,
            context,
            &resolution,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CITATION_RESOLVE_ERROR,
        ATM_CITATION_RESOLVE_ERROR_ARGUMENT
    );
    g_assert_null (resolution);

    g_clear_error (&error);
    atm_grounding_context_free (context);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/citation-labels/known-unknown-duplicates",
        test_known_unknown_and_duplicate_labels
    );
    g_test_add_func (
        "/citation-labels/persistent-copy",
        test_citation_copy_survives_turn_context_free
    );
    g_test_add_func (
        "/citation-labels/no-labels",
        test_no_labels_is_valid_empty_resolution
    );
    g_test_add_func (
        "/citation-labels/similar-text",
        test_similar_text_is_not_misclassified
    );
    g_test_add_func (
        "/citation-labels/incomplete-provenance",
        test_incomplete_context_provenance_fails_closed
    );
    g_test_add_func (
        "/citation-labels/invalid-utf8",
        test_invalid_utf8_output_is_rejected
    );

    return g_test_run ();
}
