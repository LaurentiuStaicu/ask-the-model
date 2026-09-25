#include "presentation_normalize.h"

#include <glib.h>
#include <string.h>

typedef struct {
    guint32 state;
} DeterministicRng;

static guint32
next_u32 (DeterministicRng *rng)
{
    guint32 x = rng->state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    rng->state = x;
    return x;
}

static char *
safe_fallback_text (
    const guint8 *input,
    gsize length
)
{
    GString *without_nul = g_string_sized_new (
        length
    );

    for (gsize i = 0; i < length; i++) {
        if (input[i] == 0) {
            g_string_append (
                without_nul,
                "\xEF\xBF\xBD"
            );
        } else {
            g_string_append_c (
                without_nul,
                (char) input[i]
            );
        }
    }

    char *safe = g_utf8_make_valid (
        without_nul->str,
        (gssize) without_nul->len
    );
    g_string_free (without_nul, TRUE);
    return safe;
}

static void
assert_document_well_formed (
    const AtmPresentationDocument *document,
    gsize input_length
)
{
    g_assert_nonnull (document);
    g_assert_nonnull (document->blocks);

    g_assert_cmpuint (
        document->blocks->len,
        <=,
        input_length + 1
    );

    for (guint i = 0; i < document->blocks->len; i++) {
        AtmPresentationBlock *block =
            g_ptr_array_index (
                document->blocks,
                i
            );

        g_assert_nonnull (block);
        g_assert_nonnull (block->segments);

        if (block->info != NULL) {
            g_assert_true (
                g_utf8_validate (
                    block->info,
                    -1,
                    NULL
                )
            );
        }

        for (guint j = 0; j < block->segments->len; j++) {
            AtmPresentationSegment *segment =
                g_ptr_array_index (
                    block->segments,
                    j
                );

            g_assert_nonnull (segment);
            g_assert_nonnull (segment->text);
            g_assert_true (
                g_utf8_validate (
                    segment->text,
                    -1,
                    NULL
                )
            );
        }
    }
}

static void
normalize_and_assert (
    const guint8 *input,
    gsize length
)
{
    guint8 *before = g_malloc (
        MAX ((gsize) 1, length)
    );

    if (length > 0) {
        memcpy (before, input, length);
    }

    gboolean expected_invalid =
        !g_utf8_validate (
            (const char *) input,
            (gssize) length,
            NULL
        ) ||
        memchr (input, '\0', length) != NULL;

    AtmPresentationDocument *document = NULL;
    GError *error = NULL;

    g_assert_true (
        atm_presentation_normalize (
            (const char *) input,
            length,
            &document,
            &error
        )
    );
    g_assert_no_error (error);

    if (length > 0) {
        g_assert_cmpmem (
            before,
            length,
            input,
            length
        );
    }

    assert_document_well_formed (
        document,
        length
    );

    char *plain =
        atm_presentation_document_to_plain_text (
            document
        );

    g_assert_nonnull (plain);
    g_assert_true (
        g_utf8_validate (
            plain,
            -1,
            NULL
        )
    );

    if (expected_invalid) {
        g_assert_true (document->fallback);
        g_assert_cmpstr (
            document->fallback_reason,
            ==,
            "invalid-utf8"
        );

        char *expected = safe_fallback_text (
            input,
            length
        );

        g_assert_cmpstr (
            plain,
            ==,
            expected
        );

        g_free (expected);
    }

    g_free (plain);
    atm_presentation_document_free (document);
    g_free (before);
}

static void
test_arbitrary_bytes (void)
{
    DeterministicRng rng = {
        .state = 0xA7C45E31u
    };

    for (guint case_index = 0;
         case_index < 512;
         case_index++) {
        gsize length =
            next_u32 (&rng) % 4097u;
        guint8 *input = g_malloc (
            MAX ((gsize) 1, length)
        );

        for (gsize i = 0; i < length; i++) {
            input[i] =
                (guint8) (
                    next_u32 (&rng) & 0xFFu
                );
        }

        normalize_and_assert (
            input,
            length
        );
        g_free (input);
    }
}

static void
test_structured_ascii (void)
{
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        " *_#>-+.[]()!\\`~\n\t<>&;:/|=";

    DeterministicRng rng = {
        .state = 0x51D3C9A7u
    };

    for (guint case_index = 0;
         case_index < 512;
         case_index++) {
        gsize length =
            1 + (next_u32 (&rng) % 4096u);
        guint8 *input = g_malloc (length);

        for (gsize i = 0; i < length; i++) {
            guint32 value = next_u32 (&rng);
            input[i] = (guint8) alphabet[
                value % (sizeof (alphabet) - 1)
            ];

            if (i > 0 &&
                i % 97 == 0 &&
                (value & 3u) == 0) {
                input[i] = '\n';
            }
        }

        normalize_and_assert (
            input,
            length
        );
        g_free (input);
    }
}

static void
test_pathological_large_inputs (void)
{
    static const gsize sizes[] = {
        4096,
        16384,
        65536,
        262144
    };
    static const char pattern[] =
        "*_[]()~\\`<>#-&;:/|=";

    for (guint size_index = 0;
         size_index < G_N_ELEMENTS (sizes);
         size_index++) {
        gsize length = sizes[size_index];
        guint8 *input = g_malloc (length);

        for (gsize i = 0; i < length; i++) {
            input[i] = (guint8) pattern[
                i % (sizeof (pattern) - 1)
            ];
        }

        if (length >= 8) {
            memcpy (input, "prefix ", 7);
        }

        normalize_and_assert (
            input,
            length
        );
        g_free (input);
    }
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func (
        "/presentation-stress/arbitrary-bytes",
        test_arbitrary_bytes
    );
    g_test_add_func (
        "/presentation-stress/structured-ascii",
        test_structured_ascii
    );
    g_test_add_func (
        "/presentation-stress/pathological-large-inputs",
        test_pathological_large_inputs
    );

    return g_test_run ();
}
