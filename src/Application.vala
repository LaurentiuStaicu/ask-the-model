namespace AskTheModel {
    private enum RepositoryOperationOutcome {
        NORMAL,
        OFFLINE,
        ERROR
    }

    private class ChatTabState : Object {
        public Gtk.Box page;
        public Gtk.TextView transcript;
        public Gtk.TextView prompt;
        public Gtk.Button send_button;
        public Gtk.Label placeholder;
        public Gtk.Label title_label;
        public Gtk.Button lifecycle_button;
        public Gtk.Button close_button;
        public OllamaConversation conversation;
        public ConversationSession session =
            new ConversationSession ();
        public uint serial;
        public bool locked = false;
        public bool generating = false;
        public string? model_name = null;
        public string? model_digest = null;
        public string[] repository_ids = {};
        public string[] grounded_answers = {};
        public CitationResolution[] grounded_citations = {};
        public string? persistent_id = null;
        public bool persistence_failed = false;
        public bool unsaved_notice_shown = false;
        public bool restored_view_only = false;
        public string? restored_read_only_reason = null;
        public bool archived = false;

        public ChatTabState (
            Gtk.Box page,
            Gtk.TextView transcript,
            Gtk.TextView prompt,
            Gtk.Button send_button,
            Gtk.Label placeholder,
            Gtk.Label title_label,
            Gtk.Button lifecycle_button,
            Gtk.Button close_button,
            OllamaConversation conversation,
            uint serial
        ) {
            Object ();
            this.page = page;
            this.transcript = transcript;
            this.prompt = prompt;
            this.send_button = send_button;
            this.placeholder = placeholder;
            this.title_label = title_label;
            this.lifecycle_button = lifecycle_button;
            this.close_button = close_button;
            this.conversation = conversation;
            this.serial = serial;
        }
    }

    public class Application : Gtk.Application {
        private const string APP_ID = "io.github.laurentiustaicu.ask_the_model";
        private const string STYLE_RESOURCE =
            "/io/github/laurentiustaicu/ask_the_model/style.css";

        private Granite.Settings granite_settings;
        private Gtk.Settings gtk_settings;
        private OllamaProvider ollama_provider;
        private Gtk.TextView? streaming_transcript;
        private Gtk.Frame? status_lcd;
        private Gtk.DropDown? model_dropdown;
        private Gtk.StringList? model_list;
        private Gtk.Button? refresh_models_button;
        private ActivityRing? refresh_models_ring;
        private Gtk.Label? no_ai_annunciator;
        private Gtk.Label? ai_scan_annunciator;
        private Gtk.MenuButton? repository_menu_button;
        private Gtk.CheckButton[] repository_check_buttons = {};
        private Gtk.Button? refresh_repositories_button;
        private ActivityRing? refresh_repositories_ring;
        private Gtk.Button? repository_action_button;
        private ActivityRing? repository_action_ring;
        private Gtk.Label? no_repos_annunciator;
        private Gtk.Label? repo_ewd_annunciator;
        private Gtk.Label? repo_cbd_annunciator;
        private Gtk.Label? repo_rmd_annunciator;
        private Gtk.Label? repo_check_annunciator;
        private Gtk.Label? repo_download_annunciator;
        private Gtk.Label? repo_update_annunciator;
        private Gtk.Label? repo_validate_annunciator;
        private Gtk.Label? repo_ready_annunciator;
        private Gtk.Label? repo_offline_annunciator;
        private Gtk.Label? repo_error_annunciator;
        private OptimizationPolicy optimization_policy =
            new OptimizationPolicy ();
        private Gtk.Switch? optimization_switch;
        private Gtk.Label? optimization_lcd_annunciator;
        private RepositorySelection repository_selection =
            new RepositorySelection ();
        private RepositoryLifecycleService repository_lifecycle =
            new RepositoryLifecycleService ();
        private GLib.SimpleAction? new_chat_action;
        private Gtk.Button? new_chat_button;
        private Gtk.Button? history_button;
        private Gtk.Notebook? chat_notebook;
        private ChatTabState[] chat_states = {};
        private ChatTabState? active_chat;
        private uint conversation_serial = 0;
        private bool generation_active = false;
        private bool restoring_chat_controls = false;
        private bool ai_scanning = false;
        private bool repository_checking = false;
        private bool repository_downloading = false;
        private bool repository_updating = false;
        private bool repository_validating = false;
        private bool repository_offline = false;
        private bool repository_error = false;
        private string? repository_status_detail = null;
        private bool assistant_stream_started = false;
        private bool updating_model_selector = false;
        private bool startup_qualification_running = false;
        private bool startup_qualification_failed = false;
        private string? startup_qualification_detail = null;
        private ConversationPersistenceStore? conversation_store = null;
        private string? conversation_store_failure = null;
        private bool durable_restore_attempted = false;
        private bool restored_qualification_running = false;
        private bool restored_qualification_pending = false;

        public Application () {
            Object (
                application_id: APP_ID,
                flags: ApplicationFlags.DEFAULT_FLAGS
            );

            repository_lifecycle.set_optimization_policy (
                optimization_policy
            );

            optimization_policy.changed.connect ((enabled) => {
                update_optimization_control ();
                stdout.printf (
                    "AtM: optimization mode %s\n",
                    enabled ? "ON" : "OFF"
                );
            });
        }

        protected override void startup () {
            base.startup ();

            var css_provider = new Gtk.CssProvider ();
            css_provider.load_from_resource (STYLE_RESOURCE);

            var display = Gdk.Display.get_default ();
            if (display != null) {
                Gtk.StyleContext.add_provider_for_display (
                    display,
                    css_provider,
                    Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
                );
            }

            granite_settings = Granite.Settings.get_default ();
            gtk_settings = Gtk.Settings.get_default ();

            granite_settings.notify["prefers-color-scheme"].connect (() => {
                apply_system_style ();
            });

            apply_system_style ();

            ollama_provider = new OllamaProvider ();

            new_chat_action = new GLib.SimpleAction (
                "new-chat",
                null
            );
            new_chat_action.activate.connect (() => {
                create_chat_tab ();
            });
            add_action (new_chat_action);
            set_accels_for_action (
                "app.new-chat",
                { "<Primary>n" }
            );

            repository_lifecycle.progress.connect ((message) => {
                repository_validating =
                    message.has_prefix ("Validating ");
                repository_downloading =
                    message.has_prefix ("Downloading ");
                repository_updating =
                    message.has_prefix ("Updating ");
                update_repository_annunciators ();
            });

            ollama_provider.discovery_progress.connect ((percent) => {
                if (percent < 100) {
                    ai_scanning = true;
                    update_ai_annunciators ();
                }
            });

            ollama_provider.response_chunk.connect ((chunk) => {
                if (streaming_transcript == null) {
                    return;
                }

                if (!assistant_stream_started) {
                    append_transcript (
                        streaming_transcript,
                        "Assistant: " + chunk
                    );
                    assistant_stream_started = true;
                } else {
                    append_transcript_raw (
                        streaming_transcript,
                        chunk
                    );
                }
            });

            start_startup_qualification ();
        }

        private string installation_qualification_detail (
            StartupQualificationReport report
        ) {
            if (!report.platform_qualified) {
                return report.platform_detail.length > 0
                    ? report.platform_detail
                    : report.platform_reason_code;
            }

            if (!report.storage_qualified) {
                return report.storage_detail.length > 0
                    ? report.storage_detail
                    : report.storage_reason_code;
            }

            if (report.repository_state_status ==
                RepositoryStateLoadStatus.INVALID) {
                foreach (
                    StartupRepositoryOutcome repository
                    in report.repositories
                ) {
                    if (repository.status ==
                            StartupRepositoryStatus.STATE_INVALID &&
                        repository.detail.length > 0) {
                        return repository.detail;
                    }
                }

                return "Repository Control DB authority is invalid.";
            }

            return "Installation qualification did not pass.";
        }

        private void initialize_conversation_persistence () {
            conversation_store = null;
            conversation_store_failure = null;

            try {
                conversation_store =
                    new ConversationPersistenceStore ();

                stdout.printf (
                    "AtM: conversation persistence ready path=%s\n",
                    conversation_store.path
                );

                restore_durable_conversations_if_ready ();
            } catch (GLib.Error error) {
                conversation_store_failure = error.message;

                stderr.printf (
                    "AtM: conversation persistence unavailable: %s\n",
                    error.message
                );
            }
        }

        private void start_startup_qualification () {
            startup_qualification_running = true;
            startup_qualification_failed = false;
            startup_qualification_detail = null;
            update_conversation_ui_state ();

            new GLib.Thread<void> ("atm-gs0", () => {
                StartupQualificationReport? report = null;
                string? failure = null;

                try {
                    var service = new StartupQualificationService ();
                    report = service.run ();
                } catch (GLib.Error error) {
                    failure = error.message;
                }

                StartupQualificationReport? completed_report = report;
                string? completed_failure = failure;

                GLib.Idle.add (() => {
                    startup_qualification_running = false;

                    if (completed_report != null) {
                        bool lifecycle_state_ready =
                            repository_lifecycle.reload_control_state ();
                        bool runtime_qualified =
                            completed_report.installation_qualified &&
                            lifecycle_state_ready;

                        repository_lifecycle.apply_installation_qualification (
                            runtime_qualified
                        );
                        startup_qualification_failed =
                            !runtime_qualified;
                        startup_qualification_detail =
                            runtime_qualified
                                ? null
                                : !lifecycle_state_ready
                                    ? "Repository Control DB could not be reloaded after startup qualification."
                                    : installation_qualification_detail (
                                        completed_report
                                    );

                        initialize_conversation_persistence ();

                        stdout.printf (
                            "AtM: G-S0 mode=%s platform=%s storage=%s state=%d record=%s\n",
                            completed_report.execution_mode,
                            completed_report.platform_qualified
                                ? "qualified"
                                : "unqualified",
                            completed_report.storage_qualified
                                ? "qualified"
                                : "unqualified",
                            (int) completed_report.repository_state_status,
                            completed_report.record_path
                        );

                        foreach (
                            StartupRepositoryOutcome repository
                            in completed_report.repositories
                        ) {
                            if (repository.status ==
                                StartupRepositoryStatus.SNAPSHOT_INVALID) {
                                repository_lifecycle.mark_integrity_invalid (
                                    repository.repository_id
                                );
                            }

                            stdout.printf (
                                "AtM: G-S0 repository %s status=%d reason=%s\n",
                                repository.repository_id,
                                (int) repository.status,
                                repository.reason_code
                            );
                        }
                    } else {
                        repository_lifecycle.apply_installation_qualification (
                            false
                        );
                        startup_qualification_failed = true;
                        startup_qualification_detail =
                            completed_failure ??
                            "Startup qualification failed.";

                        stderr.printf (
                            "AtM: G-S0 startup qualification failed: %s\n",
                            completed_failure ??
                                "unknown qualification error"
                        );
                    }

                    update_repository_annunciators ();
                    update_repository_selector_label ();
                    update_conversation_ui_state ();
                    discover_local_provider.begin ();
                    return false;
                });
            });
        }

        private async void discover_local_provider () {
            begin_model_scan_status ();

            bool found = yield ollama_provider.discover ();
            update_model_selector ();

            if (found && ollama_provider.model_name != null) {
                finish_model_scan_status ();

                stdout.printf (
                    "AtM: Ollama detected at %s; using %s (%u model%s available)\n",
                    ollama_provider.base_url,
                    ollama_provider.model_name,
                    ollama_provider.model_count,
                    ollama_provider.model_count == 1 ? "" : "s"
                );
            } else if (found) {
                finish_model_scan_status ();

                stderr.printf (
                    "AtM: Ollama detected at %s, but no completion-capable model was found.\n",
                    ollama_provider.base_url
                );
            } else {
                finish_model_scan_status ();

                stderr.printf (
                    "AtM: local Ollama provider not detected on 127.0.0.1 ports 11434 or 11435.\n"
                );
            }

            qualify_restored_conversations.begin ();
        }

        private Gtk.Label build_annunciator_label (
            string text,
            string? extra_class = null
        ) {
            var label = new Gtk.Label (text) {
                valign = Gtk.Align.CENTER,
                single_line_mode = true
            };
            label.update_state (
                Gtk.AccessibleState.HIDDEN,
                true
            );
            label.add_css_class ("atm-annunciator");

            if (extra_class != null) {
                label.add_css_class (extra_class);
            }

            return label;
        }

        private Gtk.Label build_annunciator_separator () {
            var separator = new Gtk.Label ("·") {
                valign = Gtk.Align.CENTER,
                single_line_mode = true
            };
            separator.update_state (
                Gtk.AccessibleState.HIDDEN,
                true
            );
            separator.add_css_class (
                "atm-annunciator-separator"
            );
            return separator;
        }

        private void set_annunciator (
            Gtk.Label? annunciator,
            bool active
        ) {
            if (annunciator == null) {
                return;
            }

            if (active) {
                annunciator.add_css_class ("active");
            } else {
                annunciator.remove_css_class ("active");
            }
        }

        private void update_status_lcd_accessibility () {
            if (status_lcd == null) {
                return;
            }

            string ai_summary =
                ai_scanning
                    ? "Local AI scan in progress"
                    : ollama_provider.is_ready ()
                        ? "Local AI ready"
                        : "No usable local AI available";

            RepositoryDescriptor[] selected =
                repository_selection.selected_repositories ();
            string acronyms = "";
            bool any_ready = false;
            bool all_ready = selected.length > 0;
            bool needs_download = false;
            bool update_available = false;

            foreach (RepositoryDescriptor descriptor in selected) {
                if (acronyms.length > 0) {
                    acronyms += ", ";
                }
                acronyms += descriptor.acronym;

                RepositoryRuntimeInfo info =
                    repository_lifecycle.info_for (
                        descriptor.id
                    );

                if (info.download_required ()) {
                    needs_download = true;
                    all_ready = false;
                } else {
                    any_ready = true;
                    if (info.update_available ()) {
                        update_available = true;
                    }
                }
            }

            string repository_summary;

            if (startup_qualification_running) {
                repository_summary =
                    "Qualifying local repository runtime";
            } else if (startup_qualification_failed) {
                repository_summary =
                    startup_qualification_detail != null
                        ? "Repository runtime blocked: %s".printf (
                            startup_qualification_detail
                        )
                        : "Repository runtime blocked by startup qualification";
            } else if (repository_error) {
                repository_summary =
                    repository_status_detail != null
                        ? "Repository operation failed: %s".printf (
                            repository_status_detail
                        )
                        : "Repository operation failed";
            } else if (repository_validating) {
                repository_summary =
                    "Validating repositories";
            } else if (repository_updating) {
                repository_summary =
                    "Updating repositories";
            } else if (repository_downloading) {
                repository_summary =
                    "Downloading repositories";
            } else if (repository_checking) {
                repository_summary =
                    "Checking repositories";
            } else if (!any_ready) {
                repository_summary =
                    "No selected repository snapshot ready";
            } else if (all_ready && update_available) {
                repository_summary =
                    "Repositories %s ready; update available".printf (
                        acronyms
                    );
            } else if (all_ready) {
                repository_summary =
                    "Repositories %s ready".printf (
                        acronyms
                    );
            } else if (needs_download) {
                repository_summary =
                    "Repositories %s partially ready; download required".printf (
                        acronyms
                    );
            } else {
                repository_summary =
                    "Repositories %s available".printf (
                        acronyms
                    );
            }

            if (repository_offline) {
                repository_summary +=
                    repository_status_detail != null
                        ? "; remote check offline: %s".printf (
                            repository_status_detail
                        )
                        : "; remote check offline";
            }

            string optimization_summary =
                optimization_policy.enabled
                    ? "Optimizations on"
                    : "Optimizations off";

            string summary =
                "%s. %s. %s.".printf (
                    ai_summary,
                    repository_summary,
                    optimization_summary
                );

            status_lcd.tooltip_text = summary;
            status_lcd.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Application status",
                Gtk.AccessibleProperty.DESCRIPTION,
                summary
            );
        }

        private void update_ai_annunciators () {
            bool ai_missing =
                !ai_scanning &&
                !ollama_provider.is_ready ();

            set_annunciator (
                no_ai_annunciator,
                ai_missing
            );
            set_annunciator (
                ai_scan_annunciator,
                ai_scanning
            );
            update_status_lcd_accessibility ();
        }

        private void update_repository_annunciators () {
            RepositoryDescriptor[] selected =
                repository_selection.selected_repositories ();
            bool ewd_selected = false;
            bool cbd_selected = false;
            bool rmd_selected = false;
            bool needs_download = false;
            bool update_available = false;
            bool any_ready = false;
            bool all_ready = selected.length > 0;

            foreach (RepositoryDescriptor descriptor in selected) {
                if (descriptor.id == "ewd") {
                    ewd_selected = true;
                } else if (descriptor.id == "cbd") {
                    cbd_selected = true;
                } else if (descriptor.id == "rmd") {
                    rmd_selected = true;
                }

                RepositoryRuntimeInfo info =
                    repository_lifecycle.info_for (
                        descriptor.id
                    );

                if (info.download_required ()) {
                    needs_download = true;
                    all_ready = false;
                } else {
                    any_ready = true;

                    if (info.update_available ()) {
                        update_available = true;
                    }
                }
            }

            bool operation_active =
                startup_qualification_running ||
                repository_checking ||
                repository_downloading ||
                repository_updating ||
                repository_validating;

            set_annunciator (
                no_repos_annunciator,
                !any_ready
            );
            set_annunciator (
                repo_ewd_annunciator,
                ewd_selected
            );
            set_annunciator (
                repo_cbd_annunciator,
                cbd_selected
            );
            set_annunciator (
                repo_rmd_annunciator,
                rmd_selected
            );
            set_annunciator (
                repo_check_annunciator,
                repository_checking
            );
            set_annunciator (
                repo_download_annunciator,
                repository_downloading ||
                (!operation_active && needs_download)
            );
            set_annunciator (
                repo_update_annunciator,
                repository_updating ||
                (!operation_active && update_available)
            );
            set_annunciator (
                repo_validate_annunciator,
                startup_qualification_running ||
                repository_validating
            );
            set_annunciator (
                repo_ready_annunciator,
                !operation_active &&
                all_ready &&
                !repository_error &&
                !startup_qualification_failed
            );
            set_annunciator (
                repo_offline_annunciator,
                repository_offline
            );
            set_annunciator (
                repo_error_annunciator,
                repository_error ||
                startup_qualification_failed
            );

            if (repo_offline_annunciator != null) {
                repo_offline_annunciator.tooltip_text =
                    repository_offline &&
                    repository_status_detail != null
                        ? "Remote check unavailable: %s".printf (
                            repository_status_detail
                        )
                        : "Remote check unavailable; local repositories may remain usable";
            }

            if (repo_error_annunciator != null) {
                repo_error_annunciator.tooltip_text =
                    startup_qualification_failed
                        ? startup_qualification_detail ??
                            "Repository runtime blocked by startup qualification"
                        : repository_error &&
                            repository_status_detail != null
                            ? repository_status_detail
                            : "Repository operation failed";
            }

            update_status_lcd_accessibility ();
        }

        private void show_model_standby_status () {
            ai_scanning = false;
            update_ai_annunciators ();
        }

        private void show_repository_standby_status () {
            repository_checking = false;
            repository_downloading = false;
            repository_updating = false;
            repository_validating = false;
            repository_offline = false;
            repository_error = false;
            repository_status_detail = null;
            update_repository_annunciators ();
        }

        private void begin_model_scan_status () {
            ai_scanning = true;
            update_ai_annunciators ();
        }

        private void finish_model_scan_status () {
            ai_scanning = false;
            update_ai_annunciators ();
        }

        private async void refresh_local_models () {
            begin_model_scan_status ();
            set_activity_working (
                refresh_models_button,
                refresh_models_ring,
                true
            );

            if (refresh_models_button != null) {
                refresh_models_button.sensitive = false;
            }

            if (model_dropdown != null) {
                model_dropdown.sensitive = false;
            }

            bool found = yield ollama_provider.discover ();
            update_model_selector ();

            ai_scanning = false;
            update_ai_annunciators ();
            update_conversation_ui_state ();

            if (found && ollama_provider.model_name != null) {
                stdout.printf (
                    "AtM: model list refreshed; using %s (%u installed model%s)\n",
                    ollama_provider.model_name,
                    ollama_provider.model_count,
                    ollama_provider.model_count == 1 ? "" : "s"
                );
            } else if (found) {
                stderr.printf (
                    "AtM: model list refreshed, but no completion-capable model was found.\n"
                );
            } else {
                stderr.printf (
                    "AtM: refresh could not reach local Ollama on ports 11434 or 11435.\n"
                );
            }

            set_activity_working (
                refresh_models_button,
                refresh_models_ring,
                false
            );

            qualify_restored_conversations.begin ();
        }

        private string[] selected_repository_ids () {
            string[] ids = {};

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                if (repository_selection.is_selected (descriptor.id)) {
                    ids += descriptor.id;
                }
            }

            return ids;
        }

        private RepositoryDescriptor[]
        repository_descriptors_for_ids (
            string[] ids
        ) {
            RepositoryDescriptor[] selected = {};

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                if (repository_id_in (ids, descriptor.id)) {
                    selected += descriptor;
                }
            }

            return selected;
        }

        private RepositoryDescriptor?
        repository_descriptor_for_id (
            string repository_id
        ) {
            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                if (descriptor.id == repository_id) {
                    return descriptor;
                }
            }

            return null;
        }

        private bool repository_id_in (
            string[] ids,
            string repository_id
        ) {
            foreach (string id in ids) {
                if (id == repository_id) {
                    return true;
                }
            }

            return false;
        }

        private ChatTabState? chat_state_for_page (
            Gtk.Widget page
        ) {
            foreach (ChatTabState state in chat_states) {
                if (state.page == page) {
                    return state;
                }
            }

            return null;
        }

        private bool chat_state_is_open (
            ChatTabState candidate
        ) {
            foreach (ChatTabState state in chat_states) {
                if (state == candidate) {
                    return true;
                }
            }

            return false;
        }

        private void restore_chat_controls (
            ChatTabState state
        ) {
            restoring_chat_controls = true;

            if (state.model_name != null) {
                ollama_provider.select_model (state.model_name);

                if (model_list != null && model_dropdown != null) {
                    for (uint i = 0; i < model_list.n_items; i++) {
                        string? listed = model_list.get_string (i);
                        if (listed == state.model_name) {
                            updating_model_selector = true;
                            model_dropdown.set_selected (i);
                            updating_model_selector = false;
                            break;
                        }
                    }
                }
            }

            RepositoryDescriptor[] catalog = RepositoryCatalog.all ();
            for (int i = 0; i < catalog.length; i++) {
                bool selected = repository_id_in (
                    state.repository_ids,
                    catalog[i].id
                );

                repository_selection.set_selected (
                    catalog[i].id,
                    selected
                );

                if (i < repository_check_buttons.length) {
                    repository_check_buttons[i].active = selected;
                }
            }

            restoring_chat_controls = false;
            update_repository_selector_label ();
            update_ai_annunciators ();
            update_repository_annunciators ();
        }

        private void activate_chat_state (
            ChatTabState state
        ) {
            active_chat = state;
            restore_chat_controls (state);
            update_conversation_ui_state ();

            if (!generation_active) {
                state.prompt.grab_focus ();
            }
        }

        private void remove_chat_state (
            ChatTabState target
        ) {
            ChatTabState[] updated = {};

            foreach (ChatTabState state in chat_states) {
                if (state != target) {
                    updated += state;
                }
            }

            chat_states = updated;
        }

        private bool durable_conversation_is_open (
            string conversation_id
        ) {
            foreach (ChatTabState state in chat_states) {
                if (state.persistent_id == conversation_id) {
                    return true;
                }
            }

            return false;
        }

        private void show_conversation_history_error (
            Gtk.Window? parent,
            string detail
        ) {
            var dialog = new Gtk.AlertDialog (
                "Conversation history action failed"
            ) {
                detail = detail,
                modal = true
            };
            dialog.show (parent);
        }

        private void open_history_conversation (
            ConversationPersistenceSummary summary,
            Gtk.Window history_window
        ) {
            if (generation_active ||
                conversation_store == null ||
                chat_notebook == null) {
                return;
            }

            if (durable_conversation_is_open (
                    summary.conversation_id
                )) {
                history_window.close ();
                return;
            }

            try {
                ConversationPersistenceSnapshot snapshot =
                    conversation_store.load_snapshot (
                        summary.conversation_id
                    );

                if (!snapshot.archived) {
                    throw new GLib.IOError.INVALID_DATA (
                        "Only explicitly archived conversations can be opened from History."
                    );
                }

                ChatTabState? restored =
                    create_restored_chat_tab (
                        snapshot
                    );

                if (restored == null) {
                    throw new GLib.IOError.FAILED (
                        "Conversation could not be opened in the current notebook."
                    );
                }

                history_window.close ();

                stdout.printf (
                    "AtM: archived conversation %s reopened from history\n",
                    summary.conversation_id
                );

                qualify_restored_conversations.begin ();
            } catch (GLib.Error error) {
                show_conversation_history_error (
                    history_window,
                    error.message
                );
            }
        }

        private async void confirm_delete_history_conversation (
            ConversationPersistenceSummary summary,
            Gtk.Window history_window
        ) {
            if (conversation_store == null ||
                durable_conversation_is_open (
                    summary.conversation_id
                )) {
                return;
            }

            var dialog = new Gtk.AlertDialog (
                "Delete this conversation permanently?"
            ) {
                detail =
                    "The archived transcript, repository pins, citation provenance and automatic JSON export will be deleted. This cannot be undone.",
                buttons = {
                    "Cancel",
                    "Delete permanently"
                },
                cancel_button = 0,
                default_button = 0,
                modal = true
            };

            int response;

            try {
                response = yield dialog.choose (
                    history_window,
                    null
                );
            } catch (GLib.Error error) {
                return;
            }

            if (response != 1 ||
                conversation_store == null ||
                durable_conversation_is_open (
                    summary.conversation_id
                )) {
                return;
            }

            try {
                conversation_store.delete_conversation (
                    summary.conversation_id
                );
            } catch (GLib.Error error) {
                show_conversation_history_error (
                    history_window,
                    error.message
                );
                return;
            }

            history_window.close ();
            show_conversation_history ();
        }

        private Gtk.Widget build_conversation_history_row (
            ConversationPersistenceSummary summary,
            Gtk.Window history_window
        ) {
            var title = new Gtk.Label (summary.title) {
                halign = Gtk.Align.START,
                xalign = 0.0f,
                hexpand = true,
                ellipsize = Pango.EllipsizeMode.END
            };

            var state_label = new Gtk.Label ("Archived") {
                halign = Gtk.Align.START,
                xalign = 0.0f
            };

            var labels = new Gtk.Box (
                Gtk.Orientation.VERTICAL,
                2
            ) {
                hexpand = true
            };
            labels.append (title);
            labels.append (state_label);

            var open_button =
                new Gtk.Button.with_label (
                    "Open"
                ) {
                    valign = Gtk.Align.CENTER
                };

            var delete_button =
                new Gtk.Button.with_label (
                    "Delete permanently"
                ) {
                    valign = Gtk.Align.CENTER
                };
            delete_button.add_css_class (
                "destructive-action"
            );

            ConversationPersistenceSummary item = summary;
            open_button.clicked.connect (() => {
                open_history_conversation (
                    item,
                    history_window
                );
            });
            delete_button.clicked.connect (() => {
                confirm_delete_history_conversation.begin (
                    item,
                    history_window
                );
            });

            var actions = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                6
            );
            actions.append (open_button);
            actions.append (delete_button);

            var row = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                12
            ) {
                margin_top = 8,
                margin_bottom = 8,
                margin_start = 10,
                margin_end = 10
            };
            row.append (labels);
            row.append (actions);

            return row;
        }

        private void show_conversation_history () {
            if (conversation_store == null ||
                generation_active) {
                return;
            }

            ConversationPersistenceSummary[] summaries;

            try {
                summaries =
                    conversation_store.list_conversations ();
            } catch (GLib.Error error) {
                show_conversation_history_error (
                    this.active_window,
                    error.message
                );
                return;
            }

            var content = new Gtk.Box (
                Gtk.Orientation.VERTICAL,
                10
            ) {
                margin_top = 12,
                margin_bottom = 12,
                margin_start = 12,
                margin_end = 12
            };

            var description = new Gtk.Label (
                "Only conversations explicitly archived with the Archive button are saved here. Open restores an archived conversation; Delete permanently removes it and its automatic JSON export."
            ) {
                halign = Gtk.Align.START,
                xalign = 0.0f,
                wrap = true
            };
            content.append (description);

            var history_window = new Gtk.Window () {
                application = this,
                title = "Conversation History",
                transient_for = this.active_window,
                modal = true,
                destroy_with_parent = true,
                resizable = true,
                default_width = 640,
                default_height = 420
            };
            history_window.add_css_class ("atm-window");

            var list = new Gtk.ListBox () {
                selection_mode = Gtk.SelectionMode.NONE,
                show_separators = true
            };

            uint visible_count = 0;

            foreach (
                ConversationPersistenceSummary summary
                in summaries
            ) {
                if (!summary.archived ||
                    durable_conversation_is_open (
                        summary.conversation_id
                    )) {
                    continue;
                }

                list.append (
                    build_conversation_history_row (
                        summary,
                        history_window
                    )
                );
                visible_count++;
            }

            if (visible_count == 0) {
                var empty = new Gtk.Label (
                    "No archived conversations."
                ) {
                    halign = Gtk.Align.CENTER,
                    margin_top = 24,
                    margin_bottom = 24
                };
                content.append (empty);
            } else {
                var scroller = new Gtk.ScrolledWindow () {
                    hscrollbar_policy = Gtk.PolicyType.NEVER,
                    vscrollbar_policy = Gtk.PolicyType.AUTOMATIC,
                    vexpand = true
                };
                scroller.set_child (list);
                content.append (scroller);
            }

            history_window.child = content;
            history_window.present ();
        }

        private void configure_conversation_archive_button (
            ChatTabState state
        ) {
            state.lifecycle_button.clicked.connect (() => {
                archive_chat_tab (
                    state
                );
            });
        }

        private void archive_chat_tab (
            ChatTabState state
        ) {
            if (state.generating ||
                conversation_store == null ||
                state.persistent_id == null) {
                return;
            }

            try {
                conversation_store.set_archived (
                    state.persistent_id,
                    true,
                    GLib.get_real_time ()
                );
            } catch (GLib.Error error) {
                append_transcript (
                    state.transcript,
                    "System: Conversation could not be archived: " +
                    error.message
                );
                return;
            }

            state.archived = true;

            close_chat_tab (
                state,
                false
            );
        }

        private void close_chat_tab (
            ChatTabState state,
            bool discard_unarchived = true
        ) {
            if (state.generating || chat_notebook == null) {
                return;
            }

            int page_num = chat_notebook.page_num (state.page);
            if (page_num < 0) {
                return;
            }

            if (discard_unarchived &&
                state.persistent_id != null &&
                !state.archived) {
                if (conversation_store == null) {
                    append_transcript (
                        state.transcript,
                        "System: Conversation could not be closed because temporary persistence is unavailable."
                    );
                    return;
                }

                try {
                    conversation_store.delete_conversation (
                        state.persistent_id
                    );
                    state.persistent_id = null;
                } catch (GLib.Error error) {
                    append_transcript (
                        state.transcript,
                        "System: Conversation could not be discarded on close: " +
                        error.message
                    );
                    return;
                }
            }

            bool was_active = active_chat == state;
            state.session.reset ();
            chat_notebook.remove_page (page_num);
            remove_chat_state (state);

            if (was_active) {
                active_chat = null;
            }

            if (chat_notebook.get_n_pages () == 0) {
                create_chat_tab ();
                return;
            }

            int current = chat_notebook.get_current_page ();
            Gtk.Widget? current_page =
                chat_notebook.get_nth_page (current);

            if (current_page != null) {
                ChatTabState? next =
                    chat_state_for_page (current_page);

                if (next != null) {
                    activate_chat_state (next);
                }
            }
        }

        private void update_conversation_ui_state () {
            bool editable =
                active_chat != null &&
                !active_chat.locked &&
                !generation_active;
            bool provider_ready =
                ollama_provider.get_completion_models ().length > 0;

            if (model_dropdown != null) {
                model_dropdown.sensitive =
                    editable && provider_ready;
            }

            if (refresh_models_button != null) {
                refresh_models_button.sensitive =
                    editable && !ai_scanning;
            }

            if (repository_menu_button != null) {
                repository_menu_button.sensitive =
                    editable &&
                    !startup_qualification_running &&
                    !repository_checking &&
                    !repository_downloading &&
                    !repository_updating &&
                    !repository_validating;
            }

            foreach (Gtk.CheckButton check in repository_check_buttons) {
                check.sensitive =
                    editable && !startup_qualification_running;
            }

            foreach (ChatTabState state in chat_states) {
                state.prompt.sensitive =
                    !generation_active &&
                    !state.persistence_failed &&
                    !state.restored_view_only;
                state.send_button.sensitive =
                    !generation_active &&
                    !state.persistence_failed &&
                    !state.restored_view_only &&
                    state.prompt.buffer.text.strip ().length > 0;
                state.lifecycle_button.sensitive =
                    !state.generating &&
                    !state.persistence_failed &&
                    state.persistent_id != null &&
                    !state.archived;
                state.close_button.sensitive =
                    !state.generating;
            }

            update_repository_selector_label ();

            bool can_start_new_chat = !generation_active;

            if (new_chat_button != null) {
                new_chat_button.sensitive = can_start_new_chat;
            }

            if (new_chat_action != null) {
                new_chat_action.set_enabled (can_start_new_chat);
            }

            if (history_button != null) {
                history_button.sensitive =
                    conversation_store != null &&
                    !generation_active;
            }
        }

        private void update_model_selector () {
            if (model_list == null || model_dropdown == null) {
                return;
            }

            updating_model_selector = true;

            string[] models = ollama_provider.get_completion_models ();
            string[] displayed_models;

            if (models.length > 0) {
                displayed_models = models;
            } else {
                displayed_models = { "No chat models detected" };
            }

            model_list.splice (
                0,
                model_list.n_items,
                displayed_models
            );

            uint selected_index = 0;
            if (ollama_provider.model_name != null) {
                for (uint i = 0; i < model_list.n_items; i++) {
                    string? listed_model = model_list.get_string (i);
                    if (listed_model == ollama_provider.model_name) {
                        selected_index = i;
                        break;
                    }
                }
            }

            model_dropdown.set_selected (selected_index);

            updating_model_selector = false;

            if (active_chat != null &&
                !active_chat.locked &&
                ollama_provider.model_name != null) {
                active_chat.model_name =
                    ollama_provider.model_name;
                active_chat.model_digest =
                    ollama_provider.model_digest;
            }
            update_ai_annunciators ();
            update_conversation_ui_state ();
        }

        private void set_activity_working (
            Gtk.Button? button,
            ActivityRing? ring,
            bool working
        ) {
            if (button != null) {
                button.update_state (
                    Gtk.AccessibleState.BUSY,
                    working
                );
            }

            if (ring == null) {
                return;
            }

            ring.set_working (
                working,
                gtk_settings.gtk_enable_animations
            );
        }

        private Gtk.Widget build_activity_overlay (
            Gtk.Button button,
            ActivityRing ring
        ) {
            var overlay = new Gtk.Overlay () {
                child = button
            };
            overlay.add_overlay (ring);
            return overlay;
        }

        private Gtk.Widget build_selector_triangle_overlay (
            Gtk.Widget selector
        ) {
            var overlay = new Gtk.Overlay () {
                child = selector
            };
            var triangle = new SelectorTriangle ();
            overlay.add_overlay (triangle);
            overlay.set_clip_overlay (
                triangle,
                true
            );
            return overlay;
        }

        private void update_repository_selector_label () {
            bool editable =
                active_chat != null &&
                !active_chat.locked &&
                !generation_active;
            bool repository_busy =
                startup_qualification_running ||
                repository_checking ||
                repository_downloading ||
                repository_updating ||
                repository_validating;

            if (repository_menu_button != null) {
                repository_menu_button.label =
                    repository_selection.summary ();
                repository_menu_button.sensitive =
                    editable && !repository_busy;
            }

            foreach (Gtk.CheckButton check in repository_check_buttons) {
                check.sensitive = editable;
            }

            bool has_selection =
                repository_selection.selected_repositories ().length > 0;

            if (refresh_repositories_button != null) {
                refresh_repositories_button.sensitive =
                    editable &&
                    !repository_busy &&
                    has_selection;
            }

            if (repository_action_button != null) {
                RepositoryDescriptor[] selected =
                    repository_selection.selected_repositories ();
                bool needs_download = false;

                foreach (RepositoryDescriptor descriptor in selected) {
                    if (repository_lifecycle.info_for (
                            descriptor.id
                        ).download_required ()) {
                        needs_download = true;
                        break;
                    }
                }

                bool has_action =
                    repository_lifecycle.selection_needs_action (
                        selected
                    );

                if (needs_download) {
                    repository_action_button.set_icon_name (
                        "folder-download-symbolic"
                    );
                } else {
                    repository_action_button.set_icon_name (
                        "software-update-available-symbolic"
                    );
                }

                bool repository_operations_allowed =
                    repository_lifecycle.repository_operations_allowed ();

                repository_action_button.sensitive =
                    editable &&
                    !repository_busy &&
                    has_action &&
                    repository_operations_allowed;
                repository_action_button.can_target =
                    editable &&
                    !repository_busy &&
                    has_action &&
                    repository_operations_allowed;
                repository_action_button.opacity =
                    has_action ? 1.0 : 0.0;
                repository_action_button.update_state (
                    Gtk.AccessibleState.HIDDEN,
                    !has_action
                );

                string? action_tooltip =
                    has_action
                        ? repository_operations_allowed
                            ? repository_lifecycle.action_tooltip (
                                selected
                            )
                            : startup_qualification_running
                                ? "Repository action unavailable while startup qualification runs"
                                : "Repository action unavailable because startup qualification did not pass"
                        : null;
                repository_action_button.tooltip_text =
                    action_tooltip;

                if (has_action && action_tooltip != null) {
                    repository_action_button.update_property (
                        Gtk.AccessibleProperty.LABEL,
                        action_tooltip
                    );
                }
            }
        }

        private void update_repository_option_labels () {
            RepositoryDescriptor[] catalog =
                RepositoryCatalog.all ();

            for (
                int i = 0;
                i < repository_check_buttons.length &&
                i < catalog.length;
                i++
            ) {
                RepositoryRuntimeInfo info =
                    repository_lifecycle.info_for (
                        catalog[i].id
                    );
                string version =
                    info.local.version ??
                    info.remote_version ??
                    catalog[i].supported_version;

                repository_check_buttons[i].label =
                    catalog[i].selector_label (version);
            }
        }

        private void begin_repository_scan_status () {
            repository_checking = true;
            repository_offline = false;
            repository_error = false;
            repository_status_detail = null;
            update_repository_annunciators ();
        }

        private void finish_repository_operation (
            RepositoryOperationOutcome outcome,
            string? detail = null
        ) {
            repository_checking = false;
            repository_downloading = false;
            repository_updating = false;
            repository_validating = false;
            repository_offline =
                outcome == RepositoryOperationOutcome.OFFLINE;
            repository_error =
                outcome == RepositoryOperationOutcome.ERROR;
            repository_status_detail =
                outcome == RepositoryOperationOutcome.NORMAL
                    ? null
                    : detail;
            update_repository_annunciators ();
        }

        private async void refresh_selected_repositories () {
            if (startup_qualification_running) {
                return;
            }

            RepositoryDescriptor[] selected =
                repository_selection.selected_repositories ();

            if (selected.length == 0) {
                finish_repository_operation (
                    RepositoryOperationOutcome.NORMAL
                );
                return;
            }

            // A batch refresh is one freshness observation for the
            // whole selected scope. Clear every previous remote identity
            // before the first network request so an early failure cannot
            // leave an unvisited repository looking freshly checked.
            repository_lifecycle.clear_remote_identities (
                selected
            );

            begin_repository_scan_status ();
            set_activity_working (
                refresh_repositories_button,
                refresh_repositories_ring,
                true
            );

            if (refresh_repositories_button != null) {
                refresh_repositories_button.sensitive = false;
            }

            if (repository_action_button != null) {
                repository_action_button.sensitive = false;
            }

            if (repository_menu_button != null) {
                repository_menu_button.sensitive = false;
            }

            try {
                foreach (RepositoryDescriptor descriptor in selected) {
                    RepositoryRuntimeInfo info =
                        yield repository_lifecycle.refresh (
                            descriptor
                        );

                    stdout.printf (
                        "AtM: repository %s remote version=%s sha=%s\n",
                        descriptor.acronym,
                        info.remote_version ?? "unknown",
                        info.remote_sha ?? "unknown"
                    );
                }

                finish_repository_operation (
                    RepositoryOperationOutcome.NORMAL
                );
            } catch (RepositoryError error) {
                finish_repository_operation (
                    RepositoryOperationOutcome.ERROR,
                    error.message
                );

                stderr.printf (
                    "AtM: repository refresh failed: %s\n",
                    error.message
                );
            } catch (GLib.Error error) {
                finish_repository_operation (
                    RepositoryOperationOutcome.OFFLINE,
                    error.message
                );

                stderr.printf (
                    "AtM: repository refresh transport failed: %s\n",
                    error.message
                );
            }

            update_conversation_ui_state ();

            set_activity_working (
                refresh_repositories_button,
                refresh_repositories_ring,
                false
            );
            update_repository_option_labels ();
            update_repository_selector_label ();
            qualify_restored_conversations.begin ();
        }

        private async void download_or_update_selected_repositories () {
            if (!repository_lifecycle.repository_operations_allowed ()) {
                finish_repository_operation (
                    RepositoryOperationOutcome.ERROR,
                    startup_qualification_detail ??
                        "Repository action blocked by startup qualification."
                );
                update_repository_selector_label ();
                return;
            }

            RepositoryDescriptor[] selected =
                repository_selection.selected_repositories ();

            if (selected.length == 0) {
                finish_repository_operation (
                    RepositoryOperationOutcome.NORMAL
                );
                return;
            }

            bool needs_download = false;

            foreach (RepositoryDescriptor descriptor in selected) {
                if (repository_lifecycle.info_for (
                        descriptor.id
                    ).download_required ()) {
                    needs_download = true;
                    break;
                }
            }

            repository_checking = false;
            repository_offline = false;
            repository_error = false;
            repository_status_detail = null;
            repository_validating = false;
            repository_downloading = needs_download;
            repository_updating = !needs_download;
            update_repository_annunciators ();
            set_activity_working (
                repository_action_button,
                repository_action_ring,
                true
            );

            if (repository_menu_button != null) {
                repository_menu_button.sensitive = false;
            }
            if (refresh_repositories_button != null) {
                refresh_repositories_button.sensitive = false;
            }
            if (repository_action_button != null) {
                repository_action_button.sensitive = false;
            }

            try {
                uint changed =
                    yield repository_lifecycle.download_or_update (
                        selected
                    );

                finish_repository_operation (
                    RepositoryOperationOutcome.NORMAL
                );

                if (changed == 1) {
                    stdout.printf (
                        "AtM: repository action completed; 1 repository changed\n"
                    );
                } else {
                    stdout.printf (
                        "AtM: repository action completed; %u repositories changed\n",
                        changed
                    );
                }
            } catch (RepositoryError error) {
                finish_repository_operation (
                    RepositoryOperationOutcome.ERROR,
                    error.message
                );

                stderr.printf (
                    "AtM: repository download/update failed: %s\n",
                    error.message
                );
            } catch (GLib.Error error) {
                finish_repository_operation (
                    RepositoryOperationOutcome.OFFLINE,
                    error.message
                );

                stderr.printf (
                    "AtM: repository download/update transport failed: %s\n",
                    error.message
                );
            }

            update_conversation_ui_state ();

            set_activity_working (
                repository_action_button,
                repository_action_ring,
                false
            );
            update_repository_option_labels ();
            update_repository_selector_label ();
            qualify_restored_conversations.begin ();
        }

        private Gtk.CheckButton build_repository_check_button (
            RepositoryDescriptor descriptor
        ) {
            RepositoryRuntimeInfo info =
                repository_lifecycle.info_for (
                    descriptor.id
                );
            string version =
                info.local.version ??
                descriptor.supported_version;

            var check = new Gtk.CheckButton.with_label (
                descriptor.selector_label (version)
            ) {
                active = repository_selection.is_selected (
                    descriptor.id
                )
            };

            check.toggled.connect (() => {
                repository_selection.set_selected (
                    descriptor.id,
                    check.active
                );

                if (active_chat != null &&
                    !active_chat.locked &&
                    !restoring_chat_controls) {
                    active_chat.repository_ids =
                        selected_repository_ids ();
                }

                update_repository_selector_label ();
                show_repository_standby_status ();
            });

            repository_check_buttons += check;
            return check;
        }

        private Gtk.Widget build_repository_selector () {
            var content = new Gtk.Box (
                Gtk.Orientation.VERTICAL,
                6
            ) {
                margin_top = 10,
                margin_bottom = 10,
                margin_start = 10,
                margin_end = 10
            };

            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                content.append (
                    build_repository_check_button (descriptor)
                );
            }

            var popover = new Gtk.Popover () {
                child = content,
                has_arrow = true,
                position = Gtk.PositionType.BOTTOM
            };

            repository_menu_button = new Gtk.MenuButton () {
                label = repository_selection.summary (),
                tooltip_text = "Select repository context",
                direction = Gtk.ArrowType.NONE,
                always_show_arrow = false,
                can_shrink = true
            };
            repository_menu_button.add_css_class (
                "atm-triangle-selector"
            );
            repository_menu_button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Select repository context"
            );
            repository_menu_button.set_popover (popover);

            return build_selector_triangle_overlay (
                repository_menu_button
            );
        }

        private bool system_prefers_dark () {
            return granite_settings.prefers_color_scheme ==
                Granite.Settings.ColorScheme.DARK;
        }

        private void apply_system_style () {
            bool use_dark = system_prefers_dark ();
            gtk_settings.gtk_application_prefer_dark_theme = use_dark;

            var main_window = this.active_window as Gtk.ApplicationWindow;
            if (main_window != null) {
                if (use_dark) {
                    main_window.add_css_class ("atm-dark");
                } else {
                    main_window.remove_css_class ("atm-dark");
                }
            }
        }

        protected override void shutdown () {
            if (conversation_store != null) {
                try {
                    conversation_store.discard_unarchived_conversations ();
                } catch (GLib.Error error) {
                    stderr.printf (
                        "AtM: temporary conversation cleanup on shutdown failed: %s\n",
                        error.message
                    );
                }
            }

            base.shutdown ();
        }

        protected override void activate () {
            var main_window = this.active_window as Gtk.ApplicationWindow;

            if (main_window == null) {
                main_window = new Gtk.ApplicationWindow (this) {
                    title = "Ask the Model",
                    default_width = 780,
                    default_height = 700,
                    titlebar = build_titlebar (),
                    child = build_main_content ()
                };
                main_window.add_css_class ("atm-window");

                if (system_prefers_dark ()) {
                    main_window.add_css_class ("atm-dark");
                }

                update_model_selector ();
            }

            main_window.present ();
        }

        private void update_optimization_control () {
            if (optimization_switch == null) {
                return;
            }

            bool enabled = optimization_policy.enabled;

            if (optimization_switch.active != enabled) {
                optimization_switch.active = enabled;
            }

            optimization_switch.tooltip_text = enabled
                ? "Optimizations ON — experimental optimized runtime behavior"
                : "Optimizations OFF — baseline runtime behavior";

            optimization_switch.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Optimization mode",
                Gtk.AccessibleProperty.DESCRIPTION,
                enabled
                    ? "Optimizations on. Experimental optimized runtime behavior."
                    : "Optimizations off. Baseline runtime behavior."
            );

            if (optimization_lcd_annunciator != null) {
                optimization_lcd_annunciator.label =
                    enabled ? "OPT ON" : "OPT OFF";
                set_annunciator (
                    optimization_lcd_annunciator,
                    true
                );
            }

            update_status_lcd_accessibility ();
        }

        private Gtk.Widget build_titlebar () {
            var headerbar = new Gtk.HeaderBar () {
                show_title_buttons = true
            };

            headerbar.set_decoration_layout (":minimize,maximize,close");

            string[] initial_items = { "Detecting models…" };
            model_list = new Gtk.StringList (initial_items);

            model_dropdown = new Gtk.DropDown (null, null) {
                sensitive = false,
                show_arrow = false,
                tooltip_text = "Select local AI model"
            };
            model_dropdown.add_css_class (
                "atm-triangle-selector"
            );
            model_dropdown.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Local AI model"
            );
            model_dropdown.set_model (model_list);
            model_dropdown.notify["selected"].connect (() => {
                if (updating_model_selector ||
                    model_list == null ||
                    model_dropdown == null) {
                    return;
                }

                uint position = model_dropdown.get_selected ();
                string? selected_model = model_list.get_string (position);

                if (selected_model == null) {
                    return;
                }

                if (ollama_provider.select_model (selected_model)) {
                    if (active_chat != null &&
                        !active_chat.locked &&
                        !restoring_chat_controls) {
                        active_chat.model_name = selected_model;
                        active_chat.model_digest =
                            ollama_provider.model_digest;
                    }

                    show_model_standby_status ();
                    stdout.printf (
                        "AtM: selected model %s\n",
                        selected_model
                    );
                }
            });

            refresh_models_button =
                new Gtk.Button.from_icon_name ("view-refresh-symbolic") {
                    tooltip_text = "Refresh models"
                };
            refresh_models_button.add_css_class ("circular");
            refresh_models_button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Refresh local AI models"
            );
            refresh_models_button.clicked.connect (() => {
                refresh_local_models.begin ();
            });

            var model_controls = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                6
            );
            refresh_models_ring = new ActivityRing ();

            model_controls.append (
                build_selector_triangle_overlay (
                    model_dropdown
                )
            );
            model_controls.append (
                build_activity_overlay (
                    refresh_models_button,
                    refresh_models_ring
                )
            );

            refresh_repositories_button =
                new Gtk.Button.from_icon_name ("view-refresh-symbolic") {
                    tooltip_text = "Refresh repositories",
                    sensitive = false
                };
            refresh_repositories_button.add_css_class ("circular");
            refresh_repositories_button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Check selected repositories"
            );
            refresh_repositories_button.clicked.connect (() => {
                refresh_selected_repositories.begin ();
            });

            repository_action_button =
                new Gtk.Button.from_icon_name ("folder-download-symbolic") {
                    sensitive = false,
                    can_target = false,
                    opacity = 0.0
                };
            repository_action_button.update_state (
                Gtk.AccessibleState.HIDDEN,
                true
            );
            repository_action_button.add_css_class ("circular");
            repository_action_button.clicked.connect (() => {
                download_or_update_selected_repositories.begin ();
            });

            var repository_controls = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                6
            );
            refresh_repositories_ring = new ActivityRing ();
            repository_action_ring = new ActivityRing ();

            repository_controls.append (build_repository_selector ());
            repository_controls.append (
                build_activity_overlay (
                    refresh_repositories_button,
                    refresh_repositories_ring
                )
            );
            repository_controls.append (
                build_activity_overlay (
                    repository_action_button,
                    repository_action_ring
                )
            );

            var header_controls = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                12
            );
            header_controls.append (model_controls);
            header_controls.append (repository_controls);

            headerbar.pack_start (header_controls);

            var optimization_title_label =
                new Gtk.Label ("Optimizations") {
                    valign = Gtk.Align.CENTER
                };
            optimization_title_label.add_css_class (
                "atm-optimization-title"
            );
            optimization_title_label.update_state (
                Gtk.AccessibleState.HIDDEN,
                true
            );

            var optimization_mode_switch =
                new Gtk.Switch () {
                    active = false,
                    valign = Gtk.Align.CENTER,
                    tooltip_text =
                        "Optimizations OFF — baseline runtime behavior"
                };
            optimization_mode_switch.add_css_class (
                "atm-optimization-switch"
            );

            optimization_switch =
                optimization_mode_switch;

            optimization_mode_switch
                .notify["active"].connect (() => {
                    optimization_policy.set_enabled_for_session (
                        optimization_mode_switch.active
                    );
                    update_optimization_control ();
                });

            var optimization_controls = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                4
            ) {
                valign = Gtk.Align.CENTER
            };
            optimization_controls.add_css_class (
                "atm-optimization-controls"
            );
            optimization_controls.append (
                optimization_title_label
            );
            optimization_controls.append (
                optimization_mode_switch
            );

            headerbar.pack_end (
                optimization_controls
            );
            update_optimization_control ();

            return headerbar;
        }

        private void append_transcript (
            Gtk.TextView transcript,
            string entry
        ) {
            Gtk.TextBuffer buffer = transcript.buffer;
            Gtk.TextIter end;
            buffer.get_end_iter (out end);

            if (buffer.get_char_count () > 0) {
                buffer.insert (
                    ref end,
                    "\n\n",
                    -1
                );
            }

            buffer.insert (
                ref end,
                entry,
                -1
            );
        }

        private void append_transcript_raw (
            Gtk.TextView transcript,
            string text
        ) {
            Gtk.TextIter end;
            transcript.buffer.get_end_iter (out end);
            transcript.buffer.insert (
                ref end,
                text,
                -1
            );
        }

        private string strip_repository_source_labels (
            string answer
        ) throws GLib.Error {
            var label_regex = new GLib.Regex (
                "[ \\t]*\\[S[1-9][0-9]{0,3}\\]"
            );

            return label_regex.replace_literal (
                answer,
                -1,
                0,
                ""
            ).strip ();
        }

        private string markdown_excerpt_to_display_text (
            string markdown
        ) {
            string text = markdown;

            try {
                var comments = new GLib.Regex (
                    "<!--.*?-->",
                    GLib.RegexCompileFlags.DOTALL
                );
                text = comments.replace_literal (
                    text, -1, 0, ""
                );

                var html_image = new GLib.Regex (
                    "(?is)<img\\b[^>]*\\balt\\s*=\\s*[\"']([^\"']*)[\"'][^>]*>"
                );
                text = html_image.replace (
                    text, -1, 0, "\\1"
                );

                var html_break = new GLib.Regex (
                    "(?i)<br\\s*/?>"
                );
                text = html_break.replace_literal (
                    text, -1, 0, "\n"
                );

                var html_block = new GLib.Regex (
                    "(?i)</?(p|div|h[1-6]|li|ul|ol|blockquote|pre|table|tr|section|article|details|summary)[^>]*>"
                );
                text = html_block.replace_literal (
                    text, -1, 0, "\n"
                );

                var html_tag = new GLib.Regex (
                    "<[^>]+>"
                );
                text = html_tag.replace_literal (
                    text, -1, 0, ""
                );

                var md_image = new GLib.Regex (
                    "!\\[([^\\]]*)\\]\\([^)]*\\)"
                );
                text = md_image.replace (
                    text, -1, 0, "\\1"
                );

                var md_link = new GLib.Regex (
                    "\\[([^\\]]+)\\]\\([^)]*\\)"
                );
                text = md_link.replace (
                    text, -1, 0, "\\1"
                );

                var heading = new GLib.Regex (
                    "(?m)^\\s{0,3}#{1,6}\\s+"
                );
                text = heading.replace_literal (
                    text, -1, 0, ""
                );

                var blockquote = new GLib.Regex (
                    "(?m)^\\s{0,3}>\\s?"
                );
                text = blockquote.replace_literal (
                    text, -1, 0, ""
                );

                var bullet = new GLib.Regex (
                    "(?m)^\\s*[-+*]\\s+"
                );
                text = bullet.replace_literal (
                    text, -1, 0, "• "
                );

                var bold_star = new GLib.Regex (
                    "\\*\\*([^*\\n]+)\\*\\*"
                );
                text = bold_star.replace (
                    text, -1, 0, "\\1"
                );

                var bold_underscore = new GLib.Regex (
                    "__([^_\\n]+)__"
                );
                text = bold_underscore.replace (
                    text, -1, 0, "\\1"
                );

                var many_blank_lines = new GLib.Regex (
                    "\\n[ \\t]*\\n(?:[ \\t]*\\n)+"
                );
                text = many_blank_lines.replace_literal (
                    text, -1, 0, "\n\n"
                );
            } catch (GLib.RegexError error) {
                return markdown.strip ();
            }

            text = text.replace ("&nbsp;", " ");
            text = text.replace ("&amp;", "&");
            text = text.replace ("&lt;", "<");
            text = text.replace ("&gt;", ">");

            return text.strip ();
        }

        private string source_excerpt_display_text (
            CitationReference citation
        ) {
            if (citation.excerpt == null) {
                return "";
            }

            string path = citation.source_path.down ();

            if (path.has_suffix (".md") ||
                path.has_suffix (".markdown")) {
                return markdown_excerpt_to_display_text (
                    citation.excerpt
                );
            }

            return citation.excerpt;
        }

        private Gtk.Widget build_source_detail_content (
            CitationReference citation,
            uint display_number
        ) {
            var content = new Gtk.Box (
                Gtk.Orientation.VERTICAL,
                6
            ) {
                margin_top = 10,
                margin_bottom = 10,
                margin_start = 12,
                margin_end = 12
            };

            RepositoryDescriptor? descriptor =
                repository_descriptor_for_id (
                    citation.repository_id
                );
            string repository_name =
                descriptor != null
                    ? "%s (%s)".printf (
                        descriptor.acronym,
                        descriptor.display_name
                    )
                    : citation.repository_id;

            var heading = new Gtk.Label (
                "[%u] %s · v%s".printf (
                    display_number,
                    repository_name,
                    citation.repository_version
                )
            ) {
                halign = Gtk.Align.START,
                xalign = 0.0f,
                selectable = true
            };
            heading.add_css_class ("heading");
            content.append (heading);

            var source = new Gtk.Label (
                "%s · %s".printf (
                    citation.source_path,
                    citation.locator
                )
            ) {
                halign = Gtk.Align.START,
                xalign = 0.0f,
                selectable = true,
                wrap = true,
                max_width_chars = 72
            };
            source.add_css_class ("dim-label");
            content.append (source);

            var sha = new Gtk.Label (
                "Snapshot: " + citation.snapshot_sha
            ) {
                halign = Gtk.Align.START,
                xalign = 0.0f,
                selectable = true,
                wrap = true,
                max_width_chars = 72
            };
            sha.add_css_class ("dim-label");
            content.append (sha);

            var logical_source = new Gtk.Label (
                "Source ID: " + citation.logical_source_id
            ) {
                halign = Gtk.Align.START,
                xalign = 0.0f,
                selectable = true,
                wrap = true,
                max_width_chars = 72
            };
            logical_source.add_css_class ("dim-label");
            content.append (logical_source);

            if (citation.title != null &&
                citation.title.strip ().length > 0) {
                var title = new Gtk.Label (
                    citation.title
                ) {
                    halign = Gtk.Align.START,
                    xalign = 0.0f,
                    selectable = true,
                    wrap = true,
                    max_width_chars = 72
                };
                content.append (title);
            }

            if (citation.excerpt != null &&
                citation.excerpt.strip ().length > 0) {
                string display_excerpt =
                    source_excerpt_display_text (citation);

                var excerpt = new Gtk.Label (
                    display_excerpt
                ) {
                    halign = Gtk.Align.START,
                    xalign = 0.0f,
                    selectable = true,
                    wrap = true,
                    max_width_chars = 72
                };
                excerpt.add_css_class ("atm-source-excerpt");
                content.append (excerpt);
            }

            string? permalink =
                citation.immutable_permalink;

            if ((permalink == null ||
                 permalink.strip ().length == 0) &&
                descriptor != null) {
                try {
                    permalink =
                        descriptor.immutable_file_permalink (
                            citation.snapshot_sha,
                            citation.source_path,
                            citation.locator
                        );
                } catch (GLib.Error error) {
                    permalink = null;
                }
            }

            if (permalink != null &&
                permalink.strip ().length > 0) {
                var link =
                    new Gtk.LinkButton.with_label (
                        permalink,
                        "Open immutable source"
                    ) {
                        halign = Gtk.Align.START
                    };
                link.add_css_class ("atm-source-link");
                content.append (link);
            }

            return content;
        }

        private void show_source_detail_window (
            CitationReference citation,
            uint display_number
        ) {
            var scroller = new Gtk.ScrolledWindow () {
                hscrollbar_policy = Gtk.PolicyType.NEVER,
                vscrollbar_policy = Gtk.PolicyType.AUTOMATIC,
                min_content_width = 520,
                min_content_height = 240,
                max_content_height = 520,
                propagate_natural_height = true
            };
            scroller.set_child (
                build_source_detail_content (
                    citation,
                    display_number
                )
            );

            var source_window = new Gtk.Window () {
                application = this,
                title = "Source [%u]".printf (display_number),
                transient_for = this.active_window,
                modal = false,
                destroy_with_parent = true,
                resizable = true,
                default_width = 620,
                default_height = 360,
                child = scroller
            };
            source_window.present ();
        }

        private Gtk.Button build_source_reference_button (
            CitationReference citation,
            uint display_number
        ) {
            var button = new Gtk.Button () {
                label = "[%u]".printf (display_number),
                has_frame = false,
                tooltip_text = "Show source %u".printf (
                    display_number
                )
            };
            button.add_css_class ("atm-source-ref");
            button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Source %u".printf (display_number)
            );
            button.clicked.connect (() => {
                show_source_detail_window (
                    citation,
                    display_number
                );
            });
            return button;
        }

        private void append_grounded_answer (
            Gtk.TextView transcript,
            string visible_answer,
            CitationResolution resolution
        ) {
            append_transcript (
                transcript,
                "Assistant: " + visible_answer
            );

            if (resolution.citation_count () == 0) {
                return;
            }

            Gtk.TextBuffer buffer = transcript.buffer;
            Gtk.TextIter end;
            buffer.get_end_iter (out end);
            buffer.insert (
                ref end,
                "\nSources: ",
                -1
            );

            for (
                uint i = 0;
                i < resolution.citation_count ();
                i++
            ) {
                CitationReference? citation =
                    resolution.citation_at (i);

                if (citation == null) {
                    continue;
                }

                buffer.get_end_iter (out end);
                unowned Gtk.TextChildAnchor anchor =
                    buffer.create_child_anchor (end);

                transcript.add_child_at_anchor (
                    build_source_reference_button (
                        citation,
                        i + 1
                    ),
                    anchor
                );

                if (i + 1 < resolution.citation_count ()) {
                    buffer.get_end_iter (out end);
                    buffer.insert (
                        ref end,
                        " ",
                        -1
                    );
                }
            }
        }

        private string normalize_conversation_title (
            string generated
        ) {
            string cleaned = generated
                .replace ("\n", " ")
                .replace ("\r", " ")
                .replace ("\t", " ")
                .strip ();

            while (
                cleaned.length >= 2 &&
                (
                    (cleaned.has_prefix ("\"") &&
                     cleaned.has_suffix ("\"")) ||
                    (cleaned.has_prefix ("'") &&
                     cleaned.has_suffix ("'"))
                )
            ) {
                cleaned = cleaned.substring (
                    1,
                    cleaned.length - 2
                ).strip ();
            }

            string[] parts = cleaned.split (" ");
            string[] words = {};

            foreach (string part in parts) {
                string word = part.strip ();
                if (word.length == 0) {
                    continue;
                }

                words += word;
                if (words.length == 3) {
                    break;
                }
            }

            if (words.length == 0) {
                return "New";
            }

            return string.joinv (" ", words);
        }

        private bool ensure_persistent_conversation (
            ChatTabState state
        ) throws GLib.Error {
            if (state.persistent_id != null) {
                return true;
            }

            if (conversation_store == null) {
                if (!state.unsaved_notice_shown) {
                    append_transcript (
                        state.transcript,
                        "System: Conversation persistence is unavailable; this chat is running unsaved for the current application session."
                    );
                    state.unsaved_notice_shown = true;
                }

                return false;
            }

            if (!state.session.is_active () ||
                state.session.model_name () == null) {
                throw new GLib.IOError.FAILED (
                    "Conversation persistence requires an active pinned session."
                );
            }

            ConversationPersistenceRepository[] repositories = {};

            for (
                uint i = 0;
                i < state.session.repository_count ();
                i++
            ) {
                ConversationRepositoryPin? pin =
                    state.session.repository_pin_at (i);

                if (pin == null) {
                    throw new GLib.IOError.INVALID_DATA (
                        "Pinned repository identity is unavailable for conversation persistence."
                    );
                }

                repositories +=
                    new ConversationPersistenceRepository (
                        pin.repository_id,
                        pin.repository_version,
                        pin.snapshot_sha
                    );
            }

            state.persistent_id =
                conversation_store.create_conversation (
                    state.title_label.label,
                    GLib.get_real_time (),
                    state.session.model_name () ?? "",
                    state.session.model_digest (),
                    state.session.repository_generation_id (),
                    repositories
                );

            return true;
        }

        private string? immutable_permalink_for_citation (
            CitationReference citation
        ) {
            foreach (
                RepositoryDescriptor descriptor
                in RepositoryCatalog.all ()
            ) {
                if (descriptor.id != citation.repository_id) {
                    continue;
                }

                try {
                    return descriptor.immutable_file_permalink (
                        citation.snapshot_sha,
                        citation.source_path,
                        citation.locator
                    );
                } catch (GLib.Error error) {
                    return null;
                }
            }

            return null;
        }

        private ConversationPersistenceCitation[]
        persistence_citations (
            CitationResolution resolution
        ) throws GLib.Error {
            ConversationPersistenceCitation[] citations = {};

            for (
                uint i = 0;
                i < resolution.citation_count ();
                i++
            ) {
                CitationReference? citation =
                    resolution.citation_at (i);

                if (citation == null) {
                    throw new GLib.IOError.INVALID_DATA (
                        "Resolved citation is unavailable for durable persistence."
                    );
                }

                citations +=
                    new ConversationPersistenceCitation (
                        citation.label,
                        citation.repository_id,
                        citation.repository_version,
                        citation.snapshot_sha,
                        citation.logical_source_id,
                        citation.source_path,
                        citation.locator,
                        citation.title,
                        citation.excerpt,
                        immutable_permalink_for_citation (
                            citation
                        )
                    );
            }

            return citations;
        }

        private async void update_conversation_title (
            ChatTabState state,
            string first_prompt,
            string first_answer,
            uint serial
        ) {
            try {
                string answer_excerpt = first_answer;
                if (answer_excerpt.length > 1200) {
                    answer_excerpt =
                        answer_excerpt.substring (0, 1200);
                }

                string topic_text =
                    "User: " + first_prompt +
                    "\nAssistant: " + answer_excerpt;

                string generated =
                    yield ollama_provider.generate_conversation_title (
                        topic_text,
                        state.model_name
                    );

                if (!chat_state_is_open (state) ||
                    state.serial != serial) {
                    return;
                }

                string title =
                    normalize_conversation_title (generated);

                state.title_label.label = title;
                state.title_label.tooltip_text = title;

                if (conversation_store != null &&
                    state.persistent_id != null) {
                    try {
                        conversation_store.update_title (
                            state.persistent_id,
                            title,
                            GLib.get_real_time ()
                        );
                    } catch (GLib.Error error) {
                        /* Title persistence is cosmetic; the turn remains committed. */
                    }
                }
            } catch (GLib.Error error) {
                /* Title generation is cosmetic; keep New on failure. */
            }
        }

        private async void send_prompt (
            ChatTabState state,
            string prompt
        ) {
            generation_active = true;
            state.generating = true;
            streaming_transcript = null;
            assistant_stream_started = false;
            update_conversation_ui_state ();

            uint serial = state.serial;
            bool should_generate_title =
                state.title_label.label == "New";
            bool grounded_turn_prepared = false;

            try {
                if (state.persistence_failed) {
                    throw new GLib.IOError.FAILED (
                        "This conversation cannot continue because its durable state did not commit after the grounded retrieval state advanced."
                    );
                }

                if (state.model_name == null ||
                    state.model_name.strip ().length == 0) {
                    throw new ConversationSessionError.INVALID_MODEL (
                        "No local AI model is selected for this conversation."
                    );
                }

                if (!ollama_provider.select_model (
                        state.model_name
                    )) {
                    throw new ConversationSessionError.INVALID_MODEL (
                        "The AI model pinned to this conversation is not currently available."
                    );
                }

                if (!state.session.is_active ()) {
                    RepositoryDescriptor[] selected =
                        repository_descriptors_for_ids (
                            state.repository_ids
                        );

                    ConversationGrounding grounding =
                        yield repository_lifecycle.prepare_conversation_grounding (
                            selected
                        );

                    state.session.begin (
                        grounding,
                        state.model_name,
                        state.model_digest
                    );
                }

                if (!ollama_provider.select_model (
                        state.model_name
                    )) {
                    throw new ConversationSessionError.INVALID_MODEL (
                        "The AI model pinned to this conversation is not currently available."
                    );
                }

                state.session.require_model (
                    state.model_name,
                    ollama_provider.model_digest
                );

                bool needs_clarification;
                string? system_instructions;
                string? evidence_text;
                string? post_evidence_reminder;

                bool has_grounding =
                    state.session.prepare_turn (
                        prompt,
                        out needs_clarification,
                        out system_instructions,
                        out evidence_text,
                        out post_evidence_reminder
                    );

                if (needs_clarification) {
                    append_transcript (
                        state.transcript,
                        "Assistant: Please restate the question with the repository, variable, source, or topic you mean."
                    );
                } else {
                    string answer;
                    string title_answer;

                    if (has_grounding) {
                        grounded_turn_prepared = true;
                        answer = yield ollama_provider.chat_grounded (
                            prompt,
                            system_instructions ?? "",
                            evidence_text ?? "",
                            post_evidence_reminder ?? "",
                            state.conversation,
                            false
                        );

                        CitationResolution citation_resolution =
                            state.session.resolve_turn_citations (
                                answer
                            );

                        if (citation_resolution.unknown_label_count () > 0) {
                            string unknown =
                                citation_resolution.unknown_label_at (0) ??
                                "unknown";

                            throw new ConversationSessionError.INVALID_GROUNDING (
                                "Grounded response used an unknown source label: %s".printf (
                                    unknown
                                )
                            );
                        }

                        string visible_answer =
                            strip_repository_source_labels (
                                answer
                            );

                        ensure_persistent_conversation (
                            state
                        );

                        if (!state.session.commit_turn ()) {
                            throw new ConversationSessionError.INVALID_GROUNDING (
                                "Grounded conversation turn could not be committed."
                            );
                        }

                        grounded_turn_prepared = false;

                        try {
                            ConversationPersistenceCitation[] citations =
                                persistence_citations (
                                    citation_resolution
                                );

                            ConversationTurnCommitter.commit (
                                conversation_store,
                                state.persistent_id,
                                state.conversation,
                                prompt,
                                answer,
                                visible_answer,
                                true,
                                GLib.get_real_time (),
                                citations
                            );
                        } catch (GLib.Error error) {
                            state.persistence_failed = true;
                            throw error;
                        }

                        state.grounded_answers += answer;
                        state.grounded_citations +=
                            citation_resolution;

                        append_grounded_answer (
                            state.transcript,
                            visible_answer,
                            citation_resolution
                        );
                        assistant_stream_started = true;
                        title_answer = visible_answer;
                    } else {
                        answer = yield ollama_provider.chat (
                            prompt,
                            state.conversation,
                            false
                        );

                        ensure_persistent_conversation (
                            state
                        );

                        ConversationTurnCommitter.commit (
                            conversation_store,
                            state.persistent_id,
                            state.conversation,
                            prompt,
                            answer,
                            answer,
                            false,
                            GLib.get_real_time (),
                            {}
                        );
                        title_answer = answer;

                        if (answer.length > 0) {
                            append_transcript (
                                state.transcript,
                                "Assistant: " + answer
                            );
                            assistant_stream_started = true;
                        }
                    }

                    if (should_generate_title &&
                        title_answer.length > 0) {
                        update_conversation_title.begin (
                            state,
                            prompt,
                            title_answer,
                            serial
                        );
                    }
                }
            } catch (GLib.Error error) {
                if (grounded_turn_prepared) {
                    state.session.abort_turn ();
                }

                if (!state.session.is_active ()) {
                    state.locked = false;
                }

                append_transcript (
                    state.transcript,
                    "System: " + error.message
                );
            }

            if (streaming_transcript == state.transcript) {
                streaming_transcript = null;
            }

            state.generating = false;
            generation_active = false;

            if (active_chat != null) {
                restore_chat_controls (active_chat);
            }

            update_conversation_ui_state ();

            if (chat_state_is_open (state) &&
                active_chat == state) {
                state.prompt.grab_focus ();
            }

            restore_durable_conversations_if_ready ();
            qualify_restored_conversations.begin ();
        }

        private Gtk.Widget build_chat_tab_label (
            Gtk.Label title_label,
            Gtk.Button lifecycle_button,
            Gtk.Button close_button
        ) {
            var box = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                4
            ) {
                valign = Gtk.Align.CENTER
            };

            box.append (title_label);
            box.append (lifecycle_button);
            box.append (close_button);
            return box;
        }

        private void create_chat_tab () {
            if (chat_notebook == null ||
                generation_active) {
                return;
            }

            var transcript = new Gtk.TextView () {
                editable = false,
                cursor_visible = false,
                monospace = true,
                wrap_mode = Gtk.WrapMode.WORD_CHAR,
                left_margin = 12,
                right_margin = 12,
                top_margin = 8,
                bottom_margin = 12,
                vexpand = true
            };

            var transcript_scroll = new Gtk.ScrolledWindow () {
                child = transcript,
                hscrollbar_policy = Gtk.PolicyType.NEVER,
                vscrollbar_policy = Gtk.PolicyType.AUTOMATIC,
                vexpand = true
            };

            var prompt_view = new Gtk.TextView () {
                monospace = true,
                wrap_mode = Gtk.WrapMode.WORD_CHAR,
                accepts_tab = false,
                left_margin = 10,
                right_margin = 10,
                top_margin = 10,
                bottom_margin = 10,
                height_request = 72,
                hexpand = true
            };

            var prompt_overlay = new Gtk.Overlay () {
                child = prompt_view
            };

            var prompt_placeholder = new Gtk.Label (
                "Ask something…"
            ) {
                halign = Gtk.Align.START,
                valign = Gtk.Align.START,
                margin_start = 14,
                margin_top = 12,
                can_target = false
            };
            prompt_placeholder.add_css_class ("dim-label");
            prompt_placeholder.add_css_class ("monospace");
            prompt_overlay.add_overlay (prompt_placeholder);

            var prompt_frame = new Gtk.Frame (null) {
                child = prompt_overlay,
                hexpand = true
            };
            prompt_frame.add_css_class ("atm-input-frame");

            var send_button = new Gtk.Button.with_label (
                "Send"
            ) {
                valign = Gtk.Align.END,
                sensitive = false
            };

            var composer = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                8
            ) {
                margin_top = 12,
                margin_bottom = 12,
                margin_start = 12,
                margin_end = 12
            };
            composer.append (prompt_frame);
            composer.append (send_button);

            var chat_page = new Gtk.Box (
                Gtk.Orientation.VERTICAL,
                0
            ) {
                hexpand = true,
                vexpand = true
            };
            chat_page.append (transcript_scroll);
            chat_page.append (composer);

            var title_label = new Gtk.Label ("New") {
                single_line_mode = true,
                ellipsize = Pango.EllipsizeMode.NONE
            };

            var lifecycle_button =
                new Gtk.Button.from_icon_name (
                    "document-save-symbolic"
                ) {
                    tooltip_text = "Save to History",
                    valign = Gtk.Align.CENTER,
                    sensitive = false
                };
            lifecycle_button.add_css_class ("flat");
            lifecycle_button.add_css_class (
                "atm-tab-lifecycle"
            );
            lifecycle_button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Save to History"
            );

            var close_button =
                new Gtk.Button.from_icon_name (
                    "window-close-symbolic"
                ) {
                    tooltip_text = "Close Chat",
                    valign = Gtk.Align.CENTER
                };
            close_button.add_css_class ("flat");
            close_button.add_css_class ("atm-tab-close");
            close_button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Close Chat"
            );

            conversation_serial++;
            var state = new ChatTabState (
                chat_page,
                transcript,
                prompt_view,
                send_button,
                prompt_placeholder,
                title_label,
                lifecycle_button,
                close_button,
                ollama_provider.create_conversation (),
                conversation_serial
            );
            state.model_name = ollama_provider.model_name;
            state.model_digest = ollama_provider.model_digest;
            state.repository_ids =
                selected_repository_ids ();

            configure_conversation_archive_button (
                state
            );

            prompt_view.buffer.changed.connect (() => {
                prompt_placeholder.visible =
                    prompt_view.buffer.get_char_count () == 0;

                send_button.sensitive =
                    !generation_active &&
                    !state.persistence_failed &&
                    prompt_view.sensitive &&
                    prompt_view.buffer.text.strip ().length > 0;
            });

            send_button.clicked.connect (() => {
                if (generation_active ||
                    state.persistence_failed) {
                    return;
                }

                string prompt =
                    prompt_view.buffer.text.strip ();
                if (prompt.length == 0) {
                    return;
                }

                if (!state.locked) {
                    state.model_name =
                        ollama_provider.model_name;
                    state.model_digest =
                        ollama_provider.model_digest;
                    state.repository_ids =
                        selected_repository_ids ();
                    state.locked = true;
                    update_conversation_ui_state ();
                }

                append_transcript (
                    transcript,
                    "You: " + prompt
                );

                prompt_view.buffer.text = "";
                send_button.sensitive = false;

                send_prompt.begin (
                    state,
                    prompt
                );
            });

            close_button.clicked.connect (() => {
                close_chat_tab (state);
            });

            Gtk.Widget tab_label =
                build_chat_tab_label (
                    title_label,
                    lifecycle_button,
                    close_button
                );

            int page_num = chat_notebook.append_page (
                chat_page,
                tab_label
            );
            chat_states += state;
            chat_notebook.set_current_page (page_num);
            activate_chat_state (state);
        }

        private bool chat_state_is_pristine_new (
            ChatTabState state
        ) {
            return state.persistent_id == null &&
                !state.locked &&
                !state.generating &&
                !state.persistence_failed &&
                state.title_label.label == "New" &&
                state.conversation.message_count () == 0 &&
                state.transcript.buffer.get_char_count () == 0 &&
                state.prompt.buffer.get_char_count () == 0;
        }

        private string[] repository_ids_from_snapshot (
            ConversationPersistenceSnapshot snapshot
        ) {
            string[] result = {};

            foreach (
                ConversationPersistenceRepository repository
                in snapshot.repositories
            ) {
                result += repository.repository_id;
            }

            return result;
        }

        private CitationResolution
        citation_resolution_from_persistence_message (
            ConversationPersistenceMessage message
        ) {
            var resolution = new CitationResolution ();

            foreach (
                ConversationPersistenceCitation citation
                in message.citations
            ) {
                resolution.add_citation (
                    new CitationReference (
                        citation.label,
                        citation.repository_id,
                        citation.repository_version,
                        citation.snapshot_sha,
                        citation.logical_source_id,
                        citation.source_path,
                        citation.locator,
                        citation.title,
                        citation.excerpt,
                        citation.immutable_permalink
                    )
                );
            }

            return resolution;
        }

        private void render_restored_snapshot (
            ChatTabState state,
            ConversationPersistenceSnapshot snapshot
        ) throws GLib.Error {
            for (
                uint i = 0;
                i < snapshot.messages.length;
                i++
            ) {
                ConversationPersistenceMessage message =
                    snapshot.messages[i];

                if (message.role == "user") {
                    append_transcript (
                        state.transcript,
                        "You: " + message.display_content
                    );
                    continue;
                }

                if (message.role != "assistant") {
                    throw new GLib.IOError.INVALID_DATA (
                        "Durable conversation contains an unsupported message role."
                    );
                }

                if (message.grounded) {
                    CitationResolution resolution =
                        citation_resolution_from_persistence_message (
                            message
                        );

                    append_grounded_answer (
                        state.transcript,
                        message.display_content,
                        resolution
                    );

                    state.grounded_answers +=
                        message.provider_content;
                    state.grounded_citations +=
                        resolution;
                } else {
                    append_transcript (
                        state.transcript,
                        "Assistant: " + message.display_content
                    );
                }
            }
        }

        private ConversationPersistenceRepository[]
        persistence_repositories_from_grounding (
            ConversationGrounding grounding
        ) throws GLib.Error {
            ConversationPersistenceRepository[] repositories = {};

            for (
                uint i = 0;
                i < grounding.repository_count ();
                i++
            ) {
                ConversationRepositoryPin? pin =
                    grounding.repository_pin_at (i);

                if (pin == null) {
                    throw new GLib.IOError.INVALID_DATA (
                        "Qualified historical repository pin is unavailable."
                    );
                }

                repositories +=
                    new ConversationPersistenceRepository (
                        pin.repository_id,
                        pin.repository_version,
                        pin.snapshot_sha
                    );
            }

            return repositories;
        }

        private void keep_restored_state_read_only (
            ChatTabState state,
            string reason
        ) {
            state.restored_view_only = true;
            state.restored_read_only_reason = reason;
            state.placeholder.label =
                "Read-only: " + reason;
        }

        private async bool qualify_restored_chat_state (
            ChatTabState state
        ) throws GLib.Error {
            if (!chat_state_is_open (state) ||
                !state.restored_view_only ||
                state.persistence_failed ||
                state.persistent_id == null ||
                conversation_store == null) {
                return false;
            }

            string persistent_id =
                state.persistent_id ?? "";

            if (persistent_id.length == 0) {
                return false;
            }

            ConversationPersistenceSnapshot snapshot =
                conversation_store.load_snapshot (
                    persistent_id
                );

            string[] available_models =
                ollama_provider.get_completion_models ();
            string[] available_digests =
                ollama_provider.get_completion_model_digests ();

            if (!snapshot.model_identity_available (
                    available_models,
                    available_digests
                )) {
                throw new GLib.IOError.NOT_FOUND (
                    "exact pinned AI model is unavailable"
                );
            }

            string[] repository_ids =
                repository_ids_from_snapshot (
                    snapshot
                );
            RepositoryDescriptor[] descriptors =
                repository_descriptors_for_ids (
                    repository_ids
                );

            if (descriptors.length !=
                snapshot.repositories.length) {
                throw new GLib.IOError.INVALID_DATA (
                    "durable repository scope contains an unknown repository"
                );
            }

            ConversationGrounding grounding =
                yield repository_lifecycle
                    .prepare_conversation_grounding_at_generation (
                        descriptors,
                        snapshot.repository_generation_id
                    );

            if (!chat_state_is_open (state) ||
                state.persistent_id != persistent_id ||
                snapshot.conversation_id != persistent_id ||
                !state.restored_view_only ||
                generation_active) {
                return false;
            }

            ConversationPersistenceRepository[]
                qualified_repositories =
                    persistence_repositories_from_grounding (
                        grounding
                    );

            snapshot.require_continuation_identity (
                available_models,
                available_digests,
                grounding.repository_generation_id (),
                qualified_repositories
            );

            snapshot.restore_provider_history (
                state.conversation
            );

            state.session.reset ();
            state.session.begin (
                grounding,
                snapshot.model_name,
                snapshot.model_digest
            );
            state.session.require_model (
                snapshot.model_name,
                snapshot.model_digest
            );

            state.model_name = snapshot.model_name;
            state.model_digest = snapshot.model_digest;
            state.repository_ids = repository_ids;
            state.locked = true;
            state.restored_view_only = false;
            state.restored_read_only_reason = null;
            state.placeholder.label = "Ask something…";

            return true;
        }

        private async void qualify_restored_conversations () {
            if (restored_qualification_running) {
                restored_qualification_pending = true;
                return;
            }

            if (generation_active ||
                conversation_store == null) {
                return;
            }

            restored_qualification_running = true;
            restored_qualification_pending = false;
            ChatTabState[] candidates = {};

            foreach (ChatTabState state in chat_states) {
                if (state.restored_view_only &&
                    !state.persistence_failed &&
                    state.persistent_id != null) {
                    candidates += state;
                }
            }

            foreach (ChatTabState state in candidates) {
                if (!chat_state_is_open (state)) {
                    continue;
                }

                try {
                    bool qualified =
                        yield qualify_restored_chat_state (
                            state
                        );

                    if (qualified) {
                        stdout.printf (
                            "AtM: restored conversation %s qualified for continuation\n",
                            state.persistent_id ?? "unknown"
                        );
                    }
                } catch (GLib.Error error) {
                    if (chat_state_is_open (state) &&
                        state.restored_view_only) {
                        keep_restored_state_read_only (
                            state,
                            error.message
                        );
                    }

                    stdout.printf (
                        "AtM: restored conversation %s remains read-only: %s\n",
                        state.persistent_id ?? "unknown",
                        error.message
                    );
                }
            }

            restored_qualification_running = false;
            update_conversation_ui_state ();

            if (active_chat != null) {
                restore_chat_controls (
                    active_chat
                );
            }

            bool retry =
                restored_qualification_pending &&
                !generation_active;
            restored_qualification_pending = false;

            if (retry) {
                qualify_restored_conversations.begin ();
            }
        }

        private ChatTabState? create_restored_chat_tab (
            ConversationPersistenceSnapshot snapshot
        ) {
            if (generation_active ||
                chat_notebook == null) {
                return null;
            }

            create_chat_tab ();

            ChatTabState? state = active_chat;

            if (state == null) {
                return null;
            }

            state.restored_view_only = true;
            state.restored_read_only_reason =
                "continuation context is being qualified";
            state.locked = true;
            state.persistent_id = snapshot.conversation_id;
            state.archived = snapshot.archived;
            state.model_name = snapshot.model_name;
            state.model_digest = snapshot.model_digest;
            state.repository_ids =
                repository_ids_from_snapshot (
                    snapshot
                );
            state.title_label.label = snapshot.title;
            state.placeholder.label =
                "Restored conversation (read-only)";
            state.prompt.buffer.text = "";

            try {
                snapshot.restore_provider_history (
                    state.conversation
                );
                render_restored_snapshot (
                    state,
                    snapshot
                );
            } catch (GLib.Error error) {
                state.persistence_failed = true;
                append_transcript (
                    state.transcript,
                    "System: Durable conversation restore failed: " +
                    error.message
                );
            }

            update_conversation_ui_state ();
            return state;
        }

        private void restore_durable_conversations_if_ready () {
            if (durable_restore_attempted ||
                conversation_store == null) {
                return;
            }

            durable_restore_attempted = true;

            try {
                conversation_store.discard_unarchived_conversations ();
            } catch (GLib.Error error) {
                stderr.printf (
                    "AtM: temporary conversation cleanup failed: %s\n",
                    error.message
                );
            }

            stdout.printf (
                "AtM: archived conversations remain available through History\n"
            );
        }

        private Gtk.Widget build_main_content () {
            no_ai_annunciator =
                build_annunciator_label (
                    "NO AI",
                    "atm-annunciator-critical"
                );
            no_repos_annunciator =
                build_annunciator_label (
                    "NO REPOS",
                    "atm-annunciator-critical"
                );
            repo_ewd_annunciator =
                build_annunciator_label ("EWD");
            repo_cbd_annunciator =
                build_annunciator_label ("CBD");
            repo_rmd_annunciator =
                build_annunciator_label ("RMD");
            ai_scan_annunciator =
                build_annunciator_label ("SCAN");
            repo_check_annunciator =
                build_annunciator_label ("CHECK");
            repo_download_annunciator =
                build_annunciator_label ("DL");
            repo_update_annunciator =
                build_annunciator_label ("UPD");
            repo_validate_annunciator =
                build_annunciator_label ("VAL");
            repo_ready_annunciator =
                build_annunciator_label ("READY");
            repo_offline_annunciator =
                build_annunciator_label ("OFFLINE");
            repo_error_annunciator =
                build_annunciator_label (
                    "ERR",
                    "atm-annunciator-critical"
                );
            optimization_lcd_annunciator =
                build_annunciator_label ("OPT OFF");
            set_annunciator (
                optimization_lcd_annunciator,
                true
            );

            no_ai_annunciator.tooltip_text =
                "No usable local AI available";
            no_repos_annunciator.tooltip_text =
                "No selected repository snapshot ready";
            ai_scan_annunciator.tooltip_text =
                "Scan local AI models";
            repo_check_annunciator.tooltip_text =
                "Check selected repositories";
            repo_download_annunciator.tooltip_text =
                "Download repository snapshot";
            repo_update_annunciator.tooltip_text =
                "Update repository snapshot";
            repo_validate_annunciator.tooltip_text =
                "Validate repository snapshot";
            repo_ready_annunciator.tooltip_text =
                "Selected repositories are ready";
            repo_offline_annunciator.tooltip_text =
                "Remote check unavailable; local repositories may remain usable";
            repo_error_annunciator.tooltip_text =
                "Repository operation failed";
            optimization_lcd_annunciator.tooltip_text =
                "Optimization mode";

            var lcd_row = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                5
            ) {
                hexpand = true,
                halign = Gtk.Align.CENTER,
                margin_start = 10,
                margin_end = 10,
                margin_top = 2,
                margin_bottom = 2
            };

            Gtk.Widget[] lcd_segments = {
                no_ai_annunciator,
                no_repos_annunciator,
                repo_ewd_annunciator,
                repo_cbd_annunciator,
                repo_rmd_annunciator,
                ai_scan_annunciator,
                repo_check_annunciator,
                repo_download_annunciator,
                repo_update_annunciator,
                repo_validate_annunciator,
                repo_ready_annunciator,
                repo_offline_annunciator,
                repo_error_annunciator,
                optimization_lcd_annunciator
            };

            for (int i = 0; i < lcd_segments.length; i++) {
                if (i > 0) {
                    lcd_row.append (
                        build_annunciator_separator ()
                    );
                }

                lcd_row.append (lcd_segments[i]);
            }

            var lcd_frame = new Gtk.Frame (null) {
                child = lcd_row,
                hexpand = true
            };
            lcd_frame.add_css_class ("atm-status-lcd");
            status_lcd = lcd_frame;

            update_ai_annunciators ();
            update_repository_annunciators ();

            var chat_tabs = new Gtk.Notebook () {
                hexpand = true,
                vexpand = true,
                scrollable = true,
                show_border = false,
                tab_pos = Gtk.PositionType.TOP
            };
            chat_tabs.add_css_class ("atm-chat-tabs");
            chat_notebook = chat_tabs;

            chat_tabs.switch_page.connect (
                (page, page_num) => {
                    ChatTabState? state =
                        chat_state_for_page (page);

                    if (state != null) {
                        activate_chat_state (state);
                    }
                }
            );

            new_chat_button =
                new Gtk.Button.from_icon_name (
                    "list-add-symbolic"
                ) {
                    tooltip_text = "New Chat (Ctrl+N)",
                    sensitive = true,
                    valign = Gtk.Align.FILL,
                    halign = Gtk.Align.START
                };
            new_chat_button.add_css_class (
                "atm-new-chat-tab"
            );
            new_chat_button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "New Chat"
            );
            new_chat_button.clicked.connect (() => {
                if (new_chat_action != null) {
                    new_chat_action.activate (null);
                }
            });

            chat_tabs.set_action_widget (
                new_chat_button,
                Gtk.PackType.START
            );

            history_button =
                new Gtk.Button.with_label (
                    "History"
                ) {
                    tooltip_text = "Open conversation history",
                    sensitive = false,
                    valign = Gtk.Align.FILL,
                    halign = Gtk.Align.END
                };
            history_button.add_css_class (
                "atm-new-chat-tab"
            );
            history_button.update_property (
                Gtk.AccessibleProperty.LABEL,
                "Conversation History"
            );
            history_button.clicked.connect (() => {
                show_conversation_history ();
            });

            chat_tabs.set_action_widget (
                history_button,
                Gtk.PackType.END
            );

            var content = new Gtk.Box (
                Gtk.Orientation.VERTICAL,
                0
            );
            content.append (lcd_frame);
            content.append (chat_tabs);

            create_chat_tab ();
            restore_durable_conversations_if_ready ();

            return content;
        }

        private static void configure_renderer_fallback () {
            string? explicit_renderer =
                Environment.get_variable ("GSK_RENDERER");

            if (explicit_renderer != null &&
                explicit_renderer.strip ().length > 0) {
                return;
            }

            string? session_type =
                Environment.get_variable ("XDG_SESSION_TYPE");
            string? wayland_display =
                Environment.get_variable ("WAYLAND_DISPLAY");

            bool is_x11_session =
                session_type != null &&
                session_type.down () == "x11";

            bool has_wayland_display =
                wayland_display != null &&
                wayland_display.strip ().length > 0;

            if (is_x11_session && !has_wayland_display) {
                Environment.set_variable (
                    "GSK_RENDERER",
                    "cairo",
                    false
                );
            }
        }

        public static int main (string[] args) {
            configure_renderer_fallback ();
            return new Application ().run (args);
        }
    }
}
