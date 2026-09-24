#pragma once

#include <glib.h>

G_BEGIN_DECLS

gboolean atm_repository_error_is_no_space (
    const GError *error
);

G_END_DECLS
