#!/usr/bin/env python3
"""Freeze the application routing boundary; GTK tests prove projection parity."""
from pathlib import Path
from validate_pres07_sources import block

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise SystemExit(f"PRES-08 validation failed: {message}")


application = (ROOT / "src/Application.vala").read_text()
renderer = (ROOT / "src/PresentationRenderer.vala").read_text()
shared = block(application, "private void append_completed_answer (",
               "private string normalize_conversation_title (")
require(shared.count("state.presentation.append_completed_turn (") == 1,
        "completed answers must use one Presentation seam")
require("CitationResolution? resolution = null" in shared,
        "ordinary and grounded answers must share the seam")
send = block(application, "private async void send_prompt (",
             "private Gtk.Widget build_chat_tab_label (")
restore = block(application, "private void render_restored_snapshot (",
                "private ConversationPersistenceRepository[]")
require(send.count("append_completed_answer (") == 3,
        "live ordinary, grounded and clarification must share completion")
require(restore.count("append_completed_answer (") == 2,
        "restored ordinary and grounded messages must share completion")
require(restore.count("message.display_content") == 3,
        "restored user and assistant text must come from display_content")
for forbidden in ("begin_assistant_generation", "reveal_pending_assistant",
                  "scroll_to_mark", "commit_turn", "ConversationTurnCommitter"):
    require(forbidden not in restore, f"History acquired {forbidden}")
for forbidden in ("state.presentation.append_assistant (",
                  "state.presentation.append_sources (",
                  "state.presentation.append_turn_separator (",
                  "state.presentation.complete_assistant_generation ("):
    require(forbidden not in application, f"application bypasses seam: {forbidden}")
completed = block(renderer, "public void append_completed_turn (",
                  "public void append_assistant (")
require(completed.index("complete_assistant_generation (text)") <
        completed.index("append_sources (source_buttons)") <
        completed.index("append_turn_separator ()"),
        "completion order must remain body, Sources, separator")
require(completed.count("append_turn_separator ()") == 1,
        "each completed turn must have exactly one separator")
tests = (ROOT / "tests/presentation_renderer_test.vala").read_text()
require('"/presentation-renderer/completed-turn-parity"' in tests,
        "GTK live/History parity test is not registered")
print("PRES-08 validation passed: live and History share completed-turn rendering; "
      "History has no pending/scroll/persistence side effects")
