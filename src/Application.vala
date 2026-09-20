namespace AskTheModel {
    private enum RepositoryOperationOutcome {
        NORMAL,
        OFFLINE,
        ERROR
    }

    public class Application : Gtk.Application {
        private const string APP_ID = "io.github.laurentiustaicu.ask_the_model";
        private const string STYLE_RESOURCE =
            "/io/github/laurentiustaicu/ask_the_model/style.css";

        private Granite.Settings granite_settings;
        private Gtk.Settings gtk_settings;
        private OllamaProvider ollama_provider;
        private Gtk.TextView? transcript_view;
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
        private RepositorySelection repository_selection =
            new RepositorySelection ();
        private RepositoryLifecycleService repository_lifecycle =
            new RepositoryLifecycleService ();
        private bool ai_scanning = false;
        private bool repository_checking = false;
        private bool repository_downloading = false;
        private bool repository_updating = false;
        private bool repository_validating = false;
        private bool repository_offline = false;
        private bool repository_error = false;
        private bool assistant_stream_started = false;
        private bool updating_model_selector = false;

        public Application () {
            Object (
                application_id: APP_ID,
                flags: ApplicationFlags.DEFAULT_FLAGS
            );
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
                if (transcript_view == null) {
                    return;
                }

                if (!assistant_stream_started) {
                    append_transcript (
                        transcript_view,
                        "Assistant: " + chunk
                    );
                    assistant_stream_started = true;
                } else {
                    append_transcript_raw (
                        transcript_view,
                        chunk
                    );
                }
            });

            discover_local_provider.begin ();
        }

        private async void discover_local_provider () {
            begin_model_scan_status ();

            bool found = yield ollama_provider.discover ();
            update_model_selector ();

            if (found && ollama_provider.model_name != null) {
                string[] chat_models =
                    ollama_provider.get_completion_models ();

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

            if (repository_error) {
                repository_summary =
                    "Repository operation failed";
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
                    "; remote check offline";
            }

            string summary =
                "%s. %s.".printf (
                    ai_summary,
                    repository_summary
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
                repository_validating
            );
            set_annunciator (
                repo_ready_annunciator,
                !operation_active &&
                all_ready &&
                !repository_error
            );
            set_annunciator (
                repo_offline_annunciator,
                repository_offline
            );
            set_annunciator (
                repo_error_annunciator,
                repository_error
            );
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

            if (refresh_models_button != null) {
                refresh_models_button.sensitive = true;
            }

            ai_scanning = false;
            update_ai_annunciators ();

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
            model_dropdown.sensitive = models.length > 0;

            updating_model_selector = false;
            update_ai_annunciators ();
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
            overlay.add_overlay (new SelectorTriangle ());
            return overlay;
        }

        private void update_repository_selector_label () {
            if (repository_menu_button != null) {
                repository_menu_button.label =
                    repository_selection.summary ();
            }

            bool has_selection =
                repository_selection.selected_repositories ().length > 0;

            if (refresh_repositories_button != null) {
                refresh_repositories_button.sensitive = has_selection;
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

                repository_action_button.sensitive = has_action;
                repository_action_button.can_target = has_action;
                repository_action_button.opacity =
                    has_action ? 1.0 : 0.0;
                repository_action_button.update_state (
                    Gtk.AccessibleState.HIDDEN,
                    !has_action
                );

                string? action_tooltip =
                    has_action
                        ? repository_lifecycle.action_tooltip (
                            selected
                        )
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
            update_repository_annunciators ();
        }

        private void finish_repository_operation (
            RepositoryOperationOutcome outcome
        ) {
            repository_checking = false;
            repository_downloading = false;
            repository_updating = false;
            repository_validating = false;
            repository_offline =
                outcome == RepositoryOperationOutcome.OFFLINE;
            repository_error =
                outcome == RepositoryOperationOutcome.ERROR;
            update_repository_annunciators ();
        }

        private async void refresh_selected_repositories () {
            RepositoryDescriptor[] selected =
                repository_selection.selected_repositories ();

            if (selected.length == 0) {
                finish_repository_operation (
                    RepositoryOperationOutcome.NORMAL
                );
                return;
            }

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
            } catch (GLib.Error error) {
                bool transport_failure =
                    error.domain != RepositoryError.quark ();

                finish_repository_operation (
                    transport_failure
                        ? RepositoryOperationOutcome.OFFLINE
                        : RepositoryOperationOutcome.ERROR
                );

                stderr.printf (
                    "AtM: repository refresh failed: %s\n",
                    error.message
                );
            }

            if (repository_menu_button != null) {
                repository_menu_button.sensitive = true;
            }

            if (refresh_repositories_button != null) {
                refresh_repositories_button.sensitive = true;
            }

            set_activity_working (
                refresh_repositories_button,
                refresh_repositories_ring,
                false
            );
            update_repository_option_labels ();
            update_repository_selector_label ();
        }

        private async void download_or_update_selected_repositories () {
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
            } catch (GLib.Error error) {
                bool transport_failure =
                    error.domain != RepositoryError.quark ();

                finish_repository_operation (
                    transport_failure
                        ? RepositoryOperationOutcome.OFFLINE
                        : RepositoryOperationOutcome.ERROR
                );

                stderr.printf (
                    "AtM: repository download/update failed: %s\n",
                    error.message
                );
            }

            if (repository_menu_button != null) {
                repository_menu_button.sensitive = true;
            }
            if (refresh_repositories_button != null) {
                refresh_repositories_button.sensitive = true;
            }

            set_activity_working (
                repository_action_button,
                repository_action_ring,
                false
            );
            update_repository_option_labels ();
            update_repository_selector_label ();
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

            return headerbar;
        }

        private void append_transcript (
            Gtk.TextView transcript,
            string entry
        ) {
            string current = transcript.buffer.text;
            string separator = current.length > 0 ? "\n\n" : "";
            transcript.buffer.text = current + separator + entry;
        }

        private void append_transcript_raw (
            Gtk.TextView transcript,
            string text
        ) {
            transcript.buffer.text =
                transcript.buffer.text + text;
        }

        private async void send_prompt (
            string prompt,
            Gtk.TextView transcript,
            Gtk.TextView prompt_view,
            Gtk.Button send_button
        ) {
            try {
                string answer = yield ollama_provider.chat (prompt);

                if (!assistant_stream_started && answer.length > 0) {
                    append_transcript (
                        transcript,
                        "Assistant: " + answer
                    );
                    assistant_stream_started = true;
                }
            } catch (GLib.Error error) {
                append_transcript (
                    transcript,
                    "System: " + error.message
                );
            }

            prompt_view.sensitive = true;
            send_button.sensitive =
                prompt_view.buffer.text.strip ().length > 0;
            prompt_view.grab_focus ();
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
                repo_error_annunciator
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
            transcript_view = transcript;

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

            var prompt_placeholder = new Gtk.Label ("Ask something…") {
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

            var send_button = new Gtk.Button.with_label ("Send") {
                valign = Gtk.Align.END,
                sensitive = false
            };

            prompt_view.buffer.changed.connect (() => {
                prompt_placeholder.visible =
                    prompt_view.buffer.get_char_count () == 0;

                send_button.sensitive =
                    prompt_view.sensitive &&
                    prompt_view.buffer.text.strip ().length > 0;
            });

            send_button.clicked.connect (() => {
                string prompt = prompt_view.buffer.text.strip ();
                if (prompt.length == 0) {
                    return;
                }

                append_transcript (
                    transcript,
                    "You: " + prompt
                );

                assistant_stream_started = false;
                prompt_view.buffer.text = "";
                prompt_view.sensitive = false;
                send_button.sensitive = false;

                send_prompt.begin (
                    prompt,
                    transcript,
                    prompt_view,
                    send_button
                );
            });

            var composer = new Gtk.Box (Gtk.Orientation.HORIZONTAL, 8) {
                margin_top = 12,
                margin_bottom = 12,
                margin_start = 12,
                margin_end = 12
            };
            composer.append (prompt_frame);
            composer.append (send_button);

            var content = new Gtk.Box (Gtk.Orientation.VERTICAL, 0);
            content.append (lcd_frame);
            content.append (transcript_scroll);
            content.append (composer);

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
