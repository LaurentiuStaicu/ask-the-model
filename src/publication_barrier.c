#include "publication_barrier.h"

#include <string.h>

/*
 * Publication barrier: the condition ConversationPersistenceStore is missing.
 *
 * Pure state machine over per-conversation publish outcomes. No I/O, no locks,
 * no wall-clock comparisons: the caller reports what happened, the barrier
 * records it and answers whether a discard is allowed.
 *
 * The reason a later failure clears an earlier success: the newer attempt is
 * the truth about the current export. If the export for a conversation failed
 * on this run, the file on disk is not the one the row describes, whatever an
 * earlier run managed to write.
 */

typedef struct {
    char *conversation_id;
    gboolean published;
    gint64 published_at_us;   /* recorded only on success; caller's use only */
} Entry;

struct AtmPublicationBarrier {
    GHashTable *entries;   /* conversation_id -> Entry* */
};

GQuark
atm_publication_barrier_error_quark (void)
{
    return g_quark_from_static_string ("atm-publication-barrier-error-quark");
}

static gboolean
fail (GError **error, AtmPublicationBarrierError code, const char *message)
{
    g_set_error_literal (error, ATM_PUBLICATION_BARRIER_ERROR, code, message);
    return FALSE;
}

static void
entry_free (gpointer data)
{
    Entry *entry = data;
    if (entry == NULL) return;
    g_free (entry->conversation_id);
    g_free (entry);
}

/* Empty or all-whitespace is not an identifier. */
static char *
normalize_id (const char *value)
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

AtmPublicationBarrier *
atm_publication_barrier_new (void)
{
    AtmPublicationBarrier *barrier = g_new0 (AtmPublicationBarrier, 1);
    barrier->entries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              NULL, entry_free);
    return barrier;
}

void
atm_publication_barrier_free (AtmPublicationBarrier *barrier)
{
    if (barrier == NULL) return;
    g_clear_pointer (&barrier->entries, g_hash_table_unref);
    g_free (barrier);
}

gboolean
atm_publication_barrier_record (AtmPublicationBarrier *barrier,
                                const char *conversation_id,
                                gboolean succeeded,
                                gint64 published_at_us,
                                GError **error)
{
    if (barrier == NULL || conversation_id == NULL)
        return fail (error, ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT,
                     "Invalid publication barrier arguments.");

    char *id = normalize_id (conversation_id);
    if (id == NULL) {
        return fail (error, ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT,
                     "A publish outcome requires a conversation identifier.");
    }

    Entry *entry = g_hash_table_lookup (barrier->entries, id);
    if (entry == NULL) {
        entry = g_new0 (Entry, 1);
        entry->conversation_id = g_strdup (id);
        g_hash_table_insert (barrier->entries, entry->conversation_id, entry);
    }

    /* The newer attempt is the truth, in both directions: a success publishes,
     * a failure unpublishes an earlier success. */
    entry->published = succeeded;
    entry->published_at_us = succeeded ? published_at_us : 0;

    g_free (id);
    return TRUE;
}

gboolean
atm_publication_barrier_may_discard (const AtmPublicationBarrier *barrier,
                                     const char *conversation_id,
                                     GError **error)
{
    if (barrier == NULL || conversation_id == NULL)
        return fail (error, ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT,
                     "Invalid publication barrier arguments.");

    char *id = normalize_id (conversation_id);
    if (id == NULL) {
        return fail (error, ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT,
                     "A discard decision requires a conversation identifier.");
    }

    const Entry *entry = g_hash_table_lookup (barrier->entries, id);
    g_free (id);

    if (entry == NULL) {
        /* Nothing is known about this conversation. Discarding on absence of
         * evidence is exactly the fault this barrier exists to prevent. */
        return fail (error, ATM_PUBLICATION_BARRIER_ERROR_UNPUBLISHED,
                     "Conversation has no recorded publish outcome and may not be discarded.");
    }

    if (!entry->published) {
        return fail (error, ATM_PUBLICATION_BARRIER_ERROR_LAST_PUBLISH_FAILED,
                     "Conversation's last publish attempt failed and it may not be discarded.");
    }

    return TRUE;
}

guint
atm_publication_barrier_published_count (const AtmPublicationBarrier *barrier)
{
    if (barrier == NULL) return 0;
    guint count = 0;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init (&iter, barrier->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value)) {
        const Entry *entry = value;
        if (entry->published) count++;
    }
    return count;
}

guint
atm_publication_barrier_failed_count (const AtmPublicationBarrier *barrier)
{
    if (barrier == NULL) return 0;
    guint count = 0;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init (&iter, barrier->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value)) {
        const Entry *entry = value;
        if (!entry->published) count++;
    }
    return count;
}

guint
atm_publication_barrier_tracked_count (const AtmPublicationBarrier *barrier)
{
    if (barrier == NULL) return 0;
    return g_hash_table_size (barrier->entries);
}
