# Presentation performance evidence v1

This directory freezes the first reviewed performance measurements for the AtM Presentation core.

The measurement covers the complete non-UI projection path: MD4C parsing, semantic document construction, conversion to visible plain text, and cleanup. It does not measure GTK rendering, Ollama generation, retrieval, persistence, or end-to-end chat latency.

Three independent GitHub-hosted `ubuntu-24.04` attempts were retained. They ran on three different CPU models while keeping GCC and GLib versions constant. Across 1 KiB through 1 MiB, median latency scaled approximately linearly. The observed 1 MiB median range was 9.884–11.776 ms; the largest observed 1 MiB p95 was 11.810 ms.

These numbers are qualification evidence, not a universal benchmark claim. GitHub-hosted VM performance varies by CPU model and host load.

No production latency threshold and no production input-size budget are selected by this evidence. Any later limit must be justified separately against the actual AtM UI behavior, local-machine measurements, memory behavior, and the application's response-size contract.

The canonical values, artifact IDs, SHA-256 digests, tested commit, and environment are in `evidence.json`. The invariant-registry workflow validates that file.
