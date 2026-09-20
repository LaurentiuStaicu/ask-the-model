#include "retrieval_scope.h"

GQuark
atm_retrieval_scope_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-retrieval-scope-error-quark"
    );
}

AtmRetrievalRepositoryScope *
atm_retrieval_repository_scope_new (
    const char *repository_id,
    const char *index_path
)
{
    g_return_val_if_fail (repository_id != NULL, NULL);
    g_return_val_if_fail (index_path != NULL, NULL);

    AtmRetrievalRepositoryScope *scope = g_new0 (
        AtmRetrievalRepositoryScope,
        1
    );
    scope->repository_id = g_strdup (repository_id);
    scope->index_path = g_strdup (index_path);
    return scope;
}

void
atm_retrieval_repository_scope_free (
    AtmRetrievalRepositoryScope *scope
)
{
    if (scope == NULL) {
        return;
    }

    g_free (scope->repository_id);
    g_free (scope->index_path);
    g_free (scope);
}

void
atm_retrieval_scope_selection_free (
    AtmRetrievalScopeSelection *selection
)
{
    if (selection == NULL) {
        return;
    }

    g_clear_pointer (
        &selection->repositories,
        g_ptr_array_unref
    );
    g_free (selection);
}

typedef enum {
    REPOSITORY_BIT_EWD = 1u << 0,
    REPOSITORY_BIT_CBD = 1u << 1,
    REPOSITORY_BIT_RMD = 1u << 2
} RepositoryBit;

static guint
repository_bit (const char *repository_id)
{
    if (g_strcmp0 (repository_id, "ewd") == 0) {
        return REPOSITORY_BIT_EWD;
    }

    if (g_strcmp0 (repository_id, "cbd") == 0) {
        return REPOSITORY_BIT_CBD;
    }

    if (g_strcmp0 (repository_id, "rmd") == 0) {
        return REPOSITORY_BIT_RMD;
    }

    return 0;
}

static gboolean
tokens_contain (
    gchar **tokens,
    const char *value
)
{
    for (guint i = 0; tokens[i] != NULL; i++) {
        if (g_strcmp0 (tokens[i], value) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean
tokens_contain_sequence (
    gchar **tokens,
    const char *const *sequence,
    guint sequence_length
)
{
    guint token_count = g_strv_length (tokens);

    if (sequence_length == 0 ||
        token_count < sequence_length) {
        return FALSE;
    }

    for (guint start = 0;
         start + sequence_length <= token_count;
         start++) {
        gboolean matches = TRUE;

        for (guint offset = 0;
             offset < sequence_length;
             offset++) {
            if (g_strcmp0 (
                    tokens[start + offset],
                    sequence[offset]
                ) != 0) {
                matches = FALSE;
                break;
            }
        }

        if (matches) {
            return TRUE;
        }
    }

    return FALSE;
}

static guint
requested_repository_bits (gchar **tokens)
{
    static const char *ewd_name[] = {
        "empirical",
        "world3",
        "dynamics"
    };
    static const char *cbd_name[] = {
        "cognitive",
        "belief",
        "dynamics"
    };
    static const char *rmd_name[] = {
        "romanian",
        "monetary",
        "dynamics"
    };
    guint requested = 0;

    if (tokens_contain (tokens, "ewd") ||
        tokens_contain_sequence (
            tokens,
            ewd_name,
            G_N_ELEMENTS (ewd_name)
        )) {
        requested |= REPOSITORY_BIT_EWD;
    }

    if (tokens_contain (tokens, "cbd") ||
        tokens_contain_sequence (
            tokens,
            cbd_name,
            G_N_ELEMENTS (cbd_name)
        )) {
        requested |= REPOSITORY_BIT_CBD;
    }

    if (tokens_contain (tokens, "rmd") ||
        tokens_contain_sequence (
            tokens,
            rmd_name,
            G_N_ELEMENTS (rmd_name)
        )) {
        requested |= REPOSITORY_BIT_RMD;
    }

    return requested;
}

gboolean
atm_retrieval_scope_select (
    const char *query,
    const GPtrArray *active_repositories,
    AtmRetrievalScopeSelection **out_selection,
    GError **error
)
{
    AtmRetrievalScopeSelection *selection = NULL;
    gchar **tokens = NULL;
    guint active_bits = 0;
    guint requested_bits;
    gsize query_length;

    g_return_val_if_fail (query != NULL, FALSE);
    g_return_val_if_fail (active_repositories != NULL, FALSE);
    g_return_val_if_fail (out_selection != NULL, FALSE);
    g_return_val_if_fail (*out_selection == NULL, FALSE);

    query_length = strlen (query);

    if (query_length > ATM_RETRIEVAL_SCOPE_MAX_QUERY_BYTES ||
        !g_utf8_validate (query, query_length, NULL)) {
        g_set_error_literal (
            error,
            ATM_RETRIEVAL_SCOPE_ERROR,
            ATM_RETRIEVAL_SCOPE_ERROR_ARGUMENT,
            "Repository-scope query is invalid."
        );
        return FALSE;
    }

    for (guint i = 0; i < active_repositories->len; i++) {
        const AtmRetrievalRepositoryScope *scope =
            g_ptr_array_index (
                (GPtrArray *) active_repositories,
                i
            );
        guint bit;

        if (scope == NULL ||
            scope->repository_id == NULL ||
            scope->index_path == NULL ||
            scope->index_path[0] == '\0' ||
            (bit = repository_bit (scope->repository_id)) == 0 ||
            (active_bits & bit) != 0) {
            g_set_error_literal (
                error,
                ATM_RETRIEVAL_SCOPE_ERROR,
                ATM_RETRIEVAL_SCOPE_ERROR_ARGUMENT,
                "Active repository scope is invalid or duplicated."
            );
            return FALSE;
        }

        active_bits |= bit;
    }

    tokens = g_str_tokenize_and_fold (
        query,
        NULL,
        NULL
    );
    requested_bits = requested_repository_bits (tokens);

    selection = g_new0 (
        AtmRetrievalScopeSelection,
        1
    );
    selection->repositories = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );
    selection->explicit_scope = requested_bits != 0;
    selection->requested_outside_scope =
        (requested_bits & ~active_bits) != 0;

    for (guint i = 0; i < active_repositories->len; i++) {
        const AtmRetrievalRepositoryScope *scope =
            g_ptr_array_index (
                (GPtrArray *) active_repositories,
                i
            );
        guint bit = repository_bit (scope->repository_id);

        if (requested_bits != 0 &&
            (requested_bits & bit) == 0) {
            continue;
        }

        g_ptr_array_add (
            selection->repositories,
            atm_retrieval_repository_scope_new (
                scope->repository_id,
                scope->index_path
            )
        );
    }

    g_strfreev (tokens);
    *out_selection = g_steal_pointer (&selection);
    return TRUE;
}
