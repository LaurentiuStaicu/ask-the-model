#pragma once

/*
 * Production retrieval/grounding policy.
 *
 * Keep these values shared between the application path and diagnostic
 * benchmarks so production-policy measurements cannot silently drift from
 * the runtime configuration.
 */
#define ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY 6
#define ATM_PRODUCTION_GROUNDING_MAX_SOURCES 12
#define ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES (32 * 1024)
