#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"PRES-06 validation failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def require_marker(text: str, marker: str, context: str) -> int:
    index = text.find(marker)
    if index < 0:
        fail(f"{context} lost marker: {marker}")
    return index


def block(text: str, start_marker: str, end_marker: str) -> str:
    start = require_marker(text, start_marker, start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        fail(f"{start_marker} has no closing boundary {end_marker}")
    return text[start:end]


def main() -> int:
    renderer = (ROOT / "src" / "PresentationRenderer.vala").read_text(
        encoding="utf-8"
    )
    application = (ROOT / "src" / "Application.vala").read_text(
        encoding="utf-8"
    )
    tests = (
        ROOT / "tests" / "presentation_renderer_test.vala"
    ).read_text(encoding="utf-8")
    css = (ROOT / "data" / "style.css").read_text(encoding="utf-8")

    for marker in (
        "private Gtk.TextView view;",
        "private Gtk.TextMark? pending_assistant_origin = null;",
        "private Gtk.TextMark? pending_assistant_start = null;",
        "public void begin_assistant_generation ()",
        "public void complete_assistant_generation (",
        "public void cancel_assistant_generation ()",
        "public void append_turn_separator ()",
        '"atm-assistant-spinner"',
        '"atm-turn-separator"',
        "public unowned Gtk.TextChildAnchor\n        append_semantic_child (",
        "view.add_child_at_anchor (",
    ):
        require_marker(renderer, marker, "PresentationRenderer PRES-06")

    separator = block(
        renderer,
        "public void append_turn_separator ()",
        "public bool assistant_generation_pending ()",
    )
    require(
        "append_semantic_child (" in separator
        and '"atm-turn-separator"' in separator,
        "turn separator is not routed through the child-anchor widget seam",
    )

    semantic_child = block(
        renderer,
        "public unowned Gtk.TextChildAnchor\n        append_semantic_child (",
        "private void append_message_separator ()",
    )
    require(
        "create_child_anchor" in semantic_child
        and "add_child_at_anchor" in semantic_child,
        "shared Presentation child seam no longer creates/attaches TextChildAnchor",
    )
    require(
        "insert_raw" not in separator
        and "buffer.insert" not in separator,
        "turn separator writes transcript text",
    )

    begin = block(
        renderer,
        "public void begin_assistant_generation ()",
        "public Gtk.TextMark? assistant_generation_start_mark ()",
    )
    require(
        begin.find("pending_assistant_origin") <
        begin.find("append_message_separator ()"),
        "pending origin is not captured before temporary message spacing",
    )
    require(
        begin.find("pending_assistant_start") <
        begin.find('"Assistant:"'),
        "Assistant scroll mark is not captured before Assistant label",
    )

    require(
        "append_semantic_child (" in begin
        and '"atm-assistant-spinner"' in begin,
        "pending Assistant spinner is not routed through the child-anchor seam",
    )

    complete = block(
        renderer,
        "public void complete_assistant_generation (",
        "public void append_assistant (",
    )
    require(
        "clear_pending_assistant ();" in complete
        and "append_message_separator ();" in complete
        and "render_assistant (text);" in complete,
        "completion no longer replaces pending row with one final Assistant message",
    )

    require_marker(
        tests,
        '"/presentation-renderer/pending-assistant"',
        "renderer pending lifecycle test",
    )
    for marker in (
        "renderer.begin_assistant_generation ();",
        "renderer.complete_assistant_generation (",
        "renderer.cancel_assistant_generation ();",
        "renderer.append_turn_separator ();",
        "assert (buffer.text == completed);",
    ):
        require_marker(tests, marker, "renderer PRES-06 test")

    require_marker(
        application,
        "public Gtk.ScrolledWindow transcript_scroll;",
        "ChatTabState transcript scroller",
    )
    require_marker(
        application,
        "public bool follow_next_assistant = false;",
        "ChatTabState follow snapshot",
    )

    click = block(
        application,
        "send_button.clicked.connect (() => {",
        "close_button.clicked.connect (() => {",
    )
    snapshot_index = require_marker(
        click,
        "state.follow_next_assistant =",
        "Send click scroll snapshot",
    )
    user_index = require_marker(
        click,
        "state.presentation.append_user (",
        "Send click user append",
    )
    require(
        snapshot_index < user_index,
        "near-bottom snapshot is taken after user text mutates the transcript",
    )

    send = block(
        application,
        "private async void send_prompt (",
        "private Gtk.Widget build_chat_tab_label (",
    )
    begin_index = require_marker(
        send,
        "state.presentation.begin_assistant_generation ();",
        "live pending Assistant start",
    )
    first_yield = send.find("yield ")
    require(first_yield >= 0, "send_prompt has no async boundary")
    require(
        begin_index < first_yield,
        "pending Assistant is not visible before the first async boundary",
    )
    require_marker(
        send,
        "reveal_pending_assistant (state);",
        "live Assistant reveal",
    )
    require_marker(
        send,
        "state.presentation.cancel_assistant_generation ();",
        "error cleanup",
    )
    require(
        send.count("state.presentation.append_turn_separator ();") == 3,
        "live flow must append exactly three completion-path separators",
    )

    reveal = block(
        application,
        "private void reveal_pending_assistant (",
        "private async void send_prompt (",
    )
    require(
        reveal.count("scroll_to_mark") == 1,
        "Assistant reveal must perform exactly one scroll call",
    )
    require(
        "state.follow_next_assistant = false;" in reveal,
        "follow snapshot is not consumed before scrolling",
    )
    require(
        application.count("scroll_to_mark") == 1,
        "Application acquired another transcript auto-scroll path",
    )
    require(
        "streaming_transcript = state.transcript" not in application,
        "visible provider token streaming was re-enabled",
    )

    grounded = block(
        application,
        "private void append_grounded_answer (",
        "private string normalize_conversation_title (",
    )
    require(
        "complete_assistant_generation" in grounded,
        "grounded live answer no longer uses pending-aware completion",
    )
    require(
        "append_turn_separator" not in grounded,
        "shared grounded renderer would add PRES-06 separator during History restore",
    )

    for marker in (
        "spinner.atm-assistant-spinner",
        "label.atm-turn-separator",
        "atm-dark textview spinner.atm-assistant-spinner",
        "atm-dark textview label.atm-turn-separator",
    ):
        require_marker(css, marker, "PRES-06 light/dark CSS")

    print(
        "PRES-06 validation passed: pending Assistant is non-streaming and "
        "replaceable, near-bottom is snapshotted before user insertion, "
        "scroll occurs once to the Assistant start, and the turn separator "
        "is a non-text child widget kept out of History until PRES-08"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
