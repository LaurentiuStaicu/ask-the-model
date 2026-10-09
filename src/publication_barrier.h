#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Publication barrier: a conversation may not be discarded until it has been
 * durably published.
 *
 * M11 identifies this fault class:
 *
 *   SUCCESS -> persist OK -> metadata OK -> publish FAILS
 *
 * and in ConversationPersistenceStore the two halves are composed in the wrong
 * order. The constructor discards every unarchived conversation BEFORE it runs
 * the automatic export:
 *
 *   native_store = opened;
 *   discard_unarchived_conversations_best_effort ();
 *   if (automatic_export_directory_ready ()) {
 *       sync_all_automatic_exports_best_effort ();
 *   }
 *
 * and the export is best-effort, so a failure becomes a warning in the log
 * rather than a condition anything reacts to. The row is still in SQLite, but
 * its export is missing, and the next startup deletes the row because it is
 * unarchived.
 *
 * The store already holds verification that could catch the inconsistency -
 * ConversationPersistenceSnapshot.require_continuation_identity compares the
 * model name AND digest and validates the repository generation - but it runs
 * on reopen. By then the row is gone.
 *
 * This module supplies the missing condition: a conversation is discardable
 * only when its last publish attempt succeeded. It is a pure state machine, no
 * I/O and no locks, so the failure mode becomes deterministically testable.
 *
 * It does not perform publishing and does not decide what publishing means.
 * The caller reports success or failure; the barrier records it and answers the
 * discard question.
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
 * Record the outcome of a publish attempt. `conversation_id` must be non-empty
 * after stripping. A success marks the conversation published; a failure marks
 * it unpublished and clears any earlier success, because the newer attempt is
 * the truth about the current export.
 *
 * `published_at_us` is recorded only on success and only for the caller's use;
 * the barrier never compares wall-clock values.
 */
gboolean atm_publication_barrier_record (AtmPublicationBarrier *barrier,
                                         const char *conversation_id,
                                         gboolean succeeded,
                                         gint64 published_at_us,
                                         GError **error);

/*
 * Answer whether the conversation may be discarded.
 *
 * TRUE only when the most recent publish attempt for that conversation
 * succeeded. An unknown conversation and a failed one both refuse, with
 * distinct error codes so a caller can tell "never published" from "publish
 * failed".
 */
gboolean atm_publication_barrier_may_discard (const AtmPublicationBarrier *barrier,
                                              const char *conversation_id,
                                              GError **error);

/* Number of conversations with a recorded successful publish. */
guint atm_publication_barrier_published_count (const AtmPublicationBarrier *barrier);

/* Number of conversations with a recorded failed publish. */
guint atm_publication_barrier_failed_count (const AtmPublicationBarrier *barrier);

/* Number of distinct conversations the barrier knows about. */
guint atm_publication_barrier_tracked_count (const AtmPublicationBarrier *barrier);

G_END_DECLS
