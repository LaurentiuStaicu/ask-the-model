# Capacity qualification evidence v1

This directory freezes the evidence used by C0 disk/inode admission planning.

`evidence.json` is **qualification evidence, not production policy**. It
records the exact GitHub Actions artifacts and repository SHAs behind C0-M1,
C0-E1, C0-E2 and C0-E3, plus the reviewed C0-M2 tmpfs and C0-M3 ext4
state-headroom matrices, so later implementation work does not depend on
expiring CI artifacts or prose-only pull-request comments.

## Exact-SHA rule

Measured repository observations are keyed by:

`repository_id + repository_sha`

A result measured for one SHA must not be treated as a capacity guarantee for
a later repository SHA. A future production policy may use an exact matching
profile as evidence, but an unknown SHA is classified as unqualified for
proactive byte prediction and must fall back to the already-qualified
fail-closed ENOSPC behavior rather than silently reusing stale numbers.

This rule is especially important because the frozen corpus has materially
different shapes:

- CBD is small and balanced;
- EWD is retrieval-index dominant;
- RMD is snapshot/repair dominant.

The C0-M1 measurements therefore rejected the idea of one universal
`archive_size × multiplier` formula.

## What the observations mean

Allocated bytes are host/filesystem observations from the C0-M1 qualification
environment. They are not portable upper bounds for every Linux filesystem.

The runtime-independent C0 model remains layered:

1. C0-F2 inspects the completed archive without writing and gives the exact
   materialized-entry requirement for inode planning.
2. C0-F3 separates pre-download admission from post-download/pre-mutation
   admission so the completed archive is not double-counted.
3. C0-F1 groups data/cache/state by `st_dev`, sums only real phase overlap
   and evaluates bytes/inodes independently.
4. E1/E2/E3 establish that genuine disk/inode exhaustion remains fail-closed
   and restart-safe even when admission cannot predict a later external
   capacity loss.

## Reserve policy

No reserve is selected here.

M2 and M3 now provide a cross-history, cross-filesystem observation for the
guarded Control DB publication path:

- tmpfs: 32 KiB available failed while 64 KiB succeeded at 1, 100 and 1000
  completed generations;
- ext4: about 28–32 KiB actual available failed while 61,440 B actual
  available succeeded at 1 and 1000 completed generations.

The Control DB itself grew from roughly 40 KiB to more than 220 KiB across
these cases without moving the observed success/failure frontier. This is
evidence against a reserve proportional to total Control DB size, but it is
still not a portable upper bound or a selected runtime reserve.

A supplementary warm/cold sidecar qualification also shows why the reserve
must use the conservative **cold** SQLite baseline. With an already allocated
32 KiB `-shm` sidecar, 32 KiB additional free space succeeded across the
tested histories. With no `-shm` present, 32 KiB failed and 64 KiB
succeeded. AtM opens and closes the Control DB store per guarded mutation, so
it must not assume that sidecar allocation is already present when admitting
the operation.

The issue contract explicitly forbids freezing an arbitrary percentage. A
later production change must document its byte/inode reserve rationale,
including state-root SQLite headroom, and must keep the reserve policy
separable from storage formats and from these raw measurements.

## Reproducibility

The manifest stores action run IDs, artifact IDs, SHA-256 digests, AtM source
heads and pinned repository SHAs. The original artifacts remain useful for
full raw JSON while retained by GitHub Actions; this checked-in manifest
preserves the reviewed evidence identity after artifact expiry.
