project('no_entry', 'c', version: '1.0.0', default_options: ['c_std=c99', 'warning_level=3', 'werror=true'])

libporpoise_dep = dependency('libPorpoise', fallback: ['libPorpoise', 'libporpoise_dep'])
cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)
generated_inc = include_directories('include')

lifted_sources = files(
  'src/porpoise_lifted.c',
  'src/porpoise_libporpoise_adapter.c',
  'src/porpoise_function_registry.c',
  'src/porpoise_function_registry_8000.c',
  'src/porpoise_data.c',
  'src/porpoise_imports.c',
  'src/porpoise_exports.c',
  'src/lifted/no_entry.c'
)

porpoise_lifted_library = static_library(
  'porpoise_lifted',
  lifted_sources,
  include_directories: generated_inc,
  dependencies: [libporpoise_dep, math_dep],
  install: false,
)

porpoise_lifted_dep = declare_dependency(
  link_with: porpoise_lifted_library,
  include_directories: generated_inc,
  dependencies: [libporpoise_dep, math_dep],
)
meson.override_dependency('porpoise-generated', porpoise_lifted_dep)
