#include "grounding_context.h"
#include "retrieval_conversation.h"
#include "retrieval_index_lifecycle.h"
#include "retrieval_policy.h"
#include "retrieval_scope.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include <string.h>
#include <sys/stat.h>

#define FROZEN_R5_MAX_RESULTS_PER_REPOSITORY 10
#define FROZEN_R5_MAX_CONTEXT_SOURCES 8
#define FROZEN_R5_MAX_CONTEXT_BYTES (32 * 1024)

typedef struct {
    const char *name;
    guint max_results_per_repository;
    guint max_context_sources;
    gsize max_context_bytes;
} BenchmarkPolicy;

static const BenchmarkPolicy FROZEN_R5_POLICY = {
    "frozen",
    FROZEN_R5_MAX_RESULTS_PER_REPOSITORY,
    FROZEN_R5_MAX_CONTEXT_SOURCES,
    FROZEN_R5_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy PRODUCTION_POLICY = {
    "production",
    ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,
    ATM_PRODUCTION_GROUNDING_MAX_SOURCES,
    ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy CANDIDATE_RESULTS5_POLICY = {
    "candidate-results5",
    5,
    ATM_PRODUCTION_GROUNDING_MAX_SOURCES,
    ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy CANDIDATE_SOURCES8_POLICY = {
    "candidate-sources8",
    ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,
    8,
    ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy CANDIDATE_SOURCES7_POLICY = {
    "candidate-sources7",
    ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,
    7,
    ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy CANDIDATE_SOURCES6_POLICY = {
    "candidate-sources6",
    ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,
    6,
    ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy CANDIDATE_SOURCES5_POLICY = {
    "candidate-sources5",
    ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,
    5,
    ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy CANDIDATE_SOURCES4_POLICY = {
    "candidate-sources4",
    ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,
    4,
    ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES
};

static const BenchmarkPolicy CANDIDATE_BYTES16K_POLICY = {
    "candidate-bytes16k",
    ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,
    ATM_PRODUCTION_GROUNDING_MAX_SOURCES,
    16 * 1024
};

static const BenchmarkPolicy CANDIDATE_COMPACT_POLICY = {
    "candidate-compact",
    5,
    8,
    16 * 1024
};

typedef struct {
    char *repository_id;
    char *expected_version;
    char *snapshot_sha;
    char *snapshot_root;
    char *index_path;
    char *actual_version;
} BenchmarkRepository;

typedef struct {
    AtmRetrievalConversationState *state;
    char *active_signature;
    guint next_turn;
} ConversationRunState;

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

        while ((name = g_dir_read_name (directory)) != NULL) {
            char *child = g_build_filename (path, name, NULL);

            remove_tree_best_effort (child);
            g_free (child);
        }

        g_dir_close (directory);
    }

    g_clear_error (&error);
    g_rmdir (path);
}

static void
benchmark_repository_free (BenchmarkRepository *repository)
{
    if (repository == NULL) {
        return;
    }

    g_free (repository->repository_id);
    g_free (repository->expected_version);
    g_free (repository->snapshot_sha);
    g_free (repository->snapshot_root);
    g_free (repository->index_path);
    g_free (repository->actual_version);
    g_free (repository);
}

static void
conversation_run_state_free (ConversationRunState *conversation)
{
    if (conversation == NULL) {
        return;
    }

    atm_retrieval_conversation_state_free (conversation->state);
    g_free (conversation->active_signature);
    g_free (conversation);
}

static const char *
snapshot_root_env (const char *repository_id)
{
    if (g_strcmp0 (repository_id, "ewd") == 0) {
        return "ATM_BENCH_EWD_ROOT";
    }

    if (g_strcmp0 (repository_id, "cbd") == 0) {
        return "ATM_BENCH_CBD_ROOT";
    }

    if (g_strcmp0 (repository_id, "rmd") == 0) {
        return "ATM_BENCH_RMD_ROOT";
    }

    return NULL;
}

static const char *
snapshot_sha_env (const char *repository_id)
{
    if (g_strcmp0 (repository_id, "ewd") == 0) {
        return "ATM_BENCH_EWD_SHA";
    }

    if (g_strcmp0 (repository_id, "cbd") == 0) {
        return "ATM_BENCH_CBD_SHA";
    }

    if (g_strcmp0 (repository_id, "rmd") == 0) {
        return "ATM_BENCH_RMD_SHA";
    }

    return NULL;
}

static const char *
required_string_member (
    JsonObject *object,
    const char *member,
    GError **error
)
{
    JsonNode *node;

    if (object == NULL ||
        !json_object_has_member (object, member)) {
        g_set_error (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Missing required string member '%s'.",
            member
        );
        return NULL;
    }

    node = json_object_get_member (object, member);

    if (node == NULL ||
        json_node_get_node_type (node) != JSON_NODE_VALUE ||
        json_node_get_value_type (node) != G_TYPE_STRING) {
        g_set_error (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Member '%s' must be a string.",
            member
        );
        return NULL;
    }

    const char *value = json_node_get_string (node);

    if (value == NULL || value[0] == '\0') {
        g_set_error (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Member '%s' must be non-empty.",
            member
        );
        return NULL;
    }

    return value;
}

static JsonArray *
required_array_member (
    JsonObject *object,
    const char *member,
    GError **error
)
{
    JsonNode *node;

    if (object == NULL ||
        !json_object_has_member (object, member)) {
        g_set_error (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Missing required array member '%s'.",
            member
        );
        return NULL;
    }

    node = json_object_get_member (object, member);

    if (node == NULL ||
        json_node_get_node_type (node) != JSON_NODE_ARRAY) {
        g_set_error (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Member '%s' must be an array.",
            member
        );
        return NULL;
    }

    return json_node_get_array (node);
}

static const char *
optional_string_member (
    JsonObject *object,
    const char *member,
    GError **error
)
{
    if (!json_object_has_member (object, member)) {
        return NULL;
    }

    JsonNode *node = json_object_get_member (object, member);

    if (node == NULL ||
        json_node_get_node_type (node) == JSON_NODE_NULL) {
        return NULL;
    }

    if (json_node_get_node_type (node) != JSON_NODE_VALUE ||
        json_node_get_value_type (node) != G_TYPE_STRING) {
        g_set_error (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Optional member '%s' must be a string or null.",
            member
        );
        return NULL;
    }

    return json_node_get_string (node);
}

static BenchmarkRepository *
find_repository (
    GPtrArray *repositories,
    const char *repository_id
)
{
    for (guint i = 0; i < repositories->len; i++) {
        BenchmarkRepository *repository = g_ptr_array_index (
            repositories,
            i
        );

        if (g_strcmp0 (
                repository->repository_id,
                repository_id
            ) == 0) {
            return repository;
        }
    }

    return NULL;
}

static gboolean
load_benchmark_repositories (
    JsonObject *benchmark,
    const char *cache_root,
    GPtrArray **out_repositories,
    GError **error
)
{
    JsonArray *corpus = required_array_member (
        benchmark,
        "corpus",
        error
    );
    GPtrArray *repositories = NULL;

    if (corpus == NULL) {
        return FALSE;
    }

    repositories = g_ptr_array_new_with_free_func (
        (GDestroyNotify) benchmark_repository_free
    );

    for (guint i = 0;
         i < json_array_get_length (corpus);
         i++) {
        JsonNode *node = json_array_get_element (corpus, i);

        if (node == NULL ||
            json_node_get_node_type (node) != JSON_NODE_OBJECT) {
            g_set_error_literal (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Benchmark corpus entry must be an object."
            );
            goto out;
        }

        JsonObject *item = json_node_get_object (node);
        const char *repository_id = required_string_member (
            item,
            "repository_id",
            error
        );
        const char *expected_version = required_string_member (
            item,
            "repository_version",
            error
        );
        const char *snapshot_sha = required_string_member (
            item,
            "snapshot_sha",
            error
        );

        if (repository_id == NULL ||
            expected_version == NULL ||
            snapshot_sha == NULL) {
            goto out;
        }

        if (find_repository (
                repositories,
                repository_id
            ) != NULL) {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Duplicate corpus repository '%s'.",
                repository_id
            );
            goto out;
        }

        const char *root_env = snapshot_root_env (
            repository_id
        );
        const char *sha_env = snapshot_sha_env (
            repository_id
        );

        if (root_env == NULL || sha_env == NULL) {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Unsupported benchmark repository '%s'.",
                repository_id
            );
            goto out;
        }

        const char *snapshot_root = g_getenv (root_env);
        const char *actual_sha = g_getenv (sha_env);

        if (snapshot_root == NULL ||
            snapshot_root[0] == '\0' ||
            actual_sha == NULL ||
            actual_sha[0] == '\0') {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Missing benchmark snapshot environment for '%s'.",
                repository_id
            );
            goto out;
        }

        if (g_strcmp0 (
                actual_sha,
                snapshot_sha
            ) != 0) {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Snapshot SHA mismatch for '%s': benchmark=%s checkout=%s.",
                repository_id,
                snapshot_sha,
                actual_sha
            );
            goto out;
        }

        BenchmarkRepository *repository = g_new0 (
            BenchmarkRepository,
            1
        );
        repository->repository_id = g_strdup (repository_id);
        repository->expected_version = g_strdup (
            expected_version
        );
        repository->snapshot_sha = g_strdup (snapshot_sha);
        repository->snapshot_root = g_strdup (
            snapshot_root
        );

        AtmRetrievalEnsureResult ensure_result;

        if (!atm_retrieval_index_ensure_for_snapshot (
                cache_root,
                repository->snapshot_root,
                repository->repository_id,
                repository->snapshot_sha,
                &repository->index_path,
                &repository->actual_version,
                &ensure_result,
                error
            )) {
            benchmark_repository_free (repository);
            goto out;
        }

        if (g_strcmp0 (
                repository->actual_version,
                repository->expected_version
            ) != 0) {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Repository version mismatch for '%s': benchmark=%s snapshot=%s.",
                repository->repository_id,
                repository->expected_version,
                repository->actual_version
            );
            benchmark_repository_free (repository);
            goto out;
        }

        g_print (
            "R5 corpus: %s version=%s sha=%s ensure=%d\n",
            repository->repository_id,
            repository->actual_version,
            repository->snapshot_sha,
            ensure_result
        );

        g_ptr_array_add (repositories, repository);
    }

    if (repositories->len == 0) {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Benchmark corpus is empty."
        );
        goto out;
    }

    *out_repositories = g_steal_pointer (&repositories);
    return TRUE;

out:
    g_clear_pointer (&repositories, g_ptr_array_unref);
    return FALSE;
}

static GPtrArray *
active_scopes_for_topic (
    JsonObject *topic,
    GPtrArray *repositories,
    GError **error
)
{
    JsonArray *active = required_array_member (
        topic,
        "active_repositories",
        error
    );

    if (active == NULL) {
        return NULL;
    }

    GPtrArray *scopes = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );

    for (guint i = 0;
         i < json_array_get_length (active);
         i++) {
        const char *repository_id =
            json_array_get_string_element (
                active,
                i
            );

        if (repository_id == NULL) {
            g_set_error_literal (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "active_repositories entries must be strings."
            );
            g_ptr_array_unref (scopes);
            return NULL;
        }

        BenchmarkRepository *repository = find_repository (
            repositories,
            repository_id
        );

        if (repository == NULL) {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Topic references repository '%s' outside the benchmark corpus.",
                repository_id
            );
            g_ptr_array_unref (scopes);
            return NULL;
        }

        g_ptr_array_add (
            scopes,
            atm_retrieval_repository_scope_new (
                repository->repository_id,
                repository->index_path
            )
        );
    }

    if (scopes->len == 0) {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Topic active repository scope is empty."
        );
        g_ptr_array_unref (scopes);
        return NULL;
    }

    return scopes;
}

static char *
active_scope_signature (
    JsonObject *topic,
    GError **error
)
{
    JsonArray *active = required_array_member (
        topic,
        "active_repositories",
        error
    );

    if (active == NULL) {
        return NULL;
    }

    GString *signature = g_string_new (NULL);

    for (guint i = 0;
         i < json_array_get_length (active);
         i++) {
        const char *repository_id =
            json_array_get_string_element (
                active,
                i
            );

        if (repository_id == NULL) {
            g_set_error_literal (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "active_repositories entries must be strings."
            );
            g_string_free (signature, TRUE);
            return NULL;
        }

        if (i > 0) {
            g_string_append_c (signature, '|');
        }

        g_string_append (signature, repository_id);
    }

    return g_string_free (signature, FALSE);
}

static gboolean
read_turn_index (
    JsonObject *topic,
    guint *out_turn_index,
    GError **error
)
{
    if (!json_object_has_member (
            topic,
            "turn_index"
        )) {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Conversation topic is missing turn_index."
        );
        return FALSE;
    }

    JsonNode *node = json_object_get_member (
        topic,
        "turn_index"
    );

    if (node == NULL ||
        json_node_get_node_type (node) != JSON_NODE_VALUE) {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Conversation turn_index must be an integer."
        );
        return FALSE;
    }

    gint64 value = json_node_get_int (node);

    if (value <= 0 || value > G_MAXUINT) {
        g_set_error_literal (
            error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Conversation turn_index is outside supported bounds."
        );
        return FALSE;
    }

    *out_turn_index = (guint) value;
    return TRUE;
}

static void
add_evidence_ref (
    JsonBuilder *builder,
    const char *repository_id,
    const char *repository_version,
    const char *snapshot_sha,
    const char *logical_source_id
)
{
    json_builder_begin_object (builder);

    json_builder_set_member_name (
        builder,
        "repository_id"
    );
    json_builder_add_string_value (
        builder,
        repository_id
    );

    json_builder_set_member_name (
        builder,
        "repository_version"
    );
    json_builder_add_string_value (
        builder,
        repository_version
    );

    json_builder_set_member_name (
        builder,
        "snapshot_sha"
    );
    json_builder_add_string_value (
        builder,
        snapshot_sha
    );

    json_builder_set_member_name (
        builder,
        "logical_source_id"
    );
    json_builder_add_string_value (
        builder,
        logical_source_id
    );

    json_builder_end_object (builder);
}

static void
add_flattened_results (
    JsonBuilder *builder,
    const AtmRetrievalResultSet *retrieval
)
{
    json_builder_begin_array (builder);

    guint round = 0;

    while (TRUE) {
        gboolean any = FALSE;

        for (guint repository_index = 0;
             repository_index < retrieval->repositories->len;
             repository_index++) {
            const AtmRepositoryEvidenceSet *set =
                g_ptr_array_index (
                    retrieval->repositories,
                    repository_index
                );

            if (round >= set->evidence->len) {
                continue;
            }

            any = TRUE;

            const AtmEvidenceRecord *record =
                g_ptr_array_index (
                    set->evidence,
                    round
                );

            add_evidence_ref (
                builder,
                record->repository_id,
                record->repository_version,
                record->snapshot_sha,
                record->logical_source_id
            );
        }

        if (!any) {
            break;
        }

        round++;
    }

    json_builder_end_array (builder);
}

static void
add_context_sources (
    JsonBuilder *builder,
    const AtmGroundingContext *context
)
{
    json_builder_begin_array (builder);

    for (guint i = 0;
         i < context->sources->len;
         i++) {
        const AtmGroundingSource *source =
            g_ptr_array_index (
                context->sources,
                i
            );

        add_evidence_ref (
            builder,
            source->repository_id,
            source->repository_version,
            source->snapshot_sha,
            source->logical_source_id
        );
    }

    json_builder_end_array (builder);
}

static guint
retrieval_result_count (
    const AtmRetrievalResultSet *retrieval
)
{
    guint total = 0;

    if (retrieval == NULL) {
        return 0;
    }

    for (guint i = 0;
         i < retrieval->repositories->len;
         i++) {
        const AtmRepositoryEvidenceSet *set =
            g_ptr_array_index (
                retrieval->repositories,
                i
            );
        total += set->evidence->len;
    }

    return total;
}

static gboolean
append_topic_run (
    JsonBuilder *builder,
    JsonObject *topic,
    GPtrArray *repositories,
    GHashTable *conversations,
    const BenchmarkPolicy *policy,
    GError **error
)
{
    const char *topic_id = required_string_member (
        topic,
        "topic_id",
        error
    );
    const char *query = required_string_member (
        topic,
        "query",
        error
    );

    if (topic_id == NULL || query == NULL) {
        return FALSE;
    }

    GPtrArray *active = active_scopes_for_topic (
        topic,
        repositories,
        error
    );

    if (active == NULL) {
        return FALSE;
    }

    const char *conversation_id = optional_string_member (
        topic,
        "conversation_id",
        error
    );

    if (error != NULL && *error != NULL) {
        g_ptr_array_unref (active);
        return FALSE;
    }

    AtmRetrievalConversationState *temporary_state = NULL;
    AtmRetrievalConversationState *state = NULL;
    ConversationRunState *conversation = NULL;

    if (conversation_id != NULL) {
        char *signature = active_scope_signature (
            topic,
            error
        );

        if (signature == NULL) {
            g_ptr_array_unref (active);
            return FALSE;
        }

        guint turn_index = 0;

        if (!read_turn_index (
                topic,
                &turn_index,
                error
            )) {
            g_free (signature);
            g_ptr_array_unref (active);
            return FALSE;
        }

        conversation = g_hash_table_lookup (
            conversations,
            conversation_id
        );

        if (conversation == NULL) {
            if (turn_index != 1) {
                g_set_error (
                    error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "Conversation '%s' starts at turn %u rather than 1.",
                    conversation_id,
                    turn_index
                );
                g_free (signature);
                g_ptr_array_unref (active);
                return FALSE;
            }

            conversation = g_new0 (
                ConversationRunState,
                1
            );
            conversation->state =
                atm_retrieval_conversation_state_new (
                    active,
                    error
                );
            conversation->active_signature =
                g_steal_pointer (&signature);
            conversation->next_turn = 1;

            if (conversation->state == NULL) {
                conversation_run_state_free (
                    conversation
                );
                g_ptr_array_unref (active);
                return FALSE;
            }

            g_hash_table_insert (
                conversations,
                g_strdup (conversation_id),
                conversation
            );
        } else {
            if (g_strcmp0 (
                    conversation->active_signature,
                    signature
                ) != 0) {
                g_set_error (
                    error,
                    G_IO_ERROR,
                    G_IO_ERROR_INVALID_DATA,
                    "Conversation '%s' changed its frozen active repository scope.",
                    conversation_id
                );
                g_free (signature);
                g_ptr_array_unref (active);
                return FALSE;
            }

            g_free (signature);
        }

        if (turn_index != conversation->next_turn) {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Conversation '%s' expected turn %u but benchmark supplied %u.",
                conversation_id,
                conversation->next_turn,
                turn_index
            );
            g_ptr_array_unref (active);
            return FALSE;
        }

        state = conversation->state;
    } else {
        temporary_state =
            atm_retrieval_conversation_state_new (
                active,
                error
            );

        if (temporary_state == NULL) {
            g_ptr_array_unref (active);
            return FALSE;
        }

        state = temporary_state;
    }

    AtmRetrievalConversationTurn *turn = NULL;
    gint64 started = g_get_monotonic_time ();

    gboolean ran = atm_retrieval_conversation_run (
        state,
        query,
        policy->max_results_per_repository,
        &turn,
        error
    );

    gint64 finished = g_get_monotonic_time ();
    double latency_ms =
        (double) (finished - started) / 1000.0;

    if (!ran) {
        atm_retrieval_conversation_state_free (
            temporary_state
        );
        g_ptr_array_unref (active);
        return FALSE;
    }

    if (conversation != NULL) {
        conversation->next_turn++;
    }

    AtmGroundingContext *context = NULL;

    if (!turn->needs_clarification) {
        if (turn->retrieval == NULL) {
            g_set_error (
                error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Topic '%s' returned retrieval outcome without retrieval results.",
                topic_id
            );
            atm_retrieval_conversation_turn_free (turn);
            atm_retrieval_conversation_state_free (
                temporary_state
            );
            g_ptr_array_unref (active);
            return FALSE;
        }

        if (!atm_grounding_context_build (
                turn->retrieval,
                policy->max_context_sources,
                policy->max_context_bytes,
                &context,
                error
            )) {
            atm_retrieval_conversation_turn_free (turn);
            atm_retrieval_conversation_state_free (
                temporary_state
            );
            g_ptr_array_unref (active);
            return FALSE;
        }
    }

    json_builder_begin_object (builder);

    json_builder_set_member_name (
        builder,
        "topic_id"
    );
    json_builder_add_string_value (
        builder,
        topic_id
    );

    json_builder_set_member_name (
        builder,
        "outcome"
    );
    json_builder_add_string_value (
        builder,
        turn->needs_clarification
            ? "needs_clarification"
            : "retrieval"
    );

    json_builder_set_member_name (
        builder,
        "latency_ms"
    );
    json_builder_add_double_value (
        builder,
        latency_ms
    );

    json_builder_set_member_name (
        builder,
        "results"
    );

    if (turn->needs_clarification) {
        json_builder_begin_array (builder);
        json_builder_end_array (builder);
    } else {
        add_flattened_results (
            builder,
            turn->retrieval
        );
    }

    json_builder_set_member_name (
        builder,
        "context_sources"
    );

    if (turn->needs_clarification) {
        json_builder_begin_array (builder);
        json_builder_end_array (builder);
    } else {
        add_context_sources (
            builder,
            context
        );
    }

    json_builder_set_member_name (
        builder,
        "evidence_bytes"
    );
    json_builder_add_int_value (
        builder,
        turn->needs_clarification
            ? 0
            : (gint64) context->evidence_bytes
    );

    json_builder_set_member_name (
        builder,
        "evidence_token_count"
    );
    json_builder_add_null_value (builder);

    json_builder_end_object (builder);

    g_print (
        "R5 topic: %s outcome=%s latency_ms=%.3f results=%u context=%u bytes=%" G_GSIZE_FORMAT "\n",
        topic_id,
        turn->needs_clarification
            ? "needs_clarification"
            : "retrieval",
        latency_ms,
        turn->needs_clarification
            ? 0
            : retrieval_result_count (
                turn->retrieval
            ),
        context != NULL
            ? context->sources->len
            : 0,
        context != NULL
            ? context->evidence_bytes
            : 0
    );

    atm_grounding_context_free (context);
    atm_retrieval_conversation_turn_free (turn);
    atm_retrieval_conversation_state_free (
        temporary_state
    );
    g_ptr_array_unref (active);

    return TRUE;
}

int
main (int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
        g_printerr (
            "Usage: %s BENCHMARK_JSON OUTPUT_JSON RUN_ID "
            "[frozen|production|candidate-results5|candidate-sources8|"
            "candidate-sources7|candidate-sources6|candidate-sources5|"
            "candidate-sources4|candidate-bytes16k|"
            "candidate-compact]\n",
            argv[0]
        );
        return 2;
    }

    const char *benchmark_path = argv[1];
    const char *output_path = argv[2];
    const char *run_id = argv[3];
    const char *policy_name =
        argc == 5 ? argv[4] : "frozen";
    const BenchmarkPolicy *policy = NULL;

    if (g_strcmp0 (policy_name, "frozen") == 0) {
        policy = &FROZEN_R5_POLICY;
    } else if (g_strcmp0 (policy_name, "production") == 0) {
        policy = &PRODUCTION_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-results5"
               ) == 0) {
        policy = &CANDIDATE_RESULTS5_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-sources8"
               ) == 0) {
        policy = &CANDIDATE_SOURCES8_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-sources7"
               ) == 0) {
        policy = &CANDIDATE_SOURCES7_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-sources6"
               ) == 0) {
        policy = &CANDIDATE_SOURCES6_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-sources5"
               ) == 0) {
        policy = &CANDIDATE_SOURCES5_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-sources4"
               ) == 0) {
        policy = &CANDIDATE_SOURCES4_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-bytes16k"
               ) == 0) {
        policy = &CANDIDATE_BYTES16K_POLICY;
    } else if (g_strcmp0 (
                   policy_name,
                   "candidate-compact"
               ) == 0) {
        policy = &CANDIDATE_COMPACT_POLICY;
    } else {
        g_printerr (
            "Unknown benchmark policy '%s'.\n",
            policy_name
        );
        return 2;
    }

    if (run_id[0] == '\0' ||
        strlen (run_id) > 120) {
        g_printerr (
            "Benchmark run ID must be 1..120 bytes.\n"
        );
        return 2;
    }

    GError *error = NULL;
    JsonParser *parser = json_parser_new ();
    JsonBuilder *builder = NULL;
    JsonGenerator *generator = NULL;
    JsonNode *output_root = NULL;
    GPtrArray *repositories = NULL;
    GHashTable *conversations = NULL;
    char *cache_root = NULL;
    int exit_code = 1;

    if (!json_parser_load_from_file (
            parser,
            benchmark_path,
            &error
        )) {
        goto out;
    }

    JsonNode *benchmark_node =
        json_parser_get_root (parser);

    if (benchmark_node == NULL ||
        json_node_get_node_type (
            benchmark_node
        ) != JSON_NODE_OBJECT) {
        g_set_error_literal (
            &error,
            G_IO_ERROR,
            G_IO_ERROR_INVALID_DATA,
            "Benchmark root must be an object."
        );
        goto out;
    }

    JsonObject *benchmark =
        json_node_get_object (
            benchmark_node
        );
    const char *benchmark_id = required_string_member (
        benchmark,
        "benchmark_id",
        &error
    );

    if (benchmark_id == NULL) {
        goto out;
    }

    JsonArray *topics = required_array_member (
        benchmark,
        "topics",
        &error
    );

    if (topics == NULL ||
        json_array_get_length (topics) == 0) {
        if (error == NULL) {
            g_set_error_literal (
                &error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Benchmark topics must be non-empty."
            );
        }
        goto out;
    }

    cache_root = g_dir_make_tmp (
        "atm-r5-benchmark-cache-XXXXXX",
        &error
    );

    if (cache_root == NULL) {
        goto out;
    }

    if (!load_benchmark_repositories (
            benchmark,
            cache_root,
            &repositories,
            &error
        )) {
        goto out;
    }

    conversations = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify) conversation_run_state_free
    );

    builder = json_builder_new ();
    json_builder_begin_object (builder);

    json_builder_set_member_name (
        builder,
        "schema_version"
    );
    json_builder_add_int_value (
        builder,
        1
    );

    json_builder_set_member_name (
        builder,
        "benchmark_id"
    );
    json_builder_add_string_value (
        builder,
        benchmark_id
    );

    json_builder_set_member_name (
        builder,
        "run_id"
    );
    json_builder_add_string_value (
        builder,
        run_id
    );

    json_builder_set_member_name (
        builder,
        "corpus"
    );
    json_builder_add_value (
        builder,
        json_node_copy (
            json_object_get_member (
                benchmark,
                "corpus"
            )
        )
    );

    json_builder_set_member_name (
        builder,
        "topics"
    );
    json_builder_begin_array (builder);

    for (guint i = 0;
         i < json_array_get_length (topics);
         i++) {
        JsonNode *topic_node =
            json_array_get_element (
                topics,
                i
            );

        if (topic_node == NULL ||
            json_node_get_node_type (
                topic_node
            ) != JSON_NODE_OBJECT) {
            g_set_error_literal (
                &error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "Benchmark topic must be an object."
            );
            goto out;
        }

        if (!append_topic_run (
                builder,
                json_node_get_object (
                    topic_node
                ),
                repositories,
                conversations,
                policy,
                &error
            )) {
            goto out;
        }
    }

    json_builder_end_array (builder);
    json_builder_end_object (builder);

    output_root = json_builder_get_root (builder);
    generator = json_generator_new ();
    json_generator_set_root (
        generator,
        output_root
    );
    json_generator_set_pretty (
        generator,
        TRUE
    );

    if (!json_generator_to_file (
            generator,
            output_path,
            &error
        )) {
        goto out;
    }

    g_print (
        "PASS: wrote deterministic R5 benchmark run policy=%s results/repository=%u context_sources=%u context_bytes=%" G_GSIZE_FORMAT " to %s\n",
        policy->name,
        policy->max_results_per_repository,
        policy->max_context_sources,
        policy->max_context_bytes,
        output_path
    );
    exit_code = 0;

out:
    if (error != NULL) {
        g_printerr (
            "R5 benchmark runner failed: %s\n",
            error->message
        );
    }

    g_clear_error (&error);
    g_clear_pointer (
        &conversations,
        g_hash_table_unref
    );
    g_clear_pointer (
        &repositories,
        g_ptr_array_unref
    );
    g_clear_object (&generator);
    g_clear_pointer (
        &output_root,
        json_node_free
    );
    g_clear_object (&builder);
    g_clear_object (&parser);

    remove_tree_best_effort (cache_root);
    g_free (cache_root);

    return exit_code;
}
