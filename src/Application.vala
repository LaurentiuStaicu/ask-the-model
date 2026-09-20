namespace AskTheModel {
    public class Application : Gtk.Application {
        private const string APP_ID = "io.github.laurentiustaicu.ask_the_model";
        private const string STYLE_RESOURCE =
            "/io/github/laurentiustaicu/ask_the_model/style.css";

        private Granite.Settings granite_settings;
        private Gtk.Settings gtk_settings;
        private OllamaProvider ollama_provider;
        private Gtk.TextView? transcript_view;
        private Gtk.DropDown? model_dropdown;
        private Gtk.StringList? model_list;
        private Gtk.Button? refresh_models_button;
        private Gtk.Label? model_scan_status;
        private Gtk.MenuButton? repository_menu_button;
        private RepositorySelection repository_selection =
            new RepositorySelection ();
        private uint model_status_generation = 0;
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
            ollama_provider.discovery_progress.connect ((percent) => {
                if (model_scan_status != null) {
                    model_scan_status.label =
                        "%u%%".printf (percent);
                    model_scan_status.visible = true;
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

                show_model_scan_result (
                    chat_models.length == 1
                        ? "1 model found"
                        : "%d models found".printf (
                            chat_models.length
                        )
                );

                stdout.printf (
                    "AtM: Ollama detected at %s; using %s (%u model%s available)\n",
                    ollama_provider.base_url,
                    ollama_provider.model_name,
                    ollama_provider.model_count,
                    ollama_provider.model_count == 1 ? "" : "s"
                );
            } else if (found) {
                show_model_scan_result ("No chat models");

                stderr.printf (
                    "AtM: Ollama detected at %s, but no completion-capable model was found.\n",
                    ollama_provider.base_url
                );
            } else {
                show_model_scan_result ("Ollama not found");

                stderr.printf (
                    "AtM: local Ollama provider not detected on 127.0.0.1 ports 11434 or 11435.\n"
                );
            }
        }

        private void begin_model_scan_status () {
            model_status_generation++;

            if (model_scan_status != null) {
                model_scan_status.label = "0%";
                model_scan_status.visible = true;
            }
        }

        private void show_model_scan_result (string message) {
            model_status_generation++;
            uint generation = model_status_generation;

            if (model_scan_status == null) {
                return;
            }

            model_scan_status.label = message;
            model_scan_status.visible = true;

            Timeout.add_seconds (3, () => {
                if (generation == model_status_generation &&
                    model_scan_status != null) {
                    model_scan_status.label = "";
                    model_scan_status.visible = false;
                }

                return false;
            });
        }

        private async void refresh_local_models () {
            begin_model_scan_status ();

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

            if (found && ollama_provider.model_name != null) {
                string[] chat_models =
                    ollama_provider.get_completion_models ();

                show_model_scan_result (
                    chat_models.length == 1
                        ? "1 model found"
                        : "%d models found".printf (
                            chat_models.length
                        )
                );

                stdout.printf (
                    "AtM: model list refreshed; using %s (%u installed model%s)\n",
                    ollama_provider.model_name,
                    ollama_provider.model_count,
                    ollama_provider.model_count == 1 ? "" : "s"
                );
            } else if (found) {
                show_model_scan_result ("No chat models");
                stderr.printf (
                    "AtM: model list refreshed, but no completion-capable model was found.\n"
                );
            } else {
                show_model_scan_result ("Ollama not found");

                stderr.printf (
                    "AtM: refresh could not reach local Ollama on ports 11434 or 11435.\n"
                );
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
            model_dropdown.sensitive = models.length > 0;

            updating_model_selector = false;
        }

        private void update_repository_selector_label () {
            if (repository_menu_button == null) {
                return;
            }

            repository_menu_button.label =
                repository_selection.summary ();
        }

        private Gtk.CheckButton build_repository_check_button (
            RepositoryDescriptor descriptor
        ) {
            var check = new Gtk.CheckButton.with_label (
                descriptor.selector_label ()
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
            });

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
                direction = Gtk.ArrowType.DOWN,
                always_show_arrow = true
            };
            repository_menu_button.set_popover (popover);

            return repository_menu_button;
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
                sensitive = false
            };
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
            refresh_models_button.clicked.connect (() => {
                refresh_local_models.begin ();
            });

            model_scan_status = new Gtk.Label ("") {
                valign = Gtk.Align.CENTER,
                visible = false
            };
            model_scan_status.add_css_class ("dim-label");

            var model_controls = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                6
            );
            model_controls.append (model_dropdown);
            model_controls.append (refresh_models_button);
            model_controls.append (model_scan_status);

            var header_controls = new Gtk.Box (
                Gtk.Orientation.HORIZONTAL,
                12
            );
            header_controls.append (build_repository_selector ());
            header_controls.append (model_controls);

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
            var transcript = new Gtk.TextView () {
                editable = false,
                cursor_visible = false,
                monospace = true,
                wrap_mode = Gtk.WrapMode.WORD_CHAR,
                left_margin = 12,
                right_margin = 12,
                top_margin = 20,
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
