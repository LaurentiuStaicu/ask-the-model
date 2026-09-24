#pragma once

#ifdef ATM_TEST_FAULT_INJECTION

void atm_test_fault_checkpoint (
    const char *checkpoint_id
);

#else

#define atm_test_fault_checkpoint(checkpoint_id) ((void) 0)

#endif
