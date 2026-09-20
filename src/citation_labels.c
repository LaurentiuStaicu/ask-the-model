#include "citation_labels.h"

#include <string.h>

GQuark
atm_citation_resolve_error_quark (void)
{
    return g_quark_from_static_string (
        "atm-citation-resolve-error-quark"
    );
}

void
atm_citation_reference_free (
    AtmCitationReference *citation
)
{
    if (citation == NULL) {
        return;
    }

    g_free (citation->label);
    g_free (citation->evidence_kind);
    g_free (citation->repository_id);
    g_free (citation->repository_version);
    g_free (citation->snapshot_sha);
    g_free (citation->logical_source_id);
    g_free (citation->source_path);
    g_free (citation->locator);
    g_free (citation->title);
    g_free (citation->excerpt);
    g_free (citation);
}

void
atm_citation_resolution_free (
    AtmCitationResolution *resolution
)
{
    if (resolution == NULL) {
        return;
    }

    g_clear_pointer (&resolution->citations, g_ptr_array_unref);
    g_clear_pointer (&resolution->unknown_labels, g_ptr_array_unref);
    g_free (resolution);
}

static gboolean
source_has_required_provenance (
    const AtmGroundingSource *source
)
{
    return source != NULL &&
        source->label != NULL &&
        source->repository_id != NULL &&
        source->repository_version != NULL &&
        source->snapshot_sha != NULL &&
        source->logical_source_id != NULL &&
        source->source_path != NULL &&
        source->locator != NULL &&
        source->evidence_kind != NULL;
}

static AtmCitationReference *
citation_from_source (
    const AtmGroundingSource *source
)
{
    AtmCitationReference *citation = g_new0 (
        AtmCitationReference,
        1
    );

    citation->label = g_strdup (source->label);
    citation->evidence_kind = g_strdup (
        source->evidence_kind
    );
    citation->evidence_id = source->evidence_id;
    citation->repository_id = g_strdup (
        source->repository_id
    );
    citation->repository_version = g_strdup (
        source->repository_version
    );
    citation->snapshot_sha = g_strdup (
        source->snapshot_sha
    );
    citation->logical_source_id = g_strdup (
        source->logical_source_id
    );
    citation->source_path = g_strdup (
        source->source_path
    );
    citation->locator = g_strdup (
        source->locator
    );
    citation->title = g_strdup (
        source->title
    );
    citation->excerpt = g_strdup (
        source->excerpt
    );
    citation->source_roles = source->source_roles;
    citation->match_kind = source->match_kind;

    return citation;
}

static const AtmGroundingSource *
find_source_by_label (
    const AtmGroundingContext *context,
    const char *label
)
{
    for (guint i = 0; i < context->sources->len; i++) {
        const AtmGroundingSource *source = g_ptr_array_index (
            context->sources,
            i
        );

        if (g_strcmp0 (source->label, label) == 0) {
            return source;
        }
    }

    return NULL;
}

static gboolean
scan_label (
    const char *cursor,
    gsize remaining,
    gsize *out_length,
    guint *out_number
)
{
    if (remaining < 4 ||
        cursor[0] != '[' ||
        cursor[1] != 'S' ||
        !g_ascii_isdigit (cursor[2])) {
        return FALSE;
    }

    guint64 number = 0;
    gsize index = 2;
    guint digits = 0;

    while (index < remaining &&
           g_ascii_isdigit (cursor[index])) {
        number =
            (number * 10) +
            (guint64) (cursor[index] - '0');
        digits++;
        index++;

        if (digits > 4 ||
            number > ATM_CITATION_MAX_LABEL_NUMBER) {
            return FALSE;
        }
    }

    if (digits == 0 ||
        number == 0 ||
        index >= remaining ||
        cursor[index] != ']') {
        return FALSE;
    }

    *out_length = index + 1;
    *out_number = (guint) number;
    return TRUE;
}

gboolean
atm_citation_resolve_labels (
    const char *model_output,
    const AtmGroundingContext *context,
    AtmCitationResolution **out_resolution,
    GError **error
)
{
    AtmCitationResolution *resolution = NULL;
    GHashTable *seen_known = NULL;
    GHashTable *seen_unknown = NULL;
    gsize output_length;

    g_return_val_if_fail (model_output != NULL, FALSE);
    g_return_val_if_fail (context != NULL, FALSE);
    g_return_val_if_fail (out_resolution != NULL, FALSE);
    g_return_val_if_fail (*out_resolution == NULL, FALSE);

    output_length = strlen (model_output);

    if (!g_utf8_validate (
            model_output,
            output_length,
            NULL
        ) ||
        context->sources == NULL) {
        g_set_error_literal (
            error,
            ATM_CITATION_RESOLVE_ERROR,
            ATM_CITATION_RESOLVE_ERROR_ARGUMENT,
            "Citation resolution input is invalid."
        );
        return FALSE;
    }

    for (guint i = 0; i < context->sources->len; i++) {
        const AtmGroundingSource *source = g_ptr_array_index (
            context->sources,
            i
        );

        if (!source_has_required_provenance (source)) {
            g_set_error_literal (
                error,
                ATM_CITATION_RESOLVE_ERROR,
                ATM_CITATION_RESOLVE_ERROR_PROVENANCE,
                "Grounding source provenance is incomplete."
            );
            return FALSE;
        }
    }

    resolution = g_new0 (AtmCitationResolution, 1);
    resolution->citations = g_ptr_array_new_with_free_func (
        (GDestroyNotify) atm_citation_reference_free
    );
    resolution->unknown_labels = g_ptr_array_new_with_free_func (
        g_free
    );
    seen_known = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );
    seen_unknown = g_hash_table_new_full (
        g_str_hash,
        g_str_equal,
        g_free,
        NULL
    );

    const char *cursor = model_output;
    const char *end = model_output + output_length;

    while (cursor < end) {
        gsize label_length = 0;
        guint label_number = 0;

        if (scan_label (
                cursor,
                (gsize) (end - cursor),
                &label_length,
                &label_number
            )) {
            char *label = g_strndup (
                cursor,
                label_length
            );
            const AtmGroundingSource *source =
                find_source_by_label (
                    context,
                    label
                );

            if (source != NULL) {
                if (!g_hash_table_contains (
                        seen_known,
                        label
                    )) {
                    g_hash_table_add (
                        seen_known,
                        g_strdup (label)
                    );
                    g_ptr_array_add (
                        resolution->citations,
                        citation_from_source (source)
                    );
                }
            } else if (!g_hash_table_contains (
                           seen_unknown,
                           label
                       )) {
                g_hash_table_add (
                    seen_unknown,
                    g_strdup (label)
                );
                g_ptr_array_add (
                    resolution->unknown_labels,
                    g_strdup (label)
                );
            }

            g_free (label);
            cursor += label_length;
            continue;
        }

        cursor = g_utf8_next_char (cursor);
    }

    g_hash_table_unref (seen_unknown);
    g_hash_table_unref (seen_known);

    *out_resolution = g_steal_pointer (&resolution);
    return TRUE;
}
