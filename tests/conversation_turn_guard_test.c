/* Qualification test for the M12 turn guard.
 *
 * The guard is the commit barrier for one conversational turn. It closes the
 * fault class M12 names: a late provider result, a model replaced mid-turn by
 * discovery, and a repository generation changing under an in-flight turn.
 *
 * Every case below is reachable without a provider, a repository or a session,
 * because the guard is a pure state machine. That is the point: a fault that
 * today can only be reasoned about becomes a test that fails when the barrier
 * is removed.
 */

#include <glib.h>

#include "conversation_turn_guard.h"

#define ATM_TEST_GENERATION 41

/* A turn that begins and commits with nothing changed must be allowed. */
static void
test_commit_honours_unchanged_identity (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);
    g_assert_true (atm_turn_guard_is_open (guard));
    g_assert_cmpint (atm_turn_guard_generation (guard), ==, ATM_TEST_GENERATION);
    g_assert_cmpstr (atm_turn_guard_model (guard), ==, "qwen3:8b");
    g_assert_cmpstr (atm_turn_guard_model_digest (guard), ==, "abc123");

    g_assert_true (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);
    g_assert_false (atm_turn_guard_is_open (guard));

    atm_turn_guard_free (guard);
}

/* The late-result path: a turn aborted before its result arrives must refuse
 * the commit rather than persist a result for an invalidated operation. */
static void
test_aborted_turn_refuses_late_result (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);

    atm_turn_guard_abort (guard);
    g_assert_false (atm_turn_guard_is_open (guard));

    /* The provider answered anyway and nothing about the model or generation
     * changed, so only the invalidation stands between this and the store. */
    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_INVALIDATED);
    g_clear_error (&error);

    /* A second attempt must not succeed either. */
    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    atm_turn_guard_free (guard);
}

/* Discovery replaced the active model mid-turn. */
static void
test_model_replaced_mid_turn_is_refused (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);

    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "llama3:70b", "abc123", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_STALE_MODEL);
    g_clear_error (&error);
    g_assert_false (atm_turn_guard_is_open (guard));

    atm_turn_guard_free (guard);
}

/* The same model name with different content: a re-pull. A name-only check
 * would let this commit, which is why the digest is recorded separately. */
static void
test_digest_change_under_same_name_is_refused (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);

    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "def456", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_STALE_DIGEST);
    g_clear_error (&error);

    atm_turn_guard_free (guard);
}

/* A repository mutation moved the Control DB generation under the turn. */
static void
test_generation_change_is_refused (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);

    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION + 1,
        "qwen3:8b", "abc123", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_STALE_GENERATION);
    g_clear_error (&error);

    atm_turn_guard_free (guard);
}

/* A turn that began without a digest does not gain one mid-flight, and an
 * empty string normalizes to absent rather than to a changed value. */
static void
test_absent_digest_normalization (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", NULL, &error));
    g_assert_no_error (error);
    g_assert_null (atm_turn_guard_model_digest (guard));

    g_assert_true (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "", &error));
    g_assert_no_error (error);

    /* The reverse: a turn that pinned a digest must not accept its absence at
     * commit, because that would silently drop the pin. */
    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);
    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_STALE_DIGEST);
    g_clear_error (&error);

    atm_turn_guard_free (guard);
}

/* Nested begin would overwrite the identity of an in-flight turn. */
static void
test_second_begin_is_refused (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);

    g_assert_false (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "llama3:70b", "xyz789", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_ALREADY_OPEN);
    g_clear_error (&error);

    /* The original identity survived the refused begin. */
    g_assert_cmpstr (atm_turn_guard_model (guard), ==, "qwen3:8b");
    g_assert_cmpstr (atm_turn_guard_model_digest (guard), ==, "abc123");
    g_assert_true (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);

    atm_turn_guard_free (guard);
}

/* Argument and lifecycle guards. */
static void
test_argument_and_lifecycle_guards (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    /* A commit with no open turn is refused: nothing may be committed without
     * a begin that recorded what it was validated against. */
    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_INVALIDATED);
    g_clear_error (&error);

    /* NULL guard and NULL model are argument faults, not state faults. */
    g_assert_false (atm_turn_guard_begin (NULL, ATM_TEST_GENERATION,
        "qwen3:8b", NULL, &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        NULL, NULL, &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_ARGUMENT);
    g_clear_error (&error);

    /* A whitespace-only model name is not a known model. */
    g_assert_false (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "   ", NULL, &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_ARGUMENT);
    g_clear_error (&error);

    /* A negative generation is never valid. */
    g_assert_false (atm_turn_guard_begin (guard, -1, "qwen3:8b", NULL, &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_ARGUMENT);
    g_clear_error (&error);

    /* A turn with no repository may legitimately pass generation zero. */
    g_assert_true (atm_turn_guard_begin (guard, 0, "qwen3:8b", NULL, &error));
    g_assert_no_error (error);
    g_assert_true (atm_turn_guard_commit (guard, 0, "qwen3:8b", NULL, &error));
    g_assert_no_error (error);

    /* Aborting a closed guard is a no-op, not an error. */
    atm_turn_guard_abort (guard);
    atm_turn_guard_abort (NULL);
    atm_turn_guard_free (NULL);

    /* Accessors on a closed guard report nothing. */
    g_assert_false (atm_turn_guard_is_open (guard));
    g_assert_null (atm_turn_guard_model (guard));
    g_assert_null (atm_turn_guard_model_digest (guard));
    g_assert_cmpint (atm_turn_guard_generation (guard), ==, 0);

    atm_turn_guard_free (guard);
}

/* A refused commit closes the turn, so a caller cannot retry a stale commit
 * after correcting the model and thereby persist a result from the old one. */
static void
test_refused_commit_closes_the_turn (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_no_error (error);

    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "llama3:70b", "abc123", &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR, ATM_TURN_GUARD_ERROR_STALE_MODEL);
    g_clear_error (&error);
    g_assert_false (atm_turn_guard_is_open (guard));

    /* Retrying with the correct model must not succeed: the turn is over. */
    g_assert_false (atm_turn_guard_commit (guard, ATM_TEST_GENERATION,
        "qwen3:8b", "abc123", &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    atm_turn_guard_free (guard);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/turn-guard/commit-unchanged", test_commit_honours_unchanged_identity);
    g_test_add_func ("/turn-guard/aborted-late-result", test_aborted_turn_refuses_late_result);
    g_test_add_func ("/turn-guard/model-replaced", test_model_replaced_mid_turn_is_refused);
    g_test_add_func ("/turn-guard/digest-changed", test_digest_change_under_same_name_is_refused);
    g_test_add_func ("/turn-guard/generation-changed", test_generation_change_is_refused);
    g_test_add_func ("/turn-guard/digest-normalization", test_absent_digest_normalization);
    g_test_add_func ("/turn-guard/second-begin", test_second_begin_is_refused);
    g_test_add_func ("/turn-guard/argument-guards", test_argument_and_lifecycle_guards);
    g_test_add_func ("/turn-guard/refused-commit-closes", test_refused_commit_closes_the_turn);

    return g_test_run ();
}
