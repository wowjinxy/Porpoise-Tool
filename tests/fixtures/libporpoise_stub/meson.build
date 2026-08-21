project(
  'libPorpoise',
  'c',
  version: '0.0.0-test',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
)

libporpoise_stub_inc = include_directories('include')
threads_dep = dependency('threads')

libporpoise_stub_lib = static_library(
  'porpoise_stub',
  [
    'src/ai_stub.c',
    'src/ar_allocator_stub.c',
    'src/card_stub.c',
    'src/gx_stub.c',
    'src/gx_objects_stub.c',
    'src/gx_values_stub.c',
    'src/host_main.c',
    'src/os_stub.c',
    'src/sdk_import_contract_stub.c',
    'src/thread_carrier_stub.c',
  ],
  include_directories: libporpoise_stub_inc,
  dependencies: threads_dep,
  install: false,
)

libporpoise_dep = declare_dependency(
  # Model an evolving dependency that exports a broad consumer flag. Generated
  # Porpoise targets must filter both flags and supply their ABI defines locally.
  compile_args: ['-w', '-DLIBPORPOISE_PORT=1'],
  dependencies: threads_dep,
  include_directories: libporpoise_stub_inc,
  link_with: libporpoise_stub_lib,
)

meson.override_dependency('libPorpoise', libporpoise_dep)

porpoise_title_host_dep = declare_dependency(
  dependencies: threads_dep,
  include_directories: libporpoise_stub_inc,
  link_with: libporpoise_stub_lib,
)

meson.override_dependency('porpoise-title-host', porpoise_title_host_dep)
