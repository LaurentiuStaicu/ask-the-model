#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"gc durable-root collector validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def main() -> int:
    collector = read("src/RepositoryGcDurableRoots.vala")
    control_native = read("src/ControlStateNative.vala")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    meson = read("meson.build")

    required = (
        "RepositoryGcDurableRootCollector",
        "ConversationPersistenceStore conversation_store",
        "conversation_store.list_conversations ()",
        "conversation_store.load_snapshot (",
        "ControlStateNative.active_generation_id_readonly (",
        "ControlStateNative.\n                    load_repository_values_at_generation_readonly (",
        "RepositoryCatalog.all ()",
        "A durable conversation repository pin does not match its Control DB generation.",
        "A positive protected repository generation contains no catalog repository state.",
    )

    for marker in required:
        if marker not in collector:
            fail(f"collector contract lost: {marker}")

    forbidden = (
        "FileUtils.remove",
        "DirUtils.remove",
        "rename (",
        "renameat",
        "unlink",
        "RepositoryNative.quarantine",
        "isolate_snapshot",
        "purge",
        ".trash",
        "ControlStateNative.active_generation_id (",
        "load_repository_values_at_generation (",
    )

    for marker in forbidden:
        if marker in collector:
            fail(f"I1 must remain non-destructive: {marker}")

    if "ControlStateNative.active_generation_id_readonly (" not in collector:
        fail("collector lost strict read-only active-generation authority read")

    if "load_repository_values_at_generation_readonly (" not in collector:
        fail("collector lost strict read-only pinned-generation authority read")

    for marker in (
        'cname = "atm_control_state_active_generation_id_readonly"',
        'cname = "atm_control_state_load_repository_values_at_generation_readonly"',
    ):
        if marker not in control_native:
            fail(f"strict read-only Control DB binding lost: {marker}")

    if "RepositoryGcDurableRootCollector" in lifecycle:
        fail("I1 collector must remain dormant in RepositoryLifecycleService")

    if "'src/RepositoryGcDurableRoots.vala'" not in meson:
        fail("application build lost dormant GC root collector")

    if "repository_gc_durable_roots_test = executable(" not in meson:
        fail("GC durable-root test target is missing")

    if "'repository-gc-durable-roots'" not in meson:
        fail("GC durable-root test registration is missing")

    print(
        "gc durable-root collector validation passed: "
        "active + conversation roots mapped read-only; lifecycle unwired"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
