namespace AskTheModel {
    private enum RepositoryOperationOutcome {
        NORMAL,
        OFFLINE,
        ERROR,
        NO_SPACE
    }

    private errordomain GistempChartQualificationError {
        EWD_NOT_SELECTED,
        SNAPSHOT_UNAV            ConversationRepositoryPin? chart_pin = null;
            for (uint i = 0; i < session.repository_count (); i++) {
                ConversationRepositoryPin? pin =
                    session.repository_pin_at (i);
                if (pin == null) {
                    continue;
                }

                if (pin.repository_id == "ewd" ||
                    pin.repository_id ==
                        "LaurentiuStaicu/empirical-world3-dynamics") {
                    chart_pin = pin;
                    break;
                }
            }

            if (chart_pin == null) {
                throw new GLib.IOError.NOT_FOUND (
                    "Qualified chart repository is unavailable."
                );
            }

            const string source_path =
                "science/data/processed/nasa_gistemp_global_2026.csv";
            ChartNative.Spec spec;
            qualify_live_gistemp_request (
                session,
                out spec
            );


