#include "retrieval_rank.h"

#include <glib.h>

static AtmEvidenceRecord *
new_record (
    const char *logical_source_id,
    guint source_roles,
    AtmEvidenceMatch match_kind,
    gboolean has_lexical_score,
    double lexical_score
)
{
    AtmEvidenceRecord *record = g_new0 (
        AtmEvidenceRecord,
        1
    );

    record->evidence_kind = g_strdup ("section");
    record->logical_source_id = g_strdup (
        logical_source_id
    );
    record->source_path = g_strdup (
        logical_source_id
    );
    record->locator = g_strdup ("fixture");
    record->source_roles = source_roles;
    record->match_kind = match_kind;
    record->has_lexical_score = has_lexical_score;
    record->lexical_score = lexical_score;
    return record;
}

static GPtrArray *
new_results (void)
{
    return g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_evidence_record_free
    );
}

static void
test_current_state_authority_precedes_lexical_magnitude (void)
{
    GPtrArray *results = new_results ();
    GError *error = NULL;

    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:implementation",
            ATM_SOURCE_ROLE_IMPLEMENTATION,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -100.0
        )
    );
    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:canonical",
            ATM_SOURCE_ROLE_CANONICAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -1.0
        )
    );

    g_assert_true (
        atm_retrieval_rank_and_deduplicate (
            results,
            ATM_RETRIEVAL_INTENT_CURRENT_STATE,
            10,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        ((AtmEvidenceRecord *) g_ptr_array_index (
            results,
            0
        ))->logical_source_id,
        ==,
        "ewd:file:canonical"
    );

    g_ptr_array_unref (results);
}

static void
test_current_state_prefers_declared_status_role (void)
{
    GPtrArray *results = new_results ();
    GError *error = NULL;

    g_ptr_array_add (
        results,
        new_record (
            "rmd:file:README.md",
            ATM_SOURCE_ROLE_CANONICAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -100.0
        )
    );
    g_ptr_array_add (
        results,
        new_record (
            "rmd:file:STATUS.md",
            ATM_SOURCE_ROLE_CANONICAL |
                ATM_SOURCE_ROLE_STATUS,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -1.0
        )
    );

    g_assert_true (
        atm_retrieval_rank_and_deduplicate (
            results,
            ATM_RETRIEVAL_INTENT_CURRENT_STATE,
            10,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        ((AtmEvidenceRecord *) g_ptr_array_index (
            results,
            0
        ))->logical_source_id,
        ==,
        "rmd:file:STATUS.md"
    );

    g_ptr_array_unref (results);
}

static void
test_implementation_intent_changes_authority_order (void)
{
    GPtrArray *results = new_results ();
    GError *error = NULL;

    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:canonical",
            ATM_SOURCE_ROLE_CANONICAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -10.0
        )
    );
    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:implementation",
            ATM_SOURCE_ROLE_IMPLEMENTATION,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -1.0
        )
    );

    g_assert_true (
        atm_retrieval_rank_and_deduplicate (
            results,
            ATM_RETRIEVAL_INTENT_IMPLEMENTATION,
            10,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        ((AtmEvidenceRecord *) g_ptr_array_index (
            results,
            0
        ))->logical_source_id,
        ==,
        "ewd:file:implementation"
    );

    g_ptr_array_unref (results);
}

static void
test_numeric_intent_prefers_tabular_role (void)
{
    AtmEvidenceRecord *canonical = new_record (
        "ewd:file:status",
        ATM_SOURCE_ROLE_CANONICAL,
        ATM_EVIDENCE_MATCH_LEXICAL,
        TRUE,
        -20.0
    );
    AtmEvidenceRecord *table = new_record (
        "ewd:dataset-row:data/series.csv:0",
        ATM_SOURCE_ROLE_EVIDENCE |
            ATM_SOURCE_ROLE_TABULAR,
        ATM_EVIDENCE_MATCH_LEXICAL,
        TRUE,
        -1.0
    );

    g_assert_cmpuint (
        atm_evidence_authority_rank (
            table,
            ATM_RETRIEVAL_INTENT_NUMERIC
        ),
        <,
        atm_evidence_authority_rank (
            canonical,
            ATM_RETRIEVAL_INTENT_NUMERIC
        )
    );

    atm_evidence_record_free (table);
    atm_evidence_record_free (canonical);
}

static void
test_structure_intent_keeps_canonical_and_structural_peers (void)
{
    AtmEvidenceRecord *canonical = new_record (
        "ewd:file:canonical-paradigm",
        ATM_SOURCE_ROLE_CANONICAL,
        ATM_EVIDENCE_MATCH_LEXICAL,
        TRUE,
        -8.0
    );
    AtmEvidenceRecord *structural = new_record (
        "ewd:file:structural-detail",
        ATM_SOURCE_ROLE_STRUCTURAL,
        ATM_EVIDENCE_MATCH_LEXICAL,
        TRUE,
        -2.0
    );

    g_assert_cmpuint (
        atm_evidence_authority_rank (
            canonical,
            ATM_RETRIEVAL_INTENT_STRUCTURE
        ),
        ==,
        atm_evidence_authority_rank (
            structural,
            ATM_RETRIEVAL_INTENT_STRUCTURE
        )
    );

    atm_evidence_record_free (structural);
    atm_evidence_record_free (canonical);
}

static void
test_structure_intent_prefers_declared_status_source (void)
{
    AtmEvidenceRecord *status = new_record (
        "cbd:file:STATUS.md",
        ATM_SOURCE_ROLE_STATUS |
            ATM_SOURCE_ROLE_CANONICAL,
        ATM_EVIDENCE_MATCH_LEXICAL,
        TRUE,
        -1.0
    );
    AtmEvidenceRecord *canonical = new_record (
        "cbd:file:README.md",
        ATM_SOURCE_ROLE_CANONICAL,
        ATM_EVIDENCE_MATCH_LEXICAL,
        TRUE,
        -100.0
    );
    AtmEvidenceRecord *structural = new_record (
        "cbd:entity:module:x",
        ATM_SOURCE_ROLE_STRUCTURAL,
        ATM_EVIDENCE_MATCH_LEXICAL,
        TRUE,
        -100.0
    );

    g_assert_cmpuint (
        atm_evidence_authority_rank (
            status,
            ATM_RETRIEVAL_INTENT_STRUCTURE
        ),
        <,
        atm_evidence_authority_rank (
            canonical,
            ATM_RETRIEVAL_INTENT_STRUCTURE
        )
    );
    g_assert_cmpuint (
        atm_evidence_authority_rank (
            status,
            ATM_RETRIEVAL_INTENT_STRUCTURE
        ),
        <,
        atm_evidence_authority_rank (
            structural,
            ATM_RETRIEVAL_INTENT_STRUCTURE
        )
    );

    atm_evidence_record_free (structural);
    atm_evidence_record_free (canonical);
    atm_evidence_record_free (status);
}

static void
test_exact_duplicate_wins_over_lexical_duplicate (void)
{
    GPtrArray *results = new_results ();
    GError *error = NULL;

    g_ptr_array_add (
        results,
        new_record (
            "rmd:entity:feedback_loop:x",
            ATM_SOURCE_ROLE_STRUCTURAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -50.0
        )
    );
    g_ptr_array_add (
        results,
        new_record (
            "rmd:entity:feedback_loop:x",
            ATM_SOURCE_ROLE_STRUCTURAL,
            ATM_EVIDENCE_MATCH_EXACT,
            FALSE,
            0.0
        )
    );

    g_assert_true (
        atm_retrieval_rank_and_deduplicate (
            results,
            ATM_RETRIEVAL_INTENT_STRUCTURE,
            10,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 1);

    AtmEvidenceRecord *record = g_ptr_array_index (
        results,
        0
    );
    g_assert_cmpint (
        record->match_kind,
        ==,
        ATM_EVIDENCE_MATCH_EXACT
    );
    g_assert_false (record->has_lexical_score);

    g_ptr_array_unref (results);
}

static void
test_same_authority_uses_bm25_order (void)
{
    GPtrArray *results = new_results ();
    GError *error = NULL;

    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:b",
            ATM_SOURCE_ROLE_CANONICAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -2.0
        )
    );
    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:a",
            ATM_SOURCE_ROLE_CANONICAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -8.0
        )
    );

    g_assert_true (
        atm_retrieval_rank_and_deduplicate (
            results,
            ATM_RETRIEVAL_INTENT_CURRENT_STATE,
            10,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpstr (
        ((AtmEvidenceRecord *) g_ptr_array_index (
            results,
            0
        ))->logical_source_id,
        ==,
        "ewd:file:a"
    );

    g_ptr_array_unref (results);
}

static void
test_rank_limit_truncates_after_deduplication (void)
{
    GPtrArray *results = new_results ();
    GError *error = NULL;

    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:a",
            ATM_SOURCE_ROLE_CANONICAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -3.0
        )
    );
    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:b",
            ATM_SOURCE_ROLE_STRUCTURAL,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -2.0
        )
    );
    g_ptr_array_add (
        results,
        new_record (
            "ewd:file:c",
            ATM_SOURCE_ROLE_EVIDENCE,
            ATM_EVIDENCE_MATCH_LEXICAL,
            TRUE,
            -1.0
        )
    );

    g_assert_true (
        atm_retrieval_rank_and_deduplicate (
            results,
            ATM_RETRIEVAL_INTENT_GENERAL,
            2,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (results->len, ==, 2);
    g_assert_cmpstr (
        ((AtmEvidenceRecord *) g_ptr_array_index (
            results,
            0
        ))->logical_source_id,
        ==,
        "ewd:file:a"
    );
    g_assert_cmpstr (
        ((AtmEvidenceRecord *) g_ptr_array_index (
            results,
            1
        ))->logical_source_id,
        ==,
        "ewd:file:b"
    );

    g_ptr_array_unref (results);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-rank/current-state-authority",
        test_current_state_authority_precedes_lexical_magnitude
    );
    g_test_add_func (
        "/retrieval-rank/current-state-status-source",
        test_current_state_prefers_declared_status_role
    );
    g_test_add_func (
        "/retrieval-rank/implementation-authority",
        test_implementation_intent_changes_authority_order
    );
    g_test_add_func (
        "/retrieval-rank/numeric-tabular-authority",
        test_numeric_intent_prefers_tabular_role
    );
    g_test_add_func (
        "/retrieval-rank/structure-canonical-peer",
        test_structure_intent_keeps_canonical_and_structural_peers
    );
    g_test_add_func (
        "/retrieval-rank/structure-status-source",
        test_structure_intent_prefers_declared_status_source
    );
    g_test_add_func (
        "/retrieval-rank/exact-dedup-wins",
        test_exact_duplicate_wins_over_lexical_duplicate
    );
    g_test_add_func (
        "/retrieval-rank/bm25-within-authority",
        test_same_authority_uses_bm25_order
    );
    g_test_add_func (
        "/retrieval-rank/truncate",
        test_rank_limit_truncates_after_deduplication
    );

    return g_test_run ();
}
