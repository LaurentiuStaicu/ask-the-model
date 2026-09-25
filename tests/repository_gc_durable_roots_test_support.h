#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean atm_c1_test_publish_empty_complete_generation (
    const char *control_path,
    GError **error
);

gint atm_c1_test_acquire_shared_generation_lease (
    const char *state_root,
    gint64 generation_id,
    GError **error
);

void atm_c1_test_release_generation_lease (
    gint lease_fd
);

G_END_DECLS
