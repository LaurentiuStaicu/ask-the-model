#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(
        f"grounded turn commit order validation failed: {message}"
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def marker(text: str, needle: str, context: str) -> int:
    pos = text.find(needle)
    if pos < 0:
        fail(f"{context} lost marker: {needle}")
    return pos


def main() -> int:
    application = (
        ROOT / "src" / "Application.vala"
    ).read_text(encoding="utf-8")

    send_start = marker(
        application,
        "private async void send_prompt (",
        "Application Send operation",
    )
    send_end = application.find(
        "\n        private ",
        send_start + 1,
    )
    require(send_end >= 0, "send_prompt method boundary missing")
    send = application[send_start:send_end]

    grounded_start = marker(
        send,
        "if (has_grounding) {",
        "grounded branch",
    )
    grounded_end = send.find(
        "                    } else {",
        grounded_start,
    )
    require(grounded_end >= 0, "grounded branch boundary missing")
    grounded = send[grounded_start:grounded_end]

    for declaration in (
        "bool grounded_turn_prepared = false;",
        "bool turn_durably_committed = false;",
    ):
        marker(send, declaration, "send_prompt durable state")

    marker(
        send,
        "state.session.prepare_turn (",
        "grounded preparation",
    )

    preparation_gate = marker(
        send,
        "state.session.prepare_turn (",
        "grounded preparation",
    )

    # Application performs one catalog-selected native qualification before
    # grounded preparation. The native factory, not a duplicate Vala switch,
    # owns the closed source-adapter dispatch.
    qualification = marker(
        send,
        "qualify_live_chart_request (",
        "catalog-selected native source qualification",
    )
    require(
        qualification < preparation_gate,
        "catalog-selected source qualification must finish before grounded preparation and any model call",
    )

    native_chart = (
        ROOT / "src" / "chart_history_reconstruct.c"
    ).read_text(encoding="utf-8")
    factory_start = marker(
        native_chart,
        "atm_chart_live_reconstruct_v4 (",
        "native catalog-selected ChartSpec v4 factory",
    )
    factory_end = native_chart.find(
        "\ngboolean\natm_chart_wrap_qualified_spec_v4 (",
        factory_start + 1,
    )
    require(factory_end >= 0, "native v4 factory boundary missing")
    factory = native_chart[factory_start:factory_end]

    closed_adapters = (
        (
            "ATM_CHART_CATALOG_ADAPTER_GISTEMP_V1",
            "atm_chart_live_reconstruct_gistemp (",
        ),
        (
            "ATM_CHART_CATALOG_ADAPTER_ENERGY_INSTITUTE_V2",
            "atm_chart_live_reconstruct_energy_institute (",
        ),
        (
            "ATM_CHART_CATALOG_ADAPTER_BNR_BLS_C01_V3",
            "atm_chart_live_reconstruct_bnr_bls_c01 (",
        ),
    )
    for selector, adapter_call in closed_adapters:
        selector_pos = marker(factory, selector, "native closed adapter dispatch")
        call_pos = marker(factory, adapter_call, "native qualified source adapter")
        spec_pos = marker(factory, "atm_chart_spec_v4_new (", "v4 source qualification")
        require(
            selector_pos < call_pos < spec_pos,
            f"{selector} must select its qualified source adapter before constructing v4",
        )

    scope_gate = marker(
        send,
        "bool selected_repository_in_scope = false;",
        "catalog-bound repository-scope gate",
    )
    scope_match = marker(
        send,
        "ChartNative.catalog_repository_matches_series (",
        "catalog-bound repository-scope match",
    )
    scope_refusal = marker(
        send,
        "The repository bound to this qualified chart series is not included",
        "catalog-bound repository-scope refusal",
    )
    require(
        scope_gate < scope_match < scope_refusal < preparation_gate,
        "catalog repository authorization must fail closed before grounded preparation or any model call",
    )

    required = [
        "yield ollama_provider.chat_grounded (",
        "state.session.resolve_turn_citations (",
        "ensure_persistent_conversation (",
        "bool persistence_ready = false;",
        "if (!persistence_ready) {",
        "if (cancellable.is_cancelled ()) {",
        "state.session.require_model (",
        "state.session.repository_generation_id ()",
        "repository_lifecycle.current_repository_generation_id ()",
        "persistence_citations (",
        "ConversationTurnCommitter.commit (",
        "if (committed_turn_no < 0) {",
        "turn_durably_committed = true;",
        "if (!state.session.commit_turn ()) {",
        "grounded_turn_prepared = false;",
        "build_live_chart_widgets (",
        "append_completed_answer (",
    ]
    positions = {
        item: marker(grounded, item, "grounded commit protocol")
        for item in required
    }

    model_gate_position = positions["state.session.require_model ("]
    generation_id_position = grounded.find(
        "state.session.repository_generation_id ()",
        model_gate_position,
    )
    require(
        generation_id_position >= 0,
        "repository_generation_id gate marker missing after model gate",
    )
    positions["state.session.repository_generation_id ()"] = generation_id_position

    order = [
        "yield ollama_provider.chat_grounded (",
        "state.session.resolve_turn_citations (",
        "ensure_persistent_conversation (",
        "if (cancellable.is_cancelled ()) {",
        "state.session.require_model (",
        "state.session.repository_generation_id ()",
        "repository_lifecycle.current_repository_generation_id ()",
        "persistence_citations (",
        "ConversationTurnCommitter.commit (",
        "if (committed_turn_no < 0) {",
        "turn_durably_committed = true;",
        "if (!state.session.commit_turn ()) {",
        "grounded_turn_prepared = false;",
        "build_live_chart_widgets (",
        "append_completed_answer (",
    ]
    prepare_position = send.find("state.session.prepare_turn (")
    require(
        prepare_position >= 0,
        "grounded prepare_turn call missing",
    )
    require(
        prepare_position < grounded_start,
        "prepare_turn must precede the grounded provider branch",
    )

    for left, right in zip(order, order[1:]):
        require(
            positions[left] < positions[right],
            f"order violation: {left} must precede {right}",
        )

    persistence_ready = positions["bool persistence_ready = false;"]
    ensure = positions["ensure_persistent_conversation ("]
    require(
        persistence_ready < ensure,
        "ensure_persistent_conversation result is not captured",
    )

    ensure_end = grounded.find(
        "                        if (!persistence_ready) {",
        ensure,
    )
    require(
        ensure_end > ensure,
        "ensure_persistent_conversation result is not fail-closed",
    )

    final_gate = positions["if (cancellable.is_cancelled ()) {"]
    durable_commit = positions["ConversationTurnCommitter.commit ("]
    between = grounded[final_gate:durable_commit]
    require(
        "yield " not in between,
        "an async yield exists between final cancellation gate and durable SQLite commit",
    )

    prepared_false = positions["grounded_turn_prepared = false;"]
    durable_flag = positions["turn_durably_committed = true;"]
    require(
        durable_flag < prepared_false,
        "grounded_turn_prepared is cleared before durable boundary",
    )

    durable_region = grounded[durable_flag:]
    require(
        "state.persistence_failed = true;" not in durable_region,
        "persistence_failed is assigned after durable boundary",
    )

    catch_start = marker(
        send,
        "} catch (GLib.Error error) {",
        "send_prompt catch",
    )
    catch = send[catch_start:]
    require(
        "if (grounded_turn_prepared &&\n                    !turn_durably_committed)" in catch,
        "grounding abort is not restricted to the pre-durable phase",
    )

    require(
        "state.session.commit_turn ();" not in durable_region,
        "unexpected bare session commit after the guarded session commit",
    )

    print(
        "grounded turn commit order validation passed: durable SQLite commit "
        "precedes session advancement, identity/cancellation are rechecked, "
        "and post-durable work cannot mark persistence failed or abort grounding"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
