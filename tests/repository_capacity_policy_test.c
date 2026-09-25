#include "repository_capacity_policy.h"

#include <glib.h>

static const char *CBD_SHA =
    "e6e3b3077b7d78e3f37d963e0734e9084e8a62ab";
static const char *EWD_SHA =
    "795a6b8e42e2a19497f686a89e7c3a6056e6930c";
static const char *RMD_SHA =
    "a8b5ff89a951d3194fdedb59771bc79937fe7bc7";
static const char *UNKNOWN_SHA =
    "1111111111111111111111111111111111111111";

static void
test_exact_download_profiles (void)
{
    struct {
        const char *repository_id;
        const char *sha;
        guint64 archive_bytes;
    } cases[] = {
        { "cbd", CBD_SHA, 200704 },
        { "ewd", EWD_SHA, 1241088 },
        { "rmd", RMD_SHA, 3747840 }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (cases); i++) {
        AtmRepositoryCapacityDownloadPolicy policy;
        GError *error = NULL;

        g_assert_true (
            atm_repository_capacity_policy_build_download (
                cases[i].repository_id,
                cases[i].sha,
                &policy,
                &error
            )
        );
        g_assert_no_error (error);
        g_assert_true (policy.byte_prediction_qualified);
        g_assert_cmpuint (
            policy.prediction.archive_additional_bytes,
            ==,
            cases[i].archive_bytes
        );
        g_assert_cmpuint (
            policy.prediction.archive_additional_inodes,
            ==,
            1
        );
    }
}

static void
test_exact_mutation_profiles (void)
{
    struct {
        const char *repository_id;
        const char *sha;
        guint64 snapshot_bytes;
        guint64 fresh_bytes;
        guint64 repair_bytes;
        guint64 index_bytes;
    } cases[] = {
        {
            "cbd", CBD_SHA,
            966656, 987136, 966656, 1011712
        },
        {
            "ewd", EWD_SHA,
            5124096, 5144576, 5124096, 13336576
        },
        {
            "rmd", RMD_SHA,
            17354752, 17375232, 17354752, 1323008
        }
    };

    for (gsize i = 0; i < G_N_ELEMENTS (cases); i++) {
        struct {
            AtmCapacityOperationKind kind;
            guint64 expected_snapshot_bytes;
        } operations[] = {
            {
                ATM_CAPACITY_OPERATION_FRESH_INSTALL,
                cases[i].fresh_bytes
            },
            {
                ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
                cases[i].snapshot_bytes
            },
            {
                ATM_CAPACITY_OPERATION_SAME_SHA_REPAIR,
                cases[i].repair_bytes
            }
        };

        for (gsize j = 0; j < G_N_ELEMENTS (operations); j++) {
            AtmRepositoryCapacityMutationPolicy policy;
            GError *error = NULL;

            g_assert_true (
                atm_repository_capacity_policy_build_mutation (
                    cases[i].repository_id,
                    cases[i].sha,
                    operations[j].kind,
                    &policy,
                    &error
                )
            );
            g_assert_no_error (error);
            g_assert_true (policy.byte_prediction_qualified);
            g_assert_cmpuint (
                policy.prediction.snapshot_additional_bytes,
                ==,
                operations[j].expected_snapshot_bytes
            );
            g_assert_cmpuint (
                policy.prediction.index_additional_bytes,
                ==,
                cases[i].index_bytes
            );
            g_assert_cmpuint (
                policy.prediction.index_additional_inodes,
                ==,
                4
            );
            g_assert_cmpuint (
                policy.prediction.state_additional_bytes,
                ==,
                131072
            );
            g_assert_cmpuint (
                policy.prediction.state_additional_inodes,
                ==,
                4
            );
        }
    }
}

static void
test_unknown_sha_preserves_nonbyte_policy (void)
{
    AtmRepositoryCapacityDownloadPolicy download;
    AtmRepositoryCapacityMutationPolicy mutation;
    GError *error = NULL;

    g_assert_true (
        atm_repository_capacity_policy_build_download (
            "rmd",
            UNKNOWN_SHA,
            &download,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (download.byte_prediction_qualified);
    g_assert_cmpuint (
        download.prediction.archive_additional_bytes,
        ==,
        0
    );
    g_assert_cmpuint (
        download.prediction.archive_additional_inodes,
        ==,
        1
    );

    g_assert_true (
        atm_repository_capacity_policy_build_mutation (
            "rmd",
            UNKNOWN_SHA,
            ATM_CAPACITY_OPERATION_DIFFERENT_SHA_UPDATE,
            &mutation,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_false (mutation.byte_prediction_qualified);
    g_assert_cmpuint (
        mutation.prediction.snapshot_additional_bytes,
        ==,
        0
    );
    g_assert_cmpuint (
        mutation.prediction.index_additional_bytes,
        ==,
        0
    );
    g_assert_cmpuint (
        mutation.prediction.index_additional_inodes,
        ==,
        4
    );
    g_assert_cmpuint (
        mutation.prediction.state_additional_bytes,
        ==,
        131072
    );
    g_assert_cmpuint (
        mutation.prediction.state_additional_inodes,
        ==,
        4
    );
}

static void
test_invalid_identity_and_operation_rejected (void)
{
    AtmRepositoryCapacityDownloadPolicy download;
    AtmRepositoryCapacityMutationPolicy mutation;
    GError *error = NULL;

    g_assert_false (
        atm_repository_capacity_policy_build_download (
            "other",
            UNKNOWN_SHA,
            &download,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_REPOSITORY_CAPACITY_POLICY_ERROR,
        ATM_REPOSITORY_CAPACITY_POLICY_ERROR_ARGUMENT
    );
    g_clear_error (&error);

    g_assert_false (
        atm_repository_capacity_policy_build_download (
            "rmd",
            "ABC",
            &download,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_REPOSITORY_CAPACITY_POLICY_ERROR,
        ATM_REPOSITORY_CAPACITY_POLICY_ERROR_ARGUMENT
    );
    g_clear_error (&error);

    g_assert_false (
        atm_repository_capacity_policy_build_mutation (
            "rmd",
            UNKNOWN_SHA,
            (AtmCapacityOperationKind) 99,
            &mutation,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_REPOSITORY_CAPACITY_POLICY_ERROR,
        ATM_REPOSITORY_CAPACITY_POLICY_ERROR_ARGUMENT
    );
    g_clear_error (&error);
}

int
main (
    int argc,
    char **argv
)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/repository-capacity-policy/exact-download",
        test_exact_download_profiles
    );
    g_test_add_func (
        "/repository-capacity-policy/exact-mutation",
        test_exact_mutation_profiles
    );
    g_test_add_func (
        "/repository-capacity-policy/unknown-sha",
        test_unknown_sha_preserves_nonbyte_policy
    );
    g_test_add_func (
        "/repository-capacity-policy/invalid-input",
        test_invalid_identity_and_operation_rejected
    );

    return g_test_run ();
}
