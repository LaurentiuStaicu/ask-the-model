#include "chart_m4.h"

#include <math.h>

static void
assert_point (const AtmChartM4Plan *plan, guint at, guint source_index, double x, double y)
{
    const AtmChartM4Point *point = atm_chart_m4_plan_point (plan, at);
    g_assert_nonnull (point);
    g_assert_cmpuint (point->source_index, ==, source_index);
    g_assert_cmpfloat (point->x, ==, x);
    g_assert_cmpfloat (point->y, ==, y);
}

static void
test_non_dense_identity (void)
{
    const AtmChartM4Point input[] = {
        {10, 0.0, 4.0}, {11, 2.0, 1.0}, {12, 5.0, 8.0}, {13, 8.0, 2.0}
    };
    AtmChartM4Plan *plan = NULL;
    g_assert_true (atm_chart_m4_plan_new (input, G_N_ELEMENTS (input), 8, &plan, NULL));
    g_assert_false (atm_chart_m4_plan_is_reduced (plan));
    g_assert_cmpuint (atm_chart_m4_plan_count (plan), ==, G_N_ELEMENTS (input));
    for (guint i = 0; i < G_N_ELEMENTS (input); i++)
        assert_point (plan, i, input[i].source_index, input[i].x, input[i].y);
    g_assert_null (atm_chart_m4_plan_point (plan, G_N_ELEMENTS (input)));
    atm_chart_m4_plan_free (plan);
}

static void
test_dense_roles_and_order (void)
{
    const AtmChartM4Point input[] = {
        {0, 0.10, 5.0}, {1, 0.20, 2.0}, {2, 0.30, 9.0},
        {3, 0.40, 1.0}, {4, 0.50, 8.0}, {5, 1.00, 4.0}
    };
    AtmChartM4Plan *plan = NULL;
    g_assert_true (atm_chart_m4_plan_new (input, G_N_ELEMENTS (input), 1, &plan, NULL));
    g_assert_true (atm_chart_m4_plan_is_reduced (plan));
    g_assert_cmpuint (atm_chart_m4_plan_count (plan), ==, 4);
    assert_point (plan, 0, 0, 0.10, 5.0);
    assert_point (plan, 1, 2, 0.30, 9.0);
    assert_point (plan, 2, 3, 0.40, 1.0);
    assert_point (plan, 3, 5, 1.00, 4.0);
    atm_chart_m4_plan_free (plan);
}

static void
test_role_collision_dedup (void)
{
    const AtmChartM4Point input[] = {
        {20, 0.1, 0.0}, {21, 0.2, 1.0}, {22, 0.3, 2.0},
        {23, 0.4, 3.0}, {24, 0.5, 4.0}, {25, 1.0, 5.0}
    };
    AtmChartM4Plan *plan = NULL;
    g_assert_true (atm_chart_m4_plan_new (input, G_N_ELEMENTS (input), 1, &plan, NULL));
    g_assert_cmpuint (atm_chart_m4_plan_count (plan), ==, 2);
    assert_point (plan, 0, 20, 0.1, 0.0);
    assert_point (plan, 1, 25, 1.0, 5.0);
    atm_chart_m4_plan_free (plan);
}

static void
test_spike_and_endpoints_survive (void)
{
    const AtmChartM4Point input[] = {
        {0, 0.1, 4.0}, {1, 0.2, 4.1}, {2, 0.3, 100.0}, {3, 0.4, 4.2},
        {4, 1.1, 4.1}, {5, 1.2, -80.0}, {6, 1.3, 4.0}, {7, 2.0, 4.3}
    };
    AtmChartM4Plan *plan = NULL;
    g_assert_true (atm_chart_m4_plan_new (input, G_N_ELEMENTS (input), 2, &plan, NULL));
    g_assert_true (atm_chart_m4_plan_is_reduced (plan));
    g_assert_cmpuint (atm_chart_m4_plan_point (plan, 0)->source_index, ==, 0);
    g_assert_cmpuint (atm_chart_m4_plan_point (plan, atm_chart_m4_plan_count (plan) - 1)->source_index, ==, 7);
    gboolean high = FALSE, low = FALSE;
    for (guint i = 0; i < atm_chart_m4_plan_count (plan); i++) {
        guint index = atm_chart_m4_plan_point (plan, i)->source_index;
        high |= index == 2;
        low |= index == 5;
    }
    g_assert_true (high);
    g_assert_true (low);
    atm_chart_m4_plan_free (plan);
}

static void
test_irregular_x_uses_projected_columns (void)
{
    const AtmChartM4Point input[] = {
        {0, 0.10, 5.0}, {1, 0.15, 4.0}, {2, 0.20, 9.0},
        {3, 0.25, 1.0}, {4, 0.30, 7.0}, {5, 0.35, 6.0},
        {6, 2.10, 3.0}, {7, 2.20, 2.0}, {8, 3.00, 4.0}
    };
    AtmChartM4Plan *plan = NULL;
    g_assert_true (atm_chart_m4_plan_new (input, G_N_ELEMENTS (input), 3, &plan, NULL));
    g_assert_true (atm_chart_m4_plan_is_reduced (plan));
    gboolean omitted_dense_middle = TRUE;
    for (guint i = 0; i < atm_chart_m4_plan_count (plan); i++)
        omitted_dense_middle &= atm_chart_m4_plan_point (plan, i)->source_index != 1;
    g_assert_true (omitted_dense_middle);
    g_assert_cmpuint (atm_chart_m4_plan_point (plan, 0)->source_index, ==, 0);
    g_assert_cmpuint (atm_chart_m4_plan_point (plan, atm_chart_m4_plan_count (plan) - 1)->source_index, ==, 8);
    atm_chart_m4_plan_free (plan);
}

static void
test_deterministic (void)
{
    AtmChartM4Point input[64];
    for (guint i = 0; i < G_N_ELEMENTS (input); i++) {
        input[i].source_index = 100 + i;
        input[i].x = i / 8.0;
        input[i].y = (double) ((i * 17) % 23) - 11.0;
    }
    AtmChartM4Plan *a = NULL, *b = NULL;
    g_assert_true (atm_chart_m4_plan_new (input, G_N_ELEMENTS (input), 8, &a, NULL));
    g_assert_true (atm_chart_m4_plan_new (input, G_N_ELEMENTS (input), 8, &b, NULL));
    g_assert_cmpuint (atm_chart_m4_plan_count (a), ==, atm_chart_m4_plan_count (b));
    for (guint i = 0; i < atm_chart_m4_plan_count (a); i++) {
        const AtmChartM4Point *pa = atm_chart_m4_plan_point (a, i);
        const AtmChartM4Point *pb = atm_chart_m4_plan_point (b, i);
        g_assert_cmpuint (pa->source_index, ==, pb->source_index);
        g_assert_cmpfloat (pa->x, ==, pb->x);
        g_assert_cmpfloat (pa->y, ==, pb->y);
    }
    atm_chart_m4_plan_free (a);
    atm_chart_m4_plan_free (b);
}

static void
test_invalid (void)
{
    AtmChartM4Point valid[] = {{0, 0.0, 0.0}, {1, 1.0, 1.0}};
    AtmChartM4Plan *plan = NULL;
    GError *error = NULL;

    g_assert_false (atm_chart_m4_plan_new (NULL, 2, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_chart_m4_plan_new (valid, 0, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (atm_chart_m4_plan_new (valid, 2, 0, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_LIMIT);
    g_clear_error (&error);

    AtmChartM4Point bad_x[] = {{0, 0.8, 0.0}, {1, 0.7, 1.0}};
    g_assert_false (atm_chart_m4_plan_new (bad_x, 2, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_ORDER);
    g_clear_error (&error);

    AtmChartM4Point bad_index[] = {{1, 0.1, 0.0}, {1, 0.2, 1.0}};
    g_assert_false (atm_chart_m4_plan_new (bad_index, 2, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_ORDER);
    g_clear_error (&error);

    AtmChartM4Point nonfinite[] = {{0, 0.0, 0.0}, {1, 1.0, NAN}};
    g_assert_false (atm_chart_m4_plan_new (nonfinite, 2, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_SHAPE);
    g_clear_error (&error);

    AtmChartM4Point outside[] = {{0, 0.0, 0.0}, {1, 2.1, 1.0}};
    g_assert_false (atm_chart_m4_plan_new (outside, 2, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_SHAPE);
    g_clear_error (&error);

    AtmChartM4Point *too_many = g_new0 (AtmChartM4Point, ATM_CHART_M4_MAX_POINTS + 1);
    g_assert_false (atm_chart_m4_plan_new (too_many, ATM_CHART_M4_MAX_POINTS + 1, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_LIMIT);
    g_clear_error (&error);
    g_free (too_many);

    g_assert_true (atm_chart_m4_plan_new (valid, 2, 2, &plan, NULL));
    AtmChartM4Plan *saved = plan;
    g_assert_false (atm_chart_m4_plan_new (valid, 2, 2, &plan, &error));
    g_assert_error (error, ATM_CHART_M4_ERROR, ATM_CHART_M4_ERROR_ARGUMENT);
    g_assert_true (plan == saved);
    g_clear_error (&error);
    atm_chart_m4_plan_free (plan);

    g_assert_cmpuint (atm_chart_m4_plan_count (NULL), ==, 0);
    g_assert_false (atm_chart_m4_plan_is_reduced (NULL));
    g_assert_null (atm_chart_m4_plan_point (NULL, 0));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/chart-m4/non-dense-identity", test_non_dense_identity);
    g_test_add_func ("/chart-m4/dense-roles-order", test_dense_roles_and_order);
    g_test_add_func ("/chart-m4/role-collision-dedup", test_role_collision_dedup);
    g_test_add_func ("/chart-m4/spike-endpoints", test_spike_and_endpoints_survive);
    g_test_add_func ("/chart-m4/irregular-x", test_irregular_x_uses_projected_columns);
    g_test_add_func ("/chart-m4/deterministic", test_deterministic);
    g_test_add_func ("/chart-m4/invalid", test_invalid);
    return g_test_run ();
}
