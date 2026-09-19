project(
  'io.github.laurentiustaicu.ask_the_model',
  'vala',
  version: '0.2.1',
  meson_version: '>= 1.0.0',
  license: 'MIT'
)

gtk_dep = dependency('gtk4')
granite_dep = dependency('granite-7')
soup_dep = dependency('libsoup-3.0')
json_glib_dep = dependency('json-glib-1.0')
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

subdir('data')

icon_sizes = ['16', '24', '32', '48', '64', '128']

foreach size : icon_sizes
  install_data(
    'data/icons/' + size + '.png',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / size + 'x' + size / 'apps',
    rename: meson.project_name() + '.png'
  )
endforeach
