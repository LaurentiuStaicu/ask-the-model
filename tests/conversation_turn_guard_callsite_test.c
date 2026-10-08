/* Call-site contract test for the M12 turn guard.
 *
 *   begin -> commit              PERMITTED
 *   begin -> abort -> commit     REFUSED (INVALIDATED)
 *   begin -> abort -> abort      abort is idempotent; the guard does not reopen
 */

#include <glib.h>
#include "conversation_turn_guard.h"

#define ATM_GEN 41
#define ATM_MODEL "qwen3:8b"
#define ATM_DIGEST "abc123"

/* The regression: an abort before the commit closes the guard, so the commit
 * refuses with INVALIDATED. That is what a misplaced abort on the success path
 * does, and no test of the module alone can see it. */
static void
test_abort_before_commit_refuses (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_GEN,
        ATM_MODEL, ATM_DIGEST, &error));
    g_assert_no_error (error);

    atm_turn_guard_abort (guard);
    g_assert_false (atm_turn_guard_is_open (guard));

    g_assert_false (atm_turn_guard_commit (guard, ATM_GEN,
        ATM_MODEL, ATM_DIGEST, &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR,
        ATM_TURN_GUARD_ERROR_INVALIDATED);
    g_clear_error (&error);

    atm_turn_guard_free (guard);
}

/* The success path: nothing aborts, so the commit must be allowed. */
static void
test_success_path_commits (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_GEN,
        ATM_MODEL, ATM_DIGEST, &error));
    g_assert_no_error (error);

    g_assert_true (atm_turn_guard_commit (guard, ATM_GEN,
        ATM_MODEL, ATM_DIGEST, &error));
    g_assert_no_error (error);
    g_assert_false (atm_turn_guard_is_open (guard));

    atm_turn_guard_free (guard);
}

/* A second abort must not reopen the turn. */
static void
test_repeated_abort_does_not_reopen (void)
{
    AtmTurnGuard *guard = atm_turn_guard_new ();
    GError *error = NULL;

    g_assert_true (atm_turn_guard_begin (guard, ATM_GEN,
        ATM_MODEL, ATM_DIGEST, &error));
    g_assert_no_error (error);

    atm_turn_guard_abort (guard);
    atm_turn_guard_abort (guard);
    g_assert_false (atm_turn_guard_is_open (guard));

    g_assert_false (atm_turn_guard_commit (guard, ATM_GEN,
        ATM_MODEL, ATM_DIGEST, &error));
    g_assert_error (error, ATM_TURN_GUARD_ERROR,
        ATM_TURN_GUARD_ERROR_INVALIDATED);
    g_clear_error (&error);

    atm_turn_guard_free (guard);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/turn-guard-callsite/abort-before-commit-refuses",
        test_abort_before_commit_refuses);
    g_test_add_func ("/turn-guard-callsite/success-path-commits",
        test_success_path_commits);
    g_test_add_func ("/turn-guard-callsite/repeated-abort-does-not-reopen",
        test_repeated_abort_does_not_reopen);

    return g_test_run ();
}
