#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean atm_c1_test_publish_empty_complete_generation (
    const char *control_path,
    GError **error
);

gboolean atm_c1_test_make_symlink (
    const char *target,
    const char *link_path,
    GError **error
);

G_END_DECLS
