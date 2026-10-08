#include "conversation_turn_guard.h"

#include <string.h>

/*
 * Turn guard: the commit barrier for one conversational turn.
 *
 * Pure state machine over the identity a turn was validated against: no locks,
 * no I/O, no provider. That is what makes the M12 fault class testable without
 * a repository, a session or a model.
 *
 * The digest is recorded separately from the model name because a model can
 * keep its name while its content changes (a re-pull), so a name-only check
 * would let a turn validated against one weight set commit under another.
 */

struct AtmTurnGuard {
    gboolean open;
    gint64 generation;
    char *model;
    char *model_digest;   /* NULL when the begin call carried none */
};

GQuark
atm_turn_guard_error_quark (void)
{
    return g_quark_from_static_string ("atm-turn-guard-error-quark");
}

static gboolean
fail (GError **error, AtmTurnGuardError code, const char *message)
{
    g_set_error_literal (error, ATM_TURN_GUARD_ERROR, code, message);
    return FALSE;
}

AtmTurnGuard *
atm_turn_guard_new (void)
{
    return g_new0 (AtmTurnGuard, 1);
}

void
atm_turn_guard_free (AtmTurnGuard *guard)
{
    if (guard == NULL) return;
    g_free (guard->model);
    g_free (guard->model_digest);
    g_free (guard);
}

/* Every terminal transition goes through here, so "the turn is over" happens
 * in exactly one place. */
static void
close (AtmTurnGuard *guard)
{
    guard->open = FALSE;
    g_clear_pointer (&guard->model, g_free);
    g_clear_pointer (&guard->model_digest, g_free);
    guard->generation = 0;
}

/* Empty or all-whitespace becomes NULL, so a caller passing "" and a caller
 * passing NULL mean the same thing. */
static char *
normalize_optional (const char *value)
{
    if (value == NULL) return NULL;
    char *stripped = g_strdup (value);
    g_strstrip (stripped);
    if (stripped[0] == '\0') {
        g_free (stripped);
        return NULL;
    }
    return stripped;
}

gboolean
atm_turn_guard_begin (AtmTurnGuard *guard,
                      gint64 repository_generation_id,
                      const char *model,
                      const char *model_digest,
                      GError **error)
{
    if (guard == NULL || model == NULL)
        return fail (error, ATM_TURN_GUARD_ERROR_ARGUMENT,
                     "Invalid turn guard arguments.");

    /* A repository-backed turn requires a positive generation; a turn with no
     * repository may pass 0. A negative value is never valid. */
    if (repository_generation_id < 0)
        return fail (error, ATM_TURN_GUARD_ERROR_ARGUMENT,
                     "Repository generation cannot be negative.");

    if (guard->open)
        return fail (error, ATM_TURN_GUARD_ERROR_ALREADY_OPEN,
                     "A turn is already open; it must commit or abort first.");

    char *normalized_model = normalize_optional (model);
    if (normalized_model == NULL)
        return fail (error, ATM_TURN_GUARD_ERROR_ARGUMENT,
                     "A turn requires a known model name.");

    guard->open = TRUE;
    guard->generation = repository_generation_id;
    guard->model = normalized_model;
    guard->model_digest = normalize_optional (model_digest);
    return TRUE;
}

gboolean
atm_turn_guard_commit (AtmTurnGuard *guard,
                       gint64 current_repository_generation_id,
                       const char *current_model,
                       const char *current_model_digest,
                       GError **error)
{
    if (guard == NULL || current_model == NULL)
        return fail (error, ATM_TURN_GUARD_ERROR_ARGUMENT,
                     "Invalid turn guard arguments.");

    if (!guard->open) {
        /* The late-result path: a result arriving after its operation was
         * invalidated, or after an unopened commit, cannot commit. */
        return fail (error, ATM_TURN_GUARD_ERROR_INVALIDATED,
                     "The turn was invalidated before its result arrived.");
    }

    char *normalized_model = normalize_optional (current_model);
    char *normalized_digest = normalize_optional (current_model_digest);

    gboolean ok = TRUE;

    if (guard->generation != current_repository_generation_id) {
        fail (error, ATM_TURN_GUARD_ERROR_STALE_GENERATION,
              "The repository generation changed while the turn was in flight.");
        ok = FALSE;
    } else if (normalized_model == NULL ||
               g_strcmp0 (normalized_model, guard->model) != 0) {
        fail (error, ATM_TURN_GUARD_ERROR_STALE_MODEL,
              "The active model changed while the turn was in flight.");
        ok = FALSE;
    } else if (g_strcmp0 (normalized_digest, guard->model_digest) != 0) {
        /* Only meaningful when a digest was pinned at begin: a turn that began
         * without one does not gain one mid-flight, and one that pinned a
         * digest does not accept its absence at commit. */
        fail (error, ATM_TURN_GUARD_ERROR_STALE_DIGEST,
              "The active model digest changed while the turn was in flight.");
        ok = FALSE;
    }

    g_free (normalized_model);
    g_free (normalized_digest);

    /* A turn commits at most once, whether or not it was allowed to. */
    close (guard);
    return ok;
}

void
atm_turn_guard_abort (AtmTurnGuard *guard)
{
    if (guard == NULL || !guard->open) return;
    close (guard);
}

gboolean
atm_turn_guard_is_open (const AtmTurnGuard *guard)
{
    return guard != NULL && guard->open;
}

const char *
atm_turn_guard_model (const AtmTurnGuard *guard)
{
    return guard != NULL && guard->open ? guard->model : NULL;
}

const char *
atm_turn_guard_model_digest (const AtmTurnGuard *guard)
{
    return guard != NULL && guard->open ? guard->model_digest : NULL;
}

gint64
atm_turn_guard_generation (const AtmTurnGuard *guard)
{
    return guard != NULL && guard->open ? guard->generation : 0;
}
