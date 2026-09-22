project(
  'io.github.laurentiustaicu.ask_the_model',
  'vala',
  'c',
  version: '0.4.0',
  meson_version: '>= 1.0.0',
  license: 'MIT'
)

gtk_dep = dependency('gtk4')
granite_dep = dependency('granite-7')
soup_dep = dependency('libsoup-3.0')
json_glib_dep = dependency('json-glib-1.0')
glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')
sqlite_dep = dependency('sqlite3')
archive_dep = dependency('libarchive')
yaml_dep = dependency('yaml-0.1')
gnome = import('gnome')

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
    'src/ActivityRing.vala',
    'src/SelectorTriangle.vala',
    'src/ChatRequestBuilder.vala',
    'src/OllamaProvider.vala',
    'src/RepositoryService.vala',
    'src/RepositoryNative.vala',
    'src/RepositorySelection.vala',
    'src/RepositoryState.vala',
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
    'src/repository_manifest.c',
    'src/repository_reconcile.c',
    'src/repository_storage.c',
    'src/repository_ingest.c',
    'src/repository_ui_bridge.c',
    'src/retrieval_index.c',
    'src/retrieval_index_lifecycle.c',
    'src/retrieval_query.c',
    'src/scientific_profile.c',
    'src/scientific_evidence.c',
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
    json_glib_dep
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
    'src/StartupQualificationRecord.vala',
    'src/StartupQualificationNative.vapi',
    'src/StartupQualificationService.vala',
    'src/startup_qualification.c',
    'src/cff_version.c',
    'src/repository_manifest.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
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

retrieval_index_lifecycle_test = executable(
  'retrieval-index-lifecycle-test',
  [
    'src/cff_version.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
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
    'src/RepositoryLifecycleService.vala',
    'src/ConversationGrounding.vala',
    'src/cff_version.c',
    'src/archive_extract.c',
    'src/repository_manifest.c',
    'src/repository_storage.c',
    'src/repository_ingest.c',
    'src/snapshot_seal.c',
    'src/repository_ui_bridge.c',
    'src/repository_sources.c',
    'src/markdown_sections.c',
    'src/csv_table.c',
    'src/structured_json.c',
    'src/retrieval_index.c',
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
    json_glib_dep
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

subdir('data')

icon_sizes = ['16', '24', '32', '48', '64', '128']

foreach size : icon_sizes
  install_data(
    'data/icons/' + size + '.png',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / size + 'x' + size / 'apps',
    rename: meson.project_name() + '.png'
  )
endforeach
