#pragma once

#include <glib.h>

void atm_m12_set_fault_checkpoint (const char *checkpoint_id);
void atm_m12_clear_fault (void);
gboolean atm_m12_fault_triggered (void);
int atm_m12_test_fsync (int fd);
gboolean atm_m12_write_ewd_archive (const char *archive_path);
