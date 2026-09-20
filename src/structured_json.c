#include "structured_json.h"

#include <json-glib/json-glib.h>

#include <string.h>

GQuark
atm_structured_json_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-structured-json-error-quark"
    );
}

void
atm_structured_entity_free (AtmStructuredEntity *entity)
{
    if (entity == NULL) {
        return;
    }

    g_free (entity->native_id);
    g_free (entity->entity_type);
    g_free (entity->locator);
    g_free (entity->label);
    g_free (entity->payload_json);
    g_free (entity);
}

void
atm_structured_relation_free (AtmStructuredRelation *relation)
{
    if (relation == NULL) {
        return;
    }

    g_free (relation->native_id);
    g_free (relation->relation_type);
    g_free (relation->locator);
    g_free (relation->from_native_id);
    g_free (relation->to_native_id);
    g_free (relation->payload_json);
    g_free (relation);
}

void
atm_structured_json_records_free (AtmStructuredJsonRecords *records)
{
    if (records == NULL) {
        return;
    }

    g_clear_pointer (&records->entities, g_ptr_array_unref);
    g_clear_pointer (&records->relations, g_ptr_array_unref);
    g_free (records);
}

static const char *
string_member_or_null (
    JsonObject *object,
    const char *member
)
{
    JsonNode *node;

    if (!json_object_has_member (object, member)) {
        return NULL;
    }

    node = json_object_get_member (object, member);

    if (node == NULL ||
        json_node_get_value_type (node) != G_TYPE_STRING) {
        return NULL;
    }

    const char *value = json_node_get_string (node);

    return value != NULL && value[0] != '\0'
        ? value
        : NULL;
}

static char *
json_pointer_escape (const char *member)
{
    GString *escaped = g_string_new (NULL);

    for (const char *cursor = member;
         *cursor != '\0';
         cursor++) {
        if (*cursor == '~') {
            g_string_append (escaped, "~0");
        } else if (*cursor == '/') {
            g_string_append (escaped, "~1");
        } else {
            g_string_append_c (escaped, *cursor);
        }
    }

    return g_string_free (escaped, FALSE);
}

static char *
child_locator (
    const char *parent,
    const char *component
)
{
    char *escaped = json_pointer_escape (component);
    char *result;

    if (parent[0] == '\0') {
        result = g_strdup_printf ("json:/%s", escaped);
    } else {
        result = g_strdup_printf ("%s/%s", parent, escaped);
    }

    g_free (escaped);
    return result;
}

static char *
array_child_locator (
    const char *parent,
    guint index
)
{
    if (parent[0] == '\0') {
        return g_strdup_printf ("json:/%u", index);
    }

    return g_strdup_printf ("%s/%u", parent, index);
}

static const char *
known_context_type (const char *context)
{
    static const struct {
        const char *plural;
        const char *singular;
    } mappings[] = {
        { "variables", "variable" },
        { "modules", "module" },
        { "processes", "process" },
        { "links", "link" },
        { "loops", "loop" },
        { "stocks", "stock" },
        { "flows", "flow" },
        { "auxiliaries", "auxiliary" },
        { "equations", "equation" },
        { "datasets", "dataset" },
        { "inputs", "input" },
        { "outputs", "output" },
        { "gates", "gate" },
        { "delay_candidates", "delay_candidate" },
        { "candidate_interfaces", "candidate_interface" },
        { "promotion_gates", "promotion_gate" },
        { "relations", "relation" }
    };

    if (context == NULL) {
        return NULL;
    }

    for (gsize i = 0; i < G_N_ELEMENTS (mappings); i++) {
        if (g_strcmp0 (context, mappings[i].plural) == 0) {
            return mappings[i].singular;
        }
    }

    return context;
}

static char *
source_stem_type (const char *source_path)
{
    char *basename = g_path_get_basename (source_path);
    char *dot = strrchr (basename, '.');

    if (dot != NULL) {
        *dot = '\0';
    }

    const char *known = known_context_type (basename);
    char *result = g_strdup (known != NULL ? known : basename);

    g_free (basename);
    return result;
}

static char *
infer_entity_type (
    const char *source_path,
    const char *context
)
{
    if (g_strcmp0 (context, "loops") == 0) {
        char *basename = g_path_get_basename (source_path);
        gboolean is_feedback_registry =
            g_strcmp0 (
                basename,
                "feedback_registry.json"
            ) == 0;

        g_free (basename);

        if (is_feedback_registry) {
            return g_strdup ("feedback_loop");
        }
    }

    const char *known = known_context_type (context);

    if (known != NULL && context != NULL) {
        return g_strdup (known);
    }

    return source_stem_type (source_path);
}

static char *
extract_label (JsonObject *object)
{
    JsonNode *label_node;

    if (json_object_has_member (object, "label")) {
        label_node = json_object_get_member (object, "label");

        if (label_node != NULL &&
            json_node_get_value_type (label_node) == G_TYPE_STRING) {
            return g_strdup (json_node_get_string (label_node));
        }

        if (label_node != NULL &&
            json_node_get_node_type (label_node) == JSON_NODE_OBJECT) {
            JsonObject *label_object = json_node_get_object (label_node);
            const char *value = string_member_or_null (
                label_object,
                "en"
            );

            if (value == NULL) {
                value = string_member_or_null (
                    label_object,
                    "ro"
                );
            }

            if (value != NULL) {
                return g_strdup (value);
            }
        }
    }

    const char *members[] = {
        "name",
        "title",
        "short_name",
        "id"
    };

    for (gsize i = 0; i < G_N_ELEMENTS (members); i++) {
        const char *value = string_member_or_null (
            object,
            members[i]
        );

        if (value != NULL) {
            return g_strdup (value);
        }
    }

    return NULL;
}

static gboolean
append_entity (
    JsonNode *node,
    JsonObject *object,
    const char *source_path,
    const char *context,
    const char *locator,
    AtmStructuredJsonRecords *records,
    GError **error
)
{
    if (records->entities->len + records->relations->len >=
        ATM_STRUCTURED_JSON_MAX_RECORDS) {
        g_set_error (
            error,
            ATM_STRUCTURED_JSON_ERROR,
            ATM_STRUCTURED_JSON_ERROR_LIMIT,
            "Structured JSON exceeds the %u-record limit.",
            ATM_STRUCTURED_JSON_MAX_RECORDS
        );
        return FALSE;
    }

    const char *native_id = string_member_or_null (
        object,
        "id"
    );

    if (native_id == NULL) {
        return TRUE;
    }

    AtmStructuredEntity *entity = g_new0 (
        AtmStructuredEntity,
        1
    );
    entity->native_id = g_strdup (native_id);
    entity->entity_type = infer_entity_type (
        source_path,
        context
    );
    entity->locator = g_strdup (
        locator[0] != '\0' ? locator : "json:/"
    );
    entity->label = extract_label (object);
    entity->payload_json = json_to_string (node, FALSE);

    g_ptr_array_add (records->entities, entity);
    return TRUE;
}

static gboolean
append_relation (
    JsonNode *node,
    JsonObject *object,
    const char *source_path,
    const char *context,
    const char *locator,
    const char *from_id,
    const char *to_id,
    AtmStructuredJsonRecords *records,
    GError **error
)
{
    if (records->entities->len + records->relations->len >=
        ATM_STRUCTURED_JSON_MAX_RECORDS) {
        g_set_error (
            error,
            ATM_STRUCTURED_JSON_ERROR,
            ATM_STRUCTURED_JSON_ERROR_LIMIT,
            "Structured JSON exceeds the %u-record limit.",
            ATM_STRUCTURED_JSON_MAX_RECORDS
        );
        return FALSE;
    }

    AtmStructuredRelation *relation = g_new0 (
        AtmStructuredRelation,
        1
    );
    const char *native_id = string_member_or_null (
        object,
        "id"
    );
    const char *relation_type = string_member_or_null (
        object,
        "relation_type"
    );

    relation->native_id = g_strdup (native_id);

    if (relation_type != NULL) {
        relation->relation_type = g_strdup (relation_type);
    } else if (g_strcmp0 (context, "path") == 0) {
        relation->relation_type = g_strdup ("path_edge");
    } else {
        relation->relation_type = infer_entity_type (
            source_path,
            context
        );
    }

    relation->locator = g_strdup (
        locator[0] != '\0' ? locator : "json:/"
    );
    relation->from_native_id = g_strdup (from_id);
    relation->to_native_id = g_strdup (to_id);
    relation->payload_json = json_to_string (node, FALSE);

    g_ptr_array_add (records->relations, relation);
    return TRUE;
}

static gint
compare_strings (
    gconstpointer left,
    gconstpointer right
)
{
    const char *a = *(const char *const *) left;
    const char *b = *(const char *const *) right;

    return g_strcmp0 (a, b);
}

static gboolean
walk_node (
    JsonNode *node,
    const char *source_path,
    const char *context,
    const char *locator,
    guint depth,
    AtmStructuredJsonRecords *records,
    GError **error
)
{
    if (depth > ATM_STRUCTURED_JSON_MAX_DEPTH) {
        g_set_error (
            error,
            ATM_STRUCTURED_JSON_ERROR,
            ATM_STRUCTURED_JSON_ERROR_LIMIT,
            "Structured JSON exceeds the maximum nesting depth of %u.",
            ATM_STRUCTURED_JSON_MAX_DEPTH
        );
        return FALSE;
    }

    if (node == NULL) {
        return TRUE;
    }

    if (json_node_get_node_type (node) == JSON_NODE_OBJECT) {
        JsonObject *object = json_node_get_object (node);
        const char *from_id = string_member_or_null (
            object,
            "source"
        );
        const char *to_id = string_member_or_null (
            object,
            "target"
        );

        if (from_id == NULL || to_id == NULL) {
            from_id = string_member_or_null (
                object,
                "from"
            );
            to_id = string_member_or_null (
                object,
                "to"
            );
        }

        if (from_id != NULL && to_id != NULL) {
            if (!append_relation (
                    node,
                    object,
                    source_path,
                    context,
                    locator,
                    from_id,
                    to_id,
                    records,
                    error
                )) {
                return FALSE;
            }
        } else if (!append_entity (
                node,
                object,
                source_path,
                context,
                locator,
                records,
                error
            )) {
            return FALSE;
        }

        GList *members = json_object_get_members (object);
        GPtrArray *sorted = g_ptr_array_new_with_free_func (
            g_free
        );

        for (GList *item = members;
             item != NULL;
             item = item->next) {
            g_ptr_array_add (
                sorted,
                g_strdup ((const char *) item->data)
            );
        }

        g_list_free (members);
        g_ptr_array_sort (sorted, compare_strings);

        for (guint i = 0; i < sorted->len; i++) {
            const char *member = g_ptr_array_index (
                sorted,
                i
            );
            JsonNode *child = json_object_get_member (
                object,
                member
            );
            char *child_path = child_locator (
                locator,
                member
            );

            if (!walk_node (
                    child,
                    source_path,
                    member,
                    child_path,
                    depth + 1,
                    records,
                    error
                )) {
                g_free (child_path);
                g_ptr_array_unref (sorted);
                return FALSE;
            }

            g_free (child_path);
        }

        g_ptr_array_unref (sorted);
        return TRUE;
    }

    if (json_node_get_node_type (node) == JSON_NODE_ARRAY) {
        JsonArray *array = json_node_get_array (node);

        for (guint i = 0;
             i < json_array_get_length (array);
             i++) {
            JsonNode *child = json_array_get_element (
                array,
                i
            );
            char *child_path = array_child_locator (
                locator,
                i
            );

            if (!walk_node (
                    child,
                    source_path,
                    context,
                    child_path,
                    depth + 1,
                    records,
                    error
                )) {
                g_free (child_path);
                return FALSE;
            }

            g_free (child_path);
        }
    }

    return TRUE;
}

gboolean
atm_structured_json_extract (
    const char *path,
    const char *source_path,
    AtmStructuredJsonRecords **out_records,
    GError **error
)
{
    char *contents = NULL;
    gsize length = 0;
    JsonParser *parser = NULL;
    AtmStructuredJsonRecords *records = NULL;
    GError *local_error = NULL;
    gboolean ok = FALSE;

    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (source_path != NULL, FALSE);
    g_return_val_if_fail (out_records != NULL, FALSE);
    g_return_val_if_fail (*out_records == NULL, FALSE);

    if (!g_file_get_contents (
            path,
            &contents,
            &length,
            error
        )) {
        return FALSE;
    }

    if (length > ATM_STRUCTURED_JSON_MAX_BYTES) {
        g_set_error (
            error,
            ATM_STRUCTURED_JSON_ERROR,
            ATM_STRUCTURED_JSON_ERROR_TOO_LARGE,
            "JSON source exceeds the %u MiB parser limit.",
            (guint) (
                ATM_STRUCTURED_JSON_MAX_BYTES /
                1024 /
                1024
            )
        );
        goto out;
    }

    parser = json_parser_new ();

    if (!json_parser_load_from_data (
            parser,
            contents,
            length,
            &local_error
        )) {
        g_set_error (
            error,
            ATM_STRUCTURED_JSON_ERROR,
            ATM_STRUCTURED_JSON_ERROR_PARSE,
            "Could not parse structured JSON: %s",
            local_error != NULL
                ? local_error->message
                : "unknown JSON error"
        );
        g_clear_error (&local_error);
        goto out;
    }

    records = g_new0 (AtmStructuredJsonRecords, 1);
    records->entities = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_structured_entity_free
    );
    records->relations = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_structured_relation_free
    );

    if (!walk_node (
            json_parser_get_root (parser),
            source_path,
            NULL,
            "",
            0,
            records,
            error
        )) {
        goto out;
    }

    *out_records = g_steal_pointer (&records);
    ok = TRUE;

out:
    g_clear_pointer (
        &records,
        atm_structured_json_records_free
    );
    g_clear_object (&parser);
    g_free (contents);
    return ok;
}
