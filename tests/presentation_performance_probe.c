#include "presentation_normalize.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    gsize bytes;
    guint iterations;
} PerfCase;

static int
compare_i64 (const void *left, const void *right)
{
    const gint64 a = *(const gint64 *) left;
    const gint64 b = *(const gint64 *) right;

    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

static char *
make_input (gsize target_bytes)
{
    static const char pattern[] =
        "## Result\n\n"
        "- **Label:** deterministic presentation text with `inline_code`.\n"
        "- Second item keeps a literal * multiplication marker and [visible](https://example.test).\n\n"
        "> Quoted evidence stays inert and local.\n\n"
        "Paragraph with Romanian text: ș ț ă î â; Δx = 2π; <b>raw HTML remains inert</b>.\n\n";

    GString *input = g_string_sized_new (target_bytes);

    while (input->len < target_bytes)
        g_string_append (input, pattern);

    g_string_truncate (input, target_bytes);

    return g_string_free (input, FALSE);
}

static gsize
run_once (const char *input, gsize length)
{
    AtmPresentationDocument *document = NULL;
    GError *error = NULL;

    if (!atm_presentation_normalize (
            input,
            length,
            &document,
            &error)) {
        g_printerr (
            "normalization failed: %s\n",
            error != NULL ? error->message : "unknown error"
        );
        g_clear_error (&error);
        exit (2);
    }

    if (document == NULL || document->fallback) {
        g_printerr (
            "unexpected fallback for valid performance fixture\n"
        );
        atm_presentation_document_free (document);
        exit (3);
    }

    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    if (plain == NULL ||
        !g_utf8_validate (plain, -1, NULL)) {
        g_printerr (
            "invalid visible output in performance fixture\n"
        );
        g_free (plain);
        atm_presentation_document_free (document);
        exit (4);
    }

    gsize output_bytes = strlen (plain);
    g_free (plain);
    atm_presentation_document_free (document);
    return output_bytes;
}

static void
measure_case (const PerfCase *perf_case)
{
    char *input = make_input (perf_case->bytes);

    for (guint i = 0; i < 8; i++)
        (void) run_once (
            input,
            perf_case->bytes
        );

    gint64 *samples = g_new (
        gint64,
        perf_case->iterations
    );
    gsize output_bytes = 0;

    for (guint i = 0;
         i < perf_case->iterations;
         i++) {
        gint64 started = g_get_monotonic_time ();
        output_bytes = run_once (
            input,
            perf_case->bytes
        );
        gint64 ended = g_get_monotonic_time ();
        samples[i] = ended - started;
    }

    qsort (
        samples,
        perf_case->iterations,
        sizeof (gint64),
        compare_i64
    );

    guint median_index =
        perf_case->iterations / 2;
    guint p95_index =
        ((perf_case->iterations * 95 + 99) / 100) - 1;
    if (p95_index >= perf_case->iterations)
        p95_index = perf_case->iterations - 1;

    g_print (
        "ATM_PRESENTATION_PERF "
        "input_bytes=%" G_GSIZE_FORMAT " "
        "iterations=%u "
        "output_bytes=%" G_GSIZE_FORMAT " "
        "median_us=%" G_GINT64_FORMAT " "
        "p95_us=%" G_GINT64_FORMAT " "
        "max_us=%" G_GINT64_FORMAT "\n",
        perf_case->bytes,
        perf_case->iterations,
        output_bytes,
        samples[median_index],
        samples[p95_index],
        samples[perf_case->iterations - 1]
    );

    g_free (samples);
    g_free (input);
}

int
main (void)
{
    static const PerfCase cases[] = {
        { 1024, 500 },
        { 4096, 250 },
        { 16384, 100 },
        { 65536, 30 },
        { 262144, 10 },
        { 1048576, 3 }
    };

    g_print (
        "ATM_PRESENTATION_PERF_ENV "
        "glib=%u.%u.%u "
        "compiler=%s\n",
        GLIB_MAJOR_VERSION,
        GLIB_MINOR_VERSION,
        GLIB_MICRO_VERSION,
        __VERSION__
    );

    for (guint i = 0;
         i < G_N_ELEMENTS (cases);
         i++) {
        measure_case (&cases[i]);
    }

    return 0;
}
