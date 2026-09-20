project(
  'io.github.laurentiustaicu.ask_the_model',
  'vala',
  'c',
  version: '0.2.2',
  meson_version: '>= 1.0.0',
  license: 'MIT'
)

gtk_dep = dependency('gtk4')
granite_dep = dependency('granite-7')
soup_dep = dependency('libsoup-3.0')
json_glib_dep = dependency('json-glib-1.0')
glib_dep = dependency('glib-2.0')
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
    'src/OllamaProvider.vala',
    'src/RepositoryService.vala',
    resources
  ],
  dependencies: [
    gtk_dep,
    granite_dep,
    soup_dep,
    json_glib_dep
  ],
  install: true
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

repository_catalog_test = executable(
  'repository-catalog-test',
  [
    'src/RepositoryService.vala',
    'tests/repository_catalog_test.vala'
  ],
  dependencies: [
    glib_dep,
    soup_dep,
    json_glib_dep
  ],
  install: false
)

test(
  'repository-catalog',
  repository_catalog_test
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
