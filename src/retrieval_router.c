#include "retrieval_router.h"

#include "retrieval_query.h"
#include "retrieval_rank.h"

#include <string.h>

GQuark
atm_retrieval_router_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-router-error-quark"
    );
}

void
atm_repository_evidence_set_free (
    AtmRepositoryEvidenceSet *set
)
{
    if (set == NULL) {
        return;
    }

    g_free (set->repository_id);
    g_clear_pointer (&set->evidence, g_ptr_array_unref);
    g_free (set);
}

void
atm_retrieval_result_set_free (
    AtmRetrievalResultSet *results
)
{
    if (results == NULL) {
        return;
    }

    g_clear_pointer (&results->repositories, g_ptr_array_unref);
    g_free (results->expanded_query);
    g_free (results);
}

static gboolean
technical_identifier_character (char character)
{
    return g_ascii_isalnum (character) ||
        character == '_' ||
        character == '.' ||
        character == ':' ||
        character == '/' ||
        character == '-';
}

static void
trim_terminal_identifier_punctuation (char *candidate)
{
    if (candidate == NULL) {
        return;
    }

    gsize length = strlen (candidate);

    while (length > 0) {
        char terminal = candidate[length - 1];

        if (terminal != '.' &&
            terminal != ':' &&
            terminal != '/' &&
            terminal != '-') {
            break;
        }

        candidate[--length] = '\0';
    }
}

static gboolean
technical_identifier_shape (const char *candidate)
{
    if (candidate == NULL || candidate[0] == '\0') {
        return FALSE;
    }

    return strchr (candidate, '_') != NULL ||
        strchr (candidate, '.') != NULL ||
        strchr (candidate, ':') != NULL;
}

static void
add_unique_candidate (
    GPtrArray *candidates,
    GHashTable *seen,
    const char *candidate
)
{
    if (candidate == NULL ||
        candidate[0] == '\0' ||
        candidates->len >= ATM_RETRIEVAL_ROUTER_MAX_CANDIDATES ||
        g_hash_table_contains (seen, candidate)) {
        return;
    }

    g_hash_table_add (
        seen,
        g_strdup (candidate)
    );
    g_ptr_array_add (
        candidates,
        g_strdup (candidate)
    );
}

static GPtrArray *
extract_technical_candidates (const char *query)
{
    GPtrArray *candidates = g_ptr_array_new_with_free_func (g_free);
    GHashTable *seen = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );
    const char *cursor = query;

    while (*cursor != '\0' &&
           candidates->len < ATM_RETRIEVAL_ROUTER_MAX_CANDIDATES) {
        while (*cursor != '\0' &&
               !technical_identifier_character (*cursor)) {
            cursor++;
        }

        const char *start = cursor;

        while (*cursor != '\0' &&
               technical_identifier_character (*cursor)) {
            cursor++;
        }

        if (cursor > start) {
            char *candidate = g_strndup (
                start,
                cursor - start
            );

            trim_terminal_identifier_punctuation (
                candidate
            );

            if (technical_identifier_shape (candidate)) {
                add_unique_candidate (
                    candidates,
                    seen,
                    candidate
                );
            }

            g_free (candidate);
        }
    }

    g_hash_table_unref (seen);
    return candidates;
}

static gboolean
token_is_year (const char *token)
{
    if (token == NULL || strlen (token) != 4) {
        return FALSE;
    }

    for (guint i = 0; i < 4; i++) {
        if (!g_ascii_isdigit (token[i])) {
            return FALSE;
        }
    }

    guint year = (guint) g_ascii_strtoull (
        token,
        NULL,
        10
    );

    return year >= 1800 && year <= 2200;
}

static gboolean
token_is_quarter (const char *token)
{
    if (token == NULL || strlen (token) != 7) {
        return FALSE;
    }

    return g_ascii_isdigit (token[0]) &&
        g_ascii_isdigit (token[1]) &&
        g_ascii_isdigit (token[2]) &&
        g_ascii_isdigit (token[3]) &&
        token[4] == '-' &&
        (token[5] == 'q' || token[5] == 'Q') &&
        token[6] >= '1' &&
        token[6] <= '4';
}

static GPtrArray *
extract_row_key_candidates (
    const char *query,
    const GPtrArray *technical_candidates
)
{
    GPtrArray *candidates = g_ptr_array_new_with_free_func (g_free);
    GHashTable *seen = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );
    gchar **tokens = g_str_tokenize_and_fold (
        query,
        NULL,
        NULL
    );

    for (guint i = 0;
         tokens[i] != NULL &&
         candidates->len < ATM_RETRIEVAL_ROUTER_MAX_CANDIDATES;
         i++) {
        if (token_is_year (tokens[i])) {
            add_unique_candidate (
                candidates,
                seen,
                tokens[i]
            );
        }
    }

    for (const char *cursor = query;
         *cursor != '\0' &&
         candidates->len < ATM_RETRIEVAL_ROUTER_MAX_CANDIDATES;
         cursor++) {
        gsize remaining = strlen (cursor);

        if (remaining >= 7 &&
            g_ascii_isdigit (cursor[0]) &&
            g_ascii_isdigit (cursor[1]) &&
            g_ascii_isdigit (cursor[2]) &&
            g_ascii_isdigit (cursor[3]) &&
            cursor[4] == '-' &&
            (cursor[5] == 'Q' || cursor[5] == 'q') &&
            cursor[6] >= '1' &&
            cursor[6] <= '4' &&
            (cursor == query ||
             !technical_identifier_character (cursor[-1])) &&
            (remaining == 7 ||
             !technical_identifier_character (cursor[7]))) {
            char quarter[8] = { 0 };

            memcpy (quarter, cursor, 7);
            quarter[5] = 'Q';

            if (token_is_quarter (quarter)) {
                add_unique_candidate (
                    candidates,
                    seen,
                    quarter
                );
            }

            cursor += 6;
        }
    }

    for (guint i = 0;
         i < technical_candidates->len &&
         candidates->len < ATM_RETRIEVAL_ROUTER_MAX_CANDIDATES;
         i++) {
        add_unique_candidate (
            candidates,
            seen,
            g_ptr_array_index (
                (GPtrArray *) technical_candidates,
                i
            )
        );
    }

    g_strfreev (tokens);
    g_hash_table_unref (seen);
    return candidates;
}

static void
append_results (
    GPtrArray *destination,
    GPtrArray *source
)
{
    for (guint i = 0; i < source->len; i++) {
        g_ptr_array_add (
            destination,
            g_ptr_array_index (source, i)
        );
    }

    g_ptr_array_set_free_func (source, NULL);
    g_ptr_array_unref (source);
}

static gboolean
collect_repository_evidence (
    const AtmRetrievalRepositoryScope *scope,
    const AtmNormalizedQuery *normalized,
    const GPtrArray *technical_candidates,
    const GPtrArray *row_key_candidates,
    guint max_results,
    AtmRepositoryEvidenceSet **out_set,
    GError **error
)
{
    AtmRepositoryEvidenceSet *set = NULL;
    GPtrArray *collected = NULL;
    GPtrArray *partial = NULL;
    guint candidate_limit = MIN (
        ATM_RETRIEVAL_MAX_EXACT_RESULTS,
        MAX (max_results * 4, max_results)
    );

    g_return_val_if_fail (scope != NULL, FALSE);
    g_return_val_if_fail (normalized != NULL, FALSE);
    g_return_val_if_fail (out_set != NULL, FALSE);
    g_return_val_if_fail (*out_set == NULL, FALSE);

    collected = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_evidence_record_free
    );

    for (guint i = 0; i < technical_candidates->len; i++) {
        const char *candidate = g_ptr_array_index (
            (GPtrArray *) technical_candidates,
            i
        );

        if (!atm_retrieval_lookup_exact (
                scope->index_path,
                candidate,
                candidate_limit,
                &partial,
                error
            )) {
            goto out;
        }

        append_results (collected, partial);
        partial = NULL;
    }

    for (guint i = 0; i < row_key_candidates->len; i++) {
        const char *row_key = g_ptr_array_index (
            (GPtrArray *) row_key_candidates,
            i
        );

        if (!atm_retrieval_lookup_dataset_rows (
                scope->index_path,
                NULL,
                row_key,
                candidate_limit,
                &partial,
                error
            )) {
            goto out;
        }

        append_results (collected, partial);
        partial = NULL;
    }

    if (!atm_retrieval_search_fts (
            scope->index_path,
            normalized->expanded_text,
            candidate_limit,
            &partial,
            error
        )) {
        goto out;
    }

    append_results (collected, partial);
    partial = NULL;

    if (!atm_retrieval_rank_and_deduplicate (
            collected,
            normalized->intents,
            max_results,
            error
        )) {
        goto out;
    }

    for (guint i = 0; i < collected->len; i++) {
        const AtmEvidenceRecord *record =
            g_ptr_array_index (collected, i);

        if (record->repository_id == NULL ||
            g_strcmp0 (
                record->repository_id,
                scope->repository_id
            ) != 0) {
            g_set_error (
                error,
                ATM_RETRIEVAL_ROUTER_ERROR,
                ATM_RETRIEVAL_ROUTER_ERROR_PROVENANCE,
                "Retrieval evidence provenance repository '%s' "
                "does not match active scope '%s'.",
                record->repository_id != NULL
                    ? record->repository_id
                    : "(null)",
                scope->repository_id
            );
            goto out;
        }
    }

    set = g_new0 (AtmRepositoryEvidenceSet, 1);
    set->repository_id = g_strdup (scope->repository_id);
    set->evidence = g_steal_pointer (&collected);

    *out_set = g_steal_pointer (&set);
    return TRUE;

out:
    g_clear_pointer (&partial, g_ptr_array_unref);
    g_clear_pointer (&collected, g_ptr_array_unref);
    g_clear_pointer (&set, atm_repository_evidence_set_free);
    return FALSE;
}

gboolean
atm_retrieval_run (
    const char *query,
    const GPtrArray *active_repositories,
    guint max_results_per_repository,
    AtmRetrievalResultSet **out_results,
    GError **error
)
{
    AtmRetrievalScopeSelection *selection = NULL;
    AtmNormalizedQuery *normalized = NULL;
    AtmRetrievalResultSet *results = NULL;
    GPtrArray *technical_candidates = NULL;
    GPtrArray *row_key_candidates = NULL;
    gsize query_length;

    g_return_val_if_fail (query != NULL, FALSE);
    g_return_val_if_fail (active_repositories != NULL, FALSE);
    g_return_val_if_fail (out_results != NULL, FALSE);
    g_return_val_if_fail (*out_results == NULL, FALSE);

    query_length = strlen (query);

    if (query_length == 0 ||
        query_length > ATM_RETRIEVAL_ROUTER_MAX_QUERY_BYTES ||
        !g_utf8_validate (query, query_length, NULL) ||
        max_results_per_repository == 0 ||
        max_results_per_repository > ATM_RETRIEVAL_MAX_EXACT_RESULTS) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_ROUTER_ERROR,
            ATM_RETRIEVAL_ROUTER_ERROR_ARGUMENT,
            "Retrieval router arguments are invalid."
        );
        return FALSE;
    }

    if (!atm_retrieval_scope_select (
            query,
            active_repositories,
            &selection,
            error
        ) ||
        !atm_retrieval_normalize_query (
            query,
            &normalized,
            error
        )) {
        goto out;
    }

    technical_candidates = extract_technical_candidates (query);
    row_key_candidates = extract_row_key_candidates (
        query,
        technical_candidates
    );

    results = g_new0 (AtmRetrievalResultSet, 1);
    results->repositories = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_repository_evidence_set_free
    );
    results->expanded_query = g_strdup (
        normalized->expanded_text
    );
    results->intents = normalized->intents;
    results->explicit_scope = selection->explicit_scope;
    results->requested_outside_scope =
        selection->requested_outside_scope;

    if (selection->requested_outside_scope) {
        *out_results = g_steal_pointer (&results);

        g_ptr_array_unref (row_key_candidates);
        g_ptr_array_unref (technical_candidates);
        atm_normalized_query_free (normalized);
        atm_retrieval_scope_selection_free (selection);
        return TRUE;
    }

    for (guint i = 0;
         i < selection->repositories->len;
         i++) {
        const AtmRetrievalRepositoryScope *scope =
            g_ptr_array_index (
                selection->repositories,
                i
            );
        AtmRepositoryEvidenceSet *repository_set = NULL;

        if (!collect_repository_evidence (
                scope,
                normalized,
                technical_candidates,
                row_key_candidates,
                max_results_per_repository,
                &repository_set,
                error
            )) {
            goto out;
        }

        g_ptr_array_add (
            results->repositories,
            repository_set
        );
    }

    *out_results = g_steal_pointer (&results);

    g_ptr_array_unref (row_key_candidates);
    g_ptr_array_unref (technical_candidates);
    atm_normalized_query_free (normalized);
    atm_retrieval_scope_selection_free (selection);
    return TRUE;

out:
    g_clear_pointer (&row_key_candidates, g_ptr_array_unref);
    g_clear_pointer (&technical_candidates, g_ptr_array_unref);
    g_clear_pointer (&results, atm_retrieval_result_set_free);
    g_clear_pointer (&normalized, atm_normalized_query_free);
    g_clear_pointer (
        &selection,
        atm_retrieval_scope_selection_free
    );
    return FALSE;
}
