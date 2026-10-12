/* Qualification test for the managed-export reconciliation barrier.
 *
 * Only archived conversations have managed JSON mirrors. An unarchived row
 * may be discarded only after any stale mirror has been removed or confirmed
 * absent; failed or unknown reconciliation must refuse deletion.
 *
 * These unit cases exercise the pure state machine without SQLite, filesystem
 * calls, or a conversation store. The store-level failure/retry regression is
 * in conversation_persistence_store_test.vala.
 */

#include <glib.h>

#include "publication_barrier.h"

/* A successful export-reconciliation outcome permits discard. */
static void
test_published_may_be_discarded (void)
{
    AtmPublicationBarrier *barrier = atm_publication_barrier_new ();
    GError *error = NULL;

    g_assert_true (atm_publication_barrier_record (barrier, "conv-1", TRUE,
        1700000000000000, &error));
    g_assert_no_error (error);

    g_assert_true (atm_publication_barrier_may_discard (barrier, "conv-1", &error));
    g_assert_no_error (error);

    g_assert_cmpuint (atm_publication_barrier_published_count (barrier), ==, 1);
    g_assert_cmpuint (atm_publication_barrier_failed_count (barrier), ==, 0);
    g_assert_cmpuint (atm_publication_barrier_tracked_count (barrier), ==, 1);

    atm_publication_barrier_free (barrier);
}

/* The fault this barrier exists for: reconciliation failed, so discard must
 * be refused rather than leaving an inconsistent managed-export path. */
static void
test_failed_publish_refuses_discard (void)
{
    AtmPublicationBarrier *barrier = atm_publication_barrier_new ();
    GError *error = NULL;

    g_assert_true (atm_publication_barrier_record (barrier, "conv-1", FALSE,
        0, &error));
    g_assert_no_error (error);

    g_assert_false (atm_publication_barrier_may_discard (barrier, "conv-1", &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_LAST_PUBLISH_FAILED);
    g_clear_error (&error);

    g_assert_cmpuint (atm_publication_barrier_published_count (barrier), ==, 0);
    g_assert_cmpuint (atm_publication_barrier_failed_count (barrier), ==, 1);

    atm_publication_barrier_free (barrier);
}

/* A conversation with no recorded reconciliation outcome may not be
 * discarded. Absence of evidence is not evidence of successful cleanup. */
static void
test_unknown_conversation_refuses_discard (void)
{
    AtmPublicationBarrier *barrier = atm_publication_barrier_new ();
    GError *error = NULL;

    g_assert_false (atm_publication_barrier_may_discard (barrier, "never-seen", &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_UNPUBLISHED);
    g_clear_error (&error);

    g_assert_cmpuint (atm_publication_barrier_tracked_count (barrier), ==, 0);

    atm_publication_barrier_free (barrier);
}

/* The newest outcome is authoritative: failed reconciliation after success
 * blocks discard, while later successful reconciliation restores permission. */
static void
test_later_outcome_overrides_earlier (void)
{
    AtmPublicationBarrier *barrier = atm_publication_barrier_new ();
    GError *error = NULL;

    g_assert_true (atm_publication_barrier_record (barrier, "conv-1", TRUE,
        1700000000000000, &error));
    g_assert_true (atm_publication_barrier_record (barrier, "conv-1", FALSE,
        0, &error));
    g_assert_no_error (error);
    g_assert_false (atm_publication_barrier_may_discard (barrier, "conv-1", &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_LAST_PUBLISH_FAILED);
    g_clear_error (&error);

    g_assert_true (atm_publication_barrier_record (barrier, "conv-1", TRUE,
        1700000001000000, &error));
    g_assert_no_error (error);
    g_assert_true (atm_publication_barrier_may_discard (barrier, "conv-1", &error));
    g_assert_no_error (error);

    /* The conversation is tracked once, not twice. */
    g_assert_cmpuint (atm_publication_barrier_tracked_count (barrier), ==, 1);
    g_assert_cmpuint (atm_publication_barrier_published_count (barrier), ==, 1);

    atm_publication_barrier_free (barrier);
}

/* Mixed outcome set: the discard decision is per conversation, not global. */
static void
test_decisions_are_per_conversation (void)
{
    AtmPublicationBarrier *barrier = atm_publication_barrier_new ();
    GError *error = NULL;

    g_assert_true (atm_publication_barrier_record (barrier, "conv-a", TRUE,
        1700000000000000, &error));
    g_assert_true (atm_publication_barrier_record (barrier, "conv-b", FALSE,
        0, &error));
    g_assert_no_error (error);

    g_assert_true (atm_publication_barrier_may_discard (barrier, "conv-a", &error));
    g_assert_no_error (error);

    g_assert_false (atm_publication_barrier_may_discard (barrier, "conv-b", &error));
    g_clear_error (&error);

    g_assert_cmpuint (atm_publication_barrier_tracked_count (barrier), ==, 2);
    g_assert_cmpuint (atm_publication_barrier_published_count (barrier), ==, 1);
    g_assert_cmpuint (atm_publication_barrier_failed_count (barrier), ==, 1);

    atm_publication_barrier_free (barrier);
}

/* Argument guards: identifiers are required, and whitespace is not an id. */
static void
test_argument_guards (void)
{
    AtmPublicationBarrier *barrier = atm_publication_barrier_new ();
    GError *error = NULL;

    g_assert_false (atm_publication_barrier_record (NULL, "conv-1", TRUE, 0, &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_publication_barrier_record (barrier, NULL, TRUE, 0, &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_publication_barrier_record (barrier, "   ", TRUE, 0, &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_publication_barrier_may_discard (NULL, "conv-1", &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_publication_barrier_may_discard (barrier, "  ", &error));
    g_assert_error (error, ATM_PUBLICATION_BARRIER_ERROR,
                    ATM_PUBLICATION_BARRIER_ERROR_ARGUMENT);
    g_clear_error (&error);

    /* A rejected record must not create an entry. */
    g_assert_cmpuint (atm_publication_barrier_tracked_count (barrier), ==, 0);

    /* Null-safe accessors and free. */
    g_assert_cmpuint (atm_publication_barrier_published_count (NULL), ==, 0);
    g_assert_cmpuint (atm_publication_barrier_failed_count (NULL), ==, 0);
    g_assert_cmpuint (atm_publication_barrier_tracked_count (NULL), ==, 0);
    atm_publication_barrier_free (NULL);

    atm_publication_barrier_free (barrier);
}

/* Model the cleanup decision for several rows. Successful reconciliation
 * allows discard; failure or an unknown outcome retains the row for retry.
 * The end-to-end persistence-store test exercises the real removal failure. */
static void
test_startup_cleanup_sequence (void)
{
    AtmPublicationBarrier *barrier = atm_publication_barrier_new ();
    GError *error = NULL;

    g_assert_true (atm_publication_barrier_record (barrier, "conv-1", TRUE,
        1700000000000000, &error));
    g_assert_true (atm_publication_barrier_record (barrier, "conv-2", TRUE,
        1700000001000000, &error));
    g_assert_true (atm_publication_barrier_record (barrier, "conv-3", FALSE,
        0, &error));
    g_assert_no_error (error);

    const char *candidates[] = { "conv-1", "conv-2", "conv-3" };
    guint discarded = 0;
    for (guint i = 0; i < G_N_ELEMENTS (candidates); i++) {
        GError *decision = NULL;
        if (atm_publication_barrier_may_discard (barrier, candidates[i], &decision)) {
            discarded++;
        } else {
            /* The refusal must be legible: it names why, not just "no". */
            g_assert_nonnull (decision);
            g_clear_error (&decision);
        }
    }

    g_assert_cmpuint (discarded, ==, 2);

    atm_publication_barrier_free (barrier);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/publication-barrier/published-discardable",
                     test_published_may_be_discarded);
    g_test_add_func ("/publication-barrier/failed-refuses",
                     test_failed_publish_refuses_discard);
    g_test_add_func ("/publication-barrier/unknown-refuses",
                     test_unknown_conversation_refuses_discard);
    g_test_add_func ("/publication-barrier/later-overrides",
                     test_later_outcome_overrides_earlier);
    g_test_add_func ("/publication-barrier/per-conversation",
                     test_decisions_are_per_conversation);
    g_test_add_func ("/publication-barrier/argument-guards",
                     test_argument_guards);
    g_test_add_func ("/publication-barrier/startup-sequence",
                     test_startup_cleanup_sequence);

    return g_test_run ();
}
