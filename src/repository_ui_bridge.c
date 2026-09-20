#include "repository_ui_bridge.h"

#include "retrieval_index_lifecycle.h"

gboolean
atm_repository_ui_ensure_index (
    const char *cache_root,
    const char *snapshot_root,
    const char *repository_id,
    const char *snapshot_sha,
    char **out_index_path,
    char **out_repository_version,
    GError **error
)
{
    AtmRetrievalEnsureResult result;

    return atm_retrieval_index_ensure_for_snapshot (
        cache_root,
        snapshot_root,
        repository_id,
        snapshot_sha,
        out_index_path,
        out_repository_version,
        &result,
        error
    );
}
