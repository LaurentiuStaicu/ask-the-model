# E4 conversation-scoped SQLite query-session reuse

## Status

E4 is a diagnostic experiment stacked on E3. It must not change the default
production retrieval behavior or the frozen R5 quality policy.

## Question

E2 established that opening and validating an SQLite query session has a
substantial cost for EWD. E3 showed that conversation retrieval, rather than
grounding-context assembly, dominates the end-to-end latency for EWD topics.

The production router already uses one read-only query session per repository
per retrieval turn. Within that turn, exact-ID, dataset-row and FTS queries
share the same connection. E4 therefore does **not** test adding sessions to
the router. It tests whether a validated session can safely be reused across
multiple retrieval turns belonging to one frozen conversation.

## Baseline and candidate

**Baseline (turn-local):**

- Each retrieval turn opens a read-only query session for each selected
  repository.
- Session open configures SQLite, begins a read transaction, loads snapshot
  provenance and validates canonical FTS consistency.
- All exact, tabular and FTS queries for that repository in that turn use the
  same connection.
- The connection is rolled back and closed when repository retrieval ends.

**Candidate (conversation-local):**

- Each retrieval-conversation state owns a private query-session cache.
- A session is opened lazily on the first retrieval for an index in that
  conversation.
- Later turns for the same frozen repository/index reuse the existing
  validated read-only session.
- The session is closed when the conversation state is destroyed.
- Different conversation states never share connections.
- Candidate mode is explicitly opt-in in the benchmark. The application
  continues to construct the baseline state unless a separately reviewed
  production change enables the candidate.

The cache is bounded by the repository scope frozen for the conversation.
AtM currently supports EWD, CBD and RMD, so no conversation can accumulate
more than three query connections.

## Source-level integration

The current call path is:

1. `ConversationSession` owns a `ConversationGrounding` for the active chat.
2. `ConversationGrounding` owns `AtmConversationGroundingState`.
3. The C grounding state owns one `AtmRetrievalConversationState`.
4. `AtmRetrievalConversationState` calls `atm_retrieval_run()` for each
   retrieval turn.
5. `atm_retrieval_run()` opens one `AtmRetrievalQuerySession` per selected
   repository and closes it after that turn's exact/tabular/FTS routes.

E4's candidate cache belongs to step 3's retrieval-conversation state. It is
not global, app-wide, shared between conversations, or keyed only by a mutable
repository name. Its lifetime is limited to a state whose repository scope and
index paths were frozen at construction.

## Correctness and lifecycle invariants

- The frozen repository order, repository version, snapshot SHA, index path,
  query scope, evidence ranking, provenance checks and grounding policy remain
  unchanged.
- First open retains the existing read-only setup, transaction start,
  provenance load and FTS-consistency validation. E4 does not skip or weaken
  validation.
- The read transaction remains tied to the pinned immutable index for the
  conversation. A cached handle may not be rebound to a different index path
  or repository snapshot.
- A query failure must not leave a suspect cached connection available for a
  later turn. The failing connection is invalidated/closed before returning the
  error; a subsequent turn may attempt a fresh validated open.
- Aborting a turn does not commit conversational retrieval state. It also does
  not transfer or share the query-session cache with another conversation.
- All cached connections close when the owning retrieval-conversation state
  is freed.
- In the GTK application, the repository-generation shared lease must remain
  held until native retrieval state and all cached SQLite sessions have been
  destroyed. The current `ConversationGrounding` destructor releases its
  Vala lease before freeing the C state. If E4 introduces retained query
  sessions into that state, disposal order must be reversed: free the C state
  first, then release the generation lease.
- Concurrent turns on the same conversation state remain unsupported unless
  explicitly serialized by the existing conversation-turn guard. E4 must not
  introduce unsynchronized concurrent use of one SQLite connection.

## Benchmark design

- Use the exact frozen R5 EWD/CBD/RMD corpus and all 33 topic identities.
- Run baseline and candidate against the same pinned index files on each
  independent runner. Keep index preparation separate from the retrieval
  comparison; E1 already measures index lifecycle.
- Balance execution order across ten independent runners so that five run
  baseline first and five run candidate first.
- Create fresh conversation states for each mode. Preserve the original order
  and adjacency of the four two-turn R5 conversations.
- Keep the per-conversation cache isolated from other conversation IDs and
  from the other benchmark mode.
- Record index identity, SQLite version, runner identity, baseline/candidate
  session opens, reuse hits, invalidations, and per-topic pipeline/retrieval
  timings.
- Compare semantic output for every topic: outcome, ordered retrieval results,
  ordered grounding-context sources and evidence byte count.
- Validate all 33 topics and ensure all 10 runner pairs use identical corpus
  pins and SQLite versions.

The frozen R5 fixture has 33 topics, including four two-turn conversations
(eight conversation-tagged topics). E4 reports one-turn and multi-turn
results separately. It must not claim a universal speedup from one-turn
topics, because those do not amortize a session open across turns.

## Reported metrics

- Per-repository query-session opens per conversation and per retrieval turn.
- Cache reuse hits and invalidations.
- Session open + validation time, by repository.
- Per-topic conversation retrieval and end-to-end pipeline median/p95.
- Paired baseline-versus-candidate latency deltas for the four multi-turn
  conversations, pooled only where repository and query semantics are
  comparable.
- Connection count and conversation-state lifetime at close.
- Cache teardown duration measured after all SQLite session destructors have run.
- Paired total pipeline + teardown time, so candidate close cost is not omitted
  merely because conversation-owned sessions close after topic execution.
- Semantic SHA-256 equality for baseline/candidate and across all runners.

Pipeline wall time retains the E3 boundary: it includes retrieval, grounding
context assembly and benchmark topic result-record construction. It is not a
pure UI or model-inference latency.

## Acceptance gates

E4 is valid only when:

1. All 10 independent runner pairs complete.
2. All 33 topics are present and ordered exactly as in R5.
3. Baseline and candidate semantic payloads are identical for every topic on
   every runner.
4. The cross-runner semantic digest is identical.
5. All runners use the same pinned corpus and SQLite version.
6. Cache open/reuse/invalidation counts match the expected conversation
   lifecycle; no connection survives its owning state.
7. Query failures invalidate the affected cached connection.
8. A native lock-probe test demonstrates that the cached read transaction
   prevents a competing exclusive writer while the cache is alive and that
   the writer succeeds immediately after cache destruction. The application
   destructor must free the native grounding state (and therefore its SQLite
   sessions) before releasing the repository-generation lease. The current
   test suite proves native handle closure and the source-level Vala owner
   order; it does not instrument the lease-release callback itself.
9. The existing R5 evaluator and all quality gates remain unchanged.

A latency improvement is **not** an acceptance gate by itself. If the speed
benefit is small, highly variable, or outweighed by connection-lifetime,
checkpointing, resource or lifecycle costs, the correct result is to retain
turn-local sessions.

## Interpretation boundary

E4 measures a bounded conversation-lifetime optimization. It does not justify
a global connection pool, cross-conversation reuse, skipping FTS validation,
a different SQLite durability mode, or any production policy change. Results
must be interpreted together with E2's session-open cost and E3's end-to-end
latency distribution.
