#include "structured_json.h"

#include <glib.h>
#include <glib/gstdio.h>

static char *
new_json_file (const char *contents)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp (
        "atm-structured-json-test-XXXXXX",
        &error
    );
    g_assert_no_error (error);
    g_assert_nonnull (directory);

    char *path = g_build_filename (
        directory,
        "fixture.json",
        NULL
    );

    g_assert_true (
        g_file_set_contents (
            path,
            contents,
            -1,
            &error
        )
    );
    g_assert_no_error (error);

    g_free (directory);
    return path;
}

static void
remove_json_file (char *path)
{
    char *directory = g_path_get_dirname (path);

    g_remove (path);
    g_rmdir (directory);

    g_free (directory);
    g_free (path);
}

static AtmStructuredEntity *
find_entity (
    AtmStructuredJsonRecords *records,
    const char *native_id
)
{
    for (guint i = 0; i < records->entities->len; i++) {
        AtmStructuredEntity *entity = g_ptr_array_index (
            records->entities,
            i
        );

        if (g_strcmp0 (entity->native_id, native_id) == 0) {
            return entity;
        }
    }

    return NULL;
}

static AtmStructuredRelation *
find_relation (
    AtmStructuredJsonRecords *records,
    const char *native_id,
    const char *locator
)
{
    for (guint i = 0; i < records->relations->len; i++) {
        AtmStructuredRelation *relation = g_ptr_array_index (
            records->relations,
            i
        );

        if (g_strcmp0 (relation->native_id, native_id) == 0 &&
            (locator == NULL ||
             g_strcmp0 (relation->locator, locator) == 0)) {
            return relation;
        }
    }

    return NULL;
}

static void
test_suite_structural_patterns (void)
{
    const char *contents =
        "{"
        "\"modules\":[{"
            "\"id\":\"population\","
            "\"label\":{\"en\":\"Population\"},"
            "\"variables\":[{"
                "\"id\":\"food_per_capita\","
                "\"kind\":\"auxiliary\""
            "}]"
        "}],"
        "\"links\":[{"
            "\"id\":\"LINK.EXPOSURE.FAMILIARITY\","
            "\"source\":\"VAR.EXPOSURE.COUNT\","
            "\"target\":\"VAR.FAMILIARITY.CLAIM\","
            "\"relation_type\":\"CAUSAL\""
        "}],"
        "\"loops\":[{"
            "\"id\":\"government_refinancing_interest_loop\","
            "\"label\":{\"en\":\"Government refinancing loop\"},"
            "\"path\":[{"
                "\"from\":\"government_financing_need\","
                "\"to\":\"government_debt_issuance\","
                "\"sign\":\"+\""
            "}]"
        "}]"
        "}";
    char *path = new_json_file (contents);
    AtmStructuredJsonRecords *records = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_structured_json_extract (
            path,
            "model/structure.json",
            &records,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_nonnull (records);
    g_assert_cmpuint (records->entities->len, ==, 3);
    g_assert_cmpuint (records->relations->len, ==, 2);

    AtmStructuredEntity *module = find_entity (
        records,
        "population"
    );
    AtmStructuredEntity *variable = find_entity (
        records,
        "food_per_capita"
    );
    AtmStructuredEntity *loop = find_entity (
        records,
        "government_refinancing_interest_loop"
    );
    AtmStructuredRelation *link = find_relation (
        records,
        "LINK.EXPOSURE.FAMILIARITY",
        NULL
    );
    AtmStructuredRelation *edge = find_relation (
        records,
        NULL,
        "json:/loops/0/path/0"
    );

    g_assert_nonnull (module);
    g_assert_cmpstr (module->entity_type, ==, "module");
    g_assert_cmpstr (module->label, ==, "Population");
    g_assert_cmpstr (module->locator, ==, "json:/modules/0");

    g_assert_nonnull (variable);
    g_assert_cmpstr (variable->entity_type, ==, "variable");
    g_assert_cmpstr (
        variable->locator,
        ==,
        "json:/modules/0/variables/0"
    );

    g_assert_nonnull (loop);
    g_assert_cmpstr (loop->entity_type, ==, "loop");
    g_assert_cmpstr (
        loop->label,
        ==,
        "Government refinancing loop"
    );

    g_assert_nonnull (link);
    g_assert_cmpstr (link->relation_type, ==, "CAUSAL");
    g_assert_cmpstr (
        link->from_native_id,
        ==,
        "VAR.EXPOSURE.COUNT"
    );
    g_assert_cmpstr (
        link->to_native_id,
        ==,
        "VAR.FAMILIARITY.CLAIM"
    );
    g_assert_true (
        strstr (
            link->payload_json,
            "LINK.EXPOSURE.FAMILIARITY"
        ) != NULL
    );

    g_assert_nonnull (edge);
    g_assert_null (edge->native_id);
    g_assert_cmpstr (edge->relation_type, ==, "path_edge");
    g_assert_cmpstr (
        edge->from_native_id,
        ==,
        "government_financing_need"
    );
    g_assert_cmpstr (
        edge->to_native_id,
        ==,
        "government_debt_issuance"
    );

    atm_structured_json_records_free (records);
    remove_json_file (path);
}

static void
test_root_array_infers_type_from_source_path (void)
{
    const char *contents =
        "[{"
        "\"id\":\"VAR.BELIEF.CLAIM\","
        "\"short_name\":\"B\","
        "\"label\":{\"en\":\"Belief\"}"
        "}]";
    char *path = new_json_file (contents);
    AtmStructuredJsonRecords *records = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_structured_json_extract (
            path,
            "model/variables.json",
            &records,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (records->entities->len, ==, 1);
    g_assert_cmpuint (records->relations->len, ==, 0);

    AtmStructuredEntity *entity = find_entity (
        records,
        "VAR.BELIEF.CLAIM"
    );

    g_assert_nonnull (entity);
    g_assert_cmpstr (entity->entity_type, ==, "variable");
    g_assert_cmpstr (entity->label, ==, "Belief");
    g_assert_cmpstr (entity->locator, ==, "json:/0");

    atm_structured_json_records_free (records);
    remove_json_file (path);
}

static void
test_json_pointer_member_escaping (void)
{
    const char *contents =
        "{"
        "\"a/b~c\":[{"
            "\"id\":\"ESCAPED.ID\""
        "}]"
        "}";
    char *path = new_json_file (contents);
    AtmStructuredJsonRecords *records = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_structured_json_extract (
            path,
            "model/custom.json",
            &records,
            &error
        )
    );
    g_assert_no_error (error);
    g_assert_cmpuint (records->entities->len, ==, 1);

    AtmStructuredEntity *entity = find_entity (
        records,
        "ESCAPED.ID"
    );

    g_assert_nonnull (entity);
    g_assert_cmpstr (
        entity->locator,
        ==,
        "json:/a~1b~0c/0"
    );

    atm_structured_json_records_free (records);
    remove_json_file (path);
}

static void
test_invalid_json_is_rejected (void)
{
    char *path = new_json_file ("{\"id\":");
    AtmStructuredJsonRecords *records = NULL;
    GError *error = NULL;

    g_assert_false (
        atm_structured_json_extract (
            path,
            "model/bad.json",
            &records,
            &error
        )
    );
    g_assert_error (
        error,
        ATM_STRUCTURED_JSON_ERROR,
        ATM_STRUCTURED_JSON_ERROR_PARSE
    );
    g_assert_null (records);

    g_clear_error (&error);
    remove_json_file (path);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/structured-json/suite-patterns",
        test_suite_structural_patterns
    );
    g_test_add_func (
        "/structured-json/root-array-type",
        test_root_array_infers_type_from_source_path
    );
    g_test_add_func (
        "/structured-json/pointer-escaping",
        test_json_pointer_member_escaping
    );
    g_test_add_func (
        "/structured-json/invalid-json",
        test_invalid_json_is_rejected
    );

    return g_test_run ();
}
