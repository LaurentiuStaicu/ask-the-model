#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Turn guard: the commit barrier for one conversational turn.
 *
 * M12 identifies a class of faults this module exists to close:
 *
 *   - a late provider result committed after the operation that requested it
 *     was invalidated;
 *   - a result produced against one model committed under another, because
 *     discovery replaced the active model mid-turn;
 *   - a repository generation changing under a turn validated against the
 *     older generation.
 *
 * The guard does not cancel transport. It records what a turn was validated
 * against and refuses to let that turn commit if anything it was validated
 * against has since changed.
 *
 *   AtmTurnGuard *guard = atm_turn_guard_new ();
 *   atm_turn_guard_begin (guard, generation_id, model, digest, NULL);
 *   ... provider call ...
 *   atm_turn_guard_commit (guard, current_generation_id, current_model,
 *                          current_digest, &error);
 *
 * The guard is deliberately silent about cancellation: a turn that was aborted
 * may or may not still receive a result, and the guard's job is to make sure a
 * late result cannot commit. atm_turn_guard_abort() is what makes that so.
 */

typedef enum {
    ATM_TURN_GUARD_OK = 0,
    /* No open turn; nothing may be committed. */
    ATM_TURN_GUARD_ERROR_INVALIDATED,
    /* A turn is already open; begin is refused. */
    ATM_TURN_GUARD_ERROR_ALREADY_OPEN,
    /* The repository generation changed since begin. */
    ATM_TURN_GUARD_ERROR_STALE_GENERATION,
    /* The active model changed since begin. */
    ATM_TURN_GUARD_ERROR_STALE_MODEL,
    /* The active model digest changed since begin. */
    ATM_TURN_GUARD_ERROR_STALE_DIGEST,
    /* The commit arguments themselves were malformed. */
    ATM_TURN_GUARD_ERROR_ARGUMENT
} AtmTurnGuardError;

#define ATM_TURN_GUARD_ERROR (atm_turn_guard_error_quark ())

GQuark atm_turn_guard_error_quark (void);

typedef struct AtmTurnGuard AtmTurnGuard;

AtmTurnGuard *atm_turn_guard_new (void);
void atm_turn_guard_free (AtmTurnGuard *guard);

/*
 * Open a turn. Refused when a turn is already open, so a nested or duplicated
 * begin cannot silently overwrite the identity of an in-flight turn. `model`
 * must be non-empty after stripping; an empty digest is treated as absent.
 */
gboolean atm_turn_guard_begin (AtmTurnGuard *guard,
                               gint64 repository_generation_id,
                               const char *model,
                               const char *model_digest,
                               GError **error);

/*
 * Close a turn and decide whether it may commit.
 *
 * The caller passes the CURRENT generation and model identity, read at commit
 * time, not the ones it began with. Any difference from what was recorded at
 * begin refuses the commit.
 *
 * A turn commits at most once: on both success and refusal the guard is
 * closed, so a corrected retry cannot persist a result produced earlier.
 */
gboolean atm_turn_guard_commit (AtmTurnGuard *guard,
                                gint64 current_repository_generation_id,
                                const char *current_model,
                                const char *current_model_digest,
                                GError **error);

/*
 * Abandon an open turn without committing. A later commit for that turn then
 * fails with ATM_TURN_GUARD_ERROR_INVALIDATED rather than silently succeeding.
 */
void atm_turn_guard_abort (AtmTurnGuard *guard);

gboolean atm_turn_guard_is_open (const AtmTurnGuard *guard);

/* Borrowed read-only values of the open turn; NULL/0 when closed. */
const char *atm_turn_guard_model (const AtmTurnGuard *guard);
const char *atm_turn_guard_model_digest (const AtmTurnGuard *guard);
gint64 atm_turn_guard_generation (const AtmTurnGuard *guard);

G_END_DECLS
