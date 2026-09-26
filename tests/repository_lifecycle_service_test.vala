namespace AskTheModel.Tests {
    private static int result_code = 0;

    private static string new_temp_root () {
        try {
            return GLib.DirUtils.make_tmp (
                "atm-repository-lifecycle-test-XXXXXX"
            );
        } catch (GLib.FileError error) {
            assert_not_reached ();
        }
    }

    private static void remove_tree_best_effort (
        string path
    ) {
        if (!GLib.FileUtils.test (
                path,
                GLib.FileTest.EXISTS
            )) {
            return;
        }

        if (!GLib.FileUtils.test (
                path,
                GLib.FileTest.IS_DIR
            )) {
            GLib.FileUtils.remove (path);
            return;
        }

        try {
            var directory = GLib.Dir.open (path);
            string? name;

            while ((name = directory.read_name ()) != null) {
                remove_tree_best_effort (
                    GLib.Path.build_filename (
                        path,
                        name
                    )
                );
            }
        } catch (GLib.FileError error) {
            return;
        }

        GLib.DirUtils.remove (path);
    }

    private static bool directory_is_empty (
        string path
    ) throws GLib.Error {
        var directory = GLib.Dir.open (path);
        return directory.read_name () == null;
    }

    private static string create_valid_snapshot (
        string data_root,
        RepositoryDescriptor descriptor,
        string sha,
        string version
    ) throws GLib.Error {
        string root =
            RepositoryLifecycleService.snapshot_path_for_root (
                data_root,
                descriptor,
                sha
            );
        string atm = GLib.Path.build_filename (
            root,
            ".atm"
        );
        string model = GLib.Path.build_filename (
            root,
            "model"
        );

        assert (
            GLib.DirUtils.create_with_parents (
                atm,
                0700
            ) == 0
        );
        assert (
            GLib.DirUtils.create_with_parents (
                model,
                0700
            ) == 0
        );

        string citation = GLib.Path.build_filename (
            root,
            "CITATION.cff"
        );
        string status = GLib.Path.build_filename (
            root,
            "STATUS.md"
        );
        string readme = GLib.Path.build_filename (
            root,
            "README.md"
        );
        string core = GLib.Path.build_filename (
            root,
            "model",
            "core.json"
        );
        string manifest_path = GLib.Path.build_filename (
            root,
            ".atm",
            "repository.json"
        );

        GLib.FileUtils.set_contents (
            citation,
            "cff-version: 1.2.0\n" +
            "message: cite this\n" +
            "type: software\n" +
            "title: Test\n" +
            "version: %s\n".printf (version)
        );
        GLib.FileUtils.set_contents (
            status,
            "# Status\nValidated test status.\n"
        );
        GLib.FileUtils.set_contents (
            readme,
            "# Test repository\nGrounded evidence.\n"
        );
        GLib.FileUtils.set_contents (
            core,
            "{\"entity\": \"test\"}\n"
        );

        string manifest =
            "{\n" +
            "  \"schema_version\": 1,\n" +
            "  \"repository_id\": \"%s\",\n".printf (
                descriptor.id
            ) +
            "  \"acronym\": \"%s\",\n".printf (
                descriptor.acronym
            ) +
            "  \"display_name\": \"%s\",\n".printf (
                descriptor.display_name
            ) +
            "  \"version_source\": {" +
            "\"type\": \"cff\", " +
            "\"path\": \"CITATION.cff\"},\n" +
            "  \"status_source\": \"STATUS.md\",\n" +
            "  \"required_paths\": [" +
            "\"CITATION.cff\", " +
            "\"STATUS.md\", " +
            "\"model/core.json\"],\n" +
            "  \"retrieval\": {\n" +
            "    \"canonical\": [" +
            "\"STATUS.md\", " +
            "\"README.md\", " +
            "\"CITATION.cff\"],\n" +
            "    \"structural\": [" +
            "\"model/core.json\"],\n" +
            "    \"evidence\": [],\n" +
            "    \"tabular\": [],\n" +
            "    \"implementation\": [],\n" +
            "    \"exclude\": [" +
            "\".github\", " +
            "\"__pycache__\"]\n" +
            "  }\n" +
            "}\n";

        GLib.FileUtils.set_contents (
            manifest_path,
            manifest
        );

        return root;
    }

    private static void write_state_v1 (
        string state_root,
        string repository_id,
        string sha,
        string version
    ) throws GLib.Error {
        GLib.FileUtils.set_contents (
            GLib.Path.build_filename (
                state_root,
                "repository-state.json"
            ),
            "{\n" +
            "  \"schema_version\": 1,\n" +
            "  \"repositories\": [\n" +
            "    {\n" +
            "      \"id\": \"%s\",\n".printf (
                repository_id
            ) +
            "      \"sha\": \"%s\",\n".printf (
                sha
            ) +
            "      \"version\": \"%s\"\n".printf (
                version
            ) +
            "    }\n" +
            "  ]\n" +
            "}\n"
        );
    }

    private static void publish_control_state (
        string state_root
    ) throws GLib.Error {
        int disposition;
        ControlStateNative.publish_cutover (
            GLib.Path.build_filename (
                state_root,
                "control-state.sqlite3"
            ),
            GLib.Path.build_filename (
                state_root,
                "repository-state.json"
            ),
            out disposition
        );
    }

    private static async ConversationSession
    begin_leased_session (
        RepositoryLifecycleService service,
        RepositoryDescriptor[] selection,
        int64 expected_generation,
        bool use_explicit_generation
    ) throws GLib.Error {
        ConversationGrounding grounding;

        if (use_explicit_generation) {
            grounding =
                yield service.
                    prepare_conversation_grounding_at_generation (
                        selection,
                        expected_generation
                    );
        } else {
            grounding =
                yield service.prepare_conversation_grounding (
                    selection
                );
        }

        assert (grounding.is_frozen ());
        assert (
            grounding.repository_generation_id () ==
            expected_generation
        );
        assert (grounding.has_generation_lease ());
        assert (
            grounding.generation_lease_id () ==
            expected_generation
        );

        var session = new ConversationSession ();
        session.begin (
            grounding,
            "test-model"
        );
        return session;
    }

    private static async void run_checks (GLib.MainLoop loop) {
        string root = new_temp_root ();

        try {
            var service = new RepositoryLifecycleService (root);
            var optimization_policy = new OptimizationPolicy ();
            service.set_optimization_policy (
                optimization_policy
            );

            assert (!service.optimization_mode_snapshot ());
            optimization_policy.set_enabled_for_session (true);
            assert (service.optimization_mode_snapshot ());
            optimization_policy.set_enabled_for_session (false);
            assert (!service.optimization_mode_snapshot ());

            RepositoryDescriptor[] catalog = RepositoryCatalog.all ();
            RepositoryDescriptor[] none = {};
            RepositoryDescriptor[] one = { catalog[0] };

            assert (catalog.length == 3);

            var capacity_record =
                new RepositoryLocalRecord (
                    catalog[0].id
                );
            var capacity_info =
                new RepositoryRuntimeInfo (
                    catalog[0],
                    capacity_record,
                    root
                );

            assert (
                RepositoryLifecycleService.
                    capacity_operation_kind (
                        capacity_info,
                        false
                    ) ==
                RepositoryNative.CapacityOperationKind.
                    FRESH_INSTALL
            );

            capacity_record.current_sha =
                "1111111111111111111111111111111111111111";
            capacity_record.version = "0.1.0";

            assert (
                RepositoryLifecycleService.
                    capacity_operation_kind (
                        capacity_info,
                        false
                    ) ==
                RepositoryNative.CapacityOperationKind.
                    DIFFERENT_SHA_UPDATE
            );

            assert (
                RepositoryLifecycleService.
                    capacity_operation_kind (
                        capacity_info,
                        true
                    ) ==
                RepositoryNative.CapacityOperationKind.
                    SAME_SHA_REPAIR
            );

            assert (
                !RepositoryLifecycleService.
                    reject_preexisting_final_target (
                        false,
                        false,
                        true
                    )
            );
            assert (
                !RepositoryLifecycleService.
                    reject_preexisting_final_target (
                        true,
                        true,
                        true
                    )
            );
            assert (
                !RepositoryLifecycleService.
                    reject_preexisting_final_target (
                        true,
                        false,
                        false
                    )
            );
            assert (
                RepositoryLifecycleService.
                    reject_preexisting_final_target (
                        true,
                        false,
                        true
                    )
            );

            assert (!service.repository_operations_allowed ());
            assert (
                service.repository_state_status () ==
                RepositoryStateLoadStatus.ABSENT
            );
            service.apply_installation_qualification (false);
            assert (!service.repository_operations_allowed ());
            service.apply_installation_qualification (true);
            assert (!service.repository_operations_allowed ());

            publish_control_state (root);
            assert (service.reload_control_state ());
            assert (
                service.repository_state_status () ==
                RepositoryStateLoadStatus.VALID
            );
            service.apply_installation_qualification (true);
            assert (service.repository_operations_allowed ());

            string mutation_lock_path =
                GLib.Path.build_filename (
                    root,
                    "repository-mutation.lock"
                );

            uint off_changed =
                yield service.download_or_update (none);
            assert (off_changed == 0);
            assert (
                GLib.FileUtils.test (
                    mutation_lock_path,
                    GLib.FileTest.IS_REGULAR
                )
            );

            int held_lease_fd;
            bool lease_contended;
            assert (
                RepositoryNative.try_acquire_mutation_lease (
                    root,
                    out held_lease_fd,
                    out lease_contended
                )
            );
            assert (!lease_contended);
            assert (held_lease_fd >= 0);

            bool off_busy_rejected = false;
            try {
                yield service.download_or_update (one);
            } catch (RepositoryError error) {
                off_busy_rejected =
                    error.code == RepositoryError.BUSY;
            }
            assert (off_busy_rejected);

            optimization_policy.set_enabled_for_session (true);

            bool on_busy_rejected = false;
            try {
                yield service.download_or_update (one);
            } catch (RepositoryError error) {
                on_busy_rejected =
                    error.code == RepositoryError.BUSY;
            }
            assert (on_busy_rejected);

            RepositoryNative.release_mutation_lease (
                held_lease_fd
            );

            uint on_changed =
                yield service.download_or_update (none);
            assert (on_changed == 0);

            optimization_policy.set_enabled_for_session (false);

            RepositoryMutationOutcome off_context =
                yield service.download_or_update_with_context (
                    none
                );
            assert (off_context.changed == 0);
            assert (!off_context.optimized_operation);

            optimization_policy.set_enabled_for_session (true);

            RepositoryMutationOutcome on_context =
                yield service.download_or_update_with_context (
                    none
                );
            assert (on_context.changed == 0);
            assert (on_context.optimized_operation);

            optimization_policy.set_enabled_for_session (false);

            string trigger_root = new_temp_root ();
            var trigger_service =
                new RepositoryLifecycleService (
                    trigger_root,
                    trigger_root,
                    trigger_root
                );
            var trigger_policy =
                new OptimizationPolicy ();
            trigger_service.set_optimization_policy (
                trigger_policy
            );
            assert (
                !trigger_service.optimization_mode_snapshot ()
            );

            var trigger_store =
                new ConversationPersistenceStore (
                    trigger_root,
                    GLib.Path.build_filename (
                        trigger_root,
                        "Export"
                    )
                );

            int trigger_b0_fd = -1;
            bool trigger_b0_contended = false;
            assert (
                RepositoryNative.try_acquire_mutation_lease (
                    trigger_root,
                    out trigger_b0_fd,
                    out trigger_b0_contended
                )
            );
            assert (!trigger_b0_contended);
            assert (trigger_b0_fd >= 0);

            string? cleanup_failure = null;

            RepositoryGcIsolationResult? off_cleanup =
                trigger_service.run_post_mutation_isolation (
                    new RepositoryMutationOutcome (
                        1,
                        false
                    ),
                    trigger_store,
                    out cleanup_failure
                );
            assert (off_cleanup == null);
            assert (cleanup_failure == null);

            RepositoryGcIsolationResult? zero_cleanup =
                trigger_service.run_post_mutation_isolation (
                    new RepositoryMutationOutcome (
                        0,
                        true
                    ),
                    trigger_store,
                    out cleanup_failure
                );
            assert (zero_cleanup == null);
            assert (cleanup_failure == null);

            RepositoryGcIsolationResult? carried_on_cleanup =
                trigger_service.run_post_mutation_isolation (
                    new RepositoryMutationOutcome (
                        1,
                        true
                    ),
                    trigger_store,
                    out cleanup_failure
                );
            assert (cleanup_failure == null);
            if (carried_on_cleanup == null) {
                assert_not_reached ();
            }
            assert (
                carried_on_cleanup.outcome ==
                RepositoryGcIsolationOutcome.B0_CONTENDED
            );

            RepositoryNative.release_mutation_lease (
                trigger_b0_fd
            );
            trigger_b0_fd = -1;

            RepositoryGcIsolationResult? failed_cleanup =
                trigger_service.run_post_mutation_isolation (
                    new RepositoryMutationOutcome (
                        1,
                        true
                    ),
                    trigger_store,
                    out cleanup_failure
                );
            assert (failed_cleanup == null);
            if (cleanup_failure == null) {
                assert_not_reached ();
            }
            assert (cleanup_failure.length > 0);

            remove_tree_best_effort (
                trigger_root
            );

            string guarded_root = new_temp_root ();
            publish_control_state (guarded_root);

            var guarded_writer_a =
                new ControlRepositoryStateStore (
                    guarded_root
                );
            var guarded_writer_b =
                new ControlRepositoryStateStore (
                    guarded_root
                );

            assert (
                guarded_writer_a.repository_generation_id ==
                guarded_writer_b.repository_generation_id
            );

            int guarded_lease_fd;
            bool guarded_lease_contended;
            assert (
                RepositoryNative.try_acquire_mutation_lease (
                    guarded_root,
                    out guarded_lease_fd,
                    out guarded_lease_contended
                )
            );
            assert (!guarded_lease_contended);
            assert (guarded_lease_fd >= 0);

            guarded_writer_a.set_current (
                catalog[0].id,
                "8888888888888888888888888888888888888888",
                "0.1.0"
            );

            bool stale_generation_rejected = false;
            try {
                guarded_writer_b.set_current (
                    catalog[1].id,
                    "9999999999999999999999999999999999999999",
                    "0.1.0"
                );
            } catch (RepositoryError error) {
                stale_generation_rejected =
                    error.code == RepositoryError.STORAGE;
            }
            assert (stale_generation_rejected);

            RepositoryNative.release_mutation_lease (
                guarded_lease_fd
            );
            remove_tree_best_effort (guarded_root);

            RepositoryRuntimeInfo freshness =
                service.info_for (catalog[0].id);
            freshness.remote_sha =
                "1111111111111111111111111111111111111111";
            freshness.remote_version = "9.9.9";
            freshness.clear_remote_identity ();
            assert (freshness.remote_sha == null);
            assert (freshness.remote_version == null);

            RepositoryRuntimeInfo first =
                service.info_for (catalog[0].id);
            RepositoryRuntimeInfo second =
                service.info_for (catalog[1].id);
            RepositoryRuntimeInfo third =
                service.info_for (catalog[2].id);
            first.remote_sha =
                "1111111111111111111111111111111111111111";
            first.remote_version = "1.0.0";
            second.remote_sha =
                "2222222222222222222222222222222222222222";
            second.remote_version = "2.0.0";
            third.remote_sha =
                "3333333333333333333333333333333333333333";
            third.remote_version = "3.0.0";

            RepositoryDescriptor[] batch = {
                catalog[0],
                catalog[2]
            };
            service.clear_remote_identities (batch);

            assert (first.remote_sha == null);
            assert (first.remote_version == null);
            assert (
                second.remote_sha ==
                "2222222222222222222222222222222222222222"
            );
            assert (second.remote_version == "2.0.0");
            assert (third.remote_sha == null);
            assert (third.remote_version == null);

            second.clear_remote_identity ();

            foreach (RepositoryDescriptor descriptor in catalog) {
                RepositoryRuntimeInfo info =
                    service.info_for (descriptor.id);

                assert (
                    info.descriptor.id == descriptor.id
                );
                assert (!info.local.is_ready ());
                assert (info.download_required ());
                assert (!info.update_available ());
                assert (info.remote_sha == null);
                assert (info.remote_version == null);
            }

            assert (!service.selection_needs_action (none));
            assert (
                service.action_tooltip (none) ==
                "Selected repositories are current"
            );

            assert (service.selection_needs_action (one));
            assert (
                service.action_tooltip (one) ==
                "Download selected repositories"
            );

            ConversationGrounding zero =
                yield service.prepare_conversation_grounding (none);

            assert (zero.is_frozen ());
            assert (zero.repository_count () == 0);

            ConversationGrounding restored_zero =
                yield service.prepare_conversation_grounding_at_generation (
                    none,
                    0
                );
            assert (restored_zero.is_frozen ());
            assert (
                restored_zero.repository_count () == 0
            );
            assert (
                restored_zero.repository_generation_id () == 0
            );

            bool invalid_zero_generation_rejected = false;
            try {
                yield service.prepare_conversation_grounding_at_generation (
                    one,
                    0
                );
            } catch (RepositoryError error) {
                invalid_zero_generation_rejected =
                    error.code ==
                    RepositoryError.INVALID_RESPONSE;
            }
            assert (invalid_zero_generation_rejected);

            bool not_ready_rejected = false;

            try {
                yield service.prepare_conversation_grounding (one);
            } catch (RepositoryError error) {
                not_ready_rejected =
                    error.code == RepositoryError.NOT_READY;
            }

            assert (not_ready_rejected);

            string sealed_state_root = new_temp_root ();
            string sealed_data_root = new_temp_root ();
            string sealed_cache_root = new_temp_root ();
            RepositoryDescriptor sealed_descriptor =
                catalog[2];
            string sealed_sha =
                "5555555555555555555555555555555555555555";

            string sealed_snapshot = create_valid_snapshot (
                sealed_data_root,
                sealed_descriptor,
                sealed_sha,
                "0.1.0"
            );
            write_state_v1 (
                sealed_state_root,
                sealed_descriptor.id,
                sealed_sha,
                "0.1.0"
            );
            publish_control_state (
                sealed_state_root
            );

            string enrolled_seal_value;
            uint64 enrolled_files;
            uint64 enrolled_bytes;
            assert (
                RepositoryNative.compute_snapshot_seal (
                    sealed_snapshot,
                    out enrolled_seal_value,
                    out enrolled_files,
                    out enrolled_bytes
                )
            );

            var presealed_state =
                new ControlRepositoryStateStore (
                    sealed_state_root
                );
            assert (
                presealed_state.repository_generation_id == 1
            );
            presealed_state.set_snapshot_seal (
                sealed_descriptor.id,
                sealed_sha,
                enrolled_seal_value
            );
            assert (
                presealed_state.repository_generation_id == 2
            );

            var sealed_service =
                new RepositoryLifecycleService (
                    sealed_state_root,
                    sealed_data_root,
                    sealed_cache_root
                );
            RepositoryDescriptor[] sealed_selection = {
                sealed_descriptor
            };

            bool startup_gate_rejected = false;
            try {
                yield sealed_service.prepare_conversation_grounding (
                    sealed_selection
                );
            } catch (RepositoryError error) {
                startup_gate_rejected =
                    error.code == RepositoryError.NOT_READY;
            }
            assert (startup_gate_rejected);

            sealed_service.apply_installation_qualification (true);

            {
                ConversationGrounding off_grounding =
                    yield sealed_service.prepare_conversation_grounding (
                        sealed_selection
                    );
                assert (off_grounding.has_generation_lease ());
                assert (
                    off_grounding.generation_lease_id () == 2
                );

                int off_generation_exclusive_fd = -1;
                bool off_generation_exclusive_contended = false;
                assert (
                    RepositoryGenerationLeaseNative.
                        try_acquire_exclusive (
                            sealed_state_root,
                            2,
                            out off_generation_exclusive_fd,
                            out off_generation_exclusive_contended
                        )
                );
                assert (off_generation_exclusive_contended);
                assert (off_generation_exclusive_fd == -1);
            }

            int released_off_generation_fd = -1;
            bool released_off_generation_contended = false;
            assert (
                RepositoryGenerationLeaseNative.
                    try_acquire_exclusive (
                        sealed_state_root,
                        2,
                        out released_off_generation_fd,
                        out released_off_generation_contended
                    )
            );
            assert (!released_off_generation_contended);
            assert (released_off_generation_fd >= 0);

            bool off_reader_busy_rejected = false;
            try {
                yield sealed_service.
                    prepare_conversation_grounding_at_generation (
                        sealed_selection,
                        2
                    );
            } catch (RepositoryError error) {
                off_reader_busy_rejected =
                    error.code == RepositoryError.BUSY;
            }
            assert (off_reader_busy_rejected);

            RepositoryGenerationLeaseNative.release (
                released_off_generation_fd
            );

            int sealed_lease_fd;
            bool sealed_lease_contended;
            assert (
                RepositoryNative.try_acquire_mutation_lease (
                    sealed_state_root,
                    out sealed_lease_fd,
                    out sealed_lease_contended
                )
            );
            assert (!sealed_lease_contended);
            assert (sealed_lease_fd >= 0);

            ControlRepositoryStateStore advancing_writer;

            {
                ConversationGrounding sealed_grounding =
                    yield sealed_service.prepare_conversation_grounding (
                        sealed_selection
                    );

            RepositoryNative.release_mutation_lease (
                sealed_lease_fd
            );

            assert (sealed_grounding.is_frozen ());
            assert (
                sealed_grounding.repository_count () == 1
            );
            assert (
                sealed_grounding.repository_generation_id () == 2
            );

            ConversationRepositoryPin? grounding_pin =
                sealed_grounding.repository_pin_at (0);
            assert (grounding_pin != null);
            assert (
                grounding_pin.repository_id ==
                sealed_descriptor.id
            );
            assert (
                grounding_pin.repository_version == "0.1.0"
            );
            assert (
                grounding_pin.snapshot_sha == sealed_sha
            );

            var pinned_session =
                new ConversationSession ();
            pinned_session.begin (
                sealed_grounding,
                "test-model"
            );
            assert (
                pinned_session.repository_generation_id () == 2
            );

            ConversationRepositoryPin? session_pin =
                pinned_session.repository_pin_at (0);
            assert (session_pin != null);
            assert (
                session_pin.repository_id ==
                sealed_descriptor.id
            );
            assert (
                session_pin.repository_version == "0.1.0"
            );
            assert (
                session_pin.snapshot_sha == sealed_sha
            );

            advancing_writer =
                new ControlRepositoryStateStore (
                    sealed_state_root
                );
            advancing_writer.set_current (
                "ewd",
                "6666666666666666666666666666666666666666",
                "0.2.0"
            );
            assert (
                advancing_writer.repository_generation_id == 3
            );
            assert (
                sealed_grounding.repository_generation_id () == 2
            );
                assert (
                    pinned_session.repository_generation_id () == 2
                );
            }

            string unavailable_sha =
                "7777777777777777777777777777777777777777";
            advancing_writer.set_current (
                sealed_descriptor.id,
                unavailable_sha,
                "0.2.0",
                enrolled_seal_value
            );
            assert (
                advancing_writer.repository_generation_id == 4
            );

            bool active_generation_missing_rejected = false;
            try {
                yield sealed_service.prepare_conversation_grounding (
                    sealed_selection
                );
            } catch (RepositoryError error) {
                active_generation_missing_rejected =
                    error.code == RepositoryError.NOT_READY;
            }
            assert (active_generation_missing_rejected);

            {
                ConversationGrounding restored_historical =
                    yield sealed_service.prepare_conversation_grounding_at_generation (
                        sealed_selection,
                        2
                    );
            assert (restored_historical.is_frozen ());
            assert (
                restored_historical.repository_count () == 1
            );
            assert (
                restored_historical.repository_generation_id () == 2
            );

            ConversationRepositoryPin? restored_pin =
                restored_historical.repository_pin_at (0);
            assert (restored_pin != null);
            assert (
                restored_pin.repository_id ==
                sealed_descriptor.id
            );
            assert (
                restored_pin.repository_version == "0.1.0"
            );
            assert (
                restored_pin.snapshot_sha == sealed_sha
            );

            var restored_session =
                new ConversationSession ();
            restored_session.begin (
                restored_historical,
                "test-model"
            );
            assert (
                restored_session.repository_generation_id () == 2
            );

            var after_historical_restore =
                new ControlRepositoryStateStore (
                    sealed_state_root
                );
                assert (
                    after_historical_restore.repository_generation_id == 4
                );
            }

            bool missing_generation_rejected = false;
            try {
                yield sealed_service.prepare_conversation_grounding_at_generation (
                    sealed_selection,
                    9999
                );
            } catch (GLib.Error error) {
                missing_generation_rejected = true;
            }
            assert (missing_generation_rejected);

            advancing_writer.set_current (
                sealed_descriptor.id,
                sealed_sha,
                "0.1.0",
                enrolled_seal_value
            );
            assert (
                advancing_writer.repository_generation_id == 5
            );

            var enrolled_state =
                new ControlRepositoryStateStore (
                    sealed_state_root
                );
            assert (
                enrolled_state.control_schema_version == 1
            );
            string? enrolled_seal =
                enrolled_state.record_for (
                    sealed_descriptor.id
                ).snapshot_seal_sha256;
            assert (enrolled_seal != null);
            assert (enrolled_seal == enrolled_seal_value);
            assert (
                enrolled_state.repository_generation_id == 5
            );

            string index_lock_root =
                GLib.Path.build_filename (
                    sealed_state_root,
                    "retrieval-index-locks"
                );
            string index_lock_path =
                GLib.Path.build_filename (
                    index_lock_root,
                    sealed_descriptor.id,
                    sealed_sha + ".lock"
                );

            remove_tree_best_effort (
                sealed_cache_root
            );
            assert (
                GLib.DirUtils.create (
                    sealed_cache_root,
                    0700
                ) == 0
            );
            assert (
                !GLib.FileUtils.test (
                    index_lock_root,
                    GLib.FileTest.EXISTS
                )
            );

            {
                ConversationGrounding baseline_rebuild =
                    yield sealed_service.prepare_conversation_grounding (
                        sealed_selection
                    );
                assert (baseline_rebuild.is_frozen ());
                assert (
                    !GLib.FileUtils.test (
                        index_lock_root,
                        GLib.FileTest.EXISTS
                    )
                );
            }

            remove_tree_best_effort (
                sealed_cache_root
            );
            assert (
                GLib.DirUtils.create (
                    sealed_cache_root,
                    0700
                ) == 0
            );

            var sealed_optimization_policy =
                new OptimizationPolicy ();
            sealed_service.set_optimization_policy (
                sealed_optimization_policy
            );
            sealed_optimization_policy.set_enabled_for_session (
                true
            );

            string generation_lock_root =
                GLib.Path.build_filename (
                    sealed_state_root,
                    "repository-generation-leases"
                );
            string generation_zero_lock =
                GLib.Path.build_filename (
                    generation_lock_root,
                    "0.lock"
                );
            string generation_five_lock =
                GLib.Path.build_filename (
                    generation_lock_root,
                    "5.lock"
                );
            string generation_two_lock =
                GLib.Path.build_filename (
                    generation_lock_root,
                    "2.lock"
                );
            string generation_9999_lock =
                GLib.Path.build_filename (
                    generation_lock_root,
                    "9999.lock"
                );

            assert (
                !GLib.FileUtils.test (
                    generation_zero_lock,
                    GLib.FileTest.EXISTS
                )
            );

            ConversationGrounding optimized_zero =
                yield sealed_service.
                    prepare_conversation_grounding_at_generation (
                        none,
                        0
                    );
            assert (optimized_zero.is_frozen ());
            assert (
                optimized_zero.repository_count () == 0
            );
            assert (
                !GLib.FileUtils.test (
                    generation_zero_lock,
                    GLib.FileTest.EXISTS
                )
            );

            ConversationSession generation_session =
                yield begin_leased_session (
                    sealed_service,
                    sealed_selection,
                    5,
                    false
                );
            assert (
                GLib.FileUtils.test (
                    generation_five_lock,
                    GLib.FileTest.IS_REGULAR
                )
            );
            assert (
                GLib.FileUtils.test (
                    index_lock_path,
                    GLib.FileTest.IS_REGULAR
                )
            );

            int generation_exclusive_fd = -1;
            bool generation_exclusive_contended = false;
            assert (
                RepositoryGenerationLeaseNative.
                    try_acquire_exclusive (
                        sealed_state_root,
                        5,
                        out generation_exclusive_fd,
                        out generation_exclusive_contended
                    )
            );
            assert (generation_exclusive_contended);
            assert (generation_exclusive_fd == -1);

            generation_session.reset ();

            generation_exclusive_contended = false;
            assert (
                RepositoryGenerationLeaseNative.
                    try_acquire_exclusive (
                        sealed_state_root,
                        5,
                        out generation_exclusive_fd,
                        out generation_exclusive_contended
                    )
            );
            assert (!generation_exclusive_contended);
            assert (generation_exclusive_fd >= 0);
            RepositoryGenerationLeaseNative.release (
                generation_exclusive_fd
            );

            bool leased_missing_generation_rejected = false;
            try {
                yield sealed_service.
                    prepare_conversation_grounding_at_generation (
                        sealed_selection,
                        9999
                    );
            } catch (GLib.Error error) {
                leased_missing_generation_rejected = true;
            }
            assert (leased_missing_generation_rejected);
            assert (
                GLib.FileUtils.test (
                    generation_9999_lock,
                    GLib.FileTest.IS_REGULAR
                )
            );

            generation_exclusive_fd = -1;
            generation_exclusive_contended = false;
            assert (
                RepositoryGenerationLeaseNative.
                    try_acquire_exclusive (
                        sealed_state_root,
                        9999,
                        out generation_exclusive_fd,
                        out generation_exclusive_contended
                    )
            );
            assert (!generation_exclusive_contended);
            assert (generation_exclusive_fd >= 0);
            RepositoryGenerationLeaseNative.release (
                generation_exclusive_fd
            );

            ConversationSession historical_lease_session =
                yield begin_leased_session (
                    sealed_service,
                    sealed_selection,
                    2,
                    true
                );
            assert (
                GLib.FileUtils.test (
                    generation_two_lock,
                    GLib.FileTest.IS_REGULAR
                )
            );

            generation_exclusive_fd = -1;
            generation_exclusive_contended = false;
            assert (
                RepositoryGenerationLeaseNative.
                    try_acquire_exclusive (
                        sealed_state_root,
                        2,
                        out generation_exclusive_fd,
                        out generation_exclusive_contended
                    )
            );
            assert (generation_exclusive_contended);
            assert (generation_exclusive_fd == -1);

            historical_lease_session.reset ();

            generation_exclusive_contended = false;
            assert (
                RepositoryGenerationLeaseNative.
                    try_acquire_exclusive (
                        sealed_state_root,
                        2,
                        out generation_exclusive_fd,
                        out generation_exclusive_contended
                    )
            );
            assert (!generation_exclusive_contended);
            assert (generation_exclusive_fd >= 0);
            RepositoryGenerationLeaseNative.release (
                generation_exclusive_fd
            );

            sealed_optimization_policy.set_enabled_for_session (
                false
            );

            remove_tree_best_effort (
                sealed_cache_root
            );
            assert (
                GLib.DirUtils.create (
                    sealed_cache_root,
                    0700
                ) == 0
            );
            assert (
                directory_is_empty (
                    sealed_cache_root
                )
            );

            GLib.FileUtils.set_contents (
                GLib.Path.build_filename (
                    sealed_snapshot,
                    "README.md"
                ),
                "# Test repository\nLocally modified.\n"
            );

            bool seal_mismatch_rejected = false;
            try {
                yield sealed_service.prepare_conversation_grounding (
                    sealed_selection
                );
            } catch (RepositoryError error) {
                seal_mismatch_rejected =
                    error.code == RepositoryError.NOT_READY;
            }

            assert (seal_mismatch_rejected);
            assert (
                sealed_service.info_for (
                    sealed_descriptor.id
                ).integrity_invalid
            );
            assert (
                sealed_service.info_for (
                    sealed_descriptor.id
                ).download_required ()
            );
            assert (
                sealed_service.selection_needs_action (
                    sealed_selection
                )
            );
            assert (
                sealed_service.action_tooltip (
                    sealed_selection
                ) ==
                "Download selected repositories"
            );
            assert (
                directory_is_empty (
                    sealed_cache_root
                )
            );

            var preserved_state =
                new ControlRepositoryStateStore (
                    sealed_state_root
                );
            assert (
                preserved_state.record_for (
                    sealed_descriptor.id
                ).snapshot_seal_sha256 == enrolled_seal
            );

            remove_tree_best_effort (
                sealed_state_root
            );
            remove_tree_best_effort (
                sealed_data_root
            );
            remove_tree_best_effort (
                sealed_cache_root
            );
        } catch (GLib.Error error) {
            stderr.printf ("%s\n", error.message);
            result_code = 1;
        }

        remove_tree_best_effort (root);
        loop.quit ();
    }

    public static int main (string[] args) {
        var loop = new GLib.MainLoop ();
        run_checks.begin (loop);
        loop.run ();
        return result_code;
    }
}
