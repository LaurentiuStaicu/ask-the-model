#include "conversation_grounding.h"

#include "retrieval_index_lifecycle.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (path == NULL ||
        g_lstat (path, &stat_buffer) != 0) {
        return;
    }

    if (!S_ISDIR (stat_buffer.st_mode) ||
        S_ISLNK (stat_buffer.st_mode)) {
        g_remove (path);
        return;
    }

    GError *error = NULL;
    GDir *directory = g_dir_open (path, 0, &error);

    if (directory != NULL) {
        const char *name;

        while ((name = g_dir_read_name (
                    directory
                )) != NULL) {
            char *child = g_build_filename (
                path,
                name,
                NULL
            );
            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static char *
new_temp_root (const char *prefix)
{
    GError *error = NULL;
    char *root = g_dir_make_tmp (
        prefix,
        &error
    );

    g_assert_no_error (error);
    g_assert_nonnull (root);
    return root;
}

static void
write_text (
    const char *root,
    const char *relative,
    const char *contents
)
{
    GError *error = NULL;
    char *path = g_build_filename (
        root,
        relative,
        NULL
    );
    char *parent = g_path_get_dirname (path);

    g_assert_cmpint (
        g_mkdir_with_parents (
            parent,
            0700
        ),
        ==,
        0
    );
    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (parent);
    g_free (path);
}

static char *
new_snapshot (
    const char *repository_id,
    const char *acronym,
    const char *display_name
)
{
    char *root = new_temp_root (
        "atm-conversation-pin-snapshot-XXXXXX"
    );
    char *manifest = g_strdup_printf (
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"%s\",\n"
        "  \"acronym\": \"%s\",\n"
        "  \"display_name\": \"%s\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": [\"STATUS.md\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [],\n"
        "    \"evidence\": [],\n"
        "    \"tabular\": [],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n",
        repository_id,
        acronym,
        display_name
    );

    write_text (
        root,
        ".atm/repository.json",
        manifest
    );
    write_text (
        root,
        "CITATION.cff",
        "cff-version: 1.2.0\n"
        "message: Cite this software.\n"
        "title: Fixture\n"
        "version: 0.1.0\n"
        "authors:\n"
        "  - family-names: Test\n"
        "    given-names: Fixture\n"
    );
    write_text (
        root,
        "STATUS.md",
        "# Scientific status\n"
        "Current fixture status.\n"
    );

    g_free (manifest);
    return root;
}

static char *
ensure_index (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_version
)
{
    char *index_path = NULL;
    AtmRetrievalEnsureResult ensure_result;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_index_ensure_for_snapshot (
            cache_root,
            snapshot_root,
            repository_id,
            snapshot_sha,
            &index_path,
            out_version,
            &ensure_result,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpint (
        ensure_result,
        ==,
        ATM_RETRIEVAL_ENSURE_REBUILT
    );

    return index_path;
}

static void
test_zero_repository_scope_can_freeze (void)
{
    AtmConversationGroundingState *state =
        atm_conversation_grounding_state_new ();
    GPtrArray *scopes = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_conversation_grounding_is_frozen (state)
    );
    g_assert_cmpuint (
        atm_conversation_grounding_repository_count (
            state
        ),
        ==,
        0
    );

    g_assert_false (
        atm_conversation_grounding_create_retrieval_scopes (
            state,
            &scopes,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_GROUNDING_ERROR,
        ATM_CONVERSATION_GROUNDING_ERROR_NOT_FROZEN
    );
    g_assert_null (scopes);
    g_clear_error (&error);

    g_assert_true (
        atm_conversation_grounding_freeze (
            state,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_conversation_grounding_is_frozen (state)
    );

    g_assert_true (
        atm_conversation_grounding_create_retrieval_scopes (
            state,
            &scopes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (scopes->len, ==, 0);

    gboolean has_grounding = TRUE;
    gboolean needs_clarification = TRUE;
    char *system_instructions = NULL;
    char *evidence_text = NULL;
    char *post_evidence_reminder = NULL;

    g_assert_true (
        atm_conversation_grounding_prepare_turn (
            state,
            "ordinary local chat",
            &has_grounding,
            &needs_clarification,
            &system_instructions,
            &evidence_text,
            &post_evidence_reminder,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (has_grounding);
    g_assert_false (needs_clarification);
    g_assert_null (system_instructions);
    g_assert_null (evidence_text);
    g_assert_null (post_evidence_reminder);

    g_ptr_array_unref (scopes);
    atm_conversation_grounding_state_free (state);
}

static void
test_valid_pins_are_canonical_and_frozen (void)
{
    const char *ewd_sha =
        "1111111111111111111111111111111111111111";
    const char *rmd_sha =
        "2222222222222222222222222222222222222222";
    char *cache_root = new_temp_root (
        "atm-conversation-pin-cache-XXXXXX"
    );
    char *ewd_root = new_snapshot (
        "ewd",
        "EWD",
        "Empirical World3 Dynamics"
    );
    char *rmd_root = new_snapshot (
        "rmd",
        "RMD",
        "Romanian Monetary Dynamics"
    );
    char *ewd_version = NULL;
    char *rmd_version = NULL;
    char *ewd_index = ensure_index (
        cache_root,
        ewd_root,
        "ewd",
        ewd_sha,
        &ewd_version
    );
    char *rmd_index = ensure_index (
        cache_root,
        rmd_root,
        "rmd",
        rmd_sha,
        &rmd_version
    );
    AtmConversationGroundingState *state =
        atm_conversation_grounding_state_new ();
    GError *error = NULL;

    g_assert_true (
        atm_conversation_grounding_add_ready_repository (
            state,
            "rmd",
            rmd_version,
            rmd_sha,
            rmd_root,
            rmd_index,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_true (
        atm_conversation_grounding_add_ready_repository (
            state,
            "ewd",
            ewd_version,
            ewd_sha,
            ewd_root,
            ewd_index,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_cmpuint (
        atm_conversation_grounding_repository_count (
            state
        ),
        ==,
        2
    );

    const AtmConversationRepositoryPin *first =
        atm_conversation_grounding_repository_at (
            state,
            0
        );
    const AtmConversationRepositoryPin *second =
        atm_conversation_grounding_repository_at (
            state,
            1
        );

    g_assert_nonnull (first);
    g_assert_nonnull (second);
    g_assert_cmpstr (first->repository_id, ==, "ewd");
    g_assert_cmpstr (first->snapshot_sha, ==, ewd_sha);
    g_assert_cmpstr (first->repository_version, ==, "0.1.0");
    g_assert_cmpstr (second->repository_id, ==, "rmd");
    g_assert_cmpstr (second->snapshot_sha, ==, rmd_sha);

    g_assert_true (
        atm_conversation_grounding_freeze (
            state,
            &error
        )
    );
    g_assert_no_error (error);

    GPtrArray *scopes = NULL;

    g_assert_true (
        atm_conversation_grounding_create_retrieval_scopes (
            state,
            &scopes,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (scopes->len, ==, 2);

    AtmRetrievalRepositoryScope *scope0 =
        g_ptr_array_index (scopes, 0);
    AtmRetrievalRepositoryScope *scope1 =
        g_ptr_array_index (scopes, 1);

    g_assert_cmpstr (scope0->repository_id, ==, "ewd");
    g_assert_cmpstr (scope0->index_path, ==, ewd_index);
    g_assert_cmpstr (scope1->repository_id, ==, "rmd");
    g_assert_cmpstr (scope1->index_path, ==, rmd_index);

    g_assert_false (
        atm_conversation_grounding_add_ready_repository (
            state,
            "cbd",
            "0.1.0",
            ewd_sha,
            ewd_root,
            ewd_index,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_GROUNDING_ERROR,
        ATM_CONVERSATION_GROUNDING_ERROR_FROZEN
    );
    g_clear_error (&error);

    gboolean has_grounding = FALSE;
    gboolean needs_clarification = FALSE;
    char *system_instructions = NULL;
    char *evidence_text = NULL;
    char *post_evidence_reminder = NULL;

    g_assert_true (
        atm_conversation_grounding_prepare_turn (
            state,
            "What is the current fixture status in EWD?",
            &has_grounding,
            &needs_clarification,
            &system_instructions,
            &evidence_text,
            &post_evidence_reminder,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (has_grounding);
    g_assert_false (needs_clarification);
    g_assert_nonnull (system_instructions);
    g_assert_nonnull (evidence_text);
    g_assert_nonnull (post_evidence_reminder);
    g_assert_nonnull (
        g_strstr_len (
            evidence_text,
            -1,
            "Current fixture status."
        )
    );
    g_assert_nonnull (
        g_strstr_len (
            evidence_text,
            -1,
            "repository_id: ewd"
        )
    );

    AtmCitationResolution *ewd_resolution = NULL;

    g_assert_true (
        atm_conversation_grounding_resolve_turn_citations (
            state,
            "EWD is supported [S1]. Unknown [S9].",
            &ewd_resolution,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        ewd_resolution->citations->len,
        ==,
        1
    );
    g_assert_cmpuint (
        ewd_resolution->unknown_labels->len,
        ==,
        1
    );

    AtmCitationReference *ewd_citation =
        g_ptr_array_index (
            ewd_resolution->citations,
            0
        );

    g_assert_cmpstr (
        ewd_citation->repository_id,
        ==,
        "ewd"
    );
    g_assert_cmpstr (
        ewd_citation->snapshot_sha,
        ==,
        ewd_sha
    );

    g_clear_pointer (
        &post_evidence_reminder,
        g_free
    );
    g_clear_pointer (&evidence_text, g_free);
    g_clear_pointer (&system_instructions, g_free);

    has_grounding = FALSE;
    needs_clarification = FALSE;

    g_assert_true (
        atm_conversation_grounding_prepare_turn (
            state,
            "What is the current fixture status in RMD?",
            &has_grounding,
            &needs_clarification,
            &system_instructions,
            &evidence_text,
            &post_evidence_reminder,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (has_grounding);
    g_assert_false (needs_clarification);

    AtmCitationResolution *rmd_resolution = NULL;

    g_assert_true (
        atm_conversation_grounding_resolve_turn_citations (
            state,
            "RMD is supported [S1].",
            &rmd_resolution,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (
        rmd_resolution->citations->len,
        ==,
        1
    );

    AtmCitationReference *rmd_citation =
        g_ptr_array_index (
            rmd_resolution->citations,
            0
        );

    g_assert_cmpstr (
        rmd_citation->repository_id,
        ==,
        "rmd"
    );
    g_assert_cmpstr (
        rmd_citation->snapshot_sha,
        ==,
        rmd_sha
    );

    /*
     * The first resolution is a deep copy. Preparing a later turn must
     * not rebind its [S1] provenance to the new RMD context.
     */
    g_assert_cmpstr (
        ewd_citation->repository_id,
        ==,
        "ewd"
    );
    g_assert_cmpstr (
        ewd_citation->snapshot_sha,
        ==,
        ewd_sha
    );

    atm_citation_resolution_free (rmd_resolution);
    atm_citation_resolution_free (ewd_resolution);
    g_free (post_evidence_reminder);
    g_free (evidence_text);
    g_free (system_instructions);

    g_ptr_array_unref (scopes);
    atm_conversation_grounding_state_free (state);
    g_free (rmd_index);
    g_free (ewd_index);
    g_free (rmd_version);
    g_free (ewd_version);
    remove_tree_best_effort (rmd_root);
    remove_tree_best_effort (ewd_root);
    remove_tree_best_effort (cache_root);
    g_free (rmd_root);
    g_free (ewd_root);
    g_free (cache_root);
}

static void
test_version_mismatch_is_rejected (void)
{
    const char *sha =
        "3333333333333333333333333333333333333333";
    char *cache_root = new_temp_root (
        "atm-conversation-pin-cache-XXXXXX"
    );
    char *root = new_snapshot (
        "ewd",
        "EWD",
        "Empirical World3 Dynamics"
    );
    char *version = NULL;
    char *index = ensure_index (
        cache_root,
        root,
        "ewd",
        sha,
        &version
    );
    AtmConversationGroundingState *state =
        atm_conversation_grounding_state_new ();
    GError *error = NULL;

    g_assert_false (
        atm_conversation_grounding_add_ready_repository (
            state,
            "ewd",
            "9.9.9",
            sha,
            root,
            index,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_GROUNDING_ERROR,
        ATM_CONVERSATION_GROUNDING_ERROR_VERSION
    );
    g_assert_cmpuint (
        atm_conversation_grounding_repository_count (
            state
        ),
        ==,
        0
    );

    g_clear_error (&error);
    atm_conversation_grounding_state_free (state);
    g_free (index);
    g_free (version);
    remove_tree_best_effort (root);
    remove_tree_best_effort (cache_root);
    g_free (root);
    g_free (cache_root);
}

static void
test_snapshot_mismatch_and_duplicate_are_rejected (void)
{
    const char *sha =
        "4444444444444444444444444444444444444444";
    const char *other_sha =
        "5555555555555555555555555555555555555555";
    char *cache_root = new_temp_root (
        "atm-conversation-pin-cache-XXXXXX"
    );
    char *root = new_snapshot (
        "rmd",
        "RMD",
        "Romanian Monetary Dynamics"
    );
    char *version = NULL;
    char *index = ensure_index (
        cache_root,
        root,
        "rmd",
        sha,
        &version
    );
    AtmConversationGroundingState *state =
        atm_conversation_grounding_state_new ();
    GError *error = NULL;

    g_assert_false (
        atm_conversation_grounding_add_ready_repository (
            state,
            "rmd",
            version,
            other_sha,
            root,
            index,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_GROUNDING_ERROR,
        ATM_CONVERSATION_GROUNDING_ERROR_VALIDATION
    );
    g_clear_error (&error);

    g_assert_true (
        atm_conversation_grounding_add_ready_repository (
            state,
            "rmd",
            version,
            sha,
            root,
            index,
            &error
        )
    );
    g_assert_no_error (error);

    g_assert_false (
        atm_conversation_grounding_add_ready_repository (
            state,
            "rmd",
            version,
            sha,
            root,
            index,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_GROUNDING_ERROR,
        ATM_CONVERSATION_GROUNDING_ERROR_DUPLICATE
    );

    g_clear_error (&error);
    atm_conversation_grounding_state_free (state);
    g_free (index);
    g_free (version);
    remove_tree_best_effort (root);
    remove_tree_best_effort (cache_root);
    g_free (root);
    g_free (cache_root);
}

static void
test_unknown_repository_and_uppercase_sha_are_rejected (void)
{
    AtmConversationGroundingState *state =
        atm_conversation_grounding_state_new ();
    GError *error = NULL;

    g_assert_false (
        atm_conversation_grounding_add_ready_repository (
            state,
            "other",
            "0.1.0",
            "6666666666666666666666666666666666666666",
            "/tmp/snapshot",
            "/tmp/index",
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_GROUNDING_ERROR,
        ATM_CONVERSATION_GROUNDING_ERROR_ARGUMENT
    );
    g_clear_error (&error);

    g_assert_false (
        atm_conversation_grounding_add_ready_repository (
            state,
            "ewd",
            "0.1.0",
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
            "/tmp/snapshot",
            "/tmp/index",
            &error
        )
    );
    g_assert_error (
        error,
        ATM_CONVERSATION_GROUNDING_ERROR,
        ATM_CONVERSATION_GROUNDING_ERROR_ARGUMENT
    );

    g_clear_error (&error);
    atm_conversation_grounding_state_free (state);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/conversation-grounding/zero-scope",
        test_zero_repository_scope_can_freeze
    );
    g_test_add_func (
        "/conversation-grounding/canonical-freeze",
        test_valid_pins_are_canonical_and_frozen
    );
    g_test_add_func (
        "/conversation-grounding/version-mismatch",
        test_version_mismatch_is_rejected
    );
    g_test_add_func (
        "/conversation-grounding/snapshot-and-duplicate",
        test_snapshot_mismatch_and_duplicate_are_rejected
    );
    g_test_add_func (
        "/conversation-grounding/invalid-identity",
        test_unknown_repository_and_uppercase_sha_are_rejected
    );

    return g_test_run ();
}
