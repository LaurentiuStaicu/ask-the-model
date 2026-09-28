#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"PRES-07 validation failed: {message}")


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
    native_vapi = (ROOT / "src" / "PresentationNative.vapi").read_text(
        encoding="utf-8"
    )
    renderer_tests = (
        ROOT / "tests" / "presentation_renderer_test.vala"
    ).read_text(encoding="utf-8")
    native_tests = (
        ROOT / "tests" / "presentation_native_test.vala"
    ).read_text(encoding="utf-8")
    citation_tests = (
        ROOT / "tests" / "citation_labels_test.c"
    ).read_text(encoding="utf-8")
    css = (ROOT / "data" / "style.css").read_text(encoding="utf-8")

    require_marker(
        native_vapi,
        'cname = "atm_presentation_document_to_plain_text"',
        "Presentation Vala plain-text bridge",
    )
    require_marker(
        native_tests,
        "PresentationNative.to_plain_text (",
        "Presentation native bridge qualification",
    )

    for marker in (
        '"RMD claim [S2]. Repeat [S2]. "',
        '"EWD claim [S1]. Unknown [S9] [S9]."',
        'g_assert_cmpstr (first->label, ==, "[S2]");',
        'g_assert_cmpstr (second->label, ==, "[S1]");',
    ):
        require_marker(
            citation_tests,
            marker,
            "native citation first-use ordering qualification",
        )

    for marker in (
        "view.copy_clipboard.connect (() => {",
        "selected_semantic_text (",
        "iter.get_child_anchor ();",
        "anchor.get_widgets ()",
        '"atm-source-ref"',
        '"atm-assistant-spinner"',
        '"atm-turn-separator"',
        "view.get_clipboard ().set_text (",
        'GLib.Signal.stop_emission_by_name (',
        '"copy-clipboard"',
    ):
        require_marker(renderer, marker, "semantic clipboard")

    semantic = block(
        renderer,
        "internal string? selected_semantic_text (",
        "public void append_sources (",
    )
    require(
        "iter.get_child_anchor ()" in semantic,
        "clipboard no longer classifies TextChildAnchor objects",
    )
    require(
        "replace" not in semantic
        and "\\uFFFC" not in semantic,
        "clipboard regressed to object-replacement-character substitution",
    )

    sources = block(
        renderer,
        "public void append_sources (",
        "public unowned Gtk.TextChildAnchor",
    )
    require(
        "if (source_buttons.length == 0)" in sources
        and 'insert_raw ("\\nSources: ");' in sources,
        "zero/nonzero Sources-row contract drifted",
    )
    require(
        "for (" in sources
        and "source_buttons[i]" in sources
        and '"atm-source-ref"' in sources,
        "Sources row no longer preserves caller order through semantic anchors",
    )

    for marker in (
        '"/presentation-renderer/semantic-anchor-copy"',
        "Gtk.Button[] no_sources = {};",
        'label = "[1]"',
        'label = "[2]"',
        '"Assistant: Răspuns\\nSources: [1] [2]"',
        '!semantic.contains ("\\uFFFC")',
    ):
        require_marker(renderer_tests, marker, "PRES-07 renderer qualification")

    require(
        "markdown_excerpt_to_display_text" not in application,
        "ad-hoc Markdown excerpt regex projector returned",
    )
    excerpt = block(
        application,
        "private string source_excerpt_display_text (",
        "private Gtk.Widget build_source_detail_content (",
    )
    for marker in (
        'path.has_suffix (".md")',
        'path.has_suffix (".markdown")',
        "PresentationNative.normalize (",
        "PresentationNative",
        ".to_plain_text (",
        "return excerpt;",
    ):
        require_marker(excerpt, marker, "Source excerpt Presentation projection")
    require(
        "GLib.Regex" not in excerpt,
        "Source excerpt projection uses regex instead of Presentation",
    )

    button = block(
        application,
        "private Gtk.Button build_source_reference_button (",
        "private void append_completed_answer (",
    )
    for marker in (
        "new Gtk.Button ()",
        "focusable = true",
        'Gtk.AccessibleProperty.LABEL,',
        '"Source %u".printf (display_number)',
        "show_source_detail_window (",
        "citation,",
    ):
        require_marker(button, marker, "Source reference button")
    require(
        "Gtk.MenuButton" not in button,
        "Source reference regressed away from real Gtk.Button",
    )

    grounded = block(
        application,
        "private void append_completed_answer (",
        "private string normalize_conversation_title (",
    )
    zero = require_marker(
        grounded,
        "resolution != null && i < resolution.citation_count ()",
        "zero-citation path",
    )
    loop = require_marker(
        grounded,
        "resolution.citation_at (i)",
        "citation first-use order",
    )
    append = require_marker(
        grounded,
        "state.presentation.append_completed_turn (",
        "shared Sources projection",
    )
    require(
        zero < loop < append,
        "Sources ordering/zero-citation control flow drifted",
    )
    require_marker(
        grounded,
        "i + 1",
        "one-based source display numbering",
    )

    restore = block(
        application,
        "private void render_restored_snapshot (",
        "private ConversationPersistenceRepository[]",
    )
    require(
        "append_completed_answer (" in restore,
        "History no longer shares grounded Sources projection with live rendering",
    )

    details = block(
        application,
        "private Gtk.Widget build_source_detail_content (",
        "private void show_source_detail_window (",
    )
    for marker in (
        "citation.repository_version",
        "citation.snapshot_sha",
        "citation.source_path",
        "citation.locator",
        "citation.logical_source_id",
        "source_excerpt_display_text (citation)",
        "citation.immutable_permalink",
        "descriptor.immutable_file_permalink (",
        "Gtk.LinkButton.with_label (",
        '"Open immutable source"',
    ):
        require_marker(details, marker, "Source detail provenance")
    require(
        details.count("Gtk.LinkButton.with_label (") == 1,
        "Source detail acquired another active URL surface",
    )

    persistence = block(
        application,
        "private ConversationPersistenceCitation[]",
        "private async void update_conversation_title (",
    )
    for marker in (
        "citation.repository_id",
        "citation.repository_version",
        "citation.snapshot_sha",
        "citation.logical_source_id",
        "citation.source_path",
        "citation.locator",
    ):
        require_marker(persistence, marker, "persisted citation provenance")
    require(
        "PresentationRenderer" not in persistence
        and "append_sources" not in persistence,
        "Presentation leaked into citation persistence authority",
    )

    require_marker(
        css,
        "textview button.atm-source-ref",
        "actual Gtk.Button source CSS",
    )
    require(
        "menubutton.atm-source-ref" not in css,
        "stale GtkMenuButton source CSS remains",
    )

    print(
        "PRES-07 validation passed: Sources preserve CitationResolution order, "
        "live/History share one projection, semantic copy classifies child "
        "anchors without U+FFFC substitution, Source excerpts use Presentation, "
        "and citation persistence/provenance remains outside display code"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
