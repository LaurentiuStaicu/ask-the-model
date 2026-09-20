#include "retrieval_normalize.h"

#include <glib.h>

static void
test_romanian_aliases_preserve_original_query (void)
{
    const char *query =
        "Care este starea curentă și sursele pentru "
        "VAR.BELIEF.CLAIM în 2024?";
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            query,
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (normalized);
    g_assert_true (
        g_str_has_prefix (
            normalized->expanded_text,
            query
        )
    );
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "VAR.BELIEF.CLAIM"
        )
    );
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "2024"
        )
    );
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "status"
        )
    );
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "current"
        )
    );
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "source"
        )
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_CURRENT_STATE) != 0
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_EVIDENCE) != 0
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_NUMERIC) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_structure_aliases_are_conservative (void)
{
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            "Explică bucla și mecanismul",
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "feedback"
        )
    );
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "loop"
        )
    );
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "mechanism"
        )
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_STRUCTURE) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_english_query_sets_intents_without_translation_requirement (void)
{
    const char *query =
        "current evidence code value 2025";
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            query,
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        g_str_has_prefix (
            normalized->expanded_text,
            query
        )
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_CURRENT_STATE) != 0
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_EVIDENCE) != 0
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_NUMERIC) != 0
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_IMPLEMENTATION) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_boundary_alias_marks_current_state (void)
{
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            "Care este limita modelului?",
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (
        strstr (
            normalized->expanded_text,
            "boundary"
        )
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_CURRENT_STATE) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_romanian_scientific_aliases_and_numeric_scope (void)
{
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            "Compară paradigmele de modelare și validarea; "
            "recovery probability la 160, cu prognoze probabilistice.",
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (strstr (normalized->expanded_text, "compare"));
    g_assert_nonnull (strstr (normalized->expanded_text, "paradigm"));
    g_assert_nonnull (strstr (normalized->expanded_text, "modeling"));
    g_assert_nonnull (strstr (normalized->expanded_text, "validation"));
    g_assert_nonnull (strstr (normalized->expanded_text, "forecast"));
    g_assert_nonnull (strstr (normalized->expanded_text, "probability"));
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_CURRENT_STATE) != 0
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_STRUCTURE) != 0
    );
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_NUMERIC) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_probabilistic_claim_is_current_boundary_not_numeric_by_itself (void)
{
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            "Does the model claim probabilistic forecasts?",
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_CURRENT_STATE) != 0
    );
    g_assert_false (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_NUMERIC) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_development_paradigm_aliases (void)
{
    const char *query =
        "Compară paradigmele canonice de modelare pentru EWD, CBD și RMD.";
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            query,
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);

    const char *required[] = {
        "compare",
        "paradigm",
        "canonical",
        "modeling",
        "model"
    };

    for (guint i = 0; i < G_N_ELEMENTS (required); i++) {
        g_assert_nonnull (
            strstr (
                normalized->expanded_text,
                required[i]
            )
        );
    }

    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_STRUCTURE) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_development_human_validation_aliases (void)
{
    const char *query =
        "A stabilit CBD v0.1.0 validarea M1.E4 pe participanți umani?";
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_normalize_query (
            query,
            &normalized,
            &error
        )
    );
    g_assert_no_error (error);

    const char *required[] = {
        "established",
        "validation",
        "participant",
        "human"
    };

    for (guint i = 0; i < G_N_ELEMENTS (required); i++) {
        g_assert_nonnull (
            strstr (
                normalized->expanded_text,
                required[i]
            )
        );
    }

    g_assert_true (
        (normalized->intents &
         ATM_RETRIEVAL_INTENT_EVIDENCE) != 0
    );

    atm_normalized_query_free (normalized);
}

static void
test_invalid_normalization_input_is_rejected (void)
{
    AtmNormalizedQuery *normalized = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_retrieval_normalize_query (
            "",
            &normalized,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_NORMALIZE_ERROR,
        ATM_RETRIEVAL_NORMALIZE_ERROR_ARGUMENT
    );
    g_assert_null (normalized);

    g_clear_error (&error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_assert_cmpint (
        ATM_RETRIEVAL_ALIAS_VERSION,
        ==,
        2
    );

    g_test_add_func (
        "/retrieval-normalize/romanian-preserve",
        test_romanian_aliases_preserve_original_query
    );
    g_test_add_func (
        "/retrieval-normalize/structure",
        test_structure_aliases_are_conservative
    );
    g_test_add_func (
        "/retrieval-normalize/english-intents",
        test_english_query_sets_intents_without_translation_requirement
    );
    g_test_add_func (
        "/retrieval-normalize/boundary",
        test_boundary_alias_marks_current_state
    );
    g_test_add_func (
        "/retrieval-normalize/scientific-aliases",
        test_romanian_scientific_aliases_and_numeric_scope
    );
    g_test_add_func (
        "/retrieval-normalize/probabilistic-boundary",
        test_probabilistic_claim_is_current_boundary_not_numeric_by_itself
    );
    g_test_add_func (
        "/retrieval-normalize/development-paradigm",
        test_development_paradigm_aliases
    );
    g_test_add_func (
        "/retrieval-normalize/development-human-validation",
        test_development_human_validation_aliases
    );
    g_test_add_func (
        "/retrieval-normalize/invalid-input",
        test_invalid_normalization_input_is_rejected
    );

    return g_test_run ();
}
