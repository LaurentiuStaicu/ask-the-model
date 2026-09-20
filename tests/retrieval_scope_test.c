#include "retrieval_scope.h"

#include <glib.h>

static GPtrArray *
active_scope_all (void)
{
    GPtrArray *active = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );

    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "ewd",
            "/cache/ewd.sqlite"
        )
    );
    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "cbd",
            "/cache/cbd.sqlite"
        )
    );
    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "rmd",
            "/cache/rmd.sqlite"
        )
    );

    return active;
}

static const AtmRetrievalRepositoryScope *
selected_at (
    AtmRetrievalScopeSelection *selection,
    guint index
)
{
    return g_ptr_array_index (
        selection->repositories,
        index
    );
}

static void
test_no_explicit_repository_keeps_active_scope (void)
{
    GPtrArray *active = active_scope_all ();
    AtmRetrievalScopeSelection *selection = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_scope_select (
            "Care este starea curentă?",
            active,
            &selection,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (selection->explicit_scope);
    g_assert_false (selection->requested_outside_scope);
    g_assert_cmpuint (selection->repositories->len, ==, 3);
    g_assert_cmpstr (
        selected_at (selection, 0)->repository_id,
        ==,
        "ewd"
    );
    g_assert_cmpstr (
        selected_at (selection, 1)->repository_id,
        ==,
        "cbd"
    );
    g_assert_cmpstr (
        selected_at (selection, 2)->repository_id,
        ==,
        "rmd"
    );

    atm_retrieval_scope_selection_free (selection);
    g_ptr_array_unref (active);
}

static void
test_explicit_acronyms_restrict_active_scope (void)
{
    GPtrArray *active = active_scope_all ();
    AtmRetrievalScopeSelection *selection = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_scope_select (
            "Compară RMD cu cbd.",
            active,
            &selection,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (selection->explicit_scope);
    g_assert_false (selection->requested_outside_scope);
    g_assert_cmpuint (selection->repositories->len, ==, 2);
    g_assert_cmpstr (
        selected_at (selection, 0)->repository_id,
        ==,
        "cbd"
    );
    g_assert_cmpstr (
        selected_at (selection, 1)->repository_id,
        ==,
        "rmd"
    );

    atm_retrieval_scope_selection_free (selection);
    g_ptr_array_unref (active);
}

static void
test_long_repository_name_is_recognized (void)
{
    GPtrArray *active = active_scope_all ();
    AtmRetrievalScopeSelection *selection = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_retrieval_scope_select (
            "EMPIRICAL World3 Dynamics: boundary",
            active,
            &selection,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (selection->explicit_scope);
    g_assert_cmpuint (selection->repositories->len, ==, 1);
    g_assert_cmpstr (
        selected_at (selection, 0)->repository_id,
        ==,
        "ewd"
    );

    atm_retrieval_scope_selection_free (selection);
    g_ptr_array_unref (active);
}

static void
test_named_repository_outside_conversation_is_not_added (void)
{
    GPtrArray *active = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );
    AtmRetrievalScopeSelection *selection = NULL;
    GError *error = NULL;

    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "ewd",
            "/cache/ewd.sqlite"
        )
    );

    g_assert_true (
        atm_retrieval_scope_select (
            "Și în RMD?",
            active,
            &selection,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (selection->explicit_scope);
    g_assert_true (selection->requested_outside_scope);
    g_assert_cmpuint (selection->repositories->len, ==, 0);

    atm_retrieval_scope_selection_free (selection);
    g_ptr_array_unref (active);
}

static void
test_mixed_available_and_unavailable_explicit_scope (void)
{
    GPtrArray *active = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );
    AtmRetrievalScopeSelection *selection = NULL;
    GError *error = NULL;

    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "ewd",
            "/cache/ewd.sqlite"
        )
    );
    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "cbd",
            "/cache/cbd.sqlite"
        )
    );

    g_assert_true (
        atm_retrieval_scope_select (
            "Compară CBD și RMD",
            active,
            &selection,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_true (selection->explicit_scope);
    g_assert_true (selection->requested_outside_scope);
    g_assert_cmpuint (selection->repositories->len, ==, 1);
    g_assert_cmpstr (
        selected_at (selection, 0)->repository_id,
        ==,
        "cbd"
    );

    atm_retrieval_scope_selection_free (selection);
    g_ptr_array_unref (active);
}

static void
test_duplicate_active_repository_is_rejected (void)
{
    GPtrArray *active = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_retrieval_repository_scope_free
    );
    AtmRetrievalScopeSelection *selection = NULL;
    GError *error = NULL;

    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "ewd",
            "/cache/a.sqlite"
        )
    );
    g_ptr_array_add (
        active,
        atm_retrieval_repository_scope_new (
            "ewd",
            "/cache/b.sqlite"
        )
    );

    g_assert_false (
        atm_retrieval_scope_select (
            "status",
            active,
            &selection,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_RETRIEVAL_SCOPE_ERROR,
        ATM_RETRIEVAL_SCOPE_ERROR_ARGUMENT
    );
    g_assert_null (selection);

    g_clear_error (&error);
    g_ptr_array_unref (active);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/retrieval-scope/inherit-active",
        test_no_explicit_repository_keeps_active_scope
    );
    g_test_add_func (
        "/retrieval-scope/acronyms",
        test_explicit_acronyms_restrict_active_scope
    );
    g_test_add_func (
        "/retrieval-scope/long-name",
        test_long_repository_name_is_recognized
    );
    g_test_add_func (
        "/retrieval-scope/outside-conversation",
        test_named_repository_outside_conversation_is_not_added
    );
    g_test_add_func (
        "/retrieval-scope/mixed-available-unavailable",
        test_mixed_available_and_unavailable_explicit_scope
    );
    g_test_add_func (
        "/retrieval-scope/duplicate-active",
        test_duplicate_active_repository_is_rejected
    );

    return g_test_run ();
}
