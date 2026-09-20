#include "retrieval_rank.h"

static guint
minimum_role_rank (
    guint roles,
    const guint role_ranks[6]
)
{
    static const guint role_bits[6] = {
        ATM_SOURCE_ROLE_STATUS,
        ATM_SOURCE_ROLE_CANONICAL,
        ATM_SOURCE_ROLE_STRUCTURAL,
        ATM_SOURCE_ROLE_EVIDENCE,
        ATM_SOURCE_ROLE_TABULAR,
        ATM_SOURCE_ROLE_IMPLEMENTATION
    };
    guint rank = 6;

    for (guint i = 0; i < 6; i++) {
        if ((roles & role_bits[i]) != 0) {
            rank = MIN (rank, role_ranks[i]);
        }
    }

    return rank;
}

static guint
authority_rank_for_intent (
    const AtmEvidenceRecord *record,
    AtmRetrievalIntent intent
)
{
    static const guint general[6] = {
        0, 0, 1, 2, 2, 3
    };
    static const guint current_state[6] = {
        0, 1, 2, 2, 3, 4
    };
    static const guint structure[6] = {
        0, 1, 1, 2, 3, 2
    };
    static const guint evidence[6] = {
        1, 1, 2, 0, 0, 3
    };
    static const guint numeric[6] = {
        2, 2, 3, 1, 0, 4
    };
    static const guint implementation[6] = {
        2, 2, 1, 2, 3, 0
    };
    const guint *ranks = general;

    switch (intent) {
        case ATM_RETRIEVAL_INTENT_CURRENT_STATE:
            ranks = current_state;
            break;
        case ATM_RETRIEVAL_INTENT_STRUCTURE:
            ranks = structure;
            break;
        case ATM_RETRIEVAL_INTENT_EVIDENCE:
            ranks = evidence;
            break;
        case ATM_RETRIEVAL_INTENT_NUMERIC:
            ranks = numeric;
            break;
        case ATM_RETRIEVAL_INTENT_IMPLEMENTATION:
            ranks = implementation;
            break;
        case ATM_RETRIEVAL_INTENT_GENERAL:
        default:
            ranks = general;
            break;
    }

    return minimum_role_rank (
        record->source_roles,
        ranks
    );
}

guint
atm_evidence_authority_rank (
    const AtmEvidenceRecord *record,
    guint intents
)
{
    static const AtmRetrievalIntent supported[] = {
        ATM_RETRIEVAL_INTENT_CURRENT_STATE,
        ATM_RETRIEVAL_INTENT_STRUCTURE,
        ATM_RETRIEVAL_INTENT_EVIDENCE,
        ATM_RETRIEVAL_INTENT_NUMERIC,
        ATM_RETRIEVAL_INTENT_IMPLEMENTATION
    };
    guint rank = 5;
    gboolean matched_intent = FALSE;

    g_return_val_if_fail (record != NULL, 5);

    for (gsize i = 0; i < G_N_ELEMENTS (supported); i++) {
        if ((intents & supported[i]) == 0) {
            continue;
        }

        matched_intent = TRUE;
        rank = MIN (
            rank,
            authority_rank_for_intent (
                record,
                supported[i]
            )
        );
    }

    if (!matched_intent) {
        return authority_rank_for_intent (
            record,
            ATM_RETRIEVAL_INTENT_GENERAL
        );
    }

    return rank;
}

static gint
compare_evidence (
    gconstpointer left,
    gconstpointer right,
    gpointer user_data
)
{
    const AtmEvidenceRecord *a =
        *(AtmEvidenceRecord *const *) left;
    const AtmEvidenceRecord *b =
        *(AtmEvidenceRecord *const *) right;
    guint intents = GPOINTER_TO_UINT (user_data);

    if (a->match_kind != b->match_kind) {
        return a->match_kind < b->match_kind ? -1 : 1;
    }

    guint authority_a = atm_evidence_authority_rank (
        a,
        intents
    );
    guint authority_b = atm_evidence_authority_rank (
        b,
        intents
    );

    if (authority_a != authority_b) {
        return authority_a < authority_b ? -1 : 1;
    }

    if (a->has_lexical_score != b->has_lexical_score) {
        return a->has_lexical_score ? 1 : -1;
    }

    if (a->has_lexical_score &&
        b->has_lexical_score &&
        a->lexical_score != b->lexical_score) {
        return a->lexical_score < b->lexical_score
            ? -1
            : 1;
    }

    gint order = g_strcmp0 (
        a->logical_source_id,
        b->logical_source_id
    );

    if (order != 0) {
        return order;
    }

    order = g_strcmp0 (
        a->source_path,
        b->source_path
    );

    if (order != 0) {
        return order;
    }

    return g_strcmp0 (
        a->locator,
        b->locator
    );
}

gboolean
atm_retrieval_rank_and_deduplicate (
    GPtrArray *results,
    guint intents,
    guint max_results,
    GError **error
)
{
    GHashTable *seen = NULL;

    g_return_val_if_fail (results != NULL, FALSE);

    if (max_results == 0 ||
        max_results > ATM_RETRIEVAL_MAX_EXACT_RESULTS) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_QUERY_ERROR,
            ATM_RETRIEVAL_QUERY_ERROR_ARGUMENT,
            "Ranked retrieval result limit is invalid."
        );
        return FALSE;
    }

    g_ptr_array_sort_with_data (
        results,
        compare_evidence,
        GUINT_TO_POINTER (intents)
    );

    seen = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );

    for (guint i = 0; i < results->len;) {
        AtmEvidenceRecord *record = g_ptr_array_index (
            results,
            i
        );

        if (record->logical_source_id != NULL &&
            g_hash_table_contains (
                seen,
                record->logical_source_id
            )) {
            g_ptr_array_remove_index (results, i);
            continue;
        }

        if (record->logical_source_id != NULL) {
            g_hash_table_add (
                seen,
                g_strdup (record->logical_source_id)
            );
        }

        i++;
    }

    while (results->len > max_results) {
        g_ptr_array_remove_index (
            results,
            results->len - 1
        );
    }

    g_hash_table_unref (seen);
    return TRUE;
}
