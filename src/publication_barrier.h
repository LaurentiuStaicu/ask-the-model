#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Managed-export reconciliation barrier (the original module name is retained
 * for compatibility within this patch).
 *
 * Only explicitly archived conversations are saved to History and receive a
 * managed JSON mirror. Unarchived working rows intentionally have no mirror.
 * Before an unarchived row is discarded, startup must successfully remove any
 * stale export path or confirm that it is absent. If that reconciliation
 * fails or cannot be evaluated, the SQLite row is retained and stays out of
 * History so cleanup can be retried later.
 *
 * For archived rows reconciliation means writing the current JSON mirror; an
 * archived row is never a startup-discard candidate. For unarchived rows it
 * means successful removal or confirmed absence of the derived mirror. The
 * barrier records the most recent reconciliation outcome per conversation and
 * authorizes discard only after success.
 *
 * The previous startup sequence discarded unarchived rows before export
 * reconciliation, while the best-effort helper swallowed filesystem errors.
 * A stale managed-export path could therefore remain after its source row was
 * deleted. This module is a pure state machine: it performs no I/O and holds
 * no locks; the store reports the real reconciliation result.
 */

typedef enum {
    ATM_PUBLICATION_BARRIER_OK = 0,
    ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT,
    /* The conversation has no recorded publish outcome; nothing is known about
     * it, so it may not be discarded. */
    ATM_PUBLICATION_BARRIER_ERROR_UNPUBLISHED,
    /* The last publish attempt for this conversation failed. */
    ATM_PUBLICATION_BARRIER_ERROR_LAST_PUBLISH_FAILED
} AtmPublicationBarrierError;

#define ATM_PUBLICATION_BARRIER_ERROR (atm_publication_barrier_error_quark ())

GQuark atm_publication_barrier_error_quark (void);

typedef struct AtmPublicationBarrier AtmPublicationBarrier;

AtmPublicationBarrier *atm_publication_barrier_new (void);
void atm_publication_barrier_free (AtmPublicationBarrier *barrier);

/*
 * Record the latest managed-export reconciliation outcome. `conversation_id`
 * must be non-empty after stripping. Success means the archived mirror was
 * synchronized or an unarchived mirror was removed/confirmed absent; failure
 * supersedes any earlier success.
 *
 * `published_at_us` is a legacy parameter retained by this internal API. The
 * barrier never compares wall-clock values.
 */
gboolean atm_publication_barrier_record (AtmPublicationBarrier *barrier,
                                         const char *conversation_id,
                                         gboolean succeeded,
                                         gint64 published_at_us,
                                         GError **error);

/*
 * Answer whether the conversation may be discarded.
 *
 * TRUE only when the most recent managed-export reconciliation succeeded.
 * An unknown conversation and a failed reconciliation both refuse, with
 * distinct error codes so the caller can distinguish "not reconciled" from
 * "reconciliation failed".
 */
gboolean atm_publication_barrier_may_discard (const AtmPublicationBarrier *barrier,
                                              const char *conversation_id,
                                              GError **error);

/* Number of conversations with a recorded successful reconciliation. */
guint atm_publication_barrier_published_count (const AtmPublicationBarrier *barrier);

/* Number of conversations with a recorded failed reconciliation. */
guint atm_publication_barrier_failed_count (const AtmPublicationBarrier *barrier);

/* Number of distinct conversations the barrier knows about. */
guint atm_publication_barrier_tracked_count (const AtmPublicationBarrier *barrier);

G_END_DECLS
