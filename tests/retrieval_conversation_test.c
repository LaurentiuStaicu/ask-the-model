#include "retrieval_conversation.h"

#include "retrieval_index.h"
#include "repository_sources.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sys/stat.h>

static void
remove_tree_best_effort (const char *path)
{
    GStatBuf stat_buffer;

    if (g_lstat (path, &stat_buffer) != 0) {
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

        while ((name = g_dir_read_name (directory)) != NULL) {
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
new_ewd_snapshot (void)
{
    char *root = new_temp_root (
        "atm-conversation-ewd-XXXXXX"
    );

    write_text (
        root,
        ".atm/repository.json",
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"ewd\",\n"
        "  \"acronym\": \"EWD\",\n"
        "  \"display_name\": \"Empirical World3 Dynamics\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": ["
        "\"STATUS.md\", \"model/core.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [\"model\"],\n"
        "    \"evidence\": [\"data\"],\n"
        "    \"tabular\": [\"data\"],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n"
    );

    write_text (
        root,
        "STATUS.md",
        "# Scientific status\n"
        "EWD current status and system boundary.\n"
    );
    write_text (
        root,
        "model/core.json",
        "{"
        "\"variables\":[{"
            "\"id\":\"food_per_capita\","
            "\"label\":{\"en\":\"Food per capita\"}"
        "}]"
        "}\n"
    );
    write_text (
        root,
        "data/series.csv",
        "year,value\n"
        "2024,0.9\n"
        "2025,1.0\n"
    );

    return root;
}

static char *
new_rmd_snapshot (void)
{
    char *root = new_temp_root (
        "atm-conversation-rmd-XXXXXX"
    );

    write_text (
        root,
        ".atm/repository.json",
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"repository_id\": \"rmd\",\n"
        "  \"acronym\": \"RMD\",\n"
        "  \"display_name\": \"Romanian Monetary Dynamics\",\n"
        "  \"version_source\": {"
        "\"type\": \"cff\", \"path\": \"CITATION.cff\"},\n"
        "  \"status_source\": \"STATUS.md\",\n"
        "  \"required_paths\": ["
        "\"STATUS.md\", \"model/dynamics/feedback_registry.json\"],\n"
        "  \"retrieval\": {\n"
        "    \"canonical\": [\"STATUS.md\"],\n"
        "    \"structural\": [\"model/dynamics\"],\n"
        "    \"evidence\": [\"data\"],\n"
        "    \"tabular\": [\"data\"],\n"
        "    \"implementation\": [],\n"
        "    \"exclude\": []\n"
        "  }\n"
        "}\n"
    );

    write_text (
        root,
        "STATUS.md",
        "# Scientific status\n"
        "RMD current status and accounting boundary.\n"
    );
    write_text (
        root,
        "model/dynamics/feedback_registry.json",
        "{"
        "\"loops\":[{"
            "\"id\":\"government_refinancing_interest_loop\","
            "\"label\":{"
                "\"en\":\"Government refinancing interest loop\""
            "}"
        "}]"
        "}\n"
    );
    write_text (
        root,
        "data/series.csv",
        "year,value\n"
        "2024,2.0\n"
        "2025,2.1\n"
    );

    return root;
}

static char *
build_index (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha
)
{
    AtmSourceCatalog *catalog = NULL;
    char *index_path = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_repository_source_catalog_build (
            snapshot_root,
            repository_id,
            &catalog,
            &error
        )
    );
    g_assert_no_error (error);

    AtmRetrievalIndexMetadata metadata = {
        .repository_id = repository_id,
        .repository_version = "0.1.0",
        .snapshot_sha = snapshot_sha,
        .manifest_schema_version = 1,
        .manifest_sha256 = catalog->manifest_sha256,
        .created_at_utc = "2026-09-20T00:00:00Z"
    };

    g_assert_true (
        atm_retrieval_index_create_with_content (
            cache_root,
            snapshot_root,
            &metadata,
            catalog,
            &index_path,
            &error
        )
    );
    g_assert_no_error (error);

    atm_source_catalog_free (catalog);
    return index_path;
}

typedef struct {
    char *cache_root;
    char *ewd_root;
    char *rmd_root;
    char *ewd_index;
    char *rmd_index;
    GPtrArray *scopes;
} Fixture;

static Fixture *
fixture_new (void)
{
    Fixture *fixture = g_new0 (Fixture, 1);

    fixture->cache_root = new_temp_root (
        "atm-conversation-cache-XXXXXX"
    );
    fixture->ewd_root = new_ewd_snapshot ();
    fixture->rmd_root = new_rmd_snapshot ();
    fixture->ewd_index = build_index (
        fixture->cache_root,
        fixture->ewd_root,
        "ewd",
        "1111111111111111111111111111111111111111"
    );
    fixture->rmd_index = build_index (
        fixture->cache_root,
        fixture->rmd_root,
        "rmd",
        "2222222222222222222222222222222222222222"
    );

    fixture->scopes = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );
    g_ptr_array_add (
        fixture->scopes,
        atm_retrieval_repository_scope_new (
            "ewd",
            fixture->ewd_index
        )
    );
    g_ptr_array_add (
        fixture->scopes,
        atm_retrieval_repository_scope_new (
            "rmd",
            fixture->rmd_index
        )
    );

    return fixture;
}

static void
fixture_free (Fixture *fixture)
{
    if (fixture == NULL) {
        return;
    }

    g_clear_pointer (&fixture->scopes, g_ptr_array_unref);
    g_free (fixture->rmd_index);
    g_free (fixture->ewd_index);

    remove_tree_best_effort (fixture->rmd_root);
    remove_tree_best_effort (fixture->ewd_root);
    remove_tree_best_effort (fixture->cache_root);

    g_free (fixture->rmd_root);
    g_free (fixture->ewd_root);
    g_free (fixture->cache_root);
    g_free (fixture);
}

static AtmRetrievalConversationState *
new_state (Fixture *fixture)
{
    GError *error = NULL;
    AtmRetrievalConversationState *state =
        atm_retrieval_conversation_state_new (
            fixture->scopes,
            &error
        );

    g_assert_no_error (error);
    g_assert_nonnull (state);
    return state;
}

static AtmRetrievalConversationTurn *
run_turn (
    AtmRetrievalConversationState *state,
    const char *query
)
{
    AtmRetrievalConversationTurn *turn = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_conversation_run (
            state,
            query,
            10,
            &turn,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (turn);
    return turn;
}

static AtmRepositoryEvidenceSet *
find_set (
    AtmRetrievalResultSet *results,
    const char *repository_id
)
{
    for (guint i = 0; i < results->repositories->len; i++) {
        AtmRepositoryEvidenceSet *set = g_ptr_array_index (
            results->repositories,
            i
        );

        if (g_strcmp0 (
                set->repository_id,
                repository_id
            ) == 0) {
            return set;
        }
    }

    return NULL;
}

static AtmEvidenceRecord *
find_match (
    AtmRepositoryEvidenceSet *set,
    AtmEvidenceMatch kind,
    const char *title
)
{
    for (guint i = 0; i < set->evidence->len; i++) {
        AtmEvidenceRecord *record = g_ptr_array_index (
            set->evidence,
            i
        );

        if (record->match_kind != kind) {
            continue;
        }

        if (title == NULL ||
            g_strcmp0 (record->title, title) == 0) {
            return record;
        }
    }

    return NULL;
}

static void
test_year_followup_inherits_scope_and_exact_anchor (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "food_per_capita EWD"
    );

    g_assert_false (first->needs_clarification);
    g_assert_nonnull (first->retrieval);
    g_assert_cmpuint (
        first->retrieval->repositories->len,
        ==,
        1
    );

    AtmRepositoryEvidenceSet *ewd = find_set (
        first->retrieval,
        "ewd"
    );

    g_assert_nonnull (ewd);
    AtmEvidenceRecord *exact = find_match (
        ewd,
        ATM_EVIDENCE_MATCH_EXACT,
        NULL
    );
    g_assert_nonnull (exact);
    g_assert_cmpstr (
        exact->logical_source_id,
        ==,
        "ewd:entity:variable:food_per_capita"
    );

    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *second = run_turn (
        state,
        "Dar în 2025?"
    );

    g_assert_false (second->needs_clarification);
    g_assert_true (second->used_previous_scope);
    g_assert_true (second->used_previous_anchor);
    g_assert_false (second->used_previous_intent);
    g_assert_nonnull (
        strstr (
            second->effective_query,
            "ewd:entity:variable:food_per_capita"
        )
    );
    g_assert_cmpuint (
        second->retrieval->repositories->len,
        ==,
        1
    );

    ewd = find_set (second->retrieval, "ewd");
    g_assert_nonnull (ewd);
    g_assert_nonnull (
        find_match (
            ewd,
            ATM_EVIDENCE_MATCH_EXACT,
            NULL
        )
    );
    g_assert_nonnull (
        find_match (
            ewd,
            ATM_EVIDENCE_MATCH_TABULAR,
            "2025"
        )
    );

    atm_retrieval_conversation_turn_free (second);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_repository_followup_inherits_current_state_intent (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "Care este starea curentă în EWD?"
    );
    g_assert_false (first->needs_clarification);
    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *second = run_turn (
        state,
        "Și în RMD?"
    );

    g_assert_false (second->needs_clarification);
    g_assert_true (second->used_previous_intent);
    g_assert_false (second->used_previous_anchor);
    g_assert_nonnull (
        strstr (
            second->effective_query,
            "current status"
        )
    );
    g_assert_cmpuint (
        second->retrieval->repositories->len,
        ==,
        1
    );

    AtmRepositoryEvidenceSet *rmd = find_set (
        second->retrieval,
        "rmd"
    );
    g_assert_nonnull (rmd);
    g_assert_cmpuint (rmd->evidence->len, >, 0);

    AtmEvidenceRecord *top = g_ptr_array_index (
        rmd->evidence,
        0
    );
    g_assert_cmpstr (top->source_path, ==, "STATUS.md");

    atm_retrieval_conversation_turn_free (second);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_comparison_followup_unions_previous_and_new_repository (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "Care este starea curentă în EWD?"
    );
    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *second = run_turn (
        state,
        "Compară cu RMD"
    );

    g_assert_false (second->needs_clarification);
    g_assert_true (second->comparison_followup);
    g_assert_true (second->used_previous_scope);
    g_assert_true (second->used_previous_intent);
    g_assert_nonnull (strstr (second->effective_query, "ewd"));
    g_assert_nonnull (strstr (second->effective_query, "rmd"));
    g_assert_cmpuint (
        second->retrieval->repositories->len,
        ==,
        2
    );
    g_assert_nonnull (find_set (second->retrieval, "ewd"));
    g_assert_nonnull (find_set (second->retrieval, "rmd"));

    atm_retrieval_conversation_turn_free (second);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_cross_repository_exact_followup_requires_clarification (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "food_per_capita EWD"
    );
    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *second = run_turn (
        state,
        "Și în RMD?"
    );

    g_assert_true (second->needs_clarification);
    g_assert_null (second->retrieval);
    g_assert_cmpstr (
        second->effective_query,
        ==,
        "Și în RMD?"
    );

    atm_retrieval_conversation_turn_free (second);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_clarification_preserves_previous_exact_state (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "food_per_capita EWD"
    );
    g_assert_false (first->needs_clarification);
    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *clarify = run_turn (
        state,
        "Și în RMD?"
    );
    g_assert_true (clarify->needs_clarification);
    g_assert_null (clarify->retrieval);
    atm_retrieval_conversation_turn_free (clarify);

    AtmRetrievalConversationTurn *third = run_turn (
        state,
        "Dar în 2025?"
    );

    g_assert_false (third->needs_clarification);
    g_assert_true (third->used_previous_scope);
    g_assert_true (third->used_previous_anchor);
    g_assert_cmpuint (
        third->retrieval->repositories->len,
        ==,
        1
    );

    AtmRepositoryEvidenceSet *ewd = find_set (
        third->retrieval,
        "ewd"
    );
    g_assert_nonnull (ewd);
    g_assert_nonnull (
        find_match (
            ewd,
            ATM_EVIDENCE_MATCH_EXACT,
            NULL
        )
    );
    g_assert_nonnull (
        find_match (
            ewd,
            ATM_EVIDENCE_MATCH_TABULAR,
            "2025"
        )
    );

    atm_retrieval_conversation_turn_free (third);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_orphan_followup_requires_clarification (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *turn = run_turn (
        state,
        "Dar în 2025?"
    );

    g_assert_true (turn->needs_clarification);
    g_assert_null (turn->retrieval);

    atm_retrieval_conversation_turn_free (turn);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_sources_followup_keeps_previous_scope_and_topic (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "Care este starea curentă în EWD?"
    );
    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *second = run_turn (
        state,
        "Sursele?"
    );

    g_assert_false (second->needs_clarification);
    g_assert_true (second->used_previous_scope);
    g_assert_true (second->used_previous_intent);
    g_assert_nonnull (
        strstr (
            second->effective_query,
            "current status"
        )
    );
    g_assert_cmpuint (
        second->retrieval->repositories->len,
        ==,
        1
    );

    AtmRepositoryEvidenceSet *ewd = find_set (
        second->retrieval,
        "ewd"
    );
    g_assert_nonnull (ewd);
    g_assert_cmpuint (ewd->evidence->len, >, 0);

    AtmEvidenceRecord *top = g_ptr_array_index (
        ewd->evidence,
        0
    );
    g_assert_true (
        (top->source_roles &
         ATM_SOURCE_ROLE_CANONICAL) != 0
    );

    atm_retrieval_conversation_turn_free (second);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_new_substantive_intent_replaces_previous_intent (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "Care este starea curentă în EWD?"
    );
    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *second = run_turn (
        state,
        "Dar structura?"
    );

    g_assert_false (second->needs_clarification);
    g_assert_true (second->used_previous_scope);
    g_assert_false (second->used_previous_intent);
    g_assert_nonnull (
        strstr (
            second->effective_query,
            "structura"
        )
    );
    g_assert_null (
        strstr (
            second->effective_query,
            "current status"
        )
    );
    g_assert_true (
        (second->retrieval->intents &
         ATM_RETRIEVAL_INTENT_STRUCTURE) != 0
    );
    g_assert_cmpuint (
        second->retrieval->repositories->len,
        ==,
        1
    );
    g_assert_nonnull (find_set (second->retrieval, "ewd"));

    atm_retrieval_conversation_turn_free (second);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_standalone_new_query_does_not_stick_to_previous_scope (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );

    AtmRetrievalConversationTurn *first = run_turn (
        state,
        "Care este starea curentă în EWD?"
    );
    atm_retrieval_conversation_turn_free (first);

    AtmRetrievalConversationTurn *second = run_turn (
        state,
        "Care este starea curentă în RMD?"
    );

    g_assert_false (second->needs_clarification);
    g_assert_false (second->used_previous_scope);
    g_assert_false (second->used_previous_intent);
    g_assert_cmpuint (
        second->retrieval->repositories->len,
        ==,
        1
    );
    g_assert_nonnull (find_set (second->retrieval, "rmd"));

    atm_retrieval_conversation_turn_free (second);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

static void
test_prepared_turn_requires_explicit_commit (void)
{
    Fixture *fixture = fixture_new ();
    AtmRetrievalConversationState *state = new_state (
        fixture
    );
    AtmRetrievalConversationTurn *first = NULL;
    AtmRetrievalConversationTurn *followup = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_conversation_prepare (
            state,
            "food_per_capita EWD",
            10,
            &first,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (first);
    g_assert_false (first->needs_clarification);

    atm_retrieval_conversation_turn_free (first);
    first = NULL;

    g_assert_true (
        atm_retrieval_conversation_prepare (
            state,
            "Dar în 2025?",
            10,
            &followup,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (followup);
    g_assert_true (followup->needs_clarification);

    atm_retrieval_conversation_turn_free (followup);
    followup = NULL;

    g_assert_true (
        atm_retrieval_conversation_prepare (
            state,
            "food_per_capita EWD",
            10,
            &first,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (
        atm_retrieval_conversation_commit (
            state,
            first,
            &error
        )
    );
    g_assert_no_error (error);
    atm_retrieval_conversation_turn_free (first);
    first = NULL;

    g_assert_true (
        atm_retrieval_conversation_prepare (
            state,
            "Dar în 2025?",
            10,
            &followup,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (followup->needs_clarification);
    g_assert_true (followup->used_previous_scope);
    g_assert_true (followup->used_previous_anchor);

    atm_retrieval_conversation_turn_free (followup);
    atm_retrieval_conversation_state_free (state);
    fixture_free (fixture);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-conversation/year-anchor",
        test_year_followup_inherits_scope_and_exact_anchor
    );
    g_test_add_func (
        "/retrieval-conversation/repository-intent",
        test_repository_followup_inherits_current_state_intent
    );
    g_test_add_func (
        "/retrieval-conversation/comparison-union",
        test_comparison_followup_unions_previous_and_new_repository
    );
    g_test_add_func (
        "/retrieval-conversation/exact-cross-repo-clarify",
        test_cross_repository_exact_followup_requires_clarification
    );
    g_test_add_func (
        "/retrieval-conversation/clarification-preserves-state",
        test_clarification_preserves_previous_exact_state
    );
    g_test_add_func (
        "/retrieval-conversation/orphan-clarify",
        test_orphan_followup_requires_clarification
    );
    g_test_add_func (
        "/retrieval-conversation/sources-followup",
        test_sources_followup_keeps_previous_scope_and_topic
    );
    g_test_add_func (
        "/retrieval-conversation/substantive-intent-replaces",
        test_new_substantive_intent_replaces_previous_intent
    );
    g_test_add_func (
        "/retrieval-conversation/new-query-not-sticky",
        test_standalone_new_query_does_not_stick_to_previous_scope
    );
    g_test_add_func (
        "/retrieval-conversation/explicit-commit",
        test_prepared_turn_requires_explicit_commit
    );

    return g_test_run ();
}
