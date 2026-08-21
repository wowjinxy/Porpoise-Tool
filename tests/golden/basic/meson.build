project('no_entry', 'c', version: '1.0.0', default_options: ['c_std=c99', 'warning_level=3', 'werror=true'])

libporpoise_raw_dep = dependency('libPorpoise', fallback: ['libPorpoise','libporpoise_dep'])
libporpoise_dep = libporpoise_raw_dep.partial_dependency(
  compile_args: false,
  includes: true,
  link_args: true,
  links: true,
  sources: true,
).as_system('system')

if host_machine.system() == 'windows'
  porpoise_consumer_c_args = [
    '-DLIBPORPOISE_PORT',
    '-DLIBPORPOISE_BUILD_WIN64',
  ]
elif host_machine.system() == 'linux'
  porpoise_consumer_c_args = [
    '-DLIBPORPOISE_PORT',
    '-DLIBPORPOISE_BUILD_LINUX',
    '-D_POSIX_C_SOURCE=200112L',
  ]
else
  error('Porpoise generated projects support only Windows and Linux hosts')
endif
porpoise_consumer_c_args += [
  '-DPORPOISE_AUTODETECT_LIBPORPOISE_HOST_THREAD_CARRIER_V1=1',
]

cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: false)

generated_inc = include_directories('include')
generated_private_inc = include_directories('src')

lifted_sources = files(
  'src/porpoise_lifted.c',
  'src/porpoise_libporpoise_adapter.c',
  'src/porpoise_libporpoise_ai.c',
  'src/porpoise_libporpoise_ar.c',
  'src/porpoise_libporpoise_arena.c',
  'src/porpoise_libporpoise_card.c',
  'src/porpoise_libporpoise_gx.c',
  'src/porpoise_libporpoise_gx_objects.c',
  'src/porpoise_libporpoise_gx_values.c',
  'src/porpoise_libporpoise_guest_os.c',
  'src/porpoise_libporpoise_message_queue.c',
  'src/porpoise_libporpoise_os_report.c',
  'src/porpoise_function_registry.c',
  'src/porpoise_function_registry_8000.c',
  'src/porpoise_data.c',
  'src/porpoise_generated.c',
  'src/porpoise_imports.c',
  'src/porpoise_exports.c',
  'src/lifted/no_entry.c'
)

porpoise_lifted_library = static_library(
  'porpoise_lifted',
  lifted_sources,
  include_directories: [generated_inc, generated_private_inc],
  dependencies: [libporpoise_dep, math_dep],
  c_args: porpoise_consumer_c_args,
  install: false,
)

porpoise_lifted_dep = declare_dependency(
  link_with: porpoise_lifted_library,
  include_directories: generated_inc,
  dependencies: [libporpoise_dep, math_dep],
)
meson.override_dependency('porpoise-generated', porpoise_lifted_dep)
