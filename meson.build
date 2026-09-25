project(
  'io.github.laurentiustaicu.ask_the_model',
  'vala',
  'c',
  version: '0.5.0',
  meson_version: '>= 1.0.0',
  license: 'MIT'
)

gtk_dep = dependency('gtk4')
granite_dep = dependency('granite-7')
soup_dep = dependency('libsoup-3.0')
json_glib_dep = dependency('json-glib-1.0')
glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')
sqlite_dep = dependency('sqlite3', version: '>=3.37.0')
archive_dep = dependency('libarchive')
yaml_dep = dependency('yaml-0.1')
gnome = import('gnome')

md4c_inc = include_directories('third_party/md4c')
md4c_lib = static_library(
  'atm-md4c',
  'third_party/md4c/md4c.c',
  include_directories: md4c_inc,
  install: false
)
md4c_dep = declare_dependency(
  link_with: md4c_lib,
  include_directories: md4c_inc
)


resources = gnome.compile_resources(
  'ask-the-model-resources',
  'data/io.github.laurentiustaicu.ask_the_model.gresource.xml',
  source_dir: 'data',
  c_name: 'ask_the_model'
)

executable(
  meson.project_name(),
  [
    'src/Application.vala',
    'src/presentation_document.c',
    'src/presentation_normalize.c',
    'src/ActivityRing.vala',
    'src/SelectorTriangle.vala',
    'src/ChatRequestBuilder.vala',
    'src/OptimizationPolicy.vala',
    'src/OllamaProvider.vala',
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/RepositorySelection.vala',
    'src/RepositoryState.vala',
    'src/ControlStateNative.vala',
    'src/ControlRepositoryState.vala',
    'src/ConversationStoreNative.vapi',
    'src/ConversationPersistenceStore.vala',
    'src/ConversationExport.vala',
    'src/StartupQualificationRecord.vala',
    'src/StartupQualificationNative.vapi',
    'src/StartupQualificationService.vala',
    'src/startup_qualification.c',
    'src/snapshot_seal.c',
    'src/RepositoryLifecycleService.vala',
    'src/ConversationGrounding.vala',
    'src/ConversationSession.vala',
    'src/cff_version.c',
    'src/archive_extract.c',
    'src/capacity_admission_model.c',
    'src/capacity_operation_plan.c',
    'src/capacity_measurement.c',
    'src/repository_capacity_policy.c',
    'src/repository_capacity_admission.c',
    'src/repository_capacity_ui_bridge.c',
    'src/repository_manifest.c',
    'src/repository_reconcile.c',
    'src/repository_storage.c',
    'src/repository_mutation_lease.c',
    'src/repository_ingest.c',
    'src/repository_ui_bridge.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/repository_generation_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/retrieval_query.c',
    'src/scientific_profile.c',
    'src/scientific_evidence.c',
    'src/scientific_profile_ewd.c',
    'src/scientific_profile_cbd.c',
    'src/scientific_profile_rmd.c',
    'src/json_pointer.c',
    'src/scientific_control.c',
    'src/scientific_canonical.c',
    'src/scientific_artifact.c',
    'src/scientific_result.c',
    'src/scientific_derivation.c',
    'src/scientific_realization.c',
    'src/scientific_operation.c',
    'src/control_state.c',
    'src/conversation_store.c',
    'src/retrieval_rank.c',
    'src/retrieval_scope.c',
    'src/retrieval_router.c',
    'src/retrieval_conversation.c',
    'src/grounding_context.c',
    'src/citation_labels.c',
    'src/conversation_grounding.c',
    'src/retrieval_normalize.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    gtk_dep,
    granite_dep,
    soup_dep,
    json_glib_dep,
    sqlite_dep,
    md4c_dep
  ],
  link_args: [
    '-larchive',
    '-lyaml',
    '-lsqlite3'
  ],
  install: true
)

repository_store_oracle = executable(
  'repository-store-oracle',
  'tests/repository_store_oracle.c',
  dependencies: [
    glib_dep,
    json_glib_dep,
    sqlite_dep,
    yaml_dep
  ],
  install: false
)

test(
  'repository-store-oracle-core',
  repository_store_oracle,
  args: [
    '--self-test'
  ]
)

repository_platform_probe = executable(
  'repository-platform-probe',
  'tests/repository_platform_probe.c',
  dependencies: [
    glib_dep,
    sqlite_dep,
    archive_dep,
    yaml_dep
  ],
  install: false
)

test(
  'repository-platform-probe',
  repository_platform_probe,
  env: {
    'ATM_FLATPAK_MANIFEST': meson.project_source_root() / 'io.github.laurentiustaicu.ask_the_model.yml'
  }
)


startup_qualification_test = executable(
  'startup-qualification-test',
  [
    'src/startup_qualification.c',
    'tests/startup_qualification_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'startup-qualification',
  startup_qualification_test
)


snapshot_seal_test = executable(
  'snapshot-seal-test',
  [
    'src/snapshot_seal.c',
    'tests/snapshot_seal_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'snapshot-seal',
  snapshot_seal_test
)


startup_qualification_record_test = executable(
  'startup-qualification-record-test',
  [
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/StartupQualificationRecord.vala',
    'src/cff_version.c',
    'tests/startup_qualification_record_test.vala'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    soup_dep,
    json_glib_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'startup-qualification-record',
  startup_qualification_record_test
)


startup_qualification_service_test = executable(
  'startup-qualification-service-test',
  [
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/RepositoryState.vala',
    'src/ControlStateNative.vala',
    'src/ControlRepositoryState.vala',
    'src/StartupQualificationRecord.vala',
    'src/StartupQualificationNative.vapi',
    'src/StartupQualificationService.vala',
    'src/control_state.c',
    'src/startup_qualification.c',
    'src/cff_version.c',
    'src/repository_manifest.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/repository_reconcile.c',
    'src/snapshot_seal.c',
    'tests/startup_qualification_service_test.vala',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    soup_dep,
    json_glib_dep,
    sqlite_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'startup-qualification-service',
  startup_qualification_service_test
)

repository_catalog_test = executable(
  'repository-catalog-test',
  [
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/cff_version.c',
    'tests/repository_catalog_test.vala'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    soup_dep,
    json_glib_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'repository-catalog',
  repository_catalog_test
)

repository_selection_test = executable(
  'repository-selection-test',
  [
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/RepositorySelection.vala',
    'src/cff_version.c',
    'tests/repository_selection_test.vala'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    soup_dep,
    json_glib_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'repository-selection',
  repository_selection_test
)

repository_state_test = executable(
  'repository-state-test',
  [
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/RepositoryState.vala',
    'src/cff_version.c',
    'tests/repository_state_test.vala'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    soup_dep,
    json_glib_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'repository-state',
  repository_state_test
)

conversation_store_test = executable(
  'conversation-store-test',
  [
    'src/conversation_store.c',
    'tests/conversation_store_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'conversation-store',
  conversation_store_test
)

conversation_persistence_store_test = executable(
  'conversation-persistence-store-test',
  [
    'src/ConversationStoreNative.vapi',
    'src/ConversationPersistenceStore.vala',
    'src/ConversationExport.vala',
    'src/OllamaProvider.vala',
    'src/ChatRequestBuilder.vala',
    'src/conversation_store.c',
    'tests/conversation_persistence_store_test.vala',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    soup_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'conversation-persistence-store',
  conversation_persistence_store_test
)

control_state_test = executable(
  'control-state-test',
  [
    'src/control_state.c',
    'tests/control_state_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'control-state',
  control_state_test
)

control_repository_state_test = executable(
  'control-repository-state-test',
  [
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/RepositoryState.vala',
    'src/ControlStateNative.vala',
    'src/ControlRepositoryState.vala',
    'src/control_state.c',
    'src/cff_version.c',
    'tests/control_repository_state_test.vala',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    soup_dep,
    json_glib_dep,
    sqlite_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'control-repository-state',
  control_repository_state_test
)

chat_request_builder_test = executable(
  'chat-request-builder-test',
  [
    'src/ChatRequestBuilder.vala',
    'tests/chat_request_builder_test.vala'
  ],
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'chat-request-builder',
  chat_request_builder_test
)

optimization_policy_test = executable(
  'optimization-policy-test',
  [
    'src/OptimizationPolicy.vala',
    'tests/optimization_policy_test.vala'
  ],
  dependencies: [
    glib_dep,
    dependency('gobject-2.0')
  ],
  install: false
)

test(
  'optimization-policy',
  optimization_policy_test
)

cff_version_test = executable(
  'cff-version-test',
  [
    'src/cff_version.c',
    'tests/cff_version_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    yaml_dep
  ],
  install: false
)

test(
  'cff-version-parser',
  cff_version_test
)

repository_manifest_test = executable(
  'repository-manifest-test',
  [
    'src/cff_version.c',
    'src/repository_manifest.c',
    'tests/repository_manifest_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep,
    yaml_dep
  ],
  install: false
)

test(
  'repository-manifest',
  repository_manifest_test
)

archive_extract_test = executable(
  'archive-extract-test',
  [
    'src/archive_extract.c',
    'tests/archive_extract_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    archive_dep
  ],
  install: false
)

test(
  'archive-extract',
  archive_extract_test
)

repository_storage_test = executable(
  'repository-storage-test',
  [
    'src/repository_storage.c',
    'tests/repository_storage_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'repository-storage',
  repository_storage_test
)

capacity_measurement_test = executable(
  'capacity-measurement-test',
  [
    'src/capacity_measurement.c',
    'tests/capacity_measurement_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'capacity-measurement',
  capacity_measurement_test
)

capacity_admission_model_test = executable(
  'capacity-admission-model-test',
  [
    'src/capacity_admission_model.c',
    'tests/capacity_admission_model_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'capacity-admission-model',
  capacity_admission_model_test
)

capacity_operation_plan_test = executable(
  'capacity-operation-plan-test',
  [
    'src/capacity_admission_model.c',
    'src/capacity_operation_plan.c',
    'tests/capacity_operation_plan_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'capacity-operation-plan',
  capacity_operation_plan_test
)

repository_capacity_policy_test = executable(
  'repository-capacity-policy-test',
  [
    'src/repository_capacity_policy.c',
    'tests/repository_capacity_policy_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'repository-capacity-policy',
  repository_capacity_policy_test
)

repository_capacity_admission_test = executable(
  'repository-capacity-admission-test',
  [
    'src/repository_capacity_admission.c',
    'src/repository_capacity_policy.c',
    'src/capacity_operation_plan.c',
    'src/capacity_admission_model.c',
    'src/capacity_measurement.c',
    'src/archive_extract.c',
    'tests/repository_capacity_admission_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    archive_dep
  ],
  install: false
)

test(
  'repository-capacity-admission',
  repository_capacity_admission_test
)

capacity_measurement_probe = executable(
  'capacity-measurement-probe',
  [
    'src/capacity_measurement.c',
    'tests/capacity_measurement_probe.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

capacity_measurement_real_runner = executable(
  'capacity-measurement-real-runner',
  [
    'src/capacity_measurement.c',
    'src/cff_version.c',
    'src/archive_extract.c',
    'src/repository_manifest.c',
    'src/repository_storage.c',
    'src/repository_ingest.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'tests/capacity_measurement_real_runner.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep,
    archive_dep,
    yaml_dep
  ],
  install: false
)

capacity_enospc_runner = executable(
  'capacity-enospc-runner',
  [
    'src/cff_version.c',
    'src/archive_extract.c',
    'src/repository_manifest.c',
    'src/repository_storage.c',
    'src/repository_ingest.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/control_state.c',
    'tests/capacity_enospc_runner.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep,
    archive_dep,
    yaml_dep
  ],
  install: false
)

capacity_state_enospc_runner = executable(
  'capacity-state-enospc-runner',
  [
    'src/control_state.c',
    'tests/capacity_state_enospc_runner.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

capacity_state_reserve_scaling_runner = executable(
  'capacity-state-reserve-scaling-runner',
  [
    'src/control_state.c',
    'tests/capacity_state_reserve_scaling_runner.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

capacity_state_inode_reserve_runner = executable(
  'capacity-state-inode-reserve-runner',
  [
    'src/control_state.c',
    'tests/capacity_state_inode_reserve_runner.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

repository_mutation_lease_test = executable(
  'repository-mutation-lease-test',
  [
    'src/coordination_lease.c',
    'src/repository_mutation_lease.c',
    'tests/repository_mutation_lease_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'repository-mutation-lease',
  repository_mutation_lease_test
)

repository_generation_lease_test = executable(
  'repository-generation-lease-test',
  [
    'src/coordination_lease.c',
    'src/repository_generation_lease.c',
    'tests/repository_generation_lease_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'repository-generation-lease',
  repository_generation_lease_test
)

repository_ingest_test = executable(
  'repository-ingest-test',
  [
    'src/cff_version.c',
    'src/archive_extract.c',
    'src/repository_manifest.c',
    'src/repository_storage.c',
    'src/repository_ingest.c',
    'tests/repository_ingest_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    archive_dep,
    yaml_dep
  ],
  install: false
)

test(
  'repository-ingest',
  repository_ingest_test
)

retrieval_index_schema_test = executable(
  'retrieval-index-schema-test',
  'tests/retrieval_index_schema_test.c',
  dependencies: [
    glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'retrieval-index-schema',
  retrieval_index_schema_test,
  env: {
    'ATM_RETRIEVAL_SCHEMA': meson.project_source_root() / 'data/schemas/retrieval-index-v2.sql'
  }
)

retrieval_index_test = executable(
  'retrieval-index-test',
  [
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'tests/retrieval_index_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'retrieval-index',
  retrieval_index_test
)

recovery_fault_helper = executable(
  'recovery-fault-helper',
  [
    'src/repository_storage.c',
    'src/control_state.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'tests/fault_injection_support.c',
    'tests/recovery_fault_helper.c',
    resources
  ],
  include_directories: [
    include_directories('src'),
    include_directories('tests')
  ],
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  c_args: [
    '-DATM_TEST_FAULT_INJECTION'
  ],
  install: false
)

recovery_fault_test = executable(
  'recovery-fault-test',
  'tests/recovery_fault_test.c',
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'recovery-fault',
  recovery_fault_test,
  env: {
    'ATM_RECOVERY_FAULT_HELPER':
      recovery_fault_helper.full_path()
  },
  timeout: 60
)

retrieval_index_lifecycle_test = executable(
  'retrieval-index-lifecycle-test',
  [
    'src/cff_version.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/retrieval_index_lifecycle.c',
    'tests/retrieval_index_lifecycle_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep,
    yaml_dep
  ],
  c_args: [
    '-DATM_TEST_INDEX_SINGLE_FLIGHT'
  ],
  install: false
)

test(
  'retrieval-index-lifecycle',
  retrieval_index_lifecycle_test
)


repository_reconcile_test = executable(
  'repository-reconcile-test',
  [
    'src/cff_version.c',
    'src/repository_manifest.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/repository_reconcile.c',
    'tests/repository_reconcile_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep,
    yaml_dep
  ],
  install: false
)

test(
  'repository-reconcile',
  repository_reconcile_test
)

repository_oracle_integration_test = executable(
  'repository-oracle-integration-test',
  [
    'src/cff_version.c',
    'src/repository_manifest.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/repository_reconcile.c',
    'src/snapshot_seal.c',
    'tests/repository_oracle_integration_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep,
    yaml_dep
  ],
  install: false
)

test(
  'repository-oracle-production-divergence',
  repository_oracle_integration_test,
  env: {
    'ATM_REPOSITORY_ORACLE': meson.current_build_dir() / 'repository-store-oracle'
  }
)

retrieval_query_test = executable(
  'retrieval-query-test',
  [
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/retrieval_query.c',
    'tests/retrieval_query_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'retrieval-query',
  retrieval_query_test
)

scientific_evidence_test = executable(
  'scientific-evidence-test',
  [
    'src/scientific_profile.c',
    'src/scientific_evidence.c',
    'tests/scientific_evidence_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'scientific-evidence',
  scientific_evidence_test
)

scientific_profiles_test = executable(
  'scientific-profiles-test',
  [
    'src/scientific_profile.c',
    'src/scientific_evidence.c',
    'src/scientific_profile_ewd.c',
    'src/scientific_profile_cbd.c',
    'src/scientific_profile_rmd.c',
    'tests/scientific_profiles_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'scientific-profiles',
  scientific_profiles_test
)

scientific_control_test = executable(
  'scientific-control-test',
  [
    'src/json_pointer.c',
    'src/scientific_control.c',
    'tests/scientific_control_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'scientific-control',
  scientific_control_test
)

scientific_canonical_test = executable(
  'scientific-canonical-test',
  [
    'src/scientific_canonical.c',
    'tests/scientific_canonical_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'scientific-canonical',
  scientific_canonical_test
)

scientific_artifact_test = executable(
  'scientific-artifact-test',
  [
    'src/scientific_canonical.c',
    'src/scientific_artifact.c',
    'tests/scientific_artifact_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'scientific-artifact',
  scientific_artifact_test
)

scientific_operation_test = executable(
  'scientific-operation-test',
  [
    'src/scientific_operation.c',
    'tests/scientific_operation_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'scientific-operation',
  scientific_operation_test
)

scientific_result_test = executable(
  'scientific-result-test',
  [
    'src/scientific_canonical.c',
    'src/scientific_artifact.c',
    'src/scientific_operation.c',
    'src/scientific_result.c',
    'src/scientific_derivation.c',
    'tests/scientific_result_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'scientific-result',
  scientific_result_test
)

scientific_gold_v0_test = executable(
  'scientific-gold-v0-test',
  [
    'src/scientific_canonical.c',
    'src/scientific_artifact.c',
    'src/scientific_operation.c',
    'src/scientific_result.c',
    'src/scientific_derivation.c',
    'tests/scientific_gold_v0_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'scientific-gold-v0',
  scientific_gold_v0_test,
  env: {
    'ATM_SCIENTIFIC_GOLD_V0': meson.current_source_dir() / 'qualification/scientific-gold-v0.json'
  }
)

scientific_derivation_test = executable(
  'scientific-derivation-test',
  [
    'src/scientific_canonical.c',
    'src/scientific_artifact.c',
    'src/scientific_operation.c',
    'src/scientific_result.c',
    'src/scientific_derivation.c',
    'tests/scientific_derivation_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'scientific-derivation',
  scientific_derivation_test
)

scientific_realization_test = executable(
  'scientific-realization-test',
  [
    'src/scientific_canonical.c',
    'src/scientific_artifact.c',
    'src/scientific_operation.c',
    'src/scientific_result.c',
    'src/scientific_derivation.c',
    'src/scientific_realization.c',
    'tests/scientific_realization_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'scientific-realization',
  scientific_realization_test
)

retrieval_rank_test = executable(
  'retrieval-rank-test',
  [
    'src/retrieval_query.c',
    'src/retrieval_rank.c',
    'tests/retrieval_rank_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'retrieval-rank',
  retrieval_rank_test
)

retrieval_scope_test = executable(
  'retrieval-scope-test',
  [
    'src/retrieval_scope.c',
    'tests/retrieval_scope_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'retrieval-scope',
  retrieval_scope_test
)

retrieval_normalize_test = executable(
  'retrieval-normalize-test',
  [
    'src/retrieval_normalize.c',
    'tests/retrieval_normalize_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'retrieval-normalize',
  retrieval_normalize_test
)

retrieval_router_test = executable(
  'retrieval-router-test',
  [
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/retrieval_query.c',
    'src/retrieval_rank.c',
    'src/retrieval_scope.c',
    'src/retrieval_normalize.c',
    'src/retrieval_router.c',
    'tests/retrieval_router_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'retrieval-router',
  retrieval_router_test
)

retrieval_conversation_test = executable(
  'retrieval-conversation-test',
  [
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/retrieval_query.c',
    'src/retrieval_rank.c',
    'src/retrieval_scope.c',
    'src/retrieval_normalize.c',
    'src/retrieval_router.c',
    'src/retrieval_conversation.c',
    'tests/retrieval_conversation_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  install: false
)

test(
  'retrieval-conversation',
  retrieval_conversation_test
)

grounding_context_test = executable(
  'grounding-context-test',
  [
    'src/grounding_context.c',
    'tests/grounding_context_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'grounding-context',
  grounding_context_test
)

citation_labels_test = executable(
  'citation-labels-test',
  [
    'src/grounding_context.c',
    'src/citation_labels.c',
    'tests/citation_labels_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'citation-labels',
  citation_labels_test
)

conversation_grounding_test = executable(
  'conversation-grounding-test',
  [
    'src/cff_version.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/retrieval_query.c',
    'src/retrieval_rank.c',
    'src/retrieval_scope.c',
    'src/retrieval_normalize.c',
    'src/retrieval_router.c',
    'src/retrieval_conversation.c',
    'src/grounding_context.c',
    'src/citation_labels.c',
    'src/conversation_grounding.c',
    'tests/conversation_grounding_test.c',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep,
    yaml_dep
  ],
  install: false
)

test(
  'conversation-grounding',
  conversation_grounding_test
)

conversation_grounding_vala_test = executable(
  'conversation-grounding-vala-test',
  [
    'src/ConversationGrounding.vala',
    'src/cff_version.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/repository_generation_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/retrieval_query.c',
    'src/retrieval_rank.c',
    'src/retrieval_scope.c',
    'src/retrieval_normalize.c',
    'src/retrieval_router.c',
    'src/retrieval_conversation.c',
    'src/grounding_context.c',
    'src/citation_labels.c',
    'src/conversation_grounding.c',
    'tests/conversation_grounding_vala_test.vala',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'conversation-grounding-vala',
  conversation_grounding_vala_test
)

conversation_session_test = executable(
  'conversation-session-test',
  [
    'src/ConversationGrounding.vala',
    'src/ConversationSession.vala',
    'src/cff_version.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/repository_generation_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/retrieval_query.c',
    'src/retrieval_rank.c',
    'src/retrieval_scope.c',
    'src/retrieval_normalize.c',
    'src/retrieval_router.c',
    'src/retrieval_conversation.c',
    'src/grounding_context.c',
    'src/citation_labels.c',
    'src/conversation_grounding.c',
    'tests/conversation_session_test.vala',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    json_glib_dep,
    sqlite_dep
  ],
  link_args: [
    '-lyaml'
  ],
  install: false
)

test(
  'conversation-session',
  conversation_session_test
)

repository_lifecycle_service_test = executable(
  'repository-lifecycle-service-test',
  [
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/RepositoryState.vala',
    'src/ControlStateNative.vala',
    'src/ControlRepositoryState.vala',
    'src/OptimizationPolicy.vala',
    'src/RepositoryLifecycleService.vala',
    'src/control_state.c',
    'src/ConversationGrounding.vala',
    'src/ConversationSession.vala',
    'src/cff_version.c',
    'src/archive_extract.c',
    'src/capacity_admission_model.c',
    'src/capacity_operation_plan.c',
    'src/capacity_measurement.c',
    'src/repository_capacity_policy.c',
    'src/repository_capacity_admission.c',
    'src/repository_capacity_ui_bridge.c',
    'src/repository_manifest.c',
    'src/repository_storage.c',
    'src/repository_mutation_lease.c',
    'src/repository_ingest.c',
    'src/snapshot_seal.c',
    'src/repository_ui_bridge.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
    'src/coordination_lease.c',
    'src/repository_generation_lease.c',
    'src/retrieval_index_lifecycle.c',
    'src/retrieval_query.c',
    'src/retrieval_rank.c',
    'src/retrieval_scope.c',
    'src/retrieval_normalize.c',
    'src/retrieval_router.c',
    'src/retrieval_conversation.c',
    'src/grounding_context.c',
    'src/citation_labels.c',
    'src/conversation_grounding.c',
    'tests/repository_lifecycle_service_test.vala',
    resources
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    gio_dep,
    soup_dep,
    json_glib_dep,
    sqlite_dep
  ],
  link_args: [
    '-larchive',
    '-lyaml',
    '-lsqlite3'
  ],
  install: false
)

test(
  'repository-lifecycle-service',
  repository_lifecycle_service_test
)

repository_sources_test = executable(
  'repository-sources-test',
  [
    'src/repository_sources.c',
    'tests/repository_sources_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'repository-sources',
  repository_sources_test
)

markdown_sections_test = executable(
  'markdown-sections-test',
  [
    'src/markdown_sections.c',
    'tests/markdown_sections_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'markdown-sections',
  markdown_sections_test
)

csv_table_test = executable(
  'csv-table-test',
  [
    'src/csv_table.c',
    'tests/csv_table_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep
  ],
  install: false
)

test(
  'csv-table',
  csv_table_test
)

structured_json_test = executable(
  'structured-json-test',
  [
    'src/structured_json.c',
    'tests/structured_json_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'structured-json',
  structured_json_test
)

presentation_normalize_test = executable(
  'presentation-normalize-test',
  [
    'src/presentation_document.c',
    'src/presentation_normalize.c',
    'tests/presentation_normalize_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    md4c_dep
  ],
  install: false
)

test(
  'presentation-normalize',
  presentation_normalize_test
)


presentation_golden_test = executable(
  'presentation-golden-test',
  [
    'src/presentation_document.c',
    'src/presentation_normalize.c',
    'tests/presentation_golden_test.c'
  ],
  include_directories: include_directories('src'),
  dependencies: [
    glib_dep,
    json_glib_dep,
    md4c_dep
  ],
  install: false
)

test(
  'presentation-golden',
  presentation_golden_test,
  env: {
    'ATM_PRESENTATION_GOLDEN_V1': meson.current_source_dir() / 'tests/fixtures/presentation/golden-v1.json'
  }
)

subdir('data')

icon_sizes = ['16', '24', '32', '48', '64', '128']

foreach size : icon_sizes
  install_data(
    'data/icons/' + size + '.png',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / size + 'x' + size / 'apps',
    rename: meson.project_name() + '.png'
  )
endforeach
